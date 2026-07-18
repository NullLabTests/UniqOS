#include "timefmt.h"

static const char *wdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};

static int is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

civil_date_t civil_from_ms(uint64_t ms) {
    uint64_t secs = ms / 1000;
    int d = secs / 86400;
    int t = secs % 86400;
    int y = 1970;
    while (d >= (is_leap(y) ? 366 : 365)) { d -= is_leap(y) ? 366 : 365; y++; }
    int m = 0;
    while (1) {
        int dim = mdays[m] + (m == 1 && is_leap(y) ? 1 : 0);
        if (d < dim) break;
        d -= dim; m++;
    }
    civil_date_t cd = {y, m + 1, d + 1, t / 3600, (t % 3600) / 60, t % 60, (secs / 86400 + 4) % 7};
    return cd;
}

static char fmt_buf[64];

const char *timefmt_clock(uint64_t ms) {
    civil_date_t cd = civil_from_ms(ms);
    int p = 0;
    fmt_buf[p++] = '0' + cd.hour / 10; fmt_buf[p++] = '0' + cd.hour % 10; fmt_buf[p++] = ':';
    fmt_buf[p++] = '0' + cd.minute / 10; fmt_buf[p++] = '0' + cd.minute % 10; fmt_buf[p++] = ':';
    fmt_buf[p++] = '0' + cd.second / 10; fmt_buf[p++] = '0' + cd.second % 10; fmt_buf[p] = 0;
    return fmt_buf;
}

const char *timefmt_filedate(uint64_t ms) {
    civil_date_t cd = civil_from_ms(ms);
    int p = 0;
    const char *m = months[cd.month - 1];
    fmt_buf[p++] = m[0]; fmt_buf[p++] = m[1]; fmt_buf[p++] = m[2]; fmt_buf[p++] = ' ';
    if (cd.day < 10) fmt_buf[p++] = ' ';
    if (cd.day >= 10) fmt_buf[p++] = '0' + cd.day / 10;
    fmt_buf[p++] = '0' + cd.day % 10; fmt_buf[p++] = ' ';
    fmt_buf[p++] = '0' + cd.hour / 10; fmt_buf[p++] = '0' + cd.hour % 10; fmt_buf[p++] = ':';
    fmt_buf[p++] = '0' + cd.minute / 10; fmt_buf[p++] = '0' + cd.minute % 10; fmt_buf[p] = 0;
    return fmt_buf;
}

const char *timefmt_uptime(uint64_t seconds) {
    int d = seconds / 86400, h = (seconds % 86400) / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
    int p = 0;
    if (d) { fmt_buf[p++] = '0' + d; fmt_buf[p++] = 'd'; }
    fmt_buf[p++] = '0' + h / 10; fmt_buf[p++] = '0' + h % 10; fmt_buf[p++] = ':';
    fmt_buf[p++] = '0' + m / 10; fmt_buf[p++] = '0' + m % 10; fmt_buf[p++] = ':';
    fmt_buf[p++] = '0' + s / 10; fmt_buf[p++] = '0' + s % 10; fmt_buf[p] = 0;
    return fmt_buf;
}

const char *numfmt_f1(double v) {
    int p = 0; int iv = (int)v; int f = (int)((v - iv) * 10 + 0.5); if (f < 0) f = 0; if (f > 9) f = 9;
    char t[16]; int tp = 0; do { t[tp++] = '0' + (iv % 10); iv /= 10; } while (iv);
    while (tp) fmt_buf[p++] = t[--tp];
    fmt_buf[p++] = '.'; fmt_buf[p++] = '0' + f; fmt_buf[p] = 0;
    return fmt_buf;
}

const char *numfmt_f2(double v) {
    int p = 0; int iv = (int)v; int f = (int)((v - iv) * 100 + 0.5); if (f < 0) f = 0; if (f > 99) f = 99;
    char t[16]; int tp = 0; do { t[tp++] = '0' + (iv % 10); iv /= 10; } while (iv);
    while (tp) fmt_buf[p++] = t[--tp];
    fmt_buf[p++] = '.'; fmt_buf[p++] = '0' + f / 10; fmt_buf[p++] = '0' + f % 10; fmt_buf[p] = 0;
    return fmt_buf;
}

const char *numfmt_bytes(int count) {
    const char *units[] = {"B","KB","MB","GB"};
    int u = 0; double v = count;
    while (v >= 1024 && u < 3) { v /= 1024; u++; }
    int p = 0; int iv = (int)v; int f = (int)((v - iv) * 10 + 0.5); if (f < 0) f = 0; if (f > 9) f = 9;
    char t[16]; int tp = 0; do { t[tp++] = '0' + (iv % 10); iv /= 10; } while (iv);
    while (tp) fmt_buf[p++] = t[--tp];
    fmt_buf[p++] = '.'; fmt_buf[p++] = '0' + f; fmt_buf[p++] = ' '; 
    const char *u2 = units[u]; while (*u2) fmt_buf[p++] = *u2++; 
    fmt_buf[p] = 0;
    return fmt_buf;
}
