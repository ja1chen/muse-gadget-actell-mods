#pragma once
#include <stddef.h>
void headless_console_start(void);
void headless_console_observe(const char *line, size_t len);
