#include "shell.h"
#include "kernel.h"
#include "vfs.h"
#include "timefmt.h"
#include "fbterm.h"
#include "window.h"
#include "serial.h"
#include <stdarg.h>

#define HIST_MAX 64
#define CMD_MAX 40

static char history[HIST_MAX][256];
static int hist_count = 0;
static int hist_pos = 0;

static const char *cmds[] = {
    "help","uptime","clear","info","reboot","poweroff","uname","date","cpuinfo","meminfo",
    "ls","cd","pwd","cat","mkdir","rm","rmdir","cp","mv","touch","find","du",
    "ps","kill","sleep","echo","head","wc","hexdump","grep","sort",
    "ifconfig","netstat","history","cal","whoami","arch","ver","env","time","which"
};

static const char *cmd_help[] = {
    "System: help uptime clear info reboot poweroff uname date cpuinfo meminfo",
    "Files:  ls cd pwd cat mkdir rm rmdir cp mv touch find du",
    "Process: ps kill sleep",
    "Network: ifconfig netstat",
    "Text:   echo head wc hexdump grep sort",
    "Misc:   history cal whoami arch ver env time which"
};

static char current_line[256];
static int line_pos = 0;
static char cwd[256] = "/home/user";
static int tab_index = 0;
static int tab_prefix_len = 0;

static void shell_write(const char *s) { fbterm_write(s); }
static void shell_printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); char buf[512]; int pos = 0;
    for (const char *p = fmt; *p && pos < 511; p++) {
        if (*p == '%') { p++;
            if (*p == 's') { const char *s = va_arg(ap, const char *); while (*s && pos < 511) buf[pos++] = *s++; }
            else if (*p == 'd') { int v = va_arg(ap, int); if (v < 0) { buf[pos++] = '-'; v = -v; } char t[16]; int tp = 0; do { t[tp++] = '0' + (v % 10); v /= 10; } while (v); while (tp > 0) buf[pos++] = t[--tp]; }
            else if (*p == 'u') { unsigned v = va_arg(ap, unsigned); char t[16]; int tp = 0; do { t[tp++] = '0' + (v % 10); v /= 10; } while (v); while (tp > 0) buf[pos++] = t[--tp]; }
            else if (*p == 'x') { unsigned v = va_arg(ap, unsigned); char t[16]; int tp = 0; do { int d = v % 16; t[tp++] = d < 10 ? '0'+d : 'a'+d-10; v /= 16; } while (v); while (tp > 0) buf[pos++] = t[--tp]; }
            else if (*p == 'l') { if (*(p+1) == 'l') p++; if (*(p+1) == 'u'||*(p+1)=='d') { int sg = (*(p+1)=='d'); p++; long long v = va_arg(ap, long long); if (sg && v < 0) { buf[pos++] = '-'; v = -v; } unsigned long long uv = v; char t[24]; int tp = 0; do { t[tp++] = '0' + (uv % 10); uv /= 10; } while (uv); while (tp > 0) buf[pos++] = t[--tp]; }
            } else if (*p == 'p') { uint64_t v = va_arg(ap, uint64_t); buf[pos++] = '0'; buf[pos++] = 'x'; char t[16]; int tp = 0; do { int d = v % 16; t[tp++] = d < 10 ? '0'+d : 'a'+d-10; v /= 16; } while (v); while (tp > 0) buf[pos++] = t[--tp]; }
            else if (*p == 'c') { buf[pos++] = (char)va_arg(ap, int); }
            else buf[pos++] = *p;
        } else buf[pos++] = *p;
    }
    buf[pos] = 0; va_end(ap); shell_write(buf);
}

static void add_history(const char *line) {
    if (!line[0]) return;
    if (hist_count > 0 && strcmp(history[hist_count - 1], line) == 0) return;
    if (hist_count < HIST_MAX) {
        strcpy(history[hist_count], line); hist_count++;
    } else {
        for (int i = 0; i < HIST_MAX - 1; i++) strcpy(history[i], history[i + 1]);
        strcpy(history[HIST_MAX - 1], line);
    }
    hist_pos = hist_count;
}

int shell_history_count(void) { return hist_count; }
const char *shell_history_get(int i) { return (i >= 0 && i < hist_count) ? history[i] : 0; }

static int split_line(const char *line, char **args, int max_args) {
    int n = 0;
    while (*line && n < max_args) {
        while (*line == ' ') line++;
        if (!*line) break;
        if (*line == '"') {
            line++;
            int p = 0;
            args[n] = (char *)kmalloc(256);
            while (*line && *line != '"' && p < 255) args[n][p++] = *line++;
            args[n][p] = 0;
            if (*line) line++;
            n++;
        } else {
            int p = 0;
            args[n] = (char *)kmalloc(256);
            while (*line && *line != ' ' && p < 255) args[n][p++] = *line++;
            args[n][p] = 0;
            n++;
        }
    }
    return n;
}

static void free_args(char **args, int n) { for (int i = 0; i < n; i++) if (args[i]) kfree(args[i]); }

static const char *find_command(const char *name) {
    for (int i = 0; i < (int)(sizeof(cmds)/sizeof(cmds[0])); i++)
        if (strcmp(cmds[i], name) == 0) return cmds[i];
    return 0;
}

void shell_init(void) {
    cwd[0] = '/'; cwd[1] = 0;
}

const char *shell_prompt(void) {
    static char prompt[64];
    int p = 0;
    prompt[p++] = 'u'; prompt[p++] = 's'; prompt[p++] = 'e'; prompt[p++] = 'r';
    prompt[p++] = '@'; prompt[p++] = 'U'; prompt[p++] = 'n'; prompt[p++] = 'i'; prompt[p++] = 'q';
    prompt[p++] = 'O'; prompt[p++] = 'S'; prompt[p++] = ':'; 
    const char *cp = cwd; while (*cp) prompt[p++] = *cp++;
    prompt[p++] = '$'; prompt[p++] = ' '; prompt[p] = 0;
    return prompt;
}

static void cmd_help_func(char **args, int n) {
    (void)args; (void)n;
    for (int i = 0; i < 6; i++) shell_printf("%s\n", cmd_help[i]);
}

static void cmd_uptime(char **args, int n) {
    (void)args; (void)n;
    shell_printf("Uptime: %s\n", timefmt_uptime(timer_get_ms() / 1000));
}

static void cmd_clear(char **args, int n) { (void)args; (void)n; fbterm_clear(); }

static void cmd_info(char **args, int n) {
    (void)args; (void)n;
    shell_printf("UniqOS %s | %d MB free | %d KB total\n", UNIQOS_VERSION,
        (int)(pmm_get_free_count() * 4 / 1024), (int)(pmm_get_free_count() * 4));
}

static void cmd_reboot(char **args, int n) {
    (void)args; (void)n;
    shell_write("Rebooting...\n");
    while (inb(0x64) & 0x02);
    outb(0x64, 0xFE);
    for(;;) asm("hlt");
}

static void cmd_poweroff(char **args, int n) {
    (void)args; (void)n;
    shell_write("Power off...\n");
    outw(0xB004, 0x2000);
    outw(0x604, 0x2000);
    for(;;) asm("hlt");
}

static void cmd_uname(char **args, int n) {
    (void)args; (void)n;
    shell_printf("UniqOS %s x86_64\n", UNIQOS_VERSION);
}

static void cmd_date(char **args, int n) {
    (void)args; (void)n;
    shell_printf("%s\n", timefmt_clock(timer_get_ms()));
}

static void cmd_cpuinfo(char **args, int n) {
    (void)args; (void)n;
    shell_write("CPU: x86_64 (QEMU/VirtualBox virtual CPU)\n");
}

static void cmd_meminfo(char **args, int n) {
    (void)args; (void)n;
    uint64_t free = pmm_get_free_count();
    uint64_t total = 512 * 1024 / 4;
    shell_printf("Memory: total %s, free %s (%d%%)\n",
        numfmt_bytes((int)(total * 4096 / 1024)),
        numfmt_bytes((int)(free * 4096 / 1024)),
        (int)(free * 100 / total));
}

static void cmd_ls(char **args, int n) {
    const char *path = (n > 1) ? args[1] : cwd;
    char full[256];
    if (path[0] != '/') { int p = 0; const char *c = cwd; while (*c) full[p++] = *c++; full[p++] = '/'; while (*path) full[p++] = *path++; full[p] = 0; path = full; }
    const char *np = vfs_normalize(path, cwd);
    vnode_t entries[128];
    int cnt = vfs_list(np, entries, 128);
    if (cnt < 0) { shell_printf("ls: %s: not found\n", args[1]); return; }
    for (int i = 0; i < cnt; i++) {
        if (entries[i].is_dir) shell_write("d"); else shell_write("-");
        shell_write(entries[i].perms + 1);
        shell_write(" ");
        shell_printf("%6d ", entries[i].size);
        shell_write(timefmt_filedate(entries[i].modified));
        shell_write(" ");
        if (entries[i].is_dir) shell_write("\033[1m");
        shell_write(entries[i].name);
        if (entries[i].is_dir) shell_write("/");
        if (entries[i].is_dir) shell_write("\033[0m");
        shell_write("\n");
    }
}

static void cmd_cd(char **args, int n) {
    if (n < 2) return;
    const char *path = args[1];
    char full[256];
    if (path[0] != '/') { int p = 0; const char *c = cwd; while (*c) full[p++] = *c++; full[p++] = '/'; while (*path) full[p++] = *path++; full[p] = 0; path = full; }
    const char *np = vfs_normalize(path, cwd);
    if (vfs_is_dir(np)) {
        int i = 0; for (; np[i] && i < 255; i++) cwd[i] = np[i];
        cwd[i] = 0;
    } else {
        shell_printf("cd: %s: not a directory\n", args[1]);
    }
}

static void cmd_pwd(char **args, int n) { (void)args; (void)n; shell_printf("%s\n", cwd); }

static void cmd_cat(char **args, int n) {
    if (n < 2) return;
    char full[256];
    const char *path = args[1];
    if (path[0] != '/') { int p = 0; const char *c = cwd; while (*c) full[p++] = *c++; full[p++] = '/'; while (*path) full[p++] = *path++; full[p] = 0; path = full; }
    const char *np = vfs_normalize(path, cwd);
    char buf[4096];
    int r = vfs_read(np, buf, 4095);
    if (r < 0) { shell_printf("cat: %s: not found\n", args[1]); return; }
    buf[r] = 0;
    shell_write(buf);
    if (r > 0 && buf[r-1] != '\n') shell_write("\n");
}

static void cmd_mkdir(char **args, int n) {
    if (n < 2) return;
    char full[256];
    const char *path = args[1];
    if (path[0] != '/') { int p = 0; const char *c = cwd; while (*c) full[p++] = *c++; full[p++] = '/'; while (*path) full[p++] = *path++; full[p] = 0; path = full; }
    const char *np = vfs_normalize(path, cwd);
    if (vfs_mkdir(np) < 0) shell_printf("mkdir: %s: failed\n", args[1]);
}

static void cmd_rm(char **args, int n) {
    if (n < 2) return;
    char full[256];
    const char *path = args[1];
    if (path[0] != '/') { int p = 0; const char *c = cwd; while (*c) full[p++] = *c++; full[p++] = '/'; while (*path) full[p++] = *path++; full[p] = 0; path = full; }
    const char *np = vfs_normalize(path, cwd);
    if (vfs_remove(np) < 0) shell_printf("rm: %s: failed\n", args[1]);
}

static void cmd_rmdir(char **args, int n) {
    if (n < 2) return;
    char full[256];
    const char *path = args[1];
    if (path[0] != '/') { int p = 0; const char *c = cwd; while (*c) full[p++] = *c++; full[p++] = '/'; while (*path) full[p++] = *path++; full[p] = 0; path = full; }
    const char *np = vfs_normalize(path, cwd);
    if (vfs_remove(np) < 0) shell_printf("rmdir: %s: failed\n", args[1]);
}

static void cmd_cp(char **args, int n) {
    if (n < 3) return;
    char full1[256], full2[256];
    const char *p1 = args[1];
    if (p1[0] != '/') { int p = 0; const char *c = cwd; while (*c) full1[p++] = *c++; full1[p++] = '/'; while (*p1) full1[p++] = *p1++; full1[p] = 0; p1 = full1; }
    const char *p2 = args[2];
    if (p2[0] != '/') { int p = 0; const char *c = cwd; while (*c) full2[p++] = *c++; full2[p++] = '/'; while (*p2) full2[p++] = *p2++; full2[p] = 0; p2 = full2; }
    const char *np1 = vfs_normalize(p1, cwd);
    const char *np2 = vfs_normalize(p2, cwd);
    char buf[4096]; int r = vfs_read(np1, buf, 4095);
    if (r < 0) { shell_printf("cp: %s: not found\n", args[1]); return; }
    buf[r] = 0;
    if (vfs_write(np2, buf) < 0) shell_printf("cp: %s: write failed\n", args[2]);
}

static void cmd_mv(char **args, int n) {
    if (n < 3) return;
    char full1[256], full2[256];
    const char *p1 = args[1];
    if (p1[0] != '/') { int p = 0; const char *c = cwd; while (*c) full1[p++] = *c++; full1[p++] = '/'; while (*p1) full1[p++] = *p1++; full1[p] = 0; p1 = full1; }
    const char *p2 = args[2];
    if (p2[0] != '/') { int p = 0; const char *c = cwd; while (*c) full2[p++] = *c++; full2[p++] = '/'; while (*p2) full2[p++] = *p2++; full2[p] = 0; p2 = full2; }
    const char *np1 = vfs_normalize(p1, cwd);
    const char *np2 = vfs_normalize(p2, cwd);
    if (vfs_rename(np1, np2) < 0) shell_printf("mv: failed\n");
}

static void cmd_touch(char **args, int n) {
    if (n < 2) return;
    char full[256];
    const char *path = args[1];
    if (path[0] != '/') { int p = 0; const char *c = cwd; while (*c) full[p++] = *c++; full[p++] = '/'; while (*path) full[p++] = *path++; full[p] = 0; path = full; }
    const char *np = vfs_normalize(path, cwd);
    if (!vfs_exists(np)) vfs_write(np, "");
}

static void cmd_find(char **args, int n) {
    const char *name = (n > 1) ? args[1] : "";
    vnode_t entries[128];
    int cnt = vfs_list(cwd, entries, 128);
    if (cnt < 0) return;
    for (int i = 0; i < cnt; i++) {
        if (name[0] == 0 || strstr(entries[i].name, name))
            shell_printf("%s/%s\n", cwd, entries[i].name);
    }
}

static void cmd_du(char **args, int n) {
    (void)args; (void)n;
    vnode_t entries[128];
    int cnt = vfs_list(cwd, entries, 128);
    if (cnt < 0) return;
    int total = 0;
    for (int i = 0; i < cnt; i++) { total += entries[i].size; shell_printf("%6d %s\n", entries[i].size, entries[i].name); }
    shell_printf("Total: %d bytes\n", total);
}

static void cmd_ps(char **args, int n) {
    (void)args; (void)n;
    shell_write("PID  NAME           STATE\n");
    shell_write("  1  kernel         running\n");
    shell_write("  2  desktop        running\n");
    shell_write("  3  shell          running\n");
}

static void cmd_kill(char **args, int n) {
    (void)args; (void)n;
    shell_write("kill: not implemented\n");
}

static void cmd_sleep_func(char **args, int n) {
    if (n < 2) return;
    int ms = 0; const char *s = args[1]; while (*s) { ms = ms * 10 + (*s - '0'); s++; }
    timer_sleep(ms);
}

static void cmd_echo(char **args, int n) {
    for (int i = 1; i < n; i++) { if (i > 1) shell_write(" "); shell_write(args[i]); }
    shell_write("\n");
}

static void cmd_head(char **args, int n) {
    if (n < 2) return;
    char full[256];
    const char *path = args[1];
    if (path[0] != '/') { int p = 0; const char *c = cwd; while (*c) full[p++] = *c++; full[p++] = '/'; while (*path) full[p++] = *path++; full[p] = 0; path = full; }
    const char *np = vfs_normalize(path, cwd);
    char buf[4096]; int r = vfs_read(np, buf, 4095);
    if (r < 0) { shell_printf("head: %s: not found\n", args[1]); return; }
    buf[r] = 0;
    int lines = 0; int i = 0;
    while (buf[i] && lines < 10) { shell_write(&buf[i]); if (buf[i] == '\n') lines++; i++; while (buf[i] && buf[i-1] != '\n') i++; }
}

static void cmd_wc(char **args, int n) {
    if (n < 2) return;
    char full[256];
    const char *path = args[1];
    if (path[0] != '/') { int p = 0; const char *c = cwd; while (*c) full[p++] = *c++; full[p++] = '/'; while (*path) full[p++] = *path++; full[p] = 0; path = full; }
    const char *np = vfs_normalize(path, cwd);
    char buf[4096]; int r = vfs_read(np, buf, 4095);
    if (r < 0) { shell_printf("wc: %s: not found\n", args[1]); return; }
    buf[r] = 0;
    int lines = 0, words = 0, inword = 0;
    for (int i = 0; i < r; i++) {
        if (buf[i] == '\n') lines++;
        if (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\t') inword = 0;
        else if (!inword) { inword = 1; words++; }
    }
    shell_printf("%6d %6d %6d %s\n", lines, words, r, args[1]);
}

static void cmd_hexdump(char **args, int n) {
    if (n < 2) return;
    char full[256];
    const char *path = args[1];
    if (path[0] != '/') { int p = 0; const char *c = cwd; while (*c) full[p++] = *c++; full[p++] = '/'; while (*path) full[p++] = *path++; full[p] = 0; path = full; }
    const char *np = vfs_normalize(path, cwd);
    char buf[4096]; int r = vfs_read(np, buf, 4095);
    if (r < 0) { shell_printf("hexdump: %s: not found\n", args[1]); return; }
    for (int i = 0; i < r; i += 16) {
        shell_printf("%08x  ", i);
        for (int j = 0; j < 16; j++) {
            if (i + j < r) shell_printf("%02x ", (unsigned char)buf[i + j]);
            else shell_write("   ");
        }
        shell_write(" ");
        for (int j = 0; j < 16 && i + j < r; j++)
            shell_printf("%c", (buf[i+j] >= 32 && buf[i+j] < 127) ? buf[i+j] : '.');
        shell_write("\n");
    }
}

static void cmd_grep(char **args, int n) {
    if (n < 3) return;
    char full[256];
    const char *path = args[2];
    if (path[0] != '/') { int p = 0; const char *c = cwd; while (*c) full[p++] = *c++; full[p++] = '/'; while (*path) full[p++] = *path++; full[p] = 0; path = full; }
    const char *np = vfs_normalize(path, cwd);
    char buf[4096]; int r = vfs_read(np, buf, 4095);
    if (r < 0) { shell_printf("grep: %s: not found\n", args[2]); return; }
    buf[r] = 0;
    const char *pat = args[1];
    char line[256]; int lp = 0;
    for (int i = 0; i <= r; i++) {
        if (buf[i] == '\n' || i == r) { line[lp] = 0; if (strstr(line, pat)) shell_printf("%s\n", line); lp = 0; }
        else if (lp < 255) line[lp++] = buf[i];
    }
}

static void cmd_sort(char **args, int n) {
    if (n < 2) return;
    char full[256];
    const char *path = args[1];
    if (path[0] != '/') { int p = 0; const char *c = cwd; while (*c) full[p++] = *c++; full[p++] = '/'; while (*path) full[p++] = *path++; full[p] = 0; path = full; }
    const char *np = vfs_normalize(path, cwd);
    char buf[4096]; int r = vfs_read(np, buf, 4095);
    if (r < 0) { shell_printf("sort: %s: not found\n", args[1]); return; }
    buf[r] = 0;
    char *lines[512]; int lc = 0;
    lines[lc] = buf;
    for (int i = 0; i < r; i++) { if (buf[i] == '\n') { buf[i] = 0; if (lc < 511) lines[++lc] = &buf[i+1]; } }
    lc++;
    for (int i = 0; i < lc - 1; i++) for (int j = 0; j < lc - 1 - i; j++) if (strcmp(lines[j], lines[j+1]) > 0) { char *t = lines[j]; lines[j] = lines[j+1]; lines[j+1] = t; }
    for (int i = 0; i < lc; i++) shell_printf("%s\n", lines[i]);
}

static void cmd_ifconfig(char **args, int n) {
    (void)args; (void)n;
    shell_write("lo: 127.0.0.1 netmask 255.0.0.0\n");
    shell_write("eth0: 10.0.2.15 netmask 255.255.255.0 gateway 10.0.2.1\n");
}

static void cmd_netstat(char **args, int n) {
    (void)args; (void)n;
    shell_write("Active connections:\n");
    shell_write("  TCP 10.0.2.15:80 -> example.com:80 ESTABLISHED\n");
}

static void cmd_history_func(char **args, int n) {
    (void)args; (void)n;
    for (int i = 0; i < hist_count; i++) shell_printf("%5d  %s\n", i + 1, history[i]);
}

static void cmd_cal(char **args, int n) {
    (void)args; (void)n;
    shell_write("  July 2026\n");
    shell_write("Su Mo Tu We Th Fr Sa\n");
    shell_write("          1  2  3  4\n");
    shell_write(" 5  6  7  8  9 10 11\n");
    shell_write("12 13 14 15 16 17 18\n");
    shell_write("19 20 21 22 23 24 25\n");
    shell_write("26 27 28 29 30 31\n");
}

static void cmd_whoami(char **args, int n) { (void)args; (void)n; shell_write("user\n"); }
static void cmd_arch(char **args, int n) { (void)args; (void)n; shell_write("x86_64\n"); }
static void cmd_ver(char **args, int n) { (void)args; (void)n; shell_printf("UniqOS version %s\n", UNIQOS_VERSION); }
static void cmd_env(char **args, int n) { (void)args; (void)n; shell_printf("HOME=%s\nSHELL=/bin/sh\nUSER=user\n", cwd); }

static void cmd_which(char **args, int n) {
    if (n < 2) return;
    if (find_command(args[1])) shell_printf("/bin/%s\n", args[1]);
    else shell_printf("%s not found\n", args[1]);
}

static void cmd_time_func(char **args, int n) {
    if (n < 2) return;
    uint64_t start = timer_get_ms();
    (void)args;
    shell_printf("time: not implemented for subcommands\n");
    uint64_t end = timer_get_ms();
    shell_printf("real: %llu ms\n", end - start);
}

typedef struct { const char *name; void (*func)(char **, int); } cmd_entry_t;

static cmd_entry_t cmd_table[] = {
    {"help", cmd_help_func}, {"uptime", cmd_uptime}, {"clear", cmd_clear}, {"info", cmd_info},
    {"reboot", cmd_reboot}, {"poweroff", cmd_poweroff}, {"uname", cmd_uname}, {"date", cmd_date},
    {"cpuinfo", cmd_cpuinfo}, {"meminfo", cmd_meminfo},
    {"ls", cmd_ls}, {"cd", cmd_cd}, {"pwd", cmd_pwd}, {"cat", cmd_cat},
    {"mkdir", cmd_mkdir}, {"rm", cmd_rm}, {"rmdir", cmd_rmdir}, {"cp", cmd_cp}, {"mv", cmd_mv},
    {"touch", cmd_touch}, {"find", cmd_find}, {"du", cmd_du},
    {"ps", cmd_ps}, {"kill", cmd_kill}, {"sleep", cmd_sleep_func},
    {"echo", cmd_echo}, {"head", cmd_head}, {"wc", cmd_wc}, {"hexdump", cmd_hexdump},
    {"grep", cmd_grep}, {"sort", cmd_sort},
    {"ifconfig", cmd_ifconfig}, {"netstat", cmd_netstat},
    {"history", cmd_history_func}, {"cal", cmd_cal}, {"whoami", cmd_whoami},
    {"arch", cmd_arch}, {"ver", cmd_ver}, {"env", cmd_env}, {"which", cmd_which},
    {"time", cmd_time_func}
};
static int cmd_count = sizeof(cmd_table) / sizeof(cmd_table[0]);

const char *shell_execute(const char *line) {
    add_history(line);
    char *args[32];
    int n = split_line(line, args, 32);
    if (n == 0) return 0;
    int handled = 0;
    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(cmd_table[i].name, args[0]) == 0) {
            cmd_table[i].func(args, n);
            handled = 1; break;
        }
    }
    if (!handled && args[0][0]) shell_printf("Unknown: %s (try 'help')\n", args[0]);
    free_args(args, n);
    return 0;
}

int shell_handle_char(char c) {
    if (c == '\n') {
        shell_write("\n");
        current_line[line_pos] = 0;
        shell_execute(current_line);
        line_pos = 0;
        shell_write(shell_prompt());
        return 1;
    }
    if (c == '\b') {
        if (line_pos > 0) { line_pos--; shell_write("\b \b"); }
        return 1;
    }
    if (c == '\t') {
        if (line_pos == 0) return 1;
        tab_prefix_len = line_pos;
        tab_index = 0;
        for (int i = 0; i < cmd_count; i++) {
            if (strncmp(cmd_table[i].name, current_line, line_pos) == 0) {
                if (tab_index < tab_prefix_len) { tab_index++; continue; }
                while (line_pos > 0) { line_pos--; shell_write("\b \b"); }
                int j = 0; for (; cmd_table[i].name[j]; j++) { current_line[j] = cmd_table[i].name[j]; shell_write(&cmd_table[i].name[j]); }
                current_line[j] = 0; line_pos = j;
                shell_write("  ");
                shell_write(cmd_table[i].name);
                return 1;
            }
        }
        return 1;
    }
    if (c >= ' ' && line_pos < 255) {
        current_line[line_pos++] = c;
        char s[2] = {c, 0};
        shell_write(s);
        return 1;
    }
    return 1;
}
