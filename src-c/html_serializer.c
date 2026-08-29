#include "html_serializer.h"
#include <stdlib.h>
#include <string.h>

/*
 * Senior Engineer Note:
 * This is a stub implementation conforming exactly to the frozen html_serializer.h contract.
 * Same memory safety practices apply: manual allocation checking and basic cleanup.
 */

html_serialize_result_t html_to_md(const char *html_src, size_t html_len) {
    (void)html_src;
    (void)html_len;
    
    html_serialize_result_t res = {
        .success = true,
        .markdown = NULL,
        .error_msg = NULL
    };

    const char *stub_md = "Stub Markdown output";
    size_t stub_len = strlen(stub_md);
    res.markdown = malloc(stub_len + 1);
    if (res.markdown) {
        memcpy(res.markdown, stub_md, stub_len + 1);
    } else {
        res.success = false;
    }

    return res;
}

void html_serialize_result_free(html_serialize_result_t *res) {
    if (res) {
        if (res->markdown) {
            free(res->markdown);
            res->markdown = NULL;
        }
        if (res->error_msg) {
            free(res->error_msg);
            res->error_msg = NULL;
        }
    }
}
