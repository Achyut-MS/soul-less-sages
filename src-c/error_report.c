#include "error_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void append_str(char **dst, const char *src) {
    if (!src || !dst) {
        return;
    }

    size_t len = *dst ? strlen(*dst) : 0;
    size_t add = strlen(src);
    char *new_buf = (char *)realloc(*dst, len + add + 1);
    if (!new_buf) {
        free(*dst);
        *dst = NULL;
        return;
    }
    memcpy(new_buf + len, src, add + 1);
    *dst = new_buf;
}

static char *copy_line(const char *src, size_t start, size_t end) {
    size_t len = end > start ? end - start : 0;
    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        return NULL;
    }
    memcpy(buf, src + start, len);
    buf[len] = '\0';
    return buf;
}

char *error_report_create(const char *source, size_t line, size_t col, const char *message) {
    if (!source || !message) {
        return NULL;
    }

    size_t line_count = 1;
    size_t offset = 0;
    size_t target_line_start = 0;
    size_t target_line_end = 0;
    size_t target_line = line;

    while (source[offset] != '\0') {
        if (source[offset] == '\n') {
            if (line_count == target_line) {
                target_line_end = offset;
                break;
            }
            line_count += 1;
        }
        if (line_count == target_line && target_line_start == 0 && source[offset] != '\n') {
            target_line_start = offset;
        }
        offset += 1;
    }

    if (target_line == 1 && target_line_start == 0) {
        target_line_start = 0;
    }

    if (target_line > 1) {
        size_t pos = 0;
        size_t current_line = 1;
        while (source[pos] != '\0' && current_line < target_line) {
            if (source[pos] == '\n') {
                current_line += 1;
            }
            pos += 1;
        }
        target_line_start = pos;
        while (source[pos] != '\0' && source[pos] != '\n') {
            pos += 1;
        }
        target_line_end = pos;
    }

    while (source[offset] != '\0' && line_count < target_line) {
        if (source[offset] == '\n') {
            line_count += 1;
        }
        offset += 1;
    }
    if (source[offset] == '\n') {
        target_line_start = offset + 1;
    } else {
        target_line_start = offset;
    }

    size_t start_scan = target_line_start;
    while (source[start_scan] != '\0' && source[start_scan] != '\n') {
        start_scan += 1;
    }
    target_line_end = start_scan;

    size_t line_start = target_line == 1 ? 0 : 0;
    size_t line_end = target_line_end;
    for (size_t i = 0; i < target_line - 1; ++i) {
        size_t pos = line_start;
        while (source[pos] != '\0' && source[pos] != '\n') {
            pos += 1;
        }
        line_start = source[pos] == '\n' ? pos + 1 : pos;
    }
    line_end = line_start;
    while (source[line_end] != '\0' && source[line_end] != '\n') {
        line_end += 1;
    }

    char *source_line = copy_line(source, line_start, line_end);
    size_t caret_len = 1;
    if (strstr(message, "**") != NULL || strstr(message, "***") != NULL) {
        caret_len = 2;
    }
    if (caret_len < 1) {
        caret_len = 1;
    }
    if (col > 1 && col < strlen(source_line) + 2) {
        size_t pad = 0;
        while (pad + 1 < col) {
            pad += 1;
        }
        (void)pad;
    }

    char *out = NULL;
    char header[256];
    snprintf(header, sizeof(header), "error: %s\n  --> line %zu, col %zu\n   |\n", message, line, col);
    append_str(&out, header);

    char num_buf[64];
    snprintf(num_buf, sizeof(num_buf), "%zu | %s\n", line, source_line ? source_line : "");
    append_str(&out, num_buf);
    append_str(&out, "   | ");
    for (size_t i = 1; i < col; ++i) {
        append_str(&out, " ");
    }
    for (size_t i = 0; i < caret_len; ++i) {
        append_str(&out, "^");
    }
    append_str(&out, "\n");

    free(source_line);
    return out;
}
