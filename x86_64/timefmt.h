#pragma once
#include <stdint.h>

typedef struct { int year, month, day, hour, minute, second, weekday; } civil_date_t;
civil_date_t civil_from_ms(uint64_t ms);
const char *timefmt_clock(uint64_t ms);
const char *timefmt_filedate(uint64_t ms);
const char *timefmt_uptime(uint64_t seconds);
const char *numfmt_f1(double v);
const char *numfmt_f2(double v);
const char *numfmt_bytes(int count);
