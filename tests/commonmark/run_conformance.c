#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src-c/md_parser.h"

typedef struct {
    char *markdown;
    char *html;
    char *section;
    int example;
} cm_case_t;

typedef struct {
    char *name;
    int total;
    int passed;
} section_stat_t;

static char *xstrdup(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

static char *read_all(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    long size;

    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[size] = '\0';
    if (out_len) *out_len = (size_t)size;
    return buf;
}

static int append_utf8(char *out, size_t cap, size_t *j, unsigned code) {
    if (code <= 0x7f) {
        if (*j + 1 >= cap) return 0;
        out[(*j)++] = (char)code;
    } else if (code <= 0x7ff) {
        if (*j + 2 >= cap) return 0;
        out[(*j)++] = (char)(0xc0 | (code >> 6));
        out[(*j)++] = (char)(0x80 | (code & 0x3f));
    } else if (code <= 0xffff) {
        if (*j + 3 >= cap) return 0;
        out[(*j)++] = (char)(0xe0 | (code >> 12));
        out[(*j)++] = (char)(0x80 | ((code >> 6) & 0x3f));
        out[(*j)++] = (char)(0x80 | (code & 0x3f));
    } else {
        if (*j + 4 >= cap) return 0;
        out[(*j)++] = (char)(0xf0 | (code >> 18));
        out[(*j)++] = (char)(0x80 | ((code >> 12) & 0x3f));
        out[(*j)++] = (char)(0x80 | ((code >> 6) & 0x3f));
        out[(*j)++] = (char)(0x80 | (code & 0x3f));
    }
    return 1;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static char *parse_json_string(const char **p) {
    const char *s = *p;
    char *out;
    size_t cap;
    size_t j = 0;

    if (*s != '"') return NULL;
    s++;
    cap = strlen(s) + 1;
    out = malloc(cap);
    if (!out) return NULL;

    while (*s) {
        if (*s == '"') {
            out[j] = '\0';
            *p = s + 1;
            return out;
        }
        if (*s == '\\') {
            s++;
            switch (*s) {
                case '"': out[j++] = '"'; break;
                case '\\': out[j++] = '\\'; break;
                case '/': out[j++] = '/'; break;
                case 'b': out[j++] = '\b'; break;
                case 'f': out[j++] = '\f'; break;
                case 'n': out[j++] = '\n'; break;
                case 'r': out[j++] = '\r'; break;
                case 't': out[j++] = '\t'; break;
                case 'u': {
                    unsigned code = 0;
                    for (int i = 0; i < 4; i++) {
                        int v = hexval(s[i + 1]);
                        if (v < 0) {
                            free(out);
                            return NULL;
                        }
                        code = (code << 4) | (unsigned)v;
                    }
                    if (!append_utf8(out, cap, &j, code)) {
                        free(out);
                        return NULL;
                    }
                    s += 4;
                    break;
                }
                default:
                    free(out);
                    return NULL;
            }
            if (*s == '\0') {
                free(out);
                return NULL;
            }
            s++;
        } else {
            out[j++] = *s++;
        }
    }

    free(out);
    return NULL;
}

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static int parse_int_value(const char **p, int *out) {
    char *end = NULL;
    long v;
    skip_ws(p);
    v = strtol(*p, &end, 10);
    if (end == *p) return 0;
    *out = (int)v;
    *p = end;
    return 1;
}

static const char *find_object_end(const char *p) {
    int in_string = 0;
    int escaped = 0;
    int depth = 0;

    for (; *p; p++) {
        if (in_string) {
            if (escaped) escaped = 0;
            else if (*p == '\\') escaped = 1;
            else if (*p == '"') in_string = 0;
        } else {
            if (*p == '"') in_string = 1;
            else if (*p == '{') depth++;
            else if (*p == '}') {
                depth--;
                if (depth == 0) return p;
            }
        }
    }
    return NULL;
}

static int parse_case(const char *start, const char *end, cm_case_t *tc) {
    const char *p = start + 1;
    memset(tc, 0, sizeof(*tc));

    while (p < end) {
        char *key = NULL;
        skip_ws(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '"') break;
        key = parse_json_string(&p);
        if (!key) return 0;
        skip_ws(&p);
        if (*p != ':') {
            free(key);
            return 0;
        }
        p++;
        skip_ws(&p);

        if (strcmp(key, "markdown") == 0 || strcmp(key, "html") == 0 || strcmp(key, "section") == 0) {
            char *value = parse_json_string(&p);
            if (!value) {
                free(key);
                return 0;
            }
            if (strcmp(key, "markdown") == 0) tc->markdown = value;
            else if (strcmp(key, "html") == 0) tc->html = value;
            else tc->section = value;
        } else if (strcmp(key, "example") == 0) {
            if (!parse_int_value(&p, &tc->example)) {
                free(key);
                return 0;
            }
        } else if (*p == '"') {
            char *ignored = parse_json_string(&p);
            free(ignored);
        } else {
            while (p < end && *p != ',') p++;
        }
        free(key);
    }

    return tc->markdown && tc->html && tc->section;
}

static void free_case(cm_case_t *tc) {
    free(tc->markdown);
    free(tc->html);
    free(tc->section);
}

static char *normalize_html(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    size_t j = 0;
    int in_ws = 0;

    if (!out) return NULL;
    while (*s && isspace((unsigned char)*s)) s++;
    for (; *s; s++) {
        if (isspace((unsigned char)*s)) {
            in_ws = 1;
        } else {
            if (in_ws && j > 0) out[j++] = ' ';
            out[j++] = *s;
            in_ws = 0;
        }
    }
    if (j > 0 && out[j - 1] == ' ') j--;
    out[j] = '\0';
    return out;
}

static section_stat_t *find_section(section_stat_t **stats, size_t *count, size_t *cap, const char *name) {
    for (size_t i = 0; i < *count; i++) {
        if (strcmp((*stats)[i].name, name) == 0) return &(*stats)[i];
    }
    if (*count == *cap) {
        size_t new_cap = *cap ? *cap * 2 : 16;
        section_stat_t *new_stats = realloc(*stats, new_cap * sizeof(**stats));
        if (!new_stats) return NULL;
        *stats = new_stats;
        *cap = new_cap;
    }
    (*stats)[*count].name = xstrdup(name);
    (*stats)[*count].total = 0;
    (*stats)[*count].passed = 0;
    return &(*stats)[(*count)++];
}

static void print_worst_sections(section_stat_t *stats, size_t count) {
    printf("\nTop failing sections:\n");
    for (int rank = 0; rank < 5; rank++) {
        int best = -1;
        int best_fail = 0;
        for (size_t i = 0; i < count; i++) {
            int fail = stats[i].total - stats[i].passed;
            int already_printed = 0;
            for (int r = 0; r < rank; r++) {
                (void)r;
            }
            if (fail > best_fail) {
                int duplicate_rank = 0;
                for (size_t j = 0; j < count; j++) {
                    (void)j;
                }
                (void)duplicate_rank;
                best = (int)i;
                best_fail = fail;
            }
            (void)already_printed;
        }
        if (best < 0 || best_fail == 0) break;
        printf("  %s: %d/%d passed, %d failed\n",
            stats[best].name, stats[best].passed, stats[best].total, best_fail);
        stats[best].total = -stats[best].total;
    }
    for (size_t i = 0; i < count; i++) {
        if (stats[i].total < 0) stats[i].total = -stats[i].total;
    }
}

int main(void) {
    size_t json_len = 0;
    char *json = read_all("tests/commonmark/spec.json", &json_len);
    const char *p;
    int total = 0;
    int passed = 0;
    int shown_failures = 0;
    section_stat_t *stats = NULL;
    size_t stat_count = 0;
    size_t stat_cap = 0;

    if (!json) {
        fprintf(stderr, "error: could not read tests/commonmark/spec.json\n");
        return 1;
    }

    printf("CommonMark 0.31.2 conformance run (%zu bytes corpus)\n", json_len);
    p = json;
    while ((p = strchr(p, '{')) != NULL) {
        const char *end = find_object_end(p);
        cm_case_t tc;
        md_parse_result_t res;
        char *actual_norm = NULL;
        char *expected_norm = NULL;
        section_stat_t *stat;
        int ok = 0;

        if (!end) break;
        if (!parse_case(p, end, &tc)) {
            p = end + 1;
            continue;
        }

        stat = find_section(&stats, &stat_count, &stat_cap, tc.section);
        if (!stat) {
            free_case(&tc);
            free(json);
            return 1;
        }
        total++;
        stat->total++;

        res = md_to_html(tc.markdown, strlen(tc.markdown));
        if (res.success && res.html) {
            actual_norm = normalize_html(res.html);
            expected_norm = normalize_html(tc.html);
            ok = actual_norm && expected_norm && strcmp(actual_norm, expected_norm) == 0;
        }

        if (ok) {
            passed++;
            stat->passed++;
        } else if (shown_failures < 10) {
            printf("FAIL example %d [%s]\n", tc.example, tc.section);
            printf("  expected: %s\n", expected_norm ? expected_norm : "(parse/allocation error)");
            printf("  actual:   %s\n", actual_norm ? actual_norm : (res.error_msg ? res.error_msg : "(null)"));
            shown_failures++;
        }

        free(actual_norm);
        free(expected_norm);
        md_parse_result_free(&res);
        free_case(&tc);
        p = end + 1;
    }

    printf("\nSection breakdown:\n");
    for (size_t i = 0; i < stat_count; i++) {
        double pct = stats[i].total ? (100.0 * stats[i].passed / stats[i].total) : 0.0;
        printf("  %s: %d/%d passed (%.2f%%)\n", stats[i].name, stats[i].passed, stats[i].total, pct);
    }
    print_worst_sections(stats, stat_count);
    printf("\nCommonMark Conformance Ratio: %d/%d passed (%.2f%%)\n",
        passed, total, total ? (100.0 * passed / total) : 0.0);

    for (size_t i = 0; i < stat_count; i++) free(stats[i].name);
    free(stats);
    free(json);
    return total > 0 ? 0 : 1;
}
