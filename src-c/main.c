#include "md_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "failed to open file: %s\n", path);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t read_count = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (read_count != (size_t)size) {
        free(buf);
        return NULL;
    }

    buf[(size_t)size] = '\0';
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <markdown-file>\n", argv[0]);
        return 1;
    }

    char *input = read_file(argv[1]);
    if (!input) {
        return 1;
    }

    md_parse_result_t result = md_to_html(input, strlen(input));
    if (!result.success) {
        fprintf(stderr, "%s\n", result.error_msg ? result.error_msg : "parse error");
        if (result.caret_snippet) {
            fprintf(stderr, "%s\n", result.caret_snippet);
        }
        free(input);
        md_parse_result_free(&result);
        return 1;
    }

    printf("%s", result.html ? result.html : "");
    free(input);
    md_parse_result_free(&result);
    return 0;
}
