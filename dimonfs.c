/* Shared FAT16 service used by the desktop, Terminal, and loadable apps.
 * The implementation deliberately supports short 8.3 names only.  It accepts
 * the historical small DimonOS FAT16 geometry while validating every BPB and
 * cluster-chain access against the attached image.
 */
#include "dimonfs.h"

#ifndef BAREMETAL
#include <stdio.h>
#endif

#define ATTR_DIR 0x10u
#define ATTR_VOLUME 0x08u
#define ATTR_LFN 0x0fu
#define FAT_EOC 0xfff8u

typedef struct {
    uint8_t *image;
    uint32_t sectors;
    uint16_t reserved;
    uint8_t fats;
    uint8_t spc;
    uint16_t spf;
    uint16_t root_entries;
    uint32_t root_lba;
    uint32_t root_sectors;
    uint32_t data_lba;
    uint32_t clusters;
    uint32_t cluster_bytes;
} Fat;

typedef struct { int root; uint16_t cluster; } Dir;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | ((uint32_t)rd16(p + 2) << 16); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)v); wr16(p + 2, (uint16_t)(v >> 16)); }

static int mount_fs(VM *vm, Fat *f) {
    if (!vm || !vm->disk_data || !vm->disk_sectors) return DFS_ERR_NODISK;
    uint8_t *b = vm->disk_data;
    if (vm->disk_sectors > DISK_MAX_SECTORS || rd16(b + 11) != 512 ||
        b[13] == 0 || (b[13] & (b[13] - 1)) != 0 || !rd16(b + 14) ||
        !b[16] || !rd16(b + 22) || b[510] != 0x55 || b[511] != 0xaa)
        return DFS_ERR_INVALID;
    uint32_t total = rd16(b + 19);
    if (!total) total = rd32(b + 32);
    if (!total || total > vm->disk_sectors) return DFS_ERR_INVALID;
    memset(f, 0, sizeof(*f));
    f->image = b; f->sectors = total; f->reserved = rd16(b + 14);
    f->fats = b[16]; f->spc = b[13]; f->spf = rd16(b + 22);
    f->root_entries = rd16(b + 17);
    f->root_sectors = ((uint32_t)f->root_entries * 32u + 511u) / 512u;
    f->root_lba = f->reserved + (uint32_t)f->fats * f->spf;
    f->data_lba = f->root_lba + f->root_sectors;
    if (f->data_lba >= total) return DFS_ERR_INVALID;
    f->clusters = (total - f->data_lba) / f->spc;
    f->cluster_bytes = (uint32_t)f->spc * 512u;
    if (!f->clusters || (uint32_t)f->spf * 256u < f->clusters + 2u) return DFS_ERR_INVALID;
    return DFS_OK;
}

static uint16_t fat_get(Fat *f, uint16_t c) {
    if (c >= f->clusters + 2u) return 0xffff;
    return rd16(f->image + (uint32_t)f->reserved * 512u + (uint32_t)c * 2u);
}

static void fat_set(Fat *f, uint16_t c, uint16_t value) {
    for (uint8_t n = 0; n < f->fats; n++) {
        uint32_t off = ((uint32_t)f->reserved + (uint32_t)n * f->spf) * 512u + (uint32_t)c * 2u;
        wr16(f->image + off, value);
    }
}

static uint8_t *cluster_ptr(Fat *f, uint16_t c) {
    if (c < 2 || c >= f->clusters + 2u) return NULL;
    uint32_t lba = f->data_lba + ((uint32_t)c - 2u) * f->spc;
    if (lba + f->spc > f->sectors) return NULL;
    return f->image + lba * 512u;
}

static int valid_char(unsigned char c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return c == '$' || c == '%' || c == '\'' || c == '-' || c == '_' ||
           c == '@' || c == '~' || c == '`' || c == '!' || c == '(' ||
           c == ')' || c == '{' || c == '}' || c == '^' || c == '#';
}

static int name83(const char *s, uint8_t out[11]) {
    if (!s || !*s) return DFS_ERR_NAME;
    if (!strcmp(s, ".")) { memset(out, ' ', 11); out[0] = '.'; return DFS_OK; }
    if (!strcmp(s, "..")) { memset(out, ' ', 11); out[0] = out[1] = '.'; return DFS_OK; }
    memset(out, ' ', 11);
    int base = 0, ext = 0, seen_dot = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '.') {
            if (seen_dot || base == 0) return DFS_ERR_NAME;
            seen_dot = 1; continue;
        }
        if (c >= 0x80 || !valid_char(c)) return DFS_ERR_NAME;
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        if (!seen_dot) { if (base >= 8) return DFS_ERR_NAME; out[base++] = c; }
        else { if (ext >= 3) return DFS_ERR_NAME; out[8 + ext++] = c; }
    }
    return (base && (!seen_dot || ext)) ? DFS_OK : DFS_ERR_NAME;
}

static void display_name(const uint8_t *e, char out[13]) {
    int p = 0;
    for (int i = 0; i < 8 && e[i] != ' '; i++) out[p++] = (char)e[i];
    if (e[8] != ' ') {
        out[p++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++) out[p++] = (char)e[i];
    }
    out[p] = 0;
}

static int entry_usable(const uint8_t *e) {
    return e[0] != 0 && e[0] != 0xe5 && e[11] != ATTR_LFN && !(e[11] & ATTR_VOLUME);
}

static int dir_entry_at(Fat *f, Dir d, uint32_t slot, uint8_t **out) {
    if (d.root) {
        if (slot >= f->root_entries) return DFS_ERR_NOT_FOUND;
        *out = f->image + f->root_lba * 512u + slot * 32u;
        return DFS_OK;
    }
    uint32_t per = f->cluster_bytes / 32u, guard = 0;
    uint16_t c = d.cluster;
    while (slot >= per) {
        uint16_t next = fat_get(f, c);
        if (next >= FAT_EOC) return DFS_ERR_NOT_FOUND;
        if (next < 2 || next >= f->clusters + 2u || ++guard > f->clusters) return DFS_ERR_INVALID;
        c = next; slot -= per;
    }
    uint8_t *p = cluster_ptr(f, c);
    if (!p) return DFS_ERR_INVALID;
    *out = p + slot * 32u;
    return DFS_OK;
}

static int find_in_dir(Fat *f, Dir d, const uint8_t n[11], uint8_t **entry) {
    uint32_t limit = d.root ? f->root_entries : f->clusters * (f->cluster_bytes / 32u);
    for (uint32_t i = 0; i < limit; i++) {
        uint8_t *e; int rc = dir_entry_at(f, d, i, &e);
        if (rc) return rc;
        if (e[0] == 0) return DFS_ERR_NOT_FOUND;
        if (entry_usable(e) && !memcmp(e, n, 11)) { *entry = e; return DFS_OK; }
    }
    return DFS_ERR_NOT_FOUND;
}

static int next_component(const char **path, char out[13]) {
    const char *p = *path;
    while (*p == '/' || *p == '\\') p++;
    if (!*p) { *path = p; return 0; }
    int n = 0;
    while (*p && *p != '/' && *p != '\\') {
        if (n >= 12) return DFS_ERR_NAME;
        out[n++] = *p++;
    }
    out[n] = 0; *path = p; return 1;
}

static int resolve_dir(Fat *f, const char *path, Dir *out) {
    Dir d = { 1, 0 }; char part[13];
    if (!path || !*path || !strcmp(path, "/")) { *out = d; return DFS_OK; }
    for (;;) {
        int more = next_component(&path, part);
        if (more <= 0) { if (more < 0) return more; *out = d; return DFS_OK; }
        if (!strcmp(part, ".")) continue;
        if (!strcmp(part, "..") && d.root) continue;
        uint8_t n[11], *e; int rc = name83(part, n);
        if (rc) return rc;
        rc = find_in_dir(f, d, n, &e); if (rc) return rc;
        if (!(e[11] & ATTR_DIR)) return DFS_ERR_NOT_DIR;
        uint16_t c = rd16(e + 26);
        if (!strcmp(part, "..") && c == 0) d = (Dir){1, 0};
        else { if (!cluster_ptr(f, c)) return DFS_ERR_INVALID; d = (Dir){0, c}; }
    }
}

static int split_parent(Fat *f, const char *path, Dir *parent, uint8_t leaf[11]) {
    if (!path || !*path) return DFS_ERR_INVALID;
    char clean[260]; size_t n = strlen(path);
    if (n >= sizeof(clean)) return DFS_ERR_NAME;
    memcpy(clean, path, n + 1);
    while (n > 1 && (clean[n - 1] == '/' || clean[n - 1] == '\\')) clean[--n] = 0;
    char *slash = NULL;
    for (char *p = clean; *p; p++) if (*p == '/' || *p == '\\') slash = p;
    const char *name = slash ? slash + 1 : clean;
    if (slash) { if (slash == clean) slash[1] = 0; else *slash = 0; }
    int rc = name83(name, leaf); if (rc) return rc;
    return resolve_dir(f, slash ? clean : "/", parent);
}

static int resolve_entry(Fat *f, const char *path, Dir *parent, uint8_t **entry) {
    uint8_t n[11]; int rc = split_parent(f, path, parent, n);
    return rc ? rc : find_in_dir(f, *parent, n, entry);
}

static int alloc_cluster(Fat *f, uint16_t *out) {
    for (uint32_t c = 2; c < f->clusters + 2u && c <= 0xffef; c++) {
        if (fat_get(f, (uint16_t)c) == 0) {
            fat_set(f, (uint16_t)c, 0xffff); memset(cluster_ptr(f, (uint16_t)c), 0, f->cluster_bytes);
            *out = (uint16_t)c; return DFS_OK;
        }
    }
    return DFS_ERR_NO_SPACE;
}

static void free_chain(Fat *f, uint16_t c) {
    uint32_t guard = 0;
    while (c >= 2 && c < f->clusters + 2u && guard++ <= f->clusters) {
        uint16_t next = fat_get(f, c); fat_set(f, c, 0);
        if (next >= FAT_EOC) break;
        c = next;
    }
}

static int new_entry(Fat *f, Dir d, uint8_t **out) {
    uint32_t per = f->cluster_bytes / 32u;
    uint32_t limit = d.root ? f->root_entries : f->clusters * per;
    for (uint32_t i = 0; i < limit; i++) {
        uint8_t *e; int rc = dir_entry_at(f, d, i, &e);
        if (rc == DFS_ERR_NOT_FOUND && !d.root) {
            uint16_t last = d.cluster, next; uint32_t guard = 0;
            while ((next = fat_get(f, last)) < FAT_EOC) {
                if (next < 2 || ++guard > f->clusters) return DFS_ERR_INVALID;
                last = next;
            }
            uint16_t added; rc = alloc_cluster(f, &added); if (rc) return rc;
            fat_set(f, last, added); *out = cluster_ptr(f, added); return DFS_OK;
        }
        if (rc) return rc;
        if (e[0] == 0 || e[0] == 0xe5) { *out = e; return DFS_OK; }
    }
    return DFS_ERR_NO_SPACE;
}

static void fill_info(const uint8_t *e, Dimon64DirEnt *out) {
    memset(out, 0, sizeof(*out)); display_name(e, out->name);
    out->attributes = e[11]; out->first_cluster = rd16(e + 26); out->size = rd32(e + 28);
}

int dimonfs_stat(VM *vm, const char *path, Dimon64DirEnt *out) {
    Fat f; int rc = mount_fs(vm, &f); if (rc) return rc;
    if (!path || !out) return DFS_ERR_INVALID;
    if (!strcmp(path, "/") || !*path) {
        memset(out, 0, sizeof(*out)); strcpy(out->name, "/"); out->attributes = ATTR_DIR; return DFS_OK;
    }
    Dir d; uint8_t *e; rc = resolve_entry(&f, path, &d, &e); if (!rc) fill_info(e, out); return rc;
}

int dimonfs_list(VM *vm, const char *path, uint32_t index, Dimon64DirEnt *out) {
    Fat f; Dir d; int rc = mount_fs(vm, &f); if (rc) return rc;
    if (!out) return DFS_ERR_INVALID;
    rc = resolve_dir(&f, path, &d);
    if (rc) return rc;
    uint32_t limit = d.root ? f.root_entries : f.clusters * (f.cluster_bytes / 32u), visible = 0;
    for (uint32_t i = 0; i < limit; i++) {
        uint8_t *e; rc = dir_entry_at(&f, d, i, &e); if (rc) return DFS_ERR_NOT_FOUND;
        if (e[0] == 0) break;
        if (entry_usable(e)) { if (visible++ == index) { fill_info(e, out); return DFS_OK; } }
    }
    return DFS_ERR_NOT_FOUND;
}

int dimonfs_read(VM *vm, const char *path, uint32_t offset, void *buf, uint32_t cap, uint32_t *size) {
    Fat f; Dir d; uint8_t *e; int rc = mount_fs(vm, &f); if (rc) return rc;
    if ((!buf && cap) || !size) return DFS_ERR_INVALID;
    rc = resolve_entry(&f, path, &d, &e); if (rc) return rc;
    if (e[11] & ATTR_DIR) return DFS_ERR_IS_DIR;
    uint32_t len = rd32(e + 28); *size = len; if (offset >= len) return 0;
    uint32_t take = len - offset; if (take > cap) take = cap;
    uint16_t c = rd16(e + 26); uint32_t skip = offset, done = 0, guard = 0;
    while (skip >= f.cluster_bytes) {
        c = fat_get(&f, c); skip -= f.cluster_bytes;
        if (c < 2 || c >= FAT_EOC || ++guard > f.clusters) return DFS_ERR_INVALID;
    }
    while (done < take) {
        uint8_t *p = cluster_ptr(&f, c); if (!p) return DFS_ERR_INVALID;
        uint32_t chunk = f.cluster_bytes - skip; if (chunk > take - done) chunk = take - done;
        memcpy((uint8_t *)buf + done, p + skip, chunk); done += chunk; skip = 0;
        if (done < take) { c = fat_get(&f, c); if (c < 2 || c >= FAT_EOC || ++guard > f.clusters) return DFS_ERR_INVALID; }
    }
    return (int)done;
}

int dimonfs_write(VM *vm, const char *path, const void *buf, uint32_t len, int create, int truncate) {
    Fat f; Dir d; uint8_t *e = NULL; uint8_t n[11]; int rc = mount_fs(vm, &f); if (rc) return rc;
    if (!vm->disk_writable) return DFS_ERR_READ_ONLY;
    if ((!buf && len) || !truncate) return DFS_ERR_INVALID;
    rc = split_parent(&f, path, &d, n); if (rc) return rc;
    rc = find_in_dir(&f, d, n, &e);
    if (rc == DFS_ERR_NOT_FOUND) {
        if (!create) return rc;
        rc = new_entry(&f, d, &e);
        if (rc) return rc;
        memset(e, 0, 32); memcpy(e, n, 11); e[11] = 0x20;
    } else if (rc) return rc;
    if (e[11] & ATTR_DIR) return DFS_ERR_IS_DIR;
    uint32_t need = len ? (len + f.cluster_bytes - 1u) / f.cluster_bytes : 0;
    uint32_t free_count = 0; for (uint32_t c = 2; c < f.clusters + 2u; c++) if (fat_get(&f, (uint16_t)c) == 0) free_count++;
    uint16_t old = rd16(e + 26); uint32_t old_count = 0, guard = 0;
    for (uint16_t c = old; c >= 2 && c < FAT_EOC && guard++ <= f.clusters; c = fat_get(&f, c)) old_count++;
    if (need > free_count + old_count) return DFS_ERR_NO_SPACE;
    if (old >= 2) free_chain(&f, old);
    wr16(e + 26, 0); wr32(e + 28, 0);
    uint16_t first = 0, prev = 0; uint32_t done = 0;
    for (uint32_t i = 0; i < need; i++) {
        uint16_t c; rc = alloc_cluster(&f, &c);
        if (rc) { if (first) free_chain(&f, first); e[0] = 0xe5; return rc; }
        if (!first) first = c;
        if (prev) fat_set(&f, prev, c);
        prev = c;
        uint32_t chunk = len - done; if (chunk > f.cluster_bytes) chunk = f.cluster_bytes;
        memcpy(cluster_ptr(&f, c), (const uint8_t *)buf + done, chunk); done += chunk;
    }
    wr16(e + 26, first); wr32(e + 28, len); return dimonfs_sync(vm);
}

int dimonfs_mkdir(VM *vm, const char *path) {
    Fat f; Dir parent; uint8_t n[11], *e; int rc = mount_fs(vm, &f); if (rc) return rc;
    if (!vm->disk_writable) return DFS_ERR_READ_ONLY;
    rc = split_parent(&f, path, &parent, n); if (rc) return rc;
    if (!find_in_dir(&f, parent, n, &e)) return DFS_ERR_EXISTS;
    uint16_t c; rc = alloc_cluster(&f, &c); if (rc) return rc;
    rc = new_entry(&f, parent, &e); if (rc) { free_chain(&f, c); return rc; }
    memset(e, 0, 32); memcpy(e, n, 11); e[11] = ATTR_DIR; wr16(e + 26, c);
    uint8_t *p = cluster_ptr(&f, c);
    if (!p) { free_chain(&f, c); e[0] = 0xe5; return DFS_ERR_INVALID; }
    memset(p, 0, f.cluster_bytes);
    memset(p, ' ', 11); p[0] = '.'; p[11] = ATTR_DIR; wr16(p + 26, c);
    memset(p + 32, ' ', 11); p[32] = p[33] = '.'; p[43] = ATTR_DIR; wr16(p + 58, parent.root ? 0 : parent.cluster);
    return dimonfs_sync(vm);
}

int dimonfs_remove(VM *vm, const char *path) {
    Fat f; Dir d; uint8_t *e; int rc = mount_fs(vm, &f); if (rc) return rc;
    if (!vm->disk_writable) return DFS_ERR_READ_ONLY;
    rc = resolve_entry(&f, path, &d, &e); if (rc) return rc;
    uint16_t c = rd16(e + 26);
    if (e[11] & ATTR_DIR) {
        Dir child = {0, c}; uint32_t limit = f.clusters * (f.cluster_bytes / 32u);
        for (uint32_t i = 0; i < limit; i++) {
            uint8_t *x; rc = dir_entry_at(&f, child, i, &x); if (rc) break;
            if (x[0] == 0) break;
            if (entry_usable(x) && !(x[0] == '.' && (x[1] == ' ' || x[1] == '.'))) return DFS_ERR_NOT_EMPTY;
        }
    }
    if (c >= 2) free_chain(&f, c);
    e[0] = 0xe5;
    return dimonfs_sync(vm);
}

int dimonfs_rename(VM *vm, const char *oldp, const char *newp) {
    Fat f; Dir olddir, newdir; uint8_t *old, *exists, nn[11]; int rc = mount_fs(vm, &f); if (rc) return rc;
    if (!vm->disk_writable) return DFS_ERR_READ_ONLY;
    rc = resolve_entry(&f, oldp, &olddir, &old); if (rc) return rc;
    rc = split_parent(&f, newp, &newdir, nn); if (rc) return rc;
    if (!find_in_dir(&f, newdir, nn, &exists)) return DFS_ERR_EXISTS;
    uint8_t copy[32]; memcpy(copy, old, 32); memcpy(copy, nn, 11);
    if (olddir.root == newdir.root && olddir.cluster == newdir.cluster) memcpy(old, copy, 32);
    else {
        uint8_t *ne; rc = new_entry(&f, newdir, &ne); if (rc) return rc;
        memcpy(ne, copy, 32); old[0] = 0xe5;
        if ((copy[11] & ATTR_DIR) && rd16(copy + 26) >= 2) {
            uint8_t *dir = cluster_ptr(&f, rd16(copy + 26));
            if (dir) wr16(dir + 32 + 26, newdir.root ? 0 : newdir.cluster);
        }
    }
    return dimonfs_sync(vm);
}

int dimonfs_copy(VM *vm, const char *src, const char *dst) {
    Fat f; Dir d; uint8_t *e; int rc = mount_fs(vm, &f); if (rc) return rc;
    rc = resolve_entry(&f, src, &d, &e); if (rc) return rc;
    if (e[11] & ATTR_DIR) return DFS_ERR_IS_DIR;
    uint32_t len = rd32(e + 28);
    /* The advertised disk is at most 4 MiB. Use free guest-independent RAM as a
       bounded staging area, never silently truncating. */
    const uint32_t staging = 0x03000000u, limit = 0x00800000u;
    if (len > limit || (uint64_t)staging + len > vm->memsize) return DFS_ERR_TOO_LARGE;
    uint32_t size = 0; rc = dimonfs_read(vm, src, 0, vm->mem + staging, len, &size);
    if (rc < 0) return rc;
    return dimonfs_write(vm, dst, vm->mem + staging, size, 1, 1);
}

int dimonfs_sync(VM *vm) {
#ifdef BAREMETAL
    (void)vm; return DFS_OK;
#else
    if (!vm || !vm->disk_path[0]) return DFS_OK;
    FILE *fp = fopen(vm->disk_path, "r+b"); if (!fp) return DFS_ERR_IO;
    size_t bytes = (size_t)vm->disk_sectors * 512u;
    int ok = fwrite(vm->disk_data, 1, bytes, fp) == bytes && fflush(fp) == 0;
    if (fclose(fp) != 0) ok = 0;
    return ok ? DFS_OK : DFS_ERR_IO;
#endif
}

const char *dimonfs_error(int rc) {
    switch (rc) {
        case DFS_OK: return "success"; case DFS_ERR_NODISK: return "no disk";
        case DFS_ERR_INVALID: return "invalid FAT16 volume or path";
        case DFS_ERR_NOT_FOUND: return "not found"; case DFS_ERR_EXISTS: return "already exists";
        case DFS_ERR_NOT_DIR: return "not a directory"; case DFS_ERR_IS_DIR: return "is a directory";
        case DFS_ERR_NO_SPACE: return "disk or directory full"; case DFS_ERR_READ_ONLY: return "read-only disk";
        case DFS_ERR_NOT_EMPTY: return "directory not empty"; case DFS_ERR_NAME: return "invalid 8.3 name";
        case DFS_ERR_IO: return "host I/O failure"; case DFS_ERR_TOO_LARGE: return "file too large";
        default: return "filesystem error";
    }
}
