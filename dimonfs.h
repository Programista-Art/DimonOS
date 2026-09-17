#ifndef DIMONFS_H
#define DIMONFS_H

#include "dimon64.h"

enum {
    DFS_OK = 0,
    DFS_ERR_NODISK = -1,
    DFS_ERR_INVALID = -2,
    DFS_ERR_NOT_FOUND = -3,
    DFS_ERR_EXISTS = -4,
    DFS_ERR_NOT_DIR = -5,
    DFS_ERR_IS_DIR = -6,
    DFS_ERR_NO_SPACE = -7,
    DFS_ERR_READ_ONLY = -8,
    DFS_ERR_NOT_EMPTY = -9,
    DFS_ERR_NAME = -10,
    DFS_ERR_IO = -11,
    DFS_ERR_TOO_LARGE = -12
};

int dimonfs_stat(VM *vm, const char *path, Dimon64DirEnt *out);
int dimonfs_list(VM *vm, const char *path, uint32_t index, Dimon64DirEnt *out);
int dimonfs_read(VM *vm, const char *path, uint32_t offset, void *buf,
                 uint32_t capacity, uint32_t *file_size);
int dimonfs_write(VM *vm, const char *path, const void *buf, uint32_t length,
                  int create, int truncate);
int dimonfs_mkdir(VM *vm, const char *path);
int dimonfs_remove(VM *vm, const char *path);
int dimonfs_rename(VM *vm, const char *old_path, const char *new_path);
int dimonfs_copy(VM *vm, const char *source, const char *destination);
int dimonfs_sync(VM *vm);
const char *dimonfs_error(int rc);

#endif
