#ifndef ERROR_REPORT_H
#define ERROR_REPORT_H

#include <stddef.h>

char *error_report_create(const char *source, size_t line, size_t col, const char *message);

#endif
