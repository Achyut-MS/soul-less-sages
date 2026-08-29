#ifndef MD_PARSER_H
#define MD_PARSER_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool   success;
    char  *html;
    char  *error_msg;
    char  *caret_snippet;
    size_t line;
    size_t col;
} md_parse_result_t;

md_parse_result_t md_to_html(const char *md_src, size_t md_len);
void md_parse_result_free(md_parse_result_t *res);

#endif
