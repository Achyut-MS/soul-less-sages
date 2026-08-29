#include "http.h"

#include "md_parser.h"
#include "html_serializer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, len + 1);
    return out;
}

http_response_t http_response_create(int status, const char *content_type, const char *body) {
    http_response_t resp;
    resp.status = status;
    resp.content_type = dup_str(content_type ? content_type : "text/plain");
    resp.body = dup_str(body ? body : "");
    resp.body_len = resp.body ? strlen(resp.body) : 0;
    return resp;
}

void http_response_free(http_response_t *resp) {
    if (!resp) {
        return;
    }
    free(resp->content_type);
    free(resp->body);
    resp->content_type = NULL;
    resp->body = NULL;
    resp->body_len = 0;
}

static char *dup_str_len(const char *s, size_t len) {
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    if (len > 0) {
        memcpy(out, s, len);
    }
    out[len] = '\0';
    return out;
}

char *http_parse_json_value(const char *json, const char *key) {
    if (!json || !key) {
        return NULL;
    }

    char needle[128];
    int written = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (written < 0 || (size_t)written >= sizeof(needle)) {
        return NULL;
    }

    const char *pos = strstr(json, needle);
    if (!pos) {
        return NULL;
    }
    pos = strchr(pos + strlen(needle), ':');
    if (!pos) {
        return NULL;
    }
    pos += 1;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') {
        pos += 1;
    }
    if (*pos != '"') {
        return NULL;
    }
    pos += 1;
    const char *end = pos;
    while (*end != '\0' && *end != '"') {
        end += 1;
    }
    if (*end != '"') {
        return NULL;
    }
    return dup_str_len(pos, (size_t)(end - pos));
}

bool http_handle_render_request(const char *request_body, const char **out_html, char **out_error) {
    if (!request_body || !out_html) {
        if (out_error) {
            *out_error = dup_str("invalid render request");
        }
        return false;
    }

    char *md = http_parse_json_value(request_body, "md");
    if (!md) {
        if (out_error) {
            *out_error = dup_str("missing md field");
        }
        return false;
    }

    md_parse_result_t res = md_to_html(md, strlen(md));
    free(md);
    if (!res.success) {
        if (out_error) {
            *out_error = dup_str(res.error_msg ? res.error_msg : "markdown parse failed");
        }
        md_parse_result_free(&res);
        return false;
    }

    *out_html = dup_str(res.html ? res.html : "");
    md_parse_result_free(&res);
    return true;
}

bool http_handle_serialize_request(const char *request_body, const char **out_md, char **out_error) {
    if (!request_body || !out_md) {
        if (out_error) {
            *out_error = dup_str("invalid serialize request");
        }
        return false;
    }

    char *html = http_parse_json_value(request_body, "html");
    if (!html) {
        if (out_error) {
            *out_error = dup_str("missing html field");
        }
        return false;
    }

    html_serialize_result_t res = html_to_md(html, strlen(html));
    free(html);
    if (!res.success) {
        if (out_error) {
            *out_error = dup_str(res.error_msg ? res.error_msg : "html serialization failed");
        }
        html_serialize_result_free(&res);
        return false;
    }

    *out_md = dup_str(res.markdown ? res.markdown : "");
    html_serialize_result_free(&res);
    return true;
}
