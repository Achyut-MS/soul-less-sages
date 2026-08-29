#ifndef FILE_WRITER_H
#define FILE_WRITER_H

#include <stdbool.h>
#include <stddef.h>

bool file_writer_atomic_write(const char *target_path, const char *content, size_t len);
void file_writer_schedule_save(const char *target_path, const char *content, size_t len);

#endif
