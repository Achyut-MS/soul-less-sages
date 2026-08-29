#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

platform_os_t platform_get_os(void) {
#ifdef _WIN32
    return PLATFORM_OS_WINDOWS;
#else
    return PLATFORM_OS_LINUX;
#endif
}

bool platform_open_browser(const char *url) {
    if (!url || url[0] == '\0') {
        return false;
    }

#ifdef _WIN32
    return (int)ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL) > 32;
#else
    char cmd[512];
    int written = snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" >/dev/null 2>&1 &", url);
    if (written < 0 || (size_t)written >= sizeof(cmd)) {
        return false;
    }
    return system(cmd) == 0;
#endif
}
