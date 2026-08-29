#include "file_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

bool file_writer_atomic_write(const char *target_path, const char *content, size_t len) {
    if (!target_path || !content) {
        return false;
    }

    size_t tmp_len = strlen(target_path) + 8;
    char *tmp_path = (char *)malloc(tmp_len + 1);
    if (!tmp_path) {
        return false;
    }
    snprintf(tmp_path, tmp_len + 1, "%s.tmp", target_path);

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) {
        free(tmp_path);
        return false;
    }

    size_t written = fwrite(content, 1, len, fp);
    fflush(fp);
#ifdef _WIN32
    FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(fp)));
#else
    fsync(fileno(fp));
#endif
    fclose(fp);

    bool ok = written == len;

    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(tmp_path, target_path, MOVEFILE_REPLACE_EXISTING);
#else
        ok = rename(tmp_path, target_path) == 0;
#endif
    }

    free(tmp_path);
    return ok;
}

void file_writer_schedule_save(const char *target_path, const char *content, size_t len) {
    (void)target_path;
    (void)content;
    (void)len;
}
