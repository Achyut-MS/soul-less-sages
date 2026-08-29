#include "md_parser.h"
#include <stdlib.h>
#include <string.h>

/*
 * Senior Engineer Note:
 * This is a stub implementation conforming exactly to the frozen md_parser.h contract.
 * Standard malloc and memcpy are used to ensure maximum compatibility with legacy toolchains
 * that do not yet fully implement the ISO C23 standard library (where strdup is officially standardized).
 */

md_parse_result_t md_to_html(const char *md_src, size_t md_len) {
    (void)md_src;
    (void)md_len;
    
    md_parse_result_t res = {
        .success = true,
        .html = NULL,
        .error_msg = NULL,
        .caret_snippet = NULL,
        .line = 0,
        .col = 0
    };

    const char *stub_html = "<p>Stub HTML output</p>";
    size_t stub_len = strlen(stub_html);
    res.html = malloc(stub_len + 1);
    if (res.html) {
        memcpy(res.html, stub_html, stub_len + 1);
    } else {
        res.success = false;
    }

    return res;
}

void md_parse_result_free(md_parse_result_t *res) {
    if (res) {
        if (res->html) {
            free(res->html);
            res->html = NULL;
        }
        if (res->error_msg) {
            free(res->error_msg);
            res->error_msg = NULL;
        }
        if (res->caret_snippet) {
            free(res->caret_snippet);
            res->caret_snippet = NULL;
        }
    }
}
