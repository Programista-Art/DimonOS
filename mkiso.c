/* DimonVirtualCPU-64 mkiso: builds standard FAT16 disk / ISO images (512B sectors).
 *
 * FAT16 Image Layout:
 *   Sector 0     : MBR / Boot sector with BIOS Parameter Block (BPB)
 *   Sectors 1..3 : Reserved sectors (0x00)
 *   Sectors 4..19: FAT 1 (16 sectors, 4096 cluster entries)
 *   Sectors 20..35: FAT 2 (16 sectors, mirror of FAT 1)
 *   Sectors 36..67: Root Directory (32 sectors = 512 32-byte directory entries)
 *   Sectors 68.. : Data Clusters (Cluster 2 begins at Sector 68)
 *
 * Usage:
 *   dimon-mkiso -b boot.bin [-o image.iso] [file[:NAME] ...]
 *   dimon-mkiso -o image.iso
 *   dimon-mkiso --list image.iso
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dimon64.h"
#include "dimonfs.h"

#define FAT16_RESERVED_SECTORS 4
#define FAT16_NUM_FATS         2
#define FAT16_SECTORS_PER_FAT  16
#define FAT16_ROOT_ENTRIES     512
#define FAT16_ROOT_SECTORS     ((FAT16_ROOT_ENTRIES * 32) / DISK_SECTOR_SIZE)
#define FAT16_ROOT_DIR_LBA     (FAT16_RESERVED_SECTORS + (FAT16_NUM_FATS * FAT16_SECTORS_PER_FAT))
#define FAT16_DATA_START_LBA   (FAT16_ROOT_DIR_LBA + FAT16_ROOT_SECTORS)
#define FAT16_DEFAULT_TOTAL_SEC 2048

static void usage(const char *p) {
    fprintf(stderr,
        "Usage:\n"
        "  %s -b boot.bin [-o image.iso] [file[:NAME] ...]\n"
        "  %s -o image.iso\n"
        "  %s --list image.iso\n"
        "  %s --add image.iso file[:NAME] [--force]\n"
        "Options:\n"
        "  -b FILE      Bootloader (sector 0, max 510 B)\n"
        "  -o FILE      Output image (default dimon.iso)\n"
        "  --list FILE  List FAT16 image contents\n"
        "  --force      Allow replacing an existing output image\n"
        "Files: path[:NAME] - NAME is 8.3 filename (default basename).\n",
        p, p, p, p);
}

static uint8_t *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    if (n == 0) n = 1;
    uint8_t *b = malloc((size_t)n);
    if (!b) { fclose(f); return NULL; }
    if (n > 0 && fread(b, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); free(b); return NULL;
    }
    fclose(f);
    *out_len = (size_t)n;
    return b;
}

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/');
    const char *s2 = strrchr(p, '\\');
    if (s2 && (!s || s2 > s)) s = s2;
    return s ? s + 1 : p;
}

static int mkdir_in_iso(const char *image_path, const char *dir_path) {
    size_t image_len = 0;
    uint8_t *image = read_file(image_path, &image_len);
    if (!image) { fprintf(stderr, "Cannot read image.\n"); return 1; }
    if (image_len % 512u || image_len / 512u > DISK_MAX_SECTORS) {
        fprintf(stderr, "Invalid image.\n"); free(image); return 1;
    }
    VM vm; memset(&vm, 0, sizeof(vm)); vm.disk_data = image;
    vm.disk_sectors = (uint32_t)(image_len / 512u); vm.disk_writable = 1;
    snprintf(vm.disk_path, sizeof(vm.disk_path), "%s", image_path);
    char path[128];
    if (dir_path[0] == '/') snprintf(path, sizeof(path), "%s", dir_path);
    else snprintf(path, sizeof(path), "/%s", dir_path);
    int rc = dimonfs_mkdir(&vm, path);
    if (rc != DFS_OK && rc != DFS_ERR_EXISTS) {
        fprintf(stderr, "mkdir failed: %s (%d)\n", dimonfs_error(rc), rc);
        free(image); return 1;
    }
    printf("Created directory %s in %s\n", path, image_path);
    free(image); return 0;
}

static int add_to_iso(const char *image_path, char *spec, int replace) {
    char *sep = strrchr(spec, ':');
    const char *name = base_name(spec);
    if (sep) {
        *sep = 0; name = sep + 1;
    }
    size_t image_len = 0, file_len = 0;
    uint8_t *image = read_file(image_path, &image_len);
    uint8_t *file = read_file(spec, &file_len);
    if (!image || !file) { fprintf(stderr, "Cannot read image or input file.\n"); free(image); free(file); return 1; }
    if (image_len % 512u || image_len / 512u > DISK_MAX_SECTORS || file_len > UINT32_MAX) {
        fprintf(stderr, "Invalid image or oversized input.\n"); free(image); free(file); return 1;
    }
    VM vm; memset(&vm, 0, sizeof(vm)); vm.disk_data = image;
    vm.disk_sectors = (uint32_t)(image_len / 512u); vm.disk_writable = 1;
    snprintf(vm.disk_path, sizeof(vm.disk_path), "%s", image_path);
    char path[128];
    if (name[0] == '/') snprintf(path, sizeof(path), "%s", name);
    else snprintf(path, sizeof(path), "/%s", name);
    Dimon64DirEnt ent;
    if (!replace && dimonfs_stat(&vm, path, &ent) == DFS_OK) {
        fprintf(stderr, "Refusing to replace existing '%s' (use --force).\n", path);
        free(image); free(file); return 1;
    }
    int rc = dimonfs_write(&vm, path, file, (uint32_t)file_len, 1, 1);
    if (rc != DFS_OK) { fprintf(stderr, "Add failed: %s (%d)\n", dimonfs_error(rc), rc); free(image); free(file); return 1; }
    printf("Added %s as %s to %s (%zu bytes)\n", spec, path, image_path, file_len);
    free(image); return 0;
}

static void put16(uint8_t *b, int off, uint16_t v) {
    b[off] = (uint8_t)(v & 0xFF);
    b[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put32(uint8_t *b, int off, uint32_t v) {
    put16(b, off, (uint16_t)(v & 0xFFFF));
    put16(b, off + 2, (uint16_t)((v >> 16) & 0xFFFF));
}

static uint16_t get16(const uint8_t *b, int off) {
    return (uint16_t)(b[off] | ((uint16_t)b[off + 1] << 8));
}

static uint32_t get32(const uint8_t *b, int off) {
    return (uint32_t)get16(b, off) | ((uint32_t)get16(b, off + 2) << 16);
}

static void to_83_name(const char *name, uint8_t *out83) {
    memset(out83, ' ', 11);
    const char *dot = strrchr(name, '.');
    int nlen = dot ? (int)(dot - name) : (int)strlen(name);
    if (nlen > 8) nlen = 8;
    for (int i = 0; i < nlen; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out83[i] = (uint8_t)c;
    }
    if (dot) {
        dot++;
        int elen = (int)strlen(dot);
        if (elen > 3) elen = 3;
        for (int i = 0; i < elen; i++) {
            char c = dot[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            out83[8 + i] = (uint8_t)c;
        }
    }
}

static void format_83_to_str(const uint8_t *e, char *out) {
    int p = 0;
    for (int i = 0; i < 8; i++) {
        if (e[i] != ' ') out[p++] = (char)e[i];
    }
    if (e[8] != ' ') {
        out[p++] = '.';
        for (int i = 8; i < 11; i++) {
            if (e[i] != ' ') out[p++] = (char)e[i];
        }
    }
    out[p] = '\0';
}

static int list_iso(const char *path) {
    size_t len = 0;
    uint8_t *img = read_file(path, &len);
    if (!img) { perror("fopen"); return 1; }
    if (len < 512 || len % DISK_SECTOR_SIZE) {
        fprintf(stderr, "Invalid image size (must be a multiple of 512)\n");
        free(img); return 1;
    }
    if (img[510] != 0x55 || img[511] != 0xAA) {
        printf("No valid boot signature (0x55, 0xAA).\n");
        free(img); return 1;
    }
    char oem[9]; memcpy(oem, img + 3, 8); oem[8] = 0;
    char label[12]; memcpy(label, img + 43, 11); label[11] = 0;
    char fstype[9]; memcpy(fstype, img + 54, 8); fstype[8] = 0;
    uint16_t bps = get16(img, 11);
    uint8_t spc = img[13];
    uint16_t rsvd = get16(img, 14);
    uint8_t nfats = img[16];
    uint16_t root_entries = get16(img, 17);
    uint16_t tot_sec = get16(img, 19);
    uint16_t spf = get16(img, 22);

    printf("Image: %s (%u sectors, %zu B)\n", path, (unsigned)(len / 512), len);
    printf("FAT16 Volume: [%s] OEM: [%s] FS: [%s]\n", label, oem, fstype);
    printf("Geometry: %u bytes/sector, %u sec/cluster, %u reserved, %u FATs (%u sec/FAT), %u root entries, %u total sectors\n",
           bps, spc, rsvd, nfats, spf, root_entries, tot_sec);

    uint32_t root_lba = rsvd + nfats * spf;
    uint32_t root_secs = (root_entries * 32 + bps - 1) / bps;
    uint32_t data_lba = root_lba + root_secs;
    printf("Layout: FAT1 @ LBA %u, RootDir @ LBA %u, Data @ LBA %u (Cluster 2)\n",
           rsvd, root_lba, data_lba);

    printf("Root Directory Files:\n");
    printf("  %-13s %-8s %-10s %s\n", "FILENAME", "CLUSTER", "SIZE (B)", "LBA");
    int nfiles = 0;
    for (uint32_t i = 0; i < root_entries; i++) {
        uint32_t offset = (root_lba * 512) + (i * 32);
        if (offset + 32 > len) break;
        uint8_t *e = img + offset;
        if (e[0] == 0x00) break; /* End of directory */
        if (e[0] == 0xE5) continue; /* Deleted */
        if (e[11] == 0x0F || (e[11] & 0x08)) continue; /* LFN or Volume label */

        char fname[16];
        format_83_to_str(e, fname);
        uint16_t clus = get16(e, 26);
        uint32_t sz = get32(e, 28);
        uint32_t f_lba = data_lba + (clus - 2) * spc;
        printf("  %-13s %-8u %-10u %u\n", fname, clus, sz, f_lba);
        nfiles++;
    }
    printf("Total files listed: %d\n", nfiles);
    free(img);
    return 0;
}

int main(int argc, char **argv) {
    const char *boot = NULL;
    const char *out = "dimon.iso";
    const char *inputs[DISK_FILE_MAX];
    const char *names[DISK_FILE_MAX];
    int ninputs = 0;
    int force = 0;

    if (argc < 2) { usage(argv[0]); return 1; }
    if (!strcmp(argv[1], "--add")) {
        if (argc < 4) { usage(argv[0]); return 1; }
        int replace = 0;
        for (int i = 4; i < argc; i++) if (!strcmp(argv[i], "--force")) replace = 1;
        return add_to_iso(argv[2], argv[3], replace);
    }
    if (!strcmp(argv[1], "--mkdir")) {
        if (argc < 4) { usage(argv[0]); return 1; }
        return mkdir_in_iso(argv[2], argv[3]);
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--list") && i + 1 < argc) {
            return list_iso(argv[++i]);
        } else if (!strcmp(argv[i], "-b") && i + 1 < argc) {
            boot = argv[++i];
        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out = argv[++i];
        } else if (!strcmp(argv[i], "--force")) {
            force = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]); return 0;
        } else if (argv[i][0] == '-') {
            usage(argv[0]); return 1;
        } else {
            char *sep = strchr(argv[i], ':');
            if (sep && strchr(sep + 1, '/')) sep = NULL;
            if (ninputs >= DISK_FILE_MAX) {
                fprintf(stderr, "Too many files (max %d)\n", DISK_FILE_MAX);
                return 1;
            }
            if (sep) {
                *sep = 0;
                inputs[ninputs] = argv[i];
                names[ninputs] = sep + 1;
            } else {
                inputs[ninputs] = argv[i];
                names[ninputs] = base_name(argv[i]);
            }
            ninputs++;
        }
    }

    if (!force) {
        FILE *existing = fopen(out, "rb");
        if (existing) {
            fclose(existing);
            fprintf(stderr, "Refusing to overwrite existing image '%s' (use --force explicitly).\n", out);
            return 1;
        }
    }

    size_t boot_len = 0;
    uint8_t *boot_data = NULL;
    if (boot) {
        boot_data = read_file(boot, &boot_len);
        if (!boot_data) { perror("boot fopen"); return 1; }
        if (boot_len > 510) {
            fprintf(stderr, "Bootloader too large (%zu B, max 510)\n", boot_len);
            free(boot_data); return 1;
        }
    }

    /* Load any extra files passed on command line */
    uint8_t *fdata[DISK_FILE_MAX];
    size_t flen[DISK_FILE_MAX];
    uint32_t fsec[DISK_FILE_MAX];
    uint32_t extra_clusters = 0;
    for (int i = 0; i < ninputs; i++) {
        flen[i] = 0;
        fdata[i] = read_file(inputs[i], &flen[i]);
        if (!fdata[i]) {
            perror("file fopen");
            fprintf(stderr, "Cannot read: %s\n", inputs[i]);
            for (int k = 0; k < i; k++) free(fdata[k]);
            free(boot_data); return 1;
        }
        fsec[i] = (uint32_t)((flen[i] + DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE);
        if (fsec[i] == 0) fsec[i] = 1;
        extra_clusters += fsec[i];
    }

    uint32_t total = FAT16_DEFAULT_TOTAL_SEC;
    if (FAT16_DATA_START_LBA + 3 + extra_clusters > total) {
        total = FAT16_DATA_START_LBA + 3 + extra_clusters + 64;
    }
    if (total > DISK_MAX_SECTORS) {
        fprintf(stderr, "Image too large (%u sectors, max %d)\n", total, DISK_MAX_SECTORS);
        for (int k = 0; k < ninputs; k++) free(fdata[k]);
        free(boot_data); return 1;
    }

    uint8_t *img = calloc(total, DISK_SECTOR_SIZE);
    if (!img) { perror("calloc"); return 1; }

    /* Sector 0: MBR / Boot Sector & FAT16 BPB */
    img[0] = 0xEB; img[1] = 0x3C; img[2] = 0x90; /* JMP short 0x3C; NOP */
    memcpy(img + 3, "DIMON64 ", 8);             /* OEM Name */
    put16(img, 11, DISK_SECTOR_SIZE);            /* Bytes per sector = 512 */
    img[13] = 1;                                 /* Sectors per cluster = 1 */
    put16(img, 14, FAT16_RESERVED_SECTORS);      /* Reserved sectors = 4 */
    img[16] = FAT16_NUM_FATS;                    /* Number of FATs = 2 */
    put16(img, 17, FAT16_ROOT_ENTRIES);          /* Root directory entries = 512 */
    put16(img, 19, (uint16_t)total);             /* Total sectors 16 */
    img[21] = 0xF8;                              /* Media descriptor (fixed disk) */
    put16(img, 22, FAT16_SECTORS_PER_FAT);       /* Sectors per FAT = 16 */
    put16(img, 24, 32);                          /* Sectors per track = 32 */
    put16(img, 26, 64);                          /* Number of heads = 64 */
    put32(img, 28, 0);                           /* Hidden sectors = 0 */
    put32(img, 32, 0);                           /* Total sectors 32 = 0 */
    img[36] = 0x80;                              /* Drive number = 0x80 */
    img[37] = 0x00;                              /* Reserved */
    img[38] = 0x29;                              /* Extended boot signature */
    put32(img, 39, 0x19980808);                  /* Volume Serial Number */
    memcpy(img + 43, "DIMONOS FAT", 11);         /* Volume Label */
    memcpy(img + 54, "FAT16   ", 8);             /* File system type */

    if (boot_data) {
        size_t copy_len = boot_len > 0x3E ? boot_len - 0x3E : 0;
        if (copy_len > 448) copy_len = 448;
        if (copy_len > 0) memcpy(img + 0x3E, boot_data + 0x3E, copy_len);
    }
    img[510] = DISK_BOOT_MAGIC0;
    img[511] = DISK_BOOT_MAGIC1;

    /* Initialize FAT 1 and FAT 2 */
    uint8_t *fat1 = img + (FAT16_RESERVED_SECTORS * DISK_SECTOR_SIZE);
    uint8_t *fat2 = img + ((FAT16_RESERVED_SECTORS + FAT16_SECTORS_PER_FAT) * DISK_SECTOR_SIZE);

    /* Cluster 0 and 1 are reserved */
    put16(fat1, 0, 0xFFF8); put16(fat2, 0, 0xFFF8);
    put16(fat1, 2, 0xFFFF); put16(fat2, 2, 0xFFFF);

    uint8_t *root_dir = img + (FAT16_ROOT_DIR_LBA * DISK_SECTOR_SIZE);
    uint8_t *data_area = img + (FAT16_DATA_START_LBA * DISK_SECTOR_SIZE);

    /* Pre-populate Root Directory with sample files */
    const char *sample_files[3] = { "notes.txt", "readme.txt", "todo.txt" };
    const char *sample_texts[3] = {
        "Welcome to DimonOS-64 Notepad with FAT16!\n"
        "Press Save (F9) to write changes to disk.\n"
        "Press Open (F10) to reload text from disk.\n",
        "DimonOS-64 Modern TrueColor Linear Framebuffer (LFB)\n"
        "Resolution: 800x600 @ 32bpp TrueColor ARGB\n"
        "Filesystem: Standard FAT16 with 8.3 Directory Entries\n",
        "1. Paint TrueColor artwork\n2. Test FAT16 Notepad read/write\n3. Browse files in File Explorer\n4. Enjoy Snake\n"
    };

    uint16_t next_cluster = 2;
    int root_entry_idx = 0;

    for (int i = 0; i < 3; i++) {
        uint8_t *entry = root_dir + (root_entry_idx++ * 32);
        to_83_name(sample_files[i], entry);
        entry[11] = 0x20; /* Archive attribute */

        uint32_t file_len = (uint32_t)strlen(sample_texts[i]);
        put16(entry, 26, next_cluster);
        put32(entry, 28, file_len);

        /* Write data to cluster */
        uint32_t clus_offset = (next_cluster - 2) * DISK_SECTOR_SIZE;
        memcpy(data_area + clus_offset, sample_texts[i], file_len);

        /* FAT entry for single cluster */
        put16(fat1, next_cluster * 2, 0xFFFF);
        put16(fat2, next_cluster * 2, 0xFFFF);

        printf("  [FAT16] %-12s <- (Cluster %u, %u B)\n",
               sample_files[i], next_cluster, file_len);
        next_cluster++;
    }

    /* Add any extra files from command line */
    for (int i = 0; i < ninputs; i++) {
        if (root_entry_idx >= FAT16_ROOT_ENTRIES) break;
        uint8_t *entry = root_dir + (root_entry_idx++ * 32);
        to_83_name(names[i], entry);
        entry[11] = 0x20;

        uint16_t start_clus = next_cluster;
        put16(entry, 26, start_clus);
        put32(entry, 28, (uint32_t)flen[i]);

        uint32_t remaining = (uint32_t)flen[i];
        const uint8_t *src = fdata[i];
        for (uint32_t s = 0; s < fsec[i]; s++) {
            uint32_t chunk = remaining > DISK_SECTOR_SIZE ? DISK_SECTOR_SIZE : remaining;
            uint32_t clus_offset = (next_cluster - 2) * DISK_SECTOR_SIZE;
            memcpy(data_area + clus_offset, src, chunk);
            src += chunk;
            remaining -= chunk;

            uint16_t next_val = (s + 1 == fsec[i]) ? 0xFFFF : (next_cluster + 1);
            put16(fat1, next_cluster * 2, next_val);
            put16(fat2, next_cluster * 2, next_val);
            next_cluster++;
        }

        printf("  [FAT16] %-12s <- %s (Cluster %u, %zu B, %u clusters)\n",
               names[i], inputs[i], start_clus, flen[i], fsec[i]);
    }

    FILE *o = fopen(out, "wb");
    if (!o) { perror("fopen out"); free(img); return 1; }
    if (fwrite(img, DISK_SECTOR_SIZE, total, o) != total) {
        perror("fwrite"); fclose(o); free(img); return 1;
    }
    fclose(o);
    printf("OK: %s (%u sectors, %u B) boot=%s\n",
           out, total, total * DISK_SECTOR_SIZE, boot);

    for (int k = 0; k < ninputs; k++) free(fdata[k]);
    free(boot_data);
    free(img);
    return 0;
}
