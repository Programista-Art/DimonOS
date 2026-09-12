/* Dimon-16 mkiso: builds disk / ISO images (512B sectors).
 *
 * Image layout (DIMON-ISO):
 *   sector 0 : bootloader (boot.bin, <=510 B, padded with zeroes)
 *              [510]=0x55 [511]=0xAA, signature "DIMN" at 0x1F0
 *   sector 1 : catalog: "DM16" + u16 nfiles + 24B entries:
 *              name[12] + start_lba u16 + nsect u16 + size_lo u16 +
 *              size_hi u16 + load_hint u16 + reserved[2]
 *              (max 21 entries; remainder of sector = 0)
 *   sector 2+: file data, each aligned to a sector boundary
 *
 * Usage:
 *   dimon-mkiso -b boot.bin [-o image.iso] [file[:NAME] ...]
 *   dimon-mkiso boot.bin prog.bin -o image.iso   (first positional = boot)
 *   dimon-mkiso --list image.iso
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dimon16.h"

#define CAT_ENTRY_SIZE 24

static void usage(const char *p) {
    fprintf(stderr,
        "Usage:\n"
        "  %s -b boot.bin [-o image.iso] [file[:NAME] ...]\n"
        "  %s boot.bin file.bin ... -o image.iso\n"
        "  %s --list image.iso\n"
        "Options:\n"
        "  -b FILE      Bootloader (sector 0, max 510 B)\n"
        "  -o FILE      Output image (default dimon.iso)\n"
        "  --list FILE  List image contents\n"
        "Files: path[:NAME] - NAME is up to 12 chars (default basename).\n",
        p, p, p);
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

static void put16(uint8_t *b, int off, uint16_t v) {
    b[off] = (uint8_t)(v & 0xFF);
    b[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

static uint16_t get16(const uint8_t *b, int off) {
    return (uint16_t)(b[off] | ((uint16_t)b[off + 1] << 8));
}

static int list_iso(const char *path) {
    size_t len = 0;
    uint8_t *img = read_file(path, &len);
    if (!img) { perror("fopen"); return 1; }
    FILE *f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    long real = ftell(f);
    fclose(f);
    if (real <= 0 || real % DISK_SECTOR_SIZE) {
        fprintf(stderr, "Invalid image size (must be a multiple of 512)\n");
        free(img); return 1;
    }
    uint32_t sec = (uint32_t)(real / DISK_SECTOR_SIZE);
    printf("Image: %s (%u sectors, %ld B)\n", path, sec, real);
    printf("Boot: magic %02X %02X %s | signature: %c%c%c%c\n",
           img[510], img[511],
           (img[510] == 0x55 && img[511] == 0xAA) ? "OK" : "NONE",
           img[0x1F0], img[0x1F0+1], img[0x1F0+2], img[0x1F0+3]);
    if (sec < 2) { printf("No catalog sector.\n"); free(img); return 0; }
    uint8_t *cat = img + DISK_SECTOR_SIZE;
    if (memcmp(cat, "DM16", 4) != 0) {
        printf("No DM16 catalog in sector 1.\n");
        free(img); return 0;
    }
    uint16_t n = get16(cat, 4);
    printf("Files: %u\n", n);
    for (int i = 0; i < n && i < DISK_FILE_MAX; i++) {
        uint8_t *e = cat + 6 + i * CAT_ENTRY_SIZE;
        char name[13]; memcpy(name, e, 12); name[12] = 0;
        uint16_t lba = get16(e, 12), ns = get16(e, 14);
        uint32_t sz = (uint32_t)get16(e, 16) | ((uint32_t)get16(e, 18) << 16);
        uint16_t hint = get16(e, 20);
        printf("  %-12s LBA=%-5u nsec=%-4u size=%-6u load=0x%04X\n",
               name, lba, ns, sz, hint);
    }
    free(img);
    return 0;
}

int main(int argc, char **argv) {
    const char *boot = NULL;
    const char *out = "dimon.iso";
    const char *inputs[DISK_FILE_MAX];
    const char *names[DISK_FILE_MAX];
    int ninputs = 0;

    if (argc < 2) { usage(argv[0]); return 1; }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--list") && i + 1 < argc) {
            return list_iso(argv[++i]);
        } else if (!strcmp(argv[i], "-b") && i + 1 < argc) {
            boot = argv[++i];
        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out = argv[++i];
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]); return 0;
        } else if (argv[i][0] == '-') {
            usage(argv[0]); return 1;
        } else {
            char *sep = strchr(argv[i], ':');
            if (sep && strchr(sep + 1, '/')) sep = NULL;
            if (!boot) {
                boot = argv[i];
            } else {
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
    }
    if (!boot) { usage(argv[0]); return 1; }

    size_t boot_len = 0;
    uint8_t *boot_data = read_file(boot, &boot_len);
    if (!boot_data) { perror("boot fopen"); return 1; }
    {
        FILE *f = fopen(boot, "rb");
        fseek(f, 0, SEEK_END);
        long real = ftell(f);
        fclose(f);
        boot_len = (size_t)(real < 0 ? 0 : real);
        if (real == 0) { fprintf(stderr, "Empty bootloader: %s\n", boot); free(boot_data); return 1; }
    }
    if (boot_len > 510) {
        fprintf(stderr, "Bootloader too large (%zu B, max 510)\n", boot_len);
        free(boot_data); return 1;
    }

    /* Load file data */
    uint8_t *fdata[DISK_FILE_MAX];
    size_t flen[DISK_FILE_MAX];
    uint32_t fsec[DISK_FILE_MAX];
    for (int i = 0; i < ninputs; i++) {
        flen[i] = 0;
        fdata[i] = read_file(inputs[i], &flen[i]);
        if (!fdata[i]) {
            perror("file fopen");
            fprintf(stderr, "Cannot read: %s\n", inputs[i]);
            for (int k = 0; k < i; k++) free(fdata[k]);
            free(boot_data); return 1;
        }
        FILE *f = fopen(inputs[i], "rb");
        fseek(f, 0, SEEK_END);
        long real = ftell(f);
        fclose(f);
        if (real <= 0) { fprintf(stderr, "Empty file: %s\n", inputs[i]); for (int k = 0; k <= i; k++) free(fdata[k]); free(boot_data); return 1; }
        flen[i] = (size_t)real;
        fsec[i] = (uint32_t)((flen[i] + DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE);
    }

    uint32_t total = 2; /* boot + catalog */
    for (int i = 0; i < ninputs; i++) total += fsec[i];
    if (total > DISK_MAX_SECTORS) {
        fprintf(stderr, "Image too large (%u sectors, max %d)\n", total, DISK_MAX_SECTORS);
        for (int k = 0; k < ninputs; k++) free(fdata[k]);
        free(boot_data); return 1;
    }

    uint8_t *img = calloc(total, DISK_SECTOR_SIZE);
    if (!img) { perror("calloc"); return 1; }
    memcpy(img, boot_data, boot_len);
    img[0x1F0] = 'D'; img[0x1F0+1] = 'I'; img[0x1F0+2] = 'M'; img[0x1F0+3] = 'N';
    img[510] = DISK_BOOT_MAGIC0; img[511] = DISK_BOOT_MAGIC1;

    uint8_t *cat = img + DISK_SECTOR_SIZE;
    memcpy(cat, "DM16", 4);
    put16(cat, 4, (uint16_t)ninputs);
    uint32_t lba = 2;
    for (int i = 0; i < ninputs; i++) {
        uint8_t *e = cat + 6 + i * CAT_ENTRY_SIZE;
        memset(e, 0, CAT_ENTRY_SIZE);
        strncpy((char *)e, names[i], 12);
        put16(e, 12, (uint16_t)lba);
        put16(e, 14, (uint16_t)fsec[i]);
        put16(e, 16, (uint16_t)(flen[i] & 0xFFFF));
        put16(e, 18, (uint16_t)((flen[i] >> 16) & 0xFFFF));
        put16(e, 20, 0x0000); /* load_hint */
        memcpy(img + lba * DISK_SECTOR_SIZE, fdata[i], flen[i]);
        printf("  %-12.12s <- %s (%zu B, LBA %u, %u sec)\n",
               names[i], inputs[i], flen[i], lba, fsec[i]);
        lba += fsec[i];
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
