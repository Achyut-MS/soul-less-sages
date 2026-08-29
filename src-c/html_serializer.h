#ifndef HTML_SERIALIZER_H
#define HTML_SERIALIZER_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    bool  success;
    char *markdown;
    char *error_msg;
} html_serialize_result_t;

html_serialize_result_t html_to_md(const char *html_src, size_t html_len);
void html_serialize_result_free(html_serialize_result_t *res);

#endif
