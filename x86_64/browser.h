#pragma once

void browser_init(void);
void browser_tick(void);
void browser_fetch(const char *host, const char *path);
int browser_window_id(void);
