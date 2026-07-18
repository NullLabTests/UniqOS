#pragma once
#include <stdint.h>

typedef struct { char name[64]; int is_dir; int size; uint64_t modified; char perms[12]; } vnode_t;

void vfs_init(void);
int vfs_list(const char *path, vnode_t *entries, int max);
int vfs_exists(const char *path);
int vfs_is_dir(const char *path);
int vfs_read(const char *path, char *buf, int max);
int vfs_write(const char *path, const char *contents);
int vfs_mkdir(const char *path);
int vfs_remove(const char *path);
int vfs_rename(const char *from, const char *to);
char *vfs_normalize(const char *path, const char *cwd);
