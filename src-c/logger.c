#include "logger.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

static FILE *g_log_file = NULL;
static log_level_t g_min_level = LOG_LEVEL_INFO;

static const char *level_name(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "?????";
    }
}

static void write_timestamp(FILE *out) {
    time_t now = time(NULL);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_buf);
    fputs(stamp, out);
}

bool logger_init(const char *log_path) {
    logger_close();

    if (!log_path) {
        return false;
    }

    g_log_file = fopen(log_path, "a");
    if (!g_log_file) {
        fprintf(stderr, "logger: could not open log file '%s'\n", log_path);
        return false;
    }

    /* Line-buffer the log file so entries land on disk promptly, which
     * matters most right before a crash. */
    setvbuf(g_log_file, NULL, _IOLBF, 0);

    fputs("---- log session start ", g_log_file);
    write_timestamp(g_log_file);
    fputc('\n', g_log_file);
    fflush(g_log_file);
    return true;
}

void logger_set_level(log_level_t min_level) {
    g_min_level = min_level;
}

void logger_close(void) {
    if (g_log_file) {
        fputs("---- log session end\n", g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void logger_log(log_level_t level, const char *file, int line, const char *func,
                 const char *fmt, ...) {
    if (level < g_min_level) {
        return;
    }

    /* Trim any directory prefix so lines stay short and readable. */
    const char *base = file;
    if (base) {
        const char *slash = strrchr(base, '/');
        const char *bslash = strrchr(base, '\\');
        if (bslash && (!slash || bslash > slash)) {
            slash = bslash;
        }
        if (slash) {
            base = slash + 1;
        }
    }

    FILE *targets[2];
    int target_count = 0;
    if (g_log_file) {
        targets[target_count++] = g_log_file;
    }
    if (level >= LOG_LEVEL_WARN || !g_log_file) {
        targets[target_count++] = stderr;
    }

    for (int i = 0; i < target_count; ++i) {
        FILE *out = targets[i];
        write_timestamp(out);
        fprintf(out, " [%-5s] %s:%d (%s): ", level_name(level), base ? base : "?", line,
                func ? func : "?");

        if (!fmt) fmt = "(null)";
        va_list args;
        va_start(args, fmt);
        vfprintf(out, fmt, args);
        va_end(args);

        fputc('\n', out);
    }
}
