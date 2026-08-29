#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>

/*
 * Senior Systems Engineer Note:
 * Minimal, zero-dependency (stdlib-only) diagnostic logger.
 *
 * Writes timestamped, leveled messages to a log file (and mirrors
 * WARN/ERROR to stderr) so failures during development/debugging can be
 * traced after the fact, without pulling in any external logging library.
 */

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

/**
 * @brief Opens (creating if needed) the log file at log_path for appending.
 *
 * Safe to call multiple times; a prior open is closed first. If log_path is
 * NULL, logger_log() falls back to stderr-only output.
 *
 * @param log_path Path to the log file, e.g. "mdview_debug.log".
 * @return true on success, false if the file could not be opened.
 */
bool logger_init(const char *log_path);

/**
 * @brief Sets the minimum level that will actually be written/printed.
 *        Defaults to LOG_LEVEL_INFO.
 */
void logger_set_level(log_level_t min_level);

/**
 * @brief Flushes and closes the log file, if open. Safe to call even if
 *        logger_init() was never called.
 */
void logger_close(void);

/**
 * @brief Writes one leveled, timestamped log line. Prefer the LOG_* macros
 *        below instead of calling this directly, so file/line/function are
 *        captured automatically.
 */
void logger_log(log_level_t level, const char *file, int line, const char *func,
                 const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 5, 6)))
#endif
    ;

#define LOG_DEBUG(...) logger_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(...)  logger_log(LOG_LEVEL_INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN(...)  logger_log(LOG_LEVEL_WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(...) logger_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

#endif /* LOGGER_H */
