#include "vfs.h"
#include "kernel.h"
#include "heap.h"
#include "timefmt.h"
#include <stdint.h>

#define MAX_FILES 256
#define MAX_PATH 256

typedef struct fnode {
    char name[64];
    int is_dir;
    char *content;
    int size;
    int cap;
    uint64_t modified;
    struct fnode *parent;
    struct fnode *children[MAX_FILES];
    int child_count;
} fnode_t;

static fnode_t root;
static int got_ms = 0;

static fnode_t *vfs_create_node(fnode_t *parent, const char *name, int is_dir) {
    fnode_t *n = (fnode_t *)kmalloc(sizeof(fnode_t));
    int i = 0; for (; name[i] && i < 63; i++) n->name[i] = name[i];
    n->name[i] = 0;
    n->is_dir = is_dir;
    n->content = 0; n->size = 0; n->cap = 0;
    n->modified = got_ms ? timer_get_ms() : 0;
    n->parent = parent;
    n->child_count = 0;
    if (parent && parent->child_count < MAX_FILES)
        parent->children[parent->child_count++] = n;
    return n;
}

void vfs_init(void) {
    got_ms = 1;
    root.name[0] = '/'; root.name[1] = 0;
    root.is_dir = 1; root.parent = 0; root.child_count = 0;
    root.modified = timer_get_ms();
    vfs_create_node(&root, "home", 1);
    vfs_create_node(&root, "tmp", 1);
    vfs_create_node(&root, "bin", 1);
    fnode_t *home = root.children[0];
    vfs_create_node(home, "user", 1);
    vfs_create_node(home, ".config", 1);
    fnode_t *user = home->children[0];
    vfs_write("/home/user/welcome.txt", "Welcome to UniqOS!\nThis is the in-memory filesystem.\n");
    vfs_write("/home/user/notes.txt", "UniqOS v1.0.0\n- x86_64 bare metal\n- GUI desktop\n- Networking\n");
}

static fnode_t *vfs_resolve(const char *path) {
    if (!path || !*path) return 0;
    if (path[0] == '/') path++;
    fnode_t *cur = &root;
    if (!*path) return cur;
    char buf[MAX_PATH]; int bp = 0;
    while (1) {
        while (*path == '/') path++;
        if (!*path) break;
        bp = 0;
        while (*path && *path != '/' && bp < MAX_PATH - 1) buf[bp++] = *path++;
        buf[bp] = 0;
        if (strcmp(buf, "..") == 0) {
            if (cur->parent) cur = cur->parent;
        } else if (strcmp(buf, ".") == 0) {
        } else {
            fnode_t *found = 0;
            for (int i = 0; i < cur->child_count; i++)
                if (strcmp(cur->children[i]->name, buf) == 0) { found = cur->children[i]; break; }
            if (!found) return 0;
            cur = found;
        }
    }
    return cur;
}

int vfs_list(const char *path, vnode_t *entries, int max) {
    fnode_t *dir = vfs_resolve(path);
    if (!dir || !dir->is_dir) return -1;
    int n = 0;
    for (int i = 0; i < dir->child_count && n < max; i++) {
        fnode_t *c = dir->children[i];
        int j = 0; for (; c->name[j] && j < 63; j++) entries[n].name[j] = c->name[j];
        entries[n].name[j] = 0;
        entries[n].is_dir = c->is_dir;
        entries[n].size = c->size;
        entries[n].modified = c->modified;
        entries[n].perms[0] = c->is_dir ? 'd' : '-';
        entries[n].perms[1] = 'r'; entries[n].perms[2] = 'w';
        entries[n].perms[3] = c->is_dir ? 'x' : '-';
        entries[n].perms[4] = 'r'; entries[n].perms[5] = '-';
        entries[n].perms[6] = '-'; entries[n].perms[7] = 'r';
        entries[n].perms[8] = '-'; entries[n].perms[9] = '-';
        entries[n].perms[10] = 0;
        n++;
    }
    return n;
}

int vfs_exists(const char *path) { return vfs_resolve(path) != 0; }
int vfs_is_dir(const char *path) { fnode_t *n = vfs_resolve(path); return n && n->is_dir; }

int vfs_read(const char *path, char *buf, int max) {
    fnode_t *n = vfs_resolve(path);
    if (!n || n->is_dir || !n->content) return -1;
    int sz = n->size < max ? n->size : max;
    memcpy(buf, n->content, sz);
    return sz;
}

int vfs_write(const char *path, const char *contents) {
    fnode_t *n = vfs_resolve(path);
    if (!n || n->is_dir) {
        const char *slash = path;
        const char *last = path;
        while (*slash) { if (*slash == '/') last = slash + 1; slash++; }
        char parent_path[MAX_PATH]; int pp = 0;
        const char *p = path;
        while (p < last) parent_path[pp++] = *p++;
        if (pp == 0) { parent_path[0] = '/'; parent_path[1] = 0; }
        else parent_path[--pp] = 0;
        fnode_t *par = vfs_resolve(parent_path);
        if (!par || !par->is_dir) return -1;
        char name[64]; int np = 0;
        const char *l = last;
        while (*l && np < 63) name[np++] = *l++;
        name[np] = 0;
        n = vfs_create_node(par, name, 0);
        if (!n) return -1;
    }
    int len = strlen(contents);
    if (len + 1 > n->cap) {
        int new_cap = len + 64;
        char *newc = (char *)kmalloc(new_cap);
        if (n->content) kfree(n->content);
        n->content = newc; n->cap = new_cap;
    }
    memcpy(n->content, contents, len + 1);
    n->size = len;
    n->modified = got_ms ? timer_get_ms() : 0;
    return len;
}

int vfs_mkdir(const char *path) {
    const char *slash = path;
    const char *last = path;
    while (*slash) { if (*slash == '/') last = slash + 1; slash++; }
    char parent_path[MAX_PATH]; int pp = 0;
    const char *p = path;
    while (p < last) parent_path[pp++] = *p++;
    if (pp == 0) { parent_path[0] = '/'; parent_path[1] = 0; }
    else parent_path[--pp] = 0;
    fnode_t *par = vfs_resolve(parent_path);
    if (!par || !par->is_dir) return -1;
    char name[64]; int np = 0;
    const char *l = last;
    while (*l && np < 63) name[np++] = *l++;
    name[np] = 0;
    return vfs_create_node(par, name, 1) ? 0 : -1;
}

int vfs_remove(const char *path) {
    fnode_t *n = vfs_resolve(path);
    if (!n || n == &root) return -1;
    fnode_t *par = n->parent;
    if (!par) return -1;
    if (n->is_dir && n->child_count > 0) return -1;
    for (int i = 0; i < par->child_count; i++)
        if (par->children[i] == n) {
            for (int j = i; j < par->child_count - 1; j++)
                par->children[j] = par->children[j + 1];
            par->child_count--;
            if (n->content) kfree(n->content);
            kfree(n);
            return 0;
        }
    return -1;
}

int vfs_rename(const char *from, const char *to) {
    fnode_t *n = vfs_resolve(from);
    if (!n || n == &root) return -1;
    const char *slash = to;
    const char *last = to;
    while (*slash) { if (*slash == '/') last = slash + 1; slash++; }
    char name[64]; int np = 0;
    const char *l = last;
    while (*l && np < 63) name[np++] = *l++;
    name[np] = 0;
    int i = 0; for (; name[i] && i < 63; i++) n->name[i] = name[i];
    n->name[i] = 0;
    return 0;
}

char *vfs_normalize(const char *path, const char *cwd) {
    static char buf[MAX_PATH];
    char stack[MAX_PATH][64]; int sp = 0;
    const char *p;
    if (path[0] == '/') p = path + 1;
    else p = cwd ? cwd : "";
    buf[0] = 0;
    char tmp[MAX_PATH]; int tp = 0;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        tp = 0;
        while (*p && *p != '/' && tp < MAX_PATH - 1) tmp[tp++] = *p++;
        tmp[tp] = 0;
        if (strcmp(tmp, "..") == 0) { if (sp > 0) sp--; }
        else if (strcmp(tmp, ".") != 0 && tmp[0]) {
            int i = 0; for (; tmp[i] && i < 63; i++) stack[sp][i] = tmp[i];
            stack[sp][i] = 0; sp++;
        }
    }
    if (sp == 0) { buf[0] = '/'; buf[1] = 0; return buf; }
    int bp = 0;
    for (int i = 0; i < sp; i++) { buf[bp++] = '/'; int j = 0; while (stack[i][j]) buf[bp++] = stack[i][j++]; }
    buf[bp] = 0;
    return buf;
}
