#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>

typedef enum {
    PLATFORM_OS_LINUX = 1,
    PLATFORM_OS_WINDOWS = 2
} platform_os_t;

platform_os_t platform_get_os(void);
bool platform_open_browser(const char *url);

#endif
