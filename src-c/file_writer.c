#define _POSIX_C_SOURCE 200809L
#include "file_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
typedef HANDLE thread_t;
typedef CRITICAL_SECTION mutex_t;
#define mutex_init(m) InitializeCriticalSection(m)
#define mutex_lock(m) EnterCriticalSection(m)
#define mutex_unlock(m) LeaveCriticalSection(m)
#else
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
typedef pthread_t thread_t;
typedef pthread_mutex_t mutex_t;
#define mutex_init(m) pthread_mutex_init(m, NULL)
#define mutex_lock(m) pthread_mutex_lock(m)
#define mutex_unlock(m) pthread_mutex_unlock(m)
#endif

#ifndef DEBOUNCE_MS
#define DEBOUNCE_MS 200
#endif

#ifdef TEST_MODE
int g_write_count = 0;
bool g_crash_mid_write = false;
#endif

static mutex_t state_mutex;
static mutex_t write_mutex;
static bool state_init_done = false;
static char *pending_path = NULL;
static char *pending_content = NULL;
static size_t pending_len = 0;
static long long pending_deadline = 0;

/* Returns monotonic system time in milliseconds */
static long long get_time_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

/* Derives temp path in the same folder as target to guarantee atomic rename */
static bool get_tmp_path(const char *target, char *tmp, size_t max_len) {
    const char *last_slash = strrchr(target, '/');
    const char *last_backslash = strrchr(target, '\\');
    const char *sep = last_slash > last_backslash ? last_slash : last_backslash;
    int written;
    if (sep) {
        size_t dir_len = (size_t)(sep - target + 1);
        char dir_prefix[512];
        if (dir_len >= sizeof(dir_prefix) || dir_len + strlen(sep + 1) + 6 >= max_len) return false;
        memcpy(dir_prefix, target, dir_len);
        dir_prefix[dir_len] = '\0';
        written = snprintf(tmp, max_len, "%s.%s.tmp", dir_prefix, sep + 1);
    } else {
        written = snprintf(tmp, max_len, ".%s.tmp", target);
    }
    return written > 0 && (size_t)written < max_len;
}

bool file_writer_atomic_write(const char *target_path, const char *content, size_t len) {
    if (!target_path || !content) return false;
    char tmp_path[512];
    if (!get_tmp_path(target_path, tmp_path, sizeof(tmp_path))) return false;

    FILE *f = fopen(tmp_path, "wb");
    if (!f) return false;

#ifdef TEST_MODE
    if (g_crash_mid_write) {
        (void)fwrite(content, 1, len / 2, f);
        (void)fflush(f);
        _exit(99); /* Simulate hard crash mid-write */
    }
#endif

    bool ok = (fwrite(content, 1, len, f) == len);
    if (ok) ok = (fflush(f) == 0);
    if (ok) {
#ifdef _WIN32
        HANDLE h = (HANDLE)_get_osfhandle(_fileno(f));
        ok = (h != INVALID_HANDLE_VALUE && FlushFileBuffers(h));
#else
        ok = (fsync(fileno(f)) == 0);
#endif
    }
    fclose(f);

    if (!ok) {
        (void)remove(tmp_path);
        return false;
    }

#ifdef _WIN32
    if (!MoveFileExA(tmp_path, target_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        (void)remove(tmp_path);
        return false;
    }
#else
    if (rename(tmp_path, target_path) < 0) {
        (void)remove(tmp_path);
        return false;
    }
#endif

#ifdef TEST_MODE
    g_write_count++;
#endif
    return true;
}

#ifdef _WIN32
static DWORD WINAPI debounce_worker(LPVOID arg) {
    long long *p_deadline = (long long *)arg;
    long long my_deadline = 0;
    if (p_deadline) {
        my_deadline = *p_deadline;
        free(p_deadline);
    }
#else
static void *debounce_worker(void *arg) {
    long long *p_deadline = (long long *)arg;
    long long my_deadline = 0;
    if (p_deadline) {
        my_deadline = *p_deadline;
        free(p_deadline);
    }
#endif
    while (true) {
        mutex_lock(&state_mutex);
        long long now = get_time_ms();
        if (now >= my_deadline) {
            /* If a newer schedule has superseded our deadline, exit without writing */
            if (my_deadline != pending_deadline) {
                mutex_unlock(&state_mutex);
                break;
            }
            char *path = pending_path;
            char *content = pending_content;
            size_t len = pending_len;
            pending_path = NULL;
            pending_content = NULL;
            mutex_unlock(&state_mutex);
            if (path && content) {
                mutex_lock(&write_mutex);
                (void)file_writer_atomic_write(path, content, len);
                mutex_unlock(&write_mutex);
                free(path);
                free(content);
            }
            break;
        }
        long long wait = my_deadline - now;
        mutex_unlock(&state_mutex);
        if (wait > 0) {
#ifdef _WIN32
            Sleep((DWORD)wait);
#else
            struct timespec ts;
            ts.tv_sec = wait / 1000;
            ts.tv_nsec = (wait % 1000) * 1000000;
            (void)nanosleep(&ts, NULL);
#endif
        }
    }
    return 0;
}

void file_writer_schedule_save(const char *target_path, const char *content, size_t len) {
    if (!target_path || !content) return;
    if (!state_init_done) {
        mutex_init(&state_mutex);
        mutex_init(&write_mutex);
        state_init_done = true;
    }
    mutex_lock(&state_mutex);
    if (pending_path) free(pending_path);
    if (pending_content) free(pending_content);

    pending_path = malloc(strlen(target_path) + 1);
    if (pending_path) strcpy(pending_path, target_path);

    pending_content = malloc(len + 1);
    if (pending_content) {
        memcpy(pending_content, content, len);
        pending_content[len] = '\0';
    }

    pending_len = len;
    long long deadline = get_time_ms() + DEBOUNCE_MS;
    /* Ensure strictly monotonic deadlines so each thread has a unique identifier */
    if (deadline <= pending_deadline) {
        deadline = pending_deadline + 1;
    }
    pending_deadline = deadline;

    long long *p_deadline = malloc(sizeof(long long));
    if (p_deadline) {
        *p_deadline = deadline;
#ifdef _WIN32
        HANDLE h = CreateThread(NULL, 0, debounce_worker, p_deadline, 0, NULL);
        if (h) CloseHandle(h);
#else
        pthread_t t;
        pthread_create(&t, NULL, debounce_worker, p_deadline);
        pthread_detach(t);
#endif
    }
    mutex_unlock(&state_mutex);
}
