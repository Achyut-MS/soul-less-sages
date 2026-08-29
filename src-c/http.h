#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int status;
    char *content_type;
    char *body;
    size_t body_len;
} http_response_t;

http_response_t http_response_create(int status, const char *content_type, const char *body);
void http_response_free(http_response_t *resp);
char *http_parse_json_value(const char *json, const char *key);
bool http_handle_render_request(const char *request_body, const char **out_html, char **out_error);
bool http_handle_serialize_request(const char *request_body, const char **out_md, char **out_error);

#endif
