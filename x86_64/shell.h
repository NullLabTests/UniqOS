#pragma once

void shell_init(void);
const char *shell_execute(const char *line);
const char *shell_prompt(void);
int shell_history_count(void);
const char *shell_history_get(int i);
int shell_handle_char(char c);
