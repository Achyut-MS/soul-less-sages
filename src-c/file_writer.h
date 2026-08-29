#ifndef FILE_WRITER_H
#define FILE_WRITER_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Atomically writes content to target_path via .tmp file + fsync + rename.
 *
 * Ensures that if the process is terminated mid-write, the original target_path remains
 * intact and uncorrupted. Cross-platform implementation handles fsync/rename on Linux
 * and FlushFileBuffers/MoveFileEx on Windows.
 *
 * @param target_path Destination file path.
 * @param content Data to write.
 * @param len Length of data to write.
 * @return true on success, false on failure.
 */
bool file_writer_atomic_write(const char *target_path, const char *content, size_t len);

/**
 * @brief Triggers a debounced write request (150-300ms delay).
 *
 * Debounces keystrokes from the client-side editor interface to minimize disk wear,
 * targeting <= 3 writes per second.
 *
 * @param target_path Destination file path.
 * @param content Data to write.
 * @param len Length of data to write.
 */
void file_writer_schedule_save(const char *target_path, const char *content, size_t len);

#endif
