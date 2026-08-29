#include "html_serializer.h"

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

static char *strip_tag(const char *text) {
    if (!text) {
        return dup_str("");
    }
    size_t len = strlen(text);
    size_t out_len = 0;
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    for (size_t i = 0; i < len; ++i) {
        if (text[i] == '<') {
            while (i < len && text[i] != '>') {
                i += 1;
            }
            if (i < len && text[i] == '>') {
                i += 1;
            }
            if (i <= len) {
                --i;
            }
            continue;
        }
        out[out_len++] = text[i];
    }
    out[out_len] = '\0';
    return out;
}

static void append(char **dst, const char *s) {
    if (!s) {
        return;
    }
    size_t old_len = *dst ? strlen(*dst) : 0;
    size_t add_len = strlen(s);
    char *new_buf = (char *)realloc(*dst, old_len + add_len + 1);
    if (!new_buf) {
        free(*dst);
        *dst = NULL;
        return;
    }
    memcpy(new_buf + old_len, s, add_len + 1);
    *dst = new_buf;
}

static char *render_tag(const char *tag, const char *content) {
    char *out = NULL;
    if (strcmp(tag, "h1") == 0) {
        append(&out, "# ");
    } else if (strcmp(tag, "h2") == 0) {
        append(&out, "## ");
    } else if (strcmp(tag, "h3") == 0) {
        append(&out, "### ");
    } else if (strcmp(tag, "h4") == 0) {
        append(&out, "#### ");
    } else if (strcmp(tag, "h5") == 0) {
        append(&out, "##### ");
    } else if (strcmp(tag, "h6") == 0) {
        append(&out, "###### ");
    } else if (strcmp(tag, "strong") == 0) {
        append(&out, "**");
    } else if (strcmp(tag, "em") == 0) {
        append(&out, "*");
    } else if (strcmp(tag, "code") == 0) {
        append(&out, "`");
    } else if (strcmp(tag, "li") == 0) {
        append(&out, "- ");
    } else if (strcmp(tag, "blockquote") == 0) {
        append(&out, "> ");
    } else if (strcmp(tag, "p") == 0) {
        append(&out, "");
    }

    append(&out, content ? content : "");

    if (strcmp(tag, "strong") == 0) {
        append(&out, "**");
    } else if (strcmp(tag, "em") == 0) {
        append(&out, "*");
    } else if (strcmp(tag, "code") == 0) {
        append(&out, "`");
    } else if (strcmp(tag, "h1") == 0 || strcmp(tag, "h2") == 0 || strcmp(tag, "h3") == 0 || strcmp(tag, "h4") == 0 || strcmp(tag, "h5") == 0 || strcmp(tag, "h6") == 0) {
        append(&out, "\n\n");
    } else if (strcmp(tag, "li") == 0) {
        append(&out, "\n");
    } else if (strcmp(tag, "blockquote") == 0) {
        append(&out, "\n");
    }

    return out;
}

html_serialize_result_t html_to_md(const char *html_src, size_t html_len) {
    html_serialize_result_t result;
    result.success = false;
    result.markdown = NULL;
    result.error_msg = NULL;

    if (!html_src || html_len == 0) {
        result.success = true;
        result.markdown = dup_str("");
        return result;
    }

    char *copy = (char *)malloc(html_len + 1);
    if (!copy) {
        result.error_msg = dup_str("out of memory");
        return result;
    }
    memcpy(copy, html_src, html_len);
    copy[html_len] = '\0';

    char *output = NULL;
    char *text = strip_tag(copy);
    free(copy);

    if (!text) {
        result.error_msg = dup_str("failed to process html");
        return result;
    }

    char *clean = text;
    while (*clean == '\n' || *clean == '\r' || *clean == ' ' || *clean == '\t') {
        clean += 1;
    }

    output = dup_str(clean);
    free(text);
    if (!output) {
        result.error_msg = dup_str("memory allocation failed");
        return result;
    }

    result.success = true;
    result.markdown = output;
    return result;
}

void html_serialize_result_free(html_serialize_result_t *res) {
    if (!res) {
        return;
    }
    free(res->markdown);
    free(res->error_msg);
    res->markdown = NULL;
    res->error_msg = NULL;
}
