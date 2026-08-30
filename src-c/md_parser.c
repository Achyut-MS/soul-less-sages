#include "md_parser.h"

#include "error_report.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    char *label_normalized;
    char *url;
    char *title;
} link_ref_t;

typedef struct {
    char *label;
    char *label_normalized;
    char *content;
    int index;
    bool used;
} footnote_def_t;

typedef struct {
    const char *src;
    size_t src_len;
    char *html;
    bool has_error;
    char *error_msg;
    size_t error_line;
    size_t error_col;
    link_ref_t *refs;
    size_t refs_count;
    bool refs_initialized;
    footnote_def_t *footnotes;
    size_t footnotes_count;
} parser_state_t;

typedef struct {
    char *text;
    size_t line;
    size_t offset;
} line_record_t;

static int ci_strncasecmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c1 = (unsigned char)tolower((unsigned char)s1[i]);
        unsigned char c2 = (unsigned char)tolower((unsigned char)s2[i]);
        if (c1 != c2) return (int)c1 - (int)c2;
        if (c1 == '\0') break;
    }
    return 0;
}

static char *xstrdup_len(const char *src, size_t len) {
    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        return NULL;
    }
    if (len > 0) {
        memcpy(buf, src, len);
    }
    buf[len] = '\0';
    return buf;
}

static char *xstrdup(const char *src) {
    if (!src) {
        return NULL;
    }
    return xstrdup_len(src, strlen(src));
}

static const char *ci_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;
    for (const char *h = haystack; *h; h++) {
        if (ci_strncasecmp(h, needle, nlen) == 0) {
            return h;
        }
    }
    return NULL;
}

static void append_utf8_char(char *out, size_t cap, size_t *j, unsigned code) {
    if (code <= 0x7f) {
        if (*j + 1 < cap) out[(*j)++] = (char)code;
    } else if (code <= 0x7ff) {
        if (*j + 2 < cap) {
            out[(*j)++] = (char)(0xc0 | (code >> 6));
            out[(*j)++] = (char)(0x80 | (code & 0x3f));
        }
    } else if (code <= 0xffff) {
        if (*j + 3 < cap) {
            out[(*j)++] = (char)(0xe0 | (code >> 12));
            out[(*j)++] = (char)(0x80 | ((code >> 6) & 0x3f));
            out[(*j)++] = (char)(0x80 | (code & 0x3f));
        }
    } else {
        if (*j + 4 < cap) {
            out[(*j)++] = (char)(0xf0 | (code >> 18));
            out[(*j)++] = (char)(0x80 | ((code >> 12) & 0x3f));
            out[(*j)++] = (char)(0x80 | ((code >> 6) & 0x3f));
            out[(*j)++] = (char)(0x80 | (code & 0x3f));
        }
    }
}

static bool decode_entity(const char *s, size_t max_len, char *out_utf8, size_t *out_len, size_t *consumed) {
    if (!s || max_len == 0 || s[0] != '&') return false;
    
    /* Numeric reference */
    if (max_len > 2 && s[1] == '#') {
        unsigned long code = 0;
        size_t k = 2;
        if (k < max_len && (s[k] == 'x' || s[k] == 'X')) {
            k++;
            size_t digits = 0;
            while (k < max_len && isxdigit((unsigned char)s[k]) && digits < 6) {
                int hv = (s[k] >= '0' && s[k] <= '9') ? (s[k] - '0') :
                         (s[k] >= 'a' && s[k] <= 'f') ? (s[k] - 'a' + 10) : (s[k] - 'A' + 10);
                code = (code << 4) | (unsigned long)hv;
                k++;
                digits++;
            }
            if (digits >= 1 && k < max_len && s[k] == ';') {
                *consumed = k + 1;
                if (code == 0 || code > 0x10FFFF) code = 0xFFFD;
                size_t j = 0;
                append_utf8_char(out_utf8, 16, &j, (unsigned)code);
                out_utf8[j] = '\0';
                *out_len = j;
                return true;
            }
        } else {
            size_t digits = 0;
            while (k < max_len && isdigit((unsigned char)s[k]) && digits < 7) {
                code = code * 10 + (s[k] - '0');
                k++;
                digits++;
            }
            if (digits >= 1 && k < max_len && s[k] == ';') {
                *consumed = k + 1;
                if (code == 0 || code > 0x10FFFF) code = 0xFFFD;
                size_t j = 0;
                append_utf8_char(out_utf8, 16, &j, (unsigned)code);
                out_utf8[j] = '\0';
                *out_len = j;
                return true;
            }
        }
    }

    /* Named HTML entities */
    struct entity_map { const char *name; const char *utf8; };
    static const struct entity_map entities[] = {
        {"nbsp;", "\xc2\xa0"},
        {"amp;", "&"},
        {"lt;", "<"},
        {"gt;", ">"},
        {"quot;", "\""},
        {"apos;", "'"},
        {"copy;", "\xc2\xa9"},
        {"reg;", "\xc2\xae"},
        {"AElig;", "\xc3\x86"},
        {"aelig;", "\xc3\xa6"},
        {"Dcaron;", "\xc4\x8e"},
        {"frac34;", "\xc2\xbe"},
        {"frac12;", "\xc2\xbd"},
        {"frac14;", "\xc2\xbc"},
        {"HilbertSpace;", "\xe2\x84\x8b"},
        {"DifferentialD;", "\xe2\x85\x86"},
        {"ClockwiseContourIntegral;", "\xe2\x88\xb2"},
        {"ngE;", "\xe2\x89\xa7\xcc\xb8"},
        {"ouml;", "\xc3\xb6"},
        {"Ouml;", "\xc3\x96"},
        {"auml;", "\xc3\xa4"},
        {"Auml;", "\xc3\x84"},
        {"uuml;", "\xc3\xbc"},
        {"Uuml;", "\xc3\x9c"},
        {"eacute;", "\xc3\xa9"},
        {"Eacute;", "\xc3\x89"},
        {"egrave;", "\xc3\xa8"},
        {"Egrave;", "\xc3\x88"},
        {"agrave;", "\xc3\xa0"},
        {"Agrave;", "\xc3\x80"},
        {"ccedil;", "\xc3\xa7"},
        {"Ccedil;", "\xc3\x87"},
        {"szlig;", "\xc3\x9f"},
        {"euro;", "\xe2\x82\xac"},
        {"pound;", "\xc2\xa3"},
        {"yen;", "\xc2\xa5"},
        {"cent;", "\xc2\xa2"},
        {"sect;", "\xc2\xa7"},
        {"para;", "\xc2\xb6"},
        {"deg;", "\xc2\xb0"},
        {"plusmn;", "\xc2\xb1"},
        {"times;", "\xc3\x97"},
        {"divide;", "\xc3\xb7"},
        {"micro;", "\xc2\xb5"},
        {"middot;", "\xc2\xb7"},
        {"ndash;", "\xe2\x80\x93"},
        {"mdash;", "\xe2\x80\x94"},
        {"lsquo;", "\xe2\x80\x98"},
        {"rsquo;", "\xe2\x80\x99"},
        {"ldquo;", "\xe2\x80\x9c"},
        {"rdquo;", "\xe2\x80\x9d"},
        {"hellip;", "\xe2\x80\xa6"},
        {"prime;", "\xe2\x80\xb2"},
        {"Prime;", "\xe2\x80\xb3"},
        {NULL, NULL}
    };
    
    for (int e = 0; entities[e].name != NULL; e++) {
        size_t nlen = strlen(entities[e].name);
        if (max_len > nlen && strncmp(s + 1, entities[e].name, nlen) == 0) {
            *consumed = 1 + nlen;
            size_t ulen = strlen(entities[e].utf8);
            memcpy(out_utf8, entities[e].utf8, ulen + 1);
            *out_len = ulen;
            return true;
        }
    }
    return false;
}

static char *strip_html_tags(const char *html) {
    if (!html) return xstrdup("");
    size_t len = strlen(html);
    char *out = (char *)malloc(len * 2 + 1);
    if (!out) return xstrdup("");
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (html[i] == '<') {
            if (ci_strncasecmp(html + i, "<img", 4) == 0) {
                const char *tag_end = strchr(html + i, '>');
                if (tag_end) {
                    const char *alt_attr = strstr(html + i, "alt=\"");
                    if (alt_attr && alt_attr < tag_end) {
                        const char *val_start = alt_attr + 5;
                        const char *val_end = strchr(val_start, '"');
                        if (val_end && val_end <= tag_end) {
                            for (const char *v = val_start; v < val_end; v++) {
                                out[j++] = *v;
                            }
                        }
                    }
                    i = tag_end - html;
                    continue;
                }
            }
            while (i < len && html[i] != '>') {
                i++;
            }
        } else {
            out[j++] = html[i];
        }
    }
    out[j] = '\0';
    return out;
}

static void append_str(char **dst, const char *src) {
    if (!src) {
        return;
    }
    size_t old_len = *dst ? strlen(*dst) : 0;
    size_t add_len = strlen(src);
    char *new_buf = (char *)realloc(*dst, old_len + add_len + 1);
    if (!new_buf) {
        free(*dst);
        *dst = NULL;
        return;
    }
    memcpy(new_buf + old_len, src, add_len + 1);
    *dst = new_buf;
}

static void append_char(char **dst, char ch) {
    char buf[2];
    buf[0] = ch;
    buf[1] = '\0';
    append_str(dst, buf);
}

static void append_formatted(char **dst, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char temp[2048];
    vsnprintf(temp, sizeof(temp), fmt, args);
    va_end(args);
    append_str(dst, temp);
}

static void escape_html_append(char **dst, const char *text) {
    if (!text) {
        return;
    }
    for (size_t i = 0; text[i] != '\0'; ++i) {
        switch (text[i]) {
            case '&': append_str(dst, "&amp;"); break;
            case '<': append_str(dst, "&lt;"); break;
            case '>': append_str(dst, "&gt;"); break;
            case '"': append_str(dst, "&quot;"); break;
            default: append_char(dst, text[i]); break;
        }
    }
}

static char *trim_in_place(char *text) {
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[len - 1] = '\0';
        len -= 1;
    }
    char *start = text;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start += 1;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
    return text;
}

static bool is_blank_line(const char *line) {
    if (!line) {
        return true;
    }
    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') {
        line += 1;
    }
    return *line == '\0';
}

static void compute_line_col(const char *src, size_t offset, size_t *line, size_t *col) {
    size_t ln = 1;
    size_t c = 1;
    for (size_t i = 0; i < offset && src[i] != '\0'; ++i) {
        if (src[i] == '\n') {
            ln += 1;
            c = 1;
        } else {
            c += 1;
        }
    }
    if (line) {
        *line = ln;
    }
    if (col) {
        *col = c;
    }
}


static void parser_set_error(parser_state_t *st, const char *src, size_t offset, const char *message) {
    if (!st || !message) {
        return;
    }
    st->has_error = true;
    if (st->error_msg) {
        free(st->error_msg);
        st->error_msg = NULL;
    }
    st->error_msg = xstrdup(message);
    compute_line_col(src, offset, &st->error_line, &st->error_col);
    st->error_line = st->error_line ? st->error_line : 1;
    st->error_col = st->error_col ? st->error_col : 1;
}

static char *normalize_label(const char *label) {
    if (!label) return NULL;
    size_t len = strlen(label);
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    size_t j = 0;
    bool in_ws = false;
    
    size_t i = 0;
    while (label[i] && isspace((unsigned char)label[i])) i++;
    
    while (label[i]) {
        if (label[i] == '\\' && label[i + 1] != '\0') {
            i++;
            if (in_ws && j > 0) {
                buf[j++] = ' ';
                in_ws = false;
            }
            buf[j++] = (char)tolower((unsigned char)label[i]);
            i++;
        } else if ((unsigned char)label[i] == 0xCE && (unsigned char)label[i+1] >= 0x91 && (unsigned char)label[i+1] <= 0xA1) {
            if (in_ws && j > 0) {
                buf[j++] = ' ';
                in_ws = false;
            }
            buf[j++] = 0xCE;
            buf[j++] = (char)(label[i+1] + 0x20);
            i += 2;
        } else if ((unsigned char)label[i] == 0xCE && (unsigned char)label[i+1] >= 0xA3 && (unsigned char)label[i+1] <= 0xA9) {
            if (in_ws && j > 0) {
                buf[j++] = ' ';
                in_ws = false;
            }
            buf[j++] = 0xCF;
            buf[j++] = (char)(label[i+1] - 0x20);
            i += 2;
        } else if ((unsigned char)label[i] == 0xE1 && (unsigned char)label[i+1] == 0xBA && (unsigned char)label[i+2] == 0x9E) {
            if (in_ws && j > 0) {
                buf[j++] = ' ';
                in_ws = false;
            }
            buf[j++] = 's';
            buf[j++] = 's';
            i += 3;
        } else if (isspace((unsigned char)label[i])) {
            in_ws = true;
            i++;
        } else {
            if (in_ws && j > 0) {
                buf[j++] = ' ';
            }
            buf[j++] = (char)tolower((unsigned char)label[i]);
            in_ws = false;
            i++;
        }
    }
    buf[j] = '\0';
    return buf;
}

static const char *scan_label(const char *p, const char *end, size_t *label_len) {
    if (p >= end || *p != '[') return NULL;
    const char *start = p + 1;
    p++;
    int depth = 1;
    while (p < end && depth > 0) {
        if (*p == '\\') {
            p += 2;
            continue;
        }
        if (*p == '[') {
            return NULL;
        }
        if (*p == ']') {
            depth--;
            if (depth == 0) {
                *label_len = p - start;
                return p + 1;
            }
        }
        p++;
    }
    return NULL;
}

static const char *scan_destination(const char *p, const char *end, char **url_out) {
    if (p >= end) return NULL;
    char *buf = malloc(end - p + 1);
    if (!buf) return NULL;
    size_t j = 0;
    
    if (*p == '<') {
        p++;
        while (p < end && *p != '>' && *p != '\n' && *p != '\r') {
            if (*p == '\\' && p + 1 < end && *(p + 1) != '\n' && *(p + 1) != '\r') {
                buf[j++] = *(p + 1);
                p += 2;
            } else if (*p == '&') {
                char ent[32];
                size_t elen = 0, econsumed = 0;
                if (decode_entity(p, end - p, ent, &elen, &econsumed)) {
                    for (size_t k = 0; k < elen; k++) buf[j++] = ent[k];
                    p += econsumed;
                } else {
                    buf[j++] = *p;
                    p++;
                }
            } else {
                buf[j++] = *p;
                p++;
            }
        }
        if (p < end && *p == '>') {
            buf[j] = '\0';
            *url_out = buf;
            return p + 1;
        }
        free(buf);
        return NULL;
    } else {
        int paren_depth = 0;
        while (p < end) {
            char c = *p;
            if (isspace((unsigned char)c)) {
                break;
            }
            if (c == '\\' && p + 1 < end && strchr("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", *(p + 1)) != NULL) {
                buf[j++] = *(p + 1);
                p += 2;
                continue;
            }
            if (c == '&') {
                char ent[32];
                size_t elen = 0, econsumed = 0;
                if (decode_entity(p, end - p, ent, &elen, &econsumed)) {
                    for (size_t k = 0; k < elen; k++) buf[j++] = ent[k];
                    p += econsumed;
                    continue;
                }
            }
            if (c == '(') {
                paren_depth++;
            } else if (c == ')') {
                if (paren_depth == 0) {
                    break;
                }
                paren_depth--;
            }
            buf[j++] = c;
            p++;
        }
        if (paren_depth != 0) {
            free(buf);
            return NULL;
        }
        buf[j] = '\0';
        *url_out = buf;
        return p;
    }
}

static const char *scan_link_ref_def(const char *start, const char *end, link_ref_t *ref, const char **next_out) {
    const char *p = start;
    int spaces = 0;
    while (p < end && *p == ' ') {
        spaces++;
        p++;
    }
    if (spaces > 3) return NULL;
    if (p < end && *p == '\t') return NULL;
    
    if (p >= end || *p != '[') return NULL;
    
    size_t label_len = 0;
    const char *after_label = scan_label(p, end, &label_len);
    if (!after_label) return NULL;
    if (label_len == 0 || label_len > 999) return NULL;
    bool has_non_ws = false;
    for (size_t k = 0; k < label_len; k++) {
        if (!isspace((unsigned char)p[1 + k])) {
            has_non_ws = true;
            break;
        }
    }
    if (!has_non_ws) return NULL;
    
    char *label_raw = xstrdup_len(p + 1, label_len);
    p = after_label;
    if (p >= end || *p != ':') {
        free(label_raw);
        return NULL;
    }
    p++;
    
    int newlines = 0;
    while (p < end) {
        if (*p == ' ' || *p == '\t') {
            p++;
        } else if (*p == '\r' || *p == '\n') {
            if (*p == '\r' && p + 1 < end && *(p + 1) == '\n') {
                p++;
            }
            p++;
            newlines++;
            if (newlines > 1) {
                break;
            }
        } else {
            break;
        }
    }
    if (newlines > 1 || p >= end) {
        free(label_raw);
        return NULL;
    }
    
    char *url = NULL;
    const char *after_url = scan_destination(p, end, &url);
    if (!after_url) {
        free(label_raw);
        return NULL;
    }
    
    // Check if it is valid without a title:
    const char *check_url_line = after_url;
    bool valid_without_title = true;
    while (check_url_line < end && *check_url_line != '\r' && *check_url_line != '\n') {
        if (*check_url_line != ' ' && *check_url_line != '\t') {
            valid_without_title = false;
            break;
        }
        check_url_line++;
    }
    
    p = after_url;
    int title_newlines = 0;
    bool has_space = false;
    while (p < end) {
        if (*p == ' ' || *p == '\t') {
            has_space = true;
            p++;
        } else if (*p == '\r' || *p == '\n') {
            has_space = true;
            if (*p == '\r' && p + 1 < end && *(p + 1) == '\n') {
                p++;
            }
            p++;
            title_newlines++;
            if (title_newlines > 1) {
                break;
            }
        } else {
            break;
        }
    }
    
    char *title = NULL;
    const char *after_title = NULL;
    bool has_title = false;
    
    if (has_space && title_newlines <= 1 && p < end && (*p == '"' || *p == '\'' || *p == '(')) {
        char open_delim = *p;
        char close_delim = (open_delim == '(') ? ')' : open_delim;
        char *t_buf = malloc(end - p + 1);
        size_t tj = 0;
        int consecutive_newlines = 0;
        p++;
        while (p < end) {
            if (*p == '\\' && p + 1 < end && strchr("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", *(p + 1)) != NULL) {
                t_buf[tj++] = *(p + 1);
                p += 2;
                consecutive_newlines = 0;
                continue;
            }
            if (*p == '&') {
                char ent[32];
                size_t elen = 0, econsumed = 0;
                if (decode_entity(p, end - p, ent, &elen, &econsumed)) {
                    for (size_t k = 0; k < elen; k++) t_buf[tj++] = ent[k];
                    p += econsumed;
                    consecutive_newlines = 0;
                    continue;
                }
            }
            if (*p == open_delim && open_delim != '(') break;
            if (*p == close_delim) break;
            
            if (*p == '\r' || *p == '\n') {
                consecutive_newlines++;
                if (consecutive_newlines > 1) {
                    break;
                }
            } else if (*p != ' ' && *p != '\t') {
                consecutive_newlines = 0;
            }
            
            t_buf[tj++] = *p;
            p++;
        }
        if (p < end && *p == close_delim && consecutive_newlines <= 1) {
            t_buf[tj] = '\0';
            title = t_buf;
            after_title = p + 1;
            has_title = true;
        } else {
            free(t_buf);
        }
    }
    
    if (has_title) {
        const char *check_p = after_title;
        bool valid_title = true;
        while (check_p < end && *check_p != '\r' && *check_p != '\n') {
            if (*check_p != ' ' && *check_p != '\t') {
                valid_title = false;
                break;
            }
            check_p++;
        }
        if (valid_title) {
            ref->label_normalized = normalize_label(label_raw);
            ref->url = url;
            ref->title = title;
            free(label_raw);
            *next_out = check_p;
            return start;
        }
        free(title);
    }
    
    if (valid_without_title) {
        ref->label_normalized = normalize_label(label_raw);
        ref->url = url;
        ref->title = NULL;
        free(label_raw);
        *next_out = check_url_line;
        return start;
    }
    
    free(label_raw);
    free(url);
    return NULL;
}

static bool starts_code_fence(const char *line);
static bool match_heading(const char *line, int *level_out, char **content_out);
static bool match_horizontal_rule(const char *line);
static bool starts_blockquote(const char *line);
static bool match_list_item(const char *line, bool *ordered_out, char **content_out);

static size_t find_last_spanned_line(line_record_t *lines, size_t count, size_t i, size_t end_idx) {
    size_t last = i;
    for (size_t k = i; k < count; k++) {
        if (lines[k].offset < end_idx) {
            last = k;
        } else {
            break;
        }
    }
    return last;
}

static void parse_all_link_refs_from_lines(line_record_t *lines, size_t count, parser_state_t *st) {
    if (!st || !st->src || count == 0) return;
    if (st->refs_initialized) return;
    st->refs_initialized = true;
    size_t cap = 8;
    st->refs = malloc(cap * sizeof(link_ref_t));
    st->refs_count = 0;
    
    char *writeable_src = (char *)st->src;
    
    bool *is_continuation = calloc(count, sizeof(bool));
    bool in_p = false;
    bool in_fence = false;
    for (size_t i = 0; i < count; i++) {
        char *line_text = lines[i].text;
        if (is_blank_line(line_text)) {
            in_p = false;
            continue;
        }
        if (starts_code_fence(line_text)) {
            in_fence = !in_fence;
            in_p = false;
            continue;
        }
        if (in_fence) {
            continue;
        }
        if (strncmp(line_text, "    ", 4) == 0) {
            in_p = false;
            continue;
        }
        
        // Skip blockquote prefixes if present in simulation
        const char *p_line = line_text;
        while (*p_line == '>' || *p_line == ' ' || *p_line == '\t') {
            if (*p_line == '>') {
                p_line++;
                if (*p_line == ' ') p_line++;
            } else {
                p_line++;
            }
        }
        size_t prefix_len = p_line - line_text;
        size_t start_offset = lines[i].offset + prefix_len;
        
        if (!in_p) {
            link_ref_t ref = {0};
            const char *next = NULL;
            const char *matched_start = scan_link_ref_def(st->src + start_offset, st->src + st->src_len, &ref, &next);
            if (matched_start && next) {
                free(ref.label_normalized);
                free(ref.url);
                free(ref.title);
                size_t end_idx = next - st->src;
                size_t last_line = find_last_spanned_line(lines, count, i, end_idx);
                i = last_line;
                in_p = false;
                continue;
            }
        }
        
        if (match_heading(line_text, NULL, NULL) || match_horizontal_rule(line_text) ||
            starts_blockquote(line_text) || match_list_item(line_text, NULL, NULL)) {
            in_p = false;
            continue;
        }
        
        if (in_p) {
            is_continuation[i] = true;
        } else {
            in_p = true;
        }
    }
    
    in_fence = false;
    for (size_t i = 0; i < count; i++) {
        char *line_text = lines[i].text;
        if (starts_code_fence(line_text)) {
            in_fence = !in_fence;
            continue;
        }
        if (in_fence || is_continuation[i]) {
            continue;
        }
        if (strncmp(line_text, "    ", 4) == 0) {
            continue;
        }
        
        const char *p_line = line_text;
        while (*p_line == '>' || *p_line == ' ' || *p_line == '\t') {
            if (*p_line == '>') {
                p_line++;
                if (*p_line == ' ') p_line++;
            } else {
                p_line++;
            }
        }
        
        size_t prefix_len = p_line - line_text;
        size_t start_offset = lines[i].offset + prefix_len;
        
        link_ref_t ref = {0};
        const char *next = NULL;
        const char *matched_start = scan_link_ref_def(st->src + start_offset, st->src + st->src_len, &ref, &next);
        if (matched_start && next) {
            if (st->refs_count >= cap) {
                cap *= 2;
                link_ref_t *new_refs = realloc(st->refs, cap * sizeof(link_ref_t));
                if (new_refs) {
                    st->refs = new_refs;
                }
            }
            st->refs[st->refs_count++] = ref;
            
            size_t start_idx = matched_start - st->src;
            size_t end_idx = next - st->src;
            for (size_t k = start_idx; k < end_idx; k++) {
                if (writeable_src[k] != '\n' && writeable_src[k] != '\r') {
                    writeable_src[k] = ' ';
                }
            }
            
            size_t last_line = find_last_spanned_line(lines, count, i, end_idx);
            for (size_t k = i; k <= last_line; k++) {
                const char *p_k = lines[k].text;
                while (*p_k == '>' || *p_k == ' ' || *p_k == '\t') {
                    if (*p_k == '>') {
                        p_k++;
                        if (*p_k == ' ') p_k++;
                    } else {
                        p_k++;
                    }
                }
                size_t pk_prefix = p_k - lines[k].text;
                char *new_text = malloc(pk_prefix + 1);
                memcpy(new_text, lines[k].text, pk_prefix);
                new_text[pk_prefix] = '\0';
                free(lines[k].text);
                lines[k].text = new_text;
            }
            i = last_line;
            continue;
        }
    }
    
    free(is_continuation);
}

static char *percent_encode_url(const char *url) {
    if (!url) return NULL;
    size_t len = strlen(url);
    char *buf = malloc(len * 3 + 1);
    if (!buf) return NULL;
    size_t j = 0;
    for (size_t i = 0; url[i] != '\0'; i++) {
        unsigned char c = (unsigned char)url[i];
        if (c == '%' && url[i+1] != '\0' && url[i+2] != '\0' &&
            isxdigit((unsigned char)url[i+1]) && isxdigit((unsigned char)url[i+2])) {
            buf[j++] = '%';
        } else if (isalnum(c) || strchr("-_.~:/?#@!$&'()*+,;=", c) != NULL) {
            buf[j++] = (char)c;
        } else {
            sprintf(buf + j, "%%%02X", c);
            j += 3;
        }
    }
    buf[j] = '\0';
    return buf;
}

static const link_ref_t *find_link_ref(parser_state_t *st, const char *label) {
    if (!st || !label || st->refs_count == 0) return NULL;
    char *norm = normalize_label(label);
    if (!norm) return NULL;
    
    const link_ref_t *found = NULL;
    for (size_t i = 0; i < st->refs_count; i++) {
        if (strcmp(st->refs[i].label_normalized, norm) == 0) {
            found = &st->refs[i];
            break;
        }
    }
    free(norm);
    return found;
}

static void parse_all_footnotes_from_lines(line_record_t *lines, size_t count, parser_state_t *st) {
    if (!lines || count == 0 || !st) return;
    for (size_t i = 0; i < count; i++) {
        const char *line = lines[i].text;
        while (*line == ' ' || *line == '\t') line++;
        if (line[0] == '[' && line[1] == '^') {
            const char *p = line + 2;
            const char *label_start = p;
            while (*p && *p != ']' && *p != ' ' && *p != '\t') p++;
            if (*p == ']' && *(p + 1) == ':') {
                size_t label_len = p - label_start;
                if (label_len > 0) {
                    char *label = xstrdup_len(label_start, label_len);
                    const char *content_start = p + 2;
                    while (*content_start == ' ' || *content_start == '\t') content_start++;
                    char *content = xstrdup(content_start);
                    
                    st->footnotes = (footnote_def_t *)realloc(st->footnotes, (st->footnotes_count + 1) * sizeof(footnote_def_t));
                    if (st->footnotes) {
                        st->footnotes[st->footnotes_count].label = label;
                        st->footnotes[st->footnotes_count].label_normalized = normalize_label(label);
                        st->footnotes[st->footnotes_count].content = content;
                        st->footnotes[st->footnotes_count].index = (int)(st->footnotes_count + 1);
                        st->footnotes[st->footnotes_count].used = false;
                        st->footnotes_count++;
                    }
                }
            }
        }
    }
}

static bool is_footnote_def_line(const char *line) {
    if (!line) return false;
    while (*line == ' ' || *line == '\t') line++;
    if (line[0] == '[' && line[1] == '^') {
        const char *p = line + 2;
        while (*p && *p != ']' && *p != ' ' && *p != '\t') p++;
        if (*p == ']' && *(p + 1) == ':') {
            return true;
        }
    }
    return false;
}

static const char *scan_html_tag(const char *p, const char *end, size_t *tag_len);

static int scan_html_block_type(const char *line) {
    if (!line) return 0;
    int spaces = 0;
    while (*line == ' ') { spaces++; line++; }
    if (spaces > 3 || *line != '<') return 0;

    const char *p = line + 1;
    
    /* Type 1: <script, <pre, <style (case-insensitive) */
    if (ci_strncasecmp(p, "script", 6) == 0 && (p[6] == '>' || p[6] == ' ' || p[6] == '\t' || p[6] == '\0' || p[6] == '\r' || p[6] == '\n')) return 1;
    if (ci_strncasecmp(p, "pre", 3) == 0 && (p[3] == '>' || p[3] == ' ' || p[3] == '\t' || p[3] == '\0' || p[3] == '\r' || p[3] == '\n')) return 1;
    if (ci_strncasecmp(p, "style", 5) == 0 && (p[5] == '>' || p[5] == ' ' || p[5] == '\t' || p[5] == '\0' || p[5] == '\r' || p[5] == '\n')) return 1;

    /* Type 2: <!-- */
    if (strncmp(p, "!--", 3) == 0) return 2;

    /* Type 3: <? */
    if (*p == '?') return 3;

    /* Type 4: <! + uppercase letter */
    if (*p == '!' && p[1] >= 'A' && p[1] <= 'Z') return 4;

    /* Type 5: <![CDATA[ */
    if (strncmp(p, "![CDATA[", 8) == 0) return 5;

    /* Type 6: < or </ followed by standard block tag + ws, >, />, \0 */
    bool is_close = (*p == '/');
    const char *tag_start = is_close ? p + 1 : p;
    
    static const char *block_tags_type6[] = {
        "address", "article", "aside", "base", "basefont", "blockquote", "body",
        "caption", "center", "col", "colgroup", "dd", "details", "dialog", "dir",
        "div", "dl", "dt", "fieldset", "figcaption", "figure", "footer", "form",
        "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header",
        "hr", "html", "iframe", "legend", "li", "link", "main", "menu", "menuitem",
        "nav", "noframes", "ol", "optgroup", "option", "p", "param", "section",
        "source", "summary", "table", "tbody", "td", "tfoot", "th", "thead",
        "title", "tr", "track", "ul", NULL
    };
    
    for (int t = 0; block_tags_type6[t] != NULL; t++) {
        size_t tlen = strlen(block_tags_type6[t]);
        if (ci_strncasecmp(tag_start, block_tags_type6[t], tlen) == 0) {
            char next_c = tag_start[tlen];
            if (next_c == '>') {
                return 6;
            }
            if (next_c == '/' && tag_start[tlen + 1] == '>') {
                return 6;
            }
            if (next_c == ' ' || next_c == '\t') {
                if (strchr(tag_start + tlen, '>') != NULL) {
                    return 6;
                }
            }
        }
    }

    /* Type 7: complete open/close HTML tag that occupies the entire line (only optional whitespace after) */
    size_t tag_len = 0;
    if (scan_html_tag(p - 1, line + strlen(line), &tag_len) != NULL) {
        const char *after = (p - 1) + tag_len;
        while (*after == ' ' || *after == '\t') after++;
        if (*after == '\0' || *after == '\r' || *after == '\n') {
            return 7;
        }
    }

    return 0;
}

static bool is_html_block_line(const char *line) {
    return scan_html_block_type(line) != 0;
}

static const char *replace_emoji_shortcode(const char *name) {
    if (!name) return NULL;
    if (strcmp(name, "rocket") == 0) return "🚀";
    if (strcmp(name, "fire") == 0) return "🔥";
    if (strcmp(name, "smile") == 0) return "😄";
    if (strcmp(name, "heart") == 0) return "❤️";
    if (strcmp(name, "thumbsup") == 0 || strcmp(name, "+1") == 0) return "👍";
    if (strcmp(name, "thumbsdown") == 0 || strcmp(name, "-1") == 0) return "👎";
    if (strcmp(name, "star") == 0) return "⭐";
    if (strcmp(name, "check") == 0 || strcmp(name, "white_check_mark") == 0) return "✅";
    if (strcmp(name, "x") == 0 || strcmp(name, "cross_mark") == 0) return "❌";
    if (strcmp(name, "warning") == 0) return "⚠️";
    if (strcmp(name, "bulb") == 0 || strcmp(name, "tip") == 0) return "💡";
    if (strcmp(name, "tada") == 0 || strcmp(name, "party") == 0) return "🎉";
    if (strcmp(name, "100") == 0) return "💯";
    if (strcmp(name, "eyes") == 0) return "👀";
    if (strcmp(name, "zap") == 0 || strcmp(name, "lightning") == 0) return "⚡";
    if (strcmp(name, "memo") == 0 || strcmp(name, "pencil") == 0) return "📝";
    if (strcmp(name, "sparkles") == 0) return "✨";
    if (strcmp(name, "computer") == 0 || strcmp(name, "laptop") == 0) return "💻";
    if (strcmp(name, "art") == 0 || strcmp(name, "palette") == 0) return "🎨";
    return NULL;
}


static char *join_strings(const char *a, const char *b) {
    if (!a && !b) {
        return xstrdup("");
    }
    if (!a) {
        return xstrdup(b);
    }
    if (!b) {
        return xstrdup(a);
    }
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    char *buf = (char *)malloc(len_a + len_b + 1);
    if (!buf) {
        return NULL;
    }
    memcpy(buf, a, len_a);
    memcpy(buf + len_a, b, len_b + 1);
    return buf;
}

static const char *scan_code_span(const char *p, const char *end, size_t *span_len, size_t *op_len_out) {
    if (p >= end || *p != '`') return NULL;
    const char *start = p;
    size_t op_len = 0;
    while (p < end && *p == '`') {
        op_len++;
        p++;
    }
    *op_len_out = op_len;
    
    while (p < end) {
        if (*p == '`') {
            size_t cl_len = 0;
            while (p < end && *p == '`') {
                cl_len++;
                p++;
            }
            if (cl_len == op_len) {
                *span_len = p - start;
                return start;
            }
        } else {
            p++;
        }
    }
    return NULL;
}

static bool is_idx_inside_code_span(const char *text, size_t len, size_t target_idx) {
    size_t i = 0;
    while (i < len) {
        if (text[i] == '`') {
            size_t span_len = 0;
            size_t op_len = 0;
            const char *matched = scan_code_span(text + i, text + len, &span_len, &op_len);
            if (matched) {
                size_t start = i;
                size_t end = i + span_len;
                if (target_idx >= start && target_idx < end) {
                    return true;
                }
                i += span_len;
                continue;
            }
        }
        i++;
    }
    return false;
}

static char *render_inline_fragment(const char *text, const char *src, size_t base_offset, parser_state_t *st);

static bool parse_inline_link_or_image(const char *text, size_t len, size_t i, bool is_img, parser_state_t *st, char **out_html, size_t *next_i) {
    size_t label_start = i + (is_img ? 2 : 1);
    size_t k = label_start;
    int depth = 1;
    size_t close_bracket = SIZE_MAX;
    while (k < len && depth > 0) {
        if (text[k] == '\\') {
            k += 2;
            continue;
        }
        if (text[k] == '[' && !is_idx_inside_code_span(text, len, k)) {
            depth++;
        } else if (text[k] == ']' && !is_idx_inside_code_span(text, len, k)) {
            depth--;
            if (depth == 0) {
                close_bracket = k;
                break;
            }
        }
        k++;
    }
    if (close_bracket == SIZE_MAX) {
        return false;
    }
    
    char *label_raw = xstrdup_len(text + label_start, close_bracket - label_start);
    
    const char *url = NULL;
    const char *title = NULL;
    char *allocated_url = NULL;
    char *allocated_title = NULL;
    size_t end_idx = SIZE_MAX;
    bool matched = false;
    
    // 1. Inline link: (url "title")
    if (close_bracket + 1 < len && text[close_bracket + 1] == '(') {
        const char *p = text + close_bracket + 2;
        const char *p_end = text + len;
        while (p < p_end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        const char *after_dest = scan_destination(p, p_end, &allocated_url);
        if (after_dest) {
            p = after_dest;
            while (p < p_end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
            if (p < p_end && (*p == '"' || *p == '\'' || *p == '(')) {
                char open_delim = *p;
                char close_delim = (open_delim == '(') ? ')' : open_delim;
                const char *t_start = p + 1;
                p++;
                while (p < p_end) {
                    if (*p == '\\' && p + 1 < p_end) {
                        p += 2;
                        continue;
                    }
                    if (*p == open_delim && open_delim != '(') break;
                    if (*p == close_delim) break;
                    p++;
                }
                if (p < p_end && *p == close_delim) {
                    char *t_buf = malloc(p - t_start + 1);
                    size_t tj = 0;
                    for (const char *tp = t_start; tp < p; ) {
                        if (*tp == '\\' && tp + 1 < p && strchr("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", *(tp + 1)) != NULL) {
                            t_buf[tj++] = *(tp + 1);
                            tp += 2;
                        } else if (*tp == '&') {
                            char ent[32];
                            size_t elen = 0, econsumed = 0;
                            if (decode_entity(tp, p - tp, ent, &elen, &econsumed)) {
                                for (size_t k = 0; k < elen; k++) t_buf[tj++] = ent[k];
                                tp += econsumed;
                            } else {
                                t_buf[tj++] = *tp;
                                tp++;
                            }
                        } else {
                            t_buf[tj++] = *tp;
                            tp++;
                        }
                    }
                    t_buf[tj] = '\0';
                    allocated_title = t_buf;
                    p++;
                }
            }
            while (p < p_end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
            if (p < p_end && *p == ')') {
                url = allocated_url;
                title = allocated_title;
                end_idx = p - text + 1;
                matched = true;
            }
        }
    }
    // 2. Reference link: [text][ref] or [text][]
    else if (close_bracket + 1 < len && text[close_bracket + 1] == '[') {
        size_t ref_start = close_bracket + 2;
        size_t ref_close = SIZE_MAX;
        size_t r = ref_start;
        int r_depth = 1;
        while (r < len && r_depth > 0) {
            if (text[r] == '\\') {
                r += 2;
                continue;
            }
            if (text[r] == '[' && !is_idx_inside_code_span(text, len, r)) r_depth++;
            else if (text[r] == ']' && !is_idx_inside_code_span(text, len, r)) {
                r_depth--;
                if (r_depth == 0) {
                    ref_close = r;
                    break;
                }
            }
            r++;
        }
        if (ref_close != SIZE_MAX) {
            char *ref_label = xstrdup_len(text + ref_start, ref_close - ref_start);
            const char *lookup_label = (ref_label[0] == '\0') ? label_raw : ref_label;
            const link_ref_t *ref = find_link_ref(st, lookup_label);
            free(ref_label);
            if (ref) {
                url = ref->url;
                title = ref->title;
                end_idx = ref_close + 1;
                matched = true;
            } else {
                return false;
            }
        }
    }
    // 3. Shortcut reference: [text]
    else {
        const link_ref_t *ref = find_link_ref(st, label_raw);
        if (ref) {
            url = ref->url;
            title = ref->title;
            end_idx = close_bracket + 1;
            matched = true;
        }
    }
    
    if (matched) {
        char *inner_html = render_inline_fragment(label_raw, st->src, label_start, st);
        if (!inner_html) {
            free(label_raw);
            free(allocated_url);
            free(allocated_title);
            return false;
        }
        
        if (!is_img && (strstr(inner_html, "<a ") != NULL || strstr(inner_html, "<a\n") != NULL || strstr(inner_html, "<a\t") != NULL)) {
            free(inner_html);
            free(label_raw);
            free(allocated_url);
            free(allocated_title);
            return false;
        }
        
        char *encoded_url = percent_encode_url(url ? url : "");
        char *escaped_url = xstrdup("");
        escape_html_append(&escaped_url, encoded_url ? encoded_url : "");
        free(encoded_url);
        char *escaped_title = xstrdup("");
        escape_html_append(&escaped_title, title ? title : "");
        
        char *result = xstrdup("");
        if (is_img) {
            char *plain_alt = strip_html_tags(inner_html);
            char *escaped_alt = xstrdup("");
            escape_html_append(&escaped_alt, plain_alt);
            append_str(&result, "<img src=\"");
            append_str(&result, escaped_url);
            append_str(&result, "\" alt=\"");
            append_str(&result, escaped_alt);
            append_str(&result, "\"");
            if (title && title[0] != '\0') {
                append_str(&result, " title=\"");
                append_str(&result, escaped_title);
                append_str(&result, "\"");
            }
            append_str(&result, " />");
            free(plain_alt);
            free(escaped_alt);
        } else {
            append_str(&result, "<a href=\"");
            append_str(&result, escaped_url);
            append_str(&result, "\"");
            if (title && title[0] != '\0') {
                append_str(&result, " title=\"");
                append_str(&result, escaped_title);
                append_str(&result, "\"");
            }
            append_str(&result, ">");
            append_str(&result, inner_html);
            append_str(&result, "</a>");
        }
        
        free(inner_html);
        free(escaped_url);
        free(escaped_title);
        free(label_raw);
        free(allocated_url);
        free(allocated_title);
        
        *out_html = result;
        *next_i = end_idx;
        return true;
    }
    
    free(label_raw);
    free(allocated_url);
    free(allocated_title);
    return false;
}

typedef enum {
    TOK_TEXT,
    TOK_DELIM,
    TOK_HTML_TAG
} token_type_t;

typedef struct {
    token_type_t type;
    char *val;
    size_t len;
    char delim_char;
    bool can_open;
    bool can_close;
} inline_token_t;

static bool is_punctuation_char(char c) {
    return strchr("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", c) != NULL;
}

static bool is_unicode_ws_at(const char *text, size_t len, size_t idx) {
    if (idx >= len) return true;
    char c = text[idx];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
        return true;
    }
    if ((unsigned char)c == 0xC2 && idx + 1 < len && (unsigned char)text[idx + 1] == 0xA0) {
        return true;
    }
    return false;
}

static bool is_unicode_ws_before(const char *text, size_t len, size_t idx) {
    (void)len;
    if (idx == 0) return true;
    char c = text[idx - 1];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
        return true;
    }
    if (idx >= 2 && (unsigned char)text[idx - 1] == 0xA0 && (unsigned char)text[idx - 2] == 0xC2) {
        return true;
    }
    return false;
}

static bool is_unicode_punct_at(const char *text, size_t len, size_t idx) {
    if (idx >= len) return false;
    char c = text[idx];
    if (is_punctuation_char(c)) return true;
    if ((unsigned char)c == 0xC2 && idx + 1 < len) {
        unsigned char next = (unsigned char)text[idx + 1];
        if (next >= 0xA1 && next <= 0xBF) return true;
    }
    if ((unsigned char)c == 0xE2 && idx + 2 < len) {
        unsigned char b2 = (unsigned char)text[idx + 1];
        if (b2 == 0x80 || b2 == 0x81 || b2 == 0x82) {
            return true;
        }
    }
    return false;
}

static bool is_unicode_punct_before(const char *text, size_t len, size_t idx) {
    if (idx == 0) return false;
    size_t start = idx - 1;
    while (start > 0 && (text[start] & 0xC0) == 0x80) {
        start--;
    }
    return is_unicode_punct_at(text, len, start);
}

static bool is_left_flanking(const char *text, size_t len, size_t start, size_t run_len) {
    size_t next_idx = start + run_len;
    if (next_idx >= len || is_unicode_ws_at(text, len, next_idx)) {
        return false;
    }
    if (!is_unicode_punct_at(text, len, next_idx)) {
        return true;
    }
    return start == 0 || is_unicode_ws_before(text, len, start) || is_unicode_punct_before(text, len, start);
}

static bool is_right_flanking(const char *text, size_t len, size_t start, size_t run_len) {
    if (start == 0 || is_unicode_ws_before(text, len, start)) {
        return false;
    }
    if (!is_unicode_punct_before(text, len, start)) {
        return true;
    }
    size_t next_idx = start + run_len;
    return next_idx >= len || is_unicode_ws_at(text, len, next_idx) || is_unicode_punct_at(text, len, next_idx);
}

static void free_inline_tokens(inline_token_t *tokens, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(tokens[i].val);
    }
    free(tokens);
}

static void insert_token_at(inline_token_t **tokens, size_t *count, size_t *cap, size_t index, inline_token_t new_tok) {
    if (*count >= *cap) {
        *cap *= 2;
        *tokens = realloc(*tokens, *cap * sizeof(inline_token_t));
    }
    for (size_t i = *count; i > index; i--) {
        (*tokens)[i] = (*tokens)[i - 1];
    }
    (*tokens)[index] = new_tok;
    (*count)++;
}

static const char *scan_html_tag(const char *p, const char *end, size_t *tag_len) {
    if (p >= end || *p != '<') return NULL;
    const char *start = p;
    p++;
    
    if (p < end && *p == '/') {
        p++;
        if (p < end && isalpha((unsigned char)*p)) {
            while (p < end && isalnum((unsigned char)*p)) p++;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
            if (p < end && *p == '>') {
                *tag_len = p + 1 - start;
                return start;
            }
        }
        return NULL;
    }
    
    if (p < end && isalpha((unsigned char)*p)) {
        while (p < end && isalnum((unsigned char)*p)) p++;
        while (p < end) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
            if (p >= end) break;
            if (*p == '>' || (p + 1 < end && *p == '/' && *(p + 1) == '>')) {
                break;
            }
            if (isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == ':') {
                while (p < end && (isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == ':' || *p == '.')) p++;
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
                if (p < end && *p == '=') {
                    p++;
                    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
                    if (p < end && (*p == '"' || *p == '\'' || (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '"' && *p != '\'' && *p != '=' && *p != '<' && *p != '>' && *p != '`'))) {
                        if (*p == '"' || *p == '\'') {
                            char delim = *p;
                            p++;
                            while (p < end && *p != delim) p++;
                            if (p < end && *p == delim) p++;
                        } else {
                            while (p < end && !isspace((unsigned char)*p) && *p != '"' && *p != '\'' && *p != '=' && *p != '<' && *p != '>' && *p != '`') p++;
                        }
                    }
                }
            } else {
                return NULL;
            }
        }
        if (p < end && *p == '/') {
            p++;
        }
        if (p < end && *p == '>') {
            *tag_len = p + 1 - start;
            return start;
        }
    }
    
    /* HTML comment: <!-- ... --> */
    if (p + 3 <= end && strncmp(p, "!--", 3) == 0) {
        p += 3;
        if (p < end && *p == '>') {
            *tag_len = p + 1 - start;
            return start;
        }
        if (p + 1 <= end && strncmp(p, "->", 2) == 0) {
            *tag_len = p + 2 - start;
            return start;
        }
        if (p + 2 <= end && strncmp(p, "-->", 3) == 0) {
            *tag_len = p + 3 - start;
            return start;
        }
        while (p + 2 < end) {
            if (strncmp(p, "-->", 3) == 0) {
                *tag_len = p + 3 - start;
                return start;
            }
            p++;
        }
        return NULL;
    }

    /* Processing instruction: <? ... ?> */
    if (p < end && *p == '?') {
        p++;
        while (p + 1 < end) {
            if (p[0] == '?' && p[1] == '>') {
                *tag_len = p + 2 - start;
                return start;
            }
            p++;
        }
        return NULL;
    }

    /* Declaration: <! [A-Z] ... > */
    if (p + 1 < end && *p == '!' && p[1] >= 'A' && p[1] <= 'Z') {
        p += 2;
        while (p < end && *p != '>') p++;
        if (p < end && *p == '>') {
            *tag_len = p + 1 - start;
            return start;
        }
        return NULL;
    }

    /* CDATA: <![CDATA[ ... ]]> */
    if (p + 8 <= end && strncmp(p, "![CDATA[", 8) == 0) {
        p += 8;
        while (p + 2 < end) {
            if (strncmp(p, "]]>", 3) == 0) {
                *tag_len = p + 3 - start;
                return start;
            }
            p++;
        }
        return NULL;
    }
    
    return NULL;
}

static bool is_scheme_char(char c) {
    return isalnum((unsigned char)c) || c == '+' || c == '-' || c == '.';
}

static const char *scan_autolink(const char *p, const char *end, size_t *autolink_len) {
    if (p >= end || *p != '<') return NULL;
    const char *start = p;
    p++;
    
    if (p < end && isalpha((unsigned char)*p)) {
        const char *scheme_start = p;
        p++;
        while (p < end && is_scheme_char(*p) && (p - scheme_start < 32)) {
            p++;
        }
        if (p < end && *p == ':') {
            p++;
            while (p < end && (unsigned char)*p > 32 && *p != '<' && *p != '>') {
                p++;
            }
            if (p < end && *p == '>') {
                *autolink_len = p + 1 - start;
                return start;
            }
        }
    }
    
    p = start + 1;
    const char *local_start = p;
    while (p < end && (isalnum((unsigned char)*p) || strchr("!#$%&'*+-/=?^_`{|}~.", *p) != NULL)) {
        p++;
    }
    if (p > local_start && p < end && *p == '@') {
        p++;
        const char *domain_start = p;
        (void)domain_start;
        bool has_label = false;
        while (p < end) {
            if (isalnum((unsigned char)*p)) {
                p++;
                has_label = true;
            } else if (*p == '-' && has_label) {
                p++;
            } else if (*p == '.' && has_label) {
                p++;
                has_label = false;
            } else {
                break;
            }
        }
        if (has_label && p < end && *p == '>') {
            *autolink_len = p + 1 - start;
            return start;
        }
    }
    
    return NULL;
}

static char *render_autolink(const char *text, size_t len) {
    char *url = xstrdup_len(text + 1, len - 2);
    char *encoded_url = percent_encode_url(url);
    char *escaped_url = xstrdup("");
    escape_html_append(&escaped_url, encoded_url ? encoded_url : "");
    free(encoded_url);
    
    char *href = NULL;
    bool is_email = (strchr(url, '@') != NULL && strchr(url, ':') == NULL);
    if (is_email) {
        href = malloc(strlen(escaped_url) + 8);
        if (!href) { free(url); free(escaped_url); return xstrdup(""); }
        sprintf(href, "mailto:%s", escaped_url);
    } else {
        href = xstrdup(escaped_url);
    }
    
    char *escaped_content = xstrdup("");
    escape_html_append(&escaped_content, url);
    
    char *html = malloc(strlen(href) + strlen(escaped_content) + 16);
    if (!html) { free(url); free(escaped_url); free(escaped_content); free(href); return xstrdup(""); }
    sprintf(html, "<a href=\"%s\">%s</a>", href, escaped_content);
    
    free(url);
    free(escaped_url);
    free(escaped_content);
    free(href);
    return html;
}


static char *render_code_span(const char *text, size_t len, size_t op_len) {
    size_t content_len = len - 2 * op_len;
    const char *content = text + op_len;
    
    char *temp = malloc(content_len + 1);
    if (!temp) return xstrdup("");
    for (size_t k = 0; k < content_len; k++) {
        if (content[k] == '\n' || content[k] == '\r') {
            temp[k] = ' ';
        } else {
            temp[k] = content[k];
        }
    }
    temp[content_len] = '\0';
    
    char *stripped = temp;
    size_t stripped_len = content_len;
    if (stripped_len >= 2 && stripped[0] == ' ' && stripped[stripped_len - 1] == ' ') {
        bool all_space = true;
        for (size_t k = 0; k < stripped_len; k++) {
            if (stripped[k] != ' ') {
                all_space = false;
                break;
            }
        }
        if (!all_space) {
            stripped++;
            stripped_len -= 2;
        }
    }
    
    char *raw_content = xstrdup_len(stripped, stripped_len);
    char *escaped = xstrdup("");
    escape_html_append(&escaped, raw_content);
    free(raw_content);
    free(temp);
    
    char *html = malloc(strlen(escaped) + 15);
    if (!html) { free(escaped); return xstrdup(""); }
    sprintf(html, "<code>%s</code>", escaped);
    free(escaped);
    return html;
}

static inline_token_t *tokenize_inline(const char *text, size_t len, parser_state_t *st, size_t *tokens_count_out, size_t *tokens_cap_out) {
    size_t cap = 16;
    inline_token_t *tokens = malloc(cap * sizeof(inline_token_t));
    size_t count = 0;
    
    size_t i = 0;
    while (i < len) {
        if (text[i] == '\\') {
            if (i + 1 >= len) {
                inline_token_t tok = {TOK_TEXT, xstrdup("\\"), 1, 0, false, false};
                insert_token_at(&tokens, &count, &cap, count, tok);
                i++;
                continue;
            }
            char next = text[i + 1];
            if (next == '\n') {
                if (count > 0 && tokens[count - 1].type == TOK_TEXT) {
                    char *val = tokens[count - 1].val;
                    size_t vlen = strlen(val);
                    while (vlen > 0 && val[vlen - 1] == ' ') vlen--;
                    val[vlen] = '\0';
                    tokens[count - 1].len = vlen;
                }
                inline_token_t tok = {TOK_HTML_TAG, xstrdup("<br />\n"), 7, 0, false, false};
                insert_token_at(&tokens, &count, &cap, count, tok);
                i += 2;
                continue;
            }
            if (strchr("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", next) != NULL) {
                char buf[8];
                size_t blen = 0;
                if (next == '&') {
                    strcpy(buf, "&amp;");
                    blen = 5;
                } else if (next == '<') {
                    strcpy(buf, "&lt;");
                    blen = 4;
                } else if (next == '>') {
                    strcpy(buf, "&gt;");
                    blen = 4;
                } else if (next == '"') {
                    strcpy(buf, "&quot;");
                    blen = 6;
                } else {
                    buf[0] = next;
                    buf[1] = '\0';
                    blen = 1;
                }
                inline_token_t tok = {TOK_TEXT, xstrdup(buf), blen, 0, false, false};
                insert_token_at(&tokens, &count, &cap, count, tok);
                i += 2;
                continue;
            }
            inline_token_t tok = {TOK_TEXT, xstrdup("\\"), 1, 0, false, false};
            insert_token_at(&tokens, &count, &cap, count, tok);
            i++;
            continue;
        }
        
        if (text[i] == '`') {
            size_t span_len = 0;
            size_t op_len = 0;
            const char *matched = scan_code_span(text + i, text + len, &span_len, &op_len);
            if (matched) {
                char *span_html = render_code_span(text + i, span_len, op_len);
                inline_token_t tok = {TOK_HTML_TAG, span_html, strlen(span_html), 0, false, false};
                insert_token_at(&tokens, &count, &cap, count, tok);
                i += span_len;
                continue;
            } else {
                char *raw = xstrdup_len(text + i, op_len);
                inline_token_t tok = {TOK_TEXT, raw, op_len, 0, false, false};
                insert_token_at(&tokens, &count, &cap, count, tok);
                i += op_len;
                continue;
            }
        }
        
        if (text[i] == '[' && i + 1 < len && text[i + 1] == '^') {
            size_t k = i + 2;
            while (k < len && text[k] != ']' && text[k] != ' ' && text[k] != '\t' && text[k] != '\n') k++;
            if (k < len && text[k] == ']') {
                size_t label_len = k - (i + 2);
                char *label = xstrdup_len(text + i + 2, label_len);
                char *norm = normalize_label(label);
                footnote_def_t *fn = NULL;
                for (size_t f = 0; f < st->footnotes_count; f++) {
                    if (st->footnotes[f].label_normalized && strcmp(st->footnotes[f].label_normalized, norm) == 0) {
                        fn = &st->footnotes[f];
                        break;
                    }
                }
                free(norm);
                if (fn) {
                    fn->used = true;
                    char fn_html[256];
                    snprintf(fn_html, sizeof(fn_html), "<sup><a href=\"#fn-%s\" id=\"fnref-%s\" class=\"footnote-ref\">[%d]</a></sup>", label, label, fn->index);
                    inline_token_t tok = {TOK_HTML_TAG, xstrdup(fn_html), strlen(fn_html), 0, false, false};
                    insert_token_at(&tokens, &count, &cap, count, tok);
                    free(label);
                    i = k + 1;
                    continue;
                }
                free(label);
            }
        }
        
        if ((text[i] == '!' && i + 1 < len && text[i + 1] == '[') || text[i] == '[') {
            bool is_img = (text[i] == '!');
            char *link_html = NULL;
            size_t next_i = 0;
            if (parse_inline_link_or_image(text, len, i, is_img, st, &link_html, &next_i)) {
                inline_token_t tok = {TOK_HTML_TAG, link_html, strlen(link_html), 0, false, false};
                insert_token_at(&tokens, &count, &cap, count, tok);
                i = next_i;
                continue;
            }
        }

        if (text[i] == '$' && (i == 0 || text[i - 1] != '\\') && i + 1 < len && text[i + 1] != '$' && text[i + 1] != ' ') {
            size_t close_math = i + 1;
            while (close_math < len && text[close_math] != '$') {
                if (text[close_math] == '\\' && close_math + 1 < len) close_math += 2;
                else close_math++;
            }
            if (close_math < len && text[close_math] == '$' && text[close_math - 1] != ' ') {
                size_t math_len = close_math - i + 1;
                char *math_str = xstrdup_len(text + i, math_len);
                char *escaped_m = xstrdup("");
                escape_html_append(&escaped_m, math_str);
                free(math_str);
                char math_html[1024];
                snprintf(math_html, sizeof(math_html), "<span class=\"math-inline\">%s</span>", escaped_m);
                free(escaped_m);
                inline_token_t tok = {TOK_HTML_TAG, xstrdup(math_html), strlen(math_html), 0, false, false};
                insert_token_at(&tokens, &count, &cap, count, tok);
                i = close_math + 1;
                continue;
            }
        }

        if (text[i] == ':' && i + 2 < len && isalpha((unsigned char)text[i + 1])) {
            size_t end_colon = i + 1;
            while (end_colon < len && (isalnum((unsigned char)text[end_colon]) || text[end_colon] == '_' || text[end_colon] == '+' || text[end_colon] == '-')) {
                end_colon++;
            }
            if (end_colon < len && text[end_colon] == ':') {
                size_t name_len = end_colon - (i + 1);
                char *sname = xstrdup_len(text + i + 1, name_len);
                const char *emoji = replace_emoji_shortcode(sname);
                free(sname);
                if (emoji) {
                    inline_token_t tok = {TOK_TEXT, xstrdup(emoji), strlen(emoji), 0, false, false};
                    insert_token_at(&tokens, &count, &cap, count, tok);
                    i = end_colon + 1;
                    continue;
                }
            }
        }
        
        if (text[i] == '\n') {
            int trailing_spaces = 0;
            size_t k = count;
            while (k > 0 && tokens[k - 1].type == TOK_TEXT) {
                char *val = tokens[k - 1].val;
                size_t vlen = strlen(val);
                size_t s = vlen;
                while (s > 0 && val[s - 1] == ' ') {
                    trailing_spaces++;
                    s--;
                }
                if (s > 0) {
                    break;
                }
                k--;
            }
            if (trailing_spaces >= 2) {
                k = count;
                int to_strip = trailing_spaces;
                while (k > 0 && to_strip > 0 && tokens[k - 1].type == TOK_TEXT) {
                    char *val = tokens[k - 1].val;
                    size_t vlen = strlen(val);
                    if (vlen <= (size_t)to_strip) {
                        to_strip -= vlen;
                        free(tokens[k - 1].val);
                        count--;
                        k--;
                    } else {
                        val[vlen - to_strip] = '\0';
                        to_strip = 0;
                    }
                }
                inline_token_t tok = {TOK_HTML_TAG, xstrdup("<br />\n"), 7, 0, false, false};
                insert_token_at(&tokens, &count, &cap, count, tok);
            } else {
                inline_token_t tok = {TOK_TEXT, xstrdup("\n"), 1, 0, false, false};
                insert_token_at(&tokens, &count, &cap, count, tok);
            }
            i++;
            continue;
        }
        
        size_t tag_len = 0;
        if (text[i] == '<') {
            const char *matched = scan_html_tag(text + i, text + len, &tag_len);
            if (matched) {
                inline_token_t tok = {TOK_HTML_TAG, xstrdup_len(text + i, tag_len), tag_len, 0, false, false};
                insert_token_at(&tokens, &count, &cap, count, tok);
                i += tag_len;
                continue;
            }
            size_t autolink_len = 0;
            const char *autolink_matched = scan_autolink(text + i, text + len, &autolink_len);
            if (autolink_matched) {
                char *autolink_html = render_autolink(text + i, autolink_len);
                inline_token_t tok = {TOK_HTML_TAG, autolink_html, strlen(autolink_html), 0, false, false};
                insert_token_at(&tokens, &count, &cap, count, tok);
                i += autolink_len;
                continue;
            }
        }

        if (text[i] == '&') {
            char entity_utf8[32];
            size_t entity_len = 0;
            size_t consumed = 0;
            if (decode_entity(text + i, len - i, entity_utf8, &entity_len, &consumed)) {
                char *escaped_e = xstrdup("");
                escape_html_append(&escaped_e, entity_utf8);
                inline_token_t tok = {TOK_TEXT, escaped_e, strlen(escaped_e), 0, false, false};
                insert_token_at(&tokens, &count, &cap, count, tok);
                i += consumed;
                continue;
            }
        }
        
        if (text[i] == '*' || text[i] == '_' || text[i] == '~') {
            char c = text[i];
            size_t run_len = 0;
            while (i + run_len < len && text[i + run_len] == c) {
                run_len++;
            }
            
            bool left = is_left_flanking(text, len, i, run_len);
            bool right = is_right_flanking(text, len, i, run_len);
            
            bool can_open = false;
            bool can_close = false;
            if (c == '*' || c == '~') {
                can_open = left;
                can_close = right;
            } else {
                // Determine preceding and succeeding characters to match the _ rules
                char prev = (i > 0) ? text[i - 1] : '\0';
                char next = (i + run_len < len) ? text[i + run_len] : '\0';
                can_open = left && (!right || is_punctuation_char(prev) || is_unicode_punct_before(text, len, i));
                can_close = right && (!left || is_punctuation_char(next) || is_unicode_punct_at(text, len, i + run_len));
            }
            
            inline_token_t tok = {TOK_DELIM, xstrdup_len(text + i, run_len), run_len, c, can_open, can_close};
            insert_token_at(&tokens, &count, &cap, count, tok);
            i += run_len;
            continue;
        }
        
        size_t start = i;
        i++;
        while (i < len && text[i] != '\\' && text[i] != '`' && text[i] != '[' && text[i] != '!' && text[i] != '*' && text[i] != '_' && text[i] != '~' && text[i] != '\n' && text[i] != '<' && text[i] != ':' && text[i] != '$' && text[i] != '&') {
            i++;
        }
        char *raw = xstrdup_len(text + start, i - start);
        char *escaped = xstrdup("");
        escape_html_append(&escaped, raw);
        free(raw);
        
        inline_token_t tok = {TOK_TEXT, escaped, strlen(escaped), 0, false, false};
        insert_token_at(&tokens, &count, &cap, count, tok);
    }
    
    *tokens_count_out = count;
    if (tokens_cap_out) {
        *tokens_cap_out = cap;
    }
    return tokens;
}

static char *process_emphasis_tokens(inline_token_t **tokens_ptr, size_t *count_ptr, size_t *cap_ptr) {
    inline_token_t *tokens = *tokens_ptr;
    size_t count = *count_ptr;
    size_t cap = *cap_ptr;

    size_t closer_idx = 0;
    while (closer_idx < count) {
        if (tokens[closer_idx].type == TOK_DELIM && tokens[closer_idx].can_close && tokens[closer_idx].len > 0) {
            size_t opener_idx = closer_idx;
            bool found_opener = false;
            while (opener_idx > 0) {
                opener_idx--;
                if (tokens[opener_idx].type == TOK_DELIM && tokens[opener_idx].can_open &&
                    tokens[opener_idx].delim_char == tokens[closer_idx].delim_char &&
                    tokens[opener_idx].len > 0) {
                    
                    bool odd_match = false;
                    if ((tokens[opener_idx].can_close || tokens[closer_idx].can_open) &&
                        (tokens[opener_idx].len % 3 != 0 || tokens[closer_idx].len % 3 != 0) &&
                        (tokens[opener_idx].len + tokens[closer_idx].len) % 3 == 0) {
                        odd_match = true;
                    }
                    if (odd_match) {
                        continue;
                    }
                    found_opener = true;
                    break;
                }
            }
            
            if (found_opener) {
                char dchar = tokens[opener_idx].delim_char;
                size_t num;
                const char *open_tag;
                const char *close_tag;
                if (dchar == '~') {
                    num = (tokens[opener_idx].len >= 2 && tokens[closer_idx].len >= 2) ? 2 : 1;
                    open_tag = "<del>";
                    close_tag = "</del>";
                } else {
                    num = (tokens[opener_idx].len >= 2 && tokens[closer_idx].len >= 2) ? 2 : 1;
                    open_tag = (num == 2) ? "<strong>" : "<em>";
                    close_tag = (num == 2) ? "</strong>" : "</em>";
                }
                
                // Deactivate all delimiters between opener and closer
                for (size_t k = opener_idx + 1; k < closer_idx; k++) {
                    if (tokens[k].type == TOK_DELIM) {
                        tokens[k].can_open = false;
                        tokens[k].can_close = false;
                    }
                }
                
                if (tokens[opener_idx].len > num) {
                    tokens[opener_idx].len -= num;
                    tokens[opener_idx].val[tokens[opener_idx].len] = '\0';
                    inline_token_t tag_tok = {TOK_HTML_TAG, xstrdup(open_tag), strlen(open_tag), 0, false, false};
                    insert_token_at(&tokens, &count, &cap, opener_idx + 1, tag_tok);
                    closer_idx++;
                } else {
                    free(tokens[opener_idx].val);
                    tokens[opener_idx].type = TOK_HTML_TAG;
                    tokens[opener_idx].val = xstrdup(open_tag);
                    tokens[opener_idx].len = strlen(open_tag);
                }
                
                if (tokens[closer_idx].len > num) {
                    tokens[closer_idx].len -= num;
                    tokens[closer_idx].val[tokens[closer_idx].len] = '\0';
                    inline_token_t tag_tok = {TOK_HTML_TAG, xstrdup(close_tag), strlen(close_tag), 0, false, false};
                    insert_token_at(&tokens, &count, &cap, closer_idx, tag_tok);
                    closer_idx++;
                } else {
                    free(tokens[closer_idx].val);
                    tokens[closer_idx].type = TOK_HTML_TAG;
                    tokens[closer_idx].val = xstrdup(close_tag);
                    tokens[closer_idx].len = strlen(close_tag);
                }
                continue;
            } else if (!tokens[closer_idx].can_open) {
                // If this closer cannot be an opener, it can never match any future opener
                tokens[closer_idx].can_close = false;
            }
        }
        closer_idx++;
    }
    
    *tokens_ptr = tokens;
    *count_ptr = count;
    *cap_ptr = cap;

    char *out = xstrdup("");
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == TOK_DELIM) {
            char *raw = xstrdup_len(tokens[i].val, tokens[i].len);
            append_str(&out, raw);
            free(raw);
        } else {
            append_str(&out, tokens[i].val);
        }
    }
    return out;
}

static char *render_inline_fragment(const char *text, const char *src, size_t base_offset, parser_state_t *st) {
    (void)src;
    (void)base_offset;
    if (!text) {
        return xstrdup("");
    }

    size_t count = 0;
    size_t cap = 0;
    inline_token_t *tokens = tokenize_inline(text, strlen(text), st, &count, &cap);
    if (!tokens) {
        return xstrdup("");
    }
    char *result = process_emphasis_tokens(&tokens, &count, &cap);
    free_inline_tokens(tokens, count);
    return result;
}

static char *expand_tabs(const char *line) {
    if (!line) return NULL;
    /* Worst case: every char is a tab expanding to 4 spaces */
    size_t src_len = strlen(line);
    size_t cap = src_len * 4 + 1;
    char *out = (char *)malloc(cap);
    if (!out) return xstrdup(line);
    size_t col = 0;  /* 0-based column */
    size_t j = 0;
    for (size_t i = 0; i < src_len; i++) {
        if (line[i] == '\t') {
            /* Expand to next multiple of 4 */
            size_t spaces = 4 - (col % 4);
            for (size_t s = 0; s < spaces; s++) {
                out[j++] = ' ';
            }
            col += spaces;
        } else {
            out[j++] = line[i];
            col++;
        }
    }
    out[j] = '\0';
    return out;
}

static void split_lines(const char *src, line_record_t **lines_out, size_t *count_out) {
    size_t cap = 16;
    line_record_t *lines = (line_record_t *)calloc(cap, sizeof(line_record_t));
    size_t count = 0;
    const char *start = src;
    const char *p = src;
    while (*p != '\0') {
        if (*p == '\n' || *p == '\r') {
            size_t len = (size_t)(p - start);
            if (count == cap) {
                cap *= 2;
                line_record_t *new_lines = (line_record_t *)realloc(lines, cap * sizeof(line_record_t));
                if (!new_lines) {
                    free(lines);
                    *lines_out = NULL;
                    *count_out = 0;
                    return;
                }
                lines = new_lines;
            }
            char *raw = xstrdup_len(start, len);
            lines[count].text = expand_tabs(raw);
            free(raw);
            lines[count].line = count + 1;
            lines[count].offset = (size_t)(start - src);
            count += 1;

            if (*p == '\r' && *(p + 1) == '\n') {
                p += 1;
            }
            start = p + 1;
        }
        p += 1;
    }

    size_t last_len = (size_t)(p - start);
    if (last_len > 0 || src[0] == '\0') {
        if (count == cap) {
            cap *= 2;
            line_record_t *new_lines = (line_record_t *)realloc(lines, cap * sizeof(line_record_t));
            if (!new_lines) {
                free(lines);
                *lines_out = NULL;
                *count_out = 0;
                return;
            }
            lines = new_lines;
        }
        char *raw = xstrdup_len(start, last_len);
        lines[count].text = expand_tabs(raw);
        free(raw);
        lines[count].line = count + 1;
        lines[count].offset = (size_t)(start - src);
        count += 1;
    }

    *lines_out = lines;
    *count_out = count;
}

static void free_lines(line_record_t *lines, size_t count) {
    if (!lines) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        free(lines[i].text);
    }
    free(lines);
}

static bool match_heading(const char *line, int *level_out, char **content_out) {
    if (!line) {
        return false;
    }
    const char *cursor = line;
    /* 1. Skip up to 3 leading spaces */
    int leading_spaces = 0;
    while (*cursor == ' ') {
        leading_spaces++;
        cursor++;
    }
    if (leading_spaces > 3) {
        return false;
    }
    
    /* 2. Count '#' characters (1 to 6) */
    int level = 0;
    while (*cursor == '#') {
        level++;
        cursor++;
    }
    if (level < 1 || level > 6) {
        return false;
    }
    
    /* 3. The '#' must be followed by space, tab, or end of line */
    if (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
        return false;
    }
    
    /* 4. Extract the heading content */
    const char *content_start = cursor;
    while (*content_start == ' ' || *content_start == '\t') {
        content_start++;
    }
    
    /* 5. Handle trailing '#' sequence */
    size_t len = strlen(content_start);
    /* Trim trailing spaces/tabs first */
    while (len > 0 && (content_start[len - 1] == ' ' || content_start[len - 1] == '\t')) {
        len--;
    }
    
    /* Now check for trailing '#' sequence */
    size_t hash_count = 0;
    while (len > hash_count && content_start[len - 1 - hash_count] == '#') {
        hash_count++;
    }
    
    if (hash_count > 0) {
        /* Check if the '#' sequence is preceded by a space/tab, or if it is the entire content */
        if (len == hash_count || content_start[len - 1 - hash_count] == ' ' || content_start[len - 1 - hash_count] == '\t') {
            /* Strip the trailing '#' characters and any spaces before them */
            len -= hash_count;
            while (len > 0 && (content_start[len - 1] == ' ' || content_start[len - 1] == '\t')) {
                len--;
            }
        }
    }
    
    if (level_out) {
        *level_out = level;
    }
    if (content_out) {
        *content_out = xstrdup_len(content_start, len);
    }
    return true;
}

/* match_list_item: returns true if 'line' starts a list item.
   marker_indent_out: number of spaces before the marker (0-3)
   content_indent_out: total columns from start to first content char
   ordered_out, bullet_char_out: list type info
   content_out: text after the marker+space (owned by caller) */
static bool match_list_item_full(const char *line, bool *ordered_out,
                                  int *marker_indent_out, int *content_indent_out,
                                  char *bullet_char_out, int *start_num_out,
                                  char **content_out) {
    if (!line || *line == '\0') return false;
    const char *cursor = line;
    int spaces = 0;
    while (*cursor == ' ' && spaces < 3) { cursor++; spaces++; }
    /* Also allow a single tab as up to 4 spaces */
    if (*cursor == '\t') { cursor++; spaces = (spaces + 4) & ~3; }
    if (spaces > 3) return false;
    if (marker_indent_out) *marker_indent_out = spaces;

    if (isdigit((unsigned char)*cursor)) {
        const char *num = cursor;
        int val = 0;
        size_t num_digits = 0;
        while (isdigit((unsigned char)*num)) {
            val = val * 10 + (*num - '0');
            num++;
            num_digits++;
        }
        if (num_digits >= 1 && num_digits <= 9 && (*num == '.' || *num == ')') && (num[1] == ' ' || num[1] == '\t' || num[1] == '\0')) {
            int marker_len = (int)(num - cursor) + 1; /* digits + punctuation */
            int total = spaces + marker_len;
            int extra = 0;
            const char *content = num + 1;
            if (*content == '\t') {
                int tab_stop = ((total + 1) + 3) & ~3;
                extra = tab_stop - (total + 1) + 1;
                content++;
            } else if (*content == ' ') {
                extra = 1;
                content++;
            } else if (*content == '\0') {
                extra = 1;
            }
            if (content_indent_out) *content_indent_out = total + extra;
            if (ordered_out) *ordered_out = true;
            if (bullet_char_out) *bullet_char_out = *num;
            if (start_num_out) *start_num_out = val;
            if (content_out) *content_out = xstrdup(content);
            return true;
        }
    }
    if (*cursor == '-' || *cursor == '*' || *cursor == '+') {
        char bc = *cursor;
        const char *rest = cursor + 1;
        if (*rest == ' ' || *rest == '\t' || *rest == '\0') {
            int total = spaces + 1;
            int extra = 0;
            const char *content = rest;
            if (*content == '\t') {
                int tab_stop = ((total + 1) + 3) & ~3;
                extra = tab_stop - (total + 1) + 1;
                content++;
            } else if (*content == ' ') {
                extra = 1;
                content++;
            } else if (*content == '\0') {
                extra = 1;
            }
            if (content_indent_out) *content_indent_out = total + extra;
            if (ordered_out) *ordered_out = false;
            if (bullet_char_out) *bullet_char_out = bc;
            if (start_num_out) *start_num_out = -1;
            if (content_out) *content_out = xstrdup(content);
            return true;
        }
    }
    return false;
}

static bool match_list_item(const char *line, bool *ordered_out, char **content_out) {
    return match_list_item_full(line, ordered_out, NULL, NULL, NULL, NULL, content_out);
}

static bool match_list_item_can_interrupt_paragraph(const char *line) {
    bool ordered = false;
    int start_num = 1;
    char *content = NULL;
    if (!match_list_item_full(line, &ordered, NULL, NULL, NULL, &start_num, &content)) {
        return false;
    }
    if (is_blank_line(content)) {
        free(content);
        return false;
    }
    free(content);
    if (ordered && start_num != 1) {
        return false;
    }
    return true;
}

static bool match_horizontal_rule(const char *line) {
    if (!line) return false;
    const char *p = line;
    int leading_spaces = 0;
    while (*p == ' ') {
        leading_spaces++;
        p++;
    }
    if (leading_spaces > 3) return false;
    if (*p == '\0') return false;
    char c = *p;
    if (c != '-' && c != '*' && c != '_') return false;
    int count = 0;
    while (*p) {
        if (*p == c) {
            count++;
        } else if (*p != ' ' && *p != '\t') {
            return false;
        }
        p++;
    }
    return count >= 3;
}

/* Returns 1 for '=' underline (h1), 2 for '-' underline (h2), 0 if not a setext underline */
static int match_setext_underline(const char *line) {
    if (!line) return 0;
    const char *p = line;
    /* Up to 3 leading spaces */
    int spaces = 0;
    while (*p == ' ') {
        spaces++;
        p++;
    }
    if (spaces > 3) return 0;
    if (*p != '=' && *p != '-') return 0;
    char c = *p;
    int count = 0;
    while (*p == c) {
        count++;
        p++;
    }
    if (count < 1) return 0;
    /* Only trailing spaces allowed */
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0') return 0;
    return (c == '=') ? 1 : 2;
}
static bool starts_blockquote(const char *line) {
    if (!line) {
        return false;
    }
    while (*line == ' ' || *line == '\t') {
        line += 1;
    }
    return *line == '>';
}

typedef struct {
    char fence_char;
    size_t fence_len;
    int indent;
    char *info;
} code_fence_info_t;

static bool parse_code_fence_open(const char *line, code_fence_info_t *out) {
    if (!line) return false;
    const char *p = line;
    int spaces = 0;
    while (*p == ' ') {
        spaces++;
        p++;
    }
    if (spaces > 3) return false;
    char c = *p;
    if (c != '`' && c != '~') return false;
    size_t len = 0;
    while (*p == c) {
        len++;
        p++;
    }
    if (len < 3) return false;
    if (c == '`' && strchr(p, '`') != NULL) return false;

    if (out) {
        out->fence_char = c;
        out->fence_len = len;
        out->indent = spaces;
        /* Extract info string: first word after leading spaces */
        while (*p == ' ' || *p == '\t') p++;
        const char *info_start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        size_t info_len = p - info_start;
        out->info = xstrdup_len(info_start, info_len);
    }
    return true;
}

static bool is_code_fence_close(const char *line, char fence_char, size_t fence_len) {
    if (!line) return false;
    const char *p = line;
    int spaces = 0;
    while (*p == ' ') {
        spaces++;
        p++;
    }
    if (spaces > 3) return false;
    if (*p != fence_char) return false;
    size_t len = 0;
    while (*p == fence_char) {
        len++;
        p++;
    }
    if (len < fence_len) return false;
    while (*p == ' ' || *p == '\t') p++;
    return (*p == '\0' || *p == '\r' || *p == '\n');
}

static bool starts_code_fence(const char *line) {
    return parse_code_fence_open(line, NULL);
}
static void free_string_array(char **arr, size_t count) {
    if (!arr) return;
    for (size_t i = 0; i < count; ++i) {
        free(arr[i]);
    }
    free(arr);
}

static bool split_table_row(const char *line, char ***cells_out, size_t *count_out) {
    if (!line) return false;
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return false;

    if (strchr(p, '|') == NULL) return false;

    if (*p == '|') p++;

    size_t cap = 8;
    char **cells = (char **)malloc(cap * sizeof(char *));
    if (!cells) return false;
    size_t count = 0;

    const char *start = p;
    while (*p != '\0') {
        if (*p == '\\' && *(p + 1) != '\0') {
            p += 2;
            continue;
        }
        if (*p == '|') {
            size_t cell_len = (size_t)(p - start);
            char *cell = xstrdup_len(start, cell_len);
            trim_in_place(cell);
            if (count == cap) {
                cap *= 2;
                char **new_c = (char **)realloc(cells, cap * sizeof(char *));
                if (!new_c) { free_string_array(cells, count); free(cell); return false; }
                cells = new_c;
            }
            cells[count++] = cell;
            start = p + 1;
        }
        p++;
    }

    size_t last_len = (size_t)(p - start);
    char *last_cell = xstrdup_len(start, last_len);
    trim_in_place(last_cell);

    if (last_cell[0] == '\0' && count > 0) {
        free(last_cell);
    } else {
        if (count == cap) {
            cap *= 2;
            char **new_c = (char **)realloc(cells, cap * sizeof(char *));
            if (!new_c) { free_string_array(cells, count); free(last_cell); return false; }
            cells = new_c;
        }
        cells[count++] = last_cell;
    }

    if (count == 0) {
        free(cells);
        return false;
    }

    *cells_out = cells;
    *count_out = count;
    return true;
}

static bool match_table_delimiter(const char *line, size_t *col_count_out) {
    char **cells = NULL;
    size_t count = 0;
    if (!split_table_row(line, &cells, &count)) return false;
    if (count == 0) return false;

    for (size_t i = 0; i < count; ++i) {
        const char *c = cells[i];
        if (*c == '\0') {
            free_string_array(cells, count);
            return false;
        }
        int dashes = 0;
        while (*c) {
            if (*c == '-') {
                dashes++;
            } else if (*c != ':' && *c != ' ' && *c != '\t') {
                free_string_array(cells, count);
                return false;
            }
            c++;
        }
        if (dashes < 1) {
            free_string_array(cells, count);
            return false;
        }
    }

    if (col_count_out) *col_count_out = count;
    free_string_array(cells, count);
    return true;
}

static char *render_inline_and_check(const char *text, const char *src, size_t base_offset, parser_state_t *st) {
    char *result = render_inline_fragment(text, src, base_offset, st);
    if (st->has_error) {
        free(result);
        return NULL;
    }
    return result;
}

static char *trim_paragraph_line(const char *line) {
    if (!line) return NULL;
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    return xstrdup(line);
}

/* Forward declaration: parse a sub-document body string using the SAME parser state
   (so link refs, error state are shared). The body_src must be a NUL-terminated
   string that this function will NOT free. */
static char *parse_block_html_with_state(const char *body_src, parser_state_t *st);

static char *build_html_for_document(const char *src, size_t src_len, parser_state_t *st, bool is_top_level) {
    (void)src_len;
    line_record_t *lines = NULL;
    size_t count = 0;
    split_lines(src, &lines, &count);
    if (!lines) {
        parser_set_error(st, src, 0, "out of memory while parsing document");
        return NULL;
    }

    parse_all_link_refs_from_lines(lines, count, st);
    parse_all_footnotes_from_lines(lines, count, st);

    char *html = xstrdup("");
    size_t i = 0;
    while (i < count) {
        char *line = lines[i].text;
        if (is_blank_line(line)) {
            i += 1;
            continue;
        }

        /* Frontmatter block at document start: must contain key: value pairs */
        if (i == 0 && strcmp(line, "---") == 0) {
            size_t end_fm = SIZE_MAX;
            bool valid_yaml = true;
            for (size_t k = 1; k < count; k++) {
                if (strcmp(lines[k].text, "---") == 0 || strcmp(lines[k].text, "...") == 0) {
                    end_fm = k;
                    break;
                }
            }
            if (end_fm != SIZE_MAX && end_fm > 1) {
                for (size_t f = 1; f < end_fm; f++) {
                    const char *t = lines[f].text;
                    while (*t == ' ' || *t == '\t') t++;
                    if (*t != '\0' && *t != '#' && *t != '-' && strchr(t, ':') == NULL) {
                        valid_yaml = false;
                        break;
                    }
                }
                if (valid_yaml) {
                    append_str(&html, "<div class=\"frontmatter\">\n");
                    for (size_t f = 1; f < end_fm; f++) {
                        char *escaped = xstrdup("");
                        escape_html_append(&escaped, lines[f].text);
                        append_str(&html, "<div class=\"frontmatter-line\">");
                        append_str(&html, escaped);
                        append_str(&html, "</div>\n");
                        free(escaped);
                    }
                    append_str(&html, "</div>\n");
                    i = end_fm + 1;
                    continue;
                }
            }
        }

        /* Skip footnote definition lines in body */
        if (is_footnote_def_line(line)) {
            i += 1;
            continue;
        }

        /* Summary tag with inline markdown */
        if (strncmp(line, "<summary>", 9) == 0) {
            const char *end_sum = strstr(line, "</summary>");
            if (end_sum) {
                char *inner = xstrdup_len(line + 9, end_sum - (line + 9));
                char *formatted = render_inline_fragment(inner, src, lines[i].offset + 9, st);
                append_str(&html, "<summary>");
                append_str(&html, formatted ? formatted : inner);
                append_str(&html, "</summary>\n");
                free(formatted);
                free(inner);
                i += 1;
                continue;
            }
        }

        /* CommonMark HTML Blocks (Types 1 - 7) */
        int html_type = scan_html_block_type(line);
        if (html_type > 0) {
            while (i < count) {
                char *cur = lines[i].text;
                append_str(&html, cur);
                append_str(&html, "\n");
                i++;
                if (html_type == 1) {
                    if (ci_strcasestr(cur, "</script>") || ci_strcasestr(cur, "</pre>") || ci_strcasestr(cur, "</style>")) {
                        break;
                    }
                } else if (html_type == 2) {
                    if (strstr(cur, "-->")) {
                        break;
                    }
                } else if (html_type == 3) {
                    if (strstr(cur, "?>")) {
                        break;
                    }
                } else if (html_type == 4) {
                    if (strchr(cur, '>')) {
                        break;
                    }
                } else if (html_type == 5) {
                    if (strstr(cur, "]]>")) {
                        break;
                    }
                } else if (html_type == 6 || html_type == 7) {
                    if (i < count && is_blank_line(lines[i].text)) {
                        break;
                    }
                }
            }
            continue;
        }

        /* Math Block: $$ ... $$ */
        if (strncmp(line, "$$", 2) == 0) {
            char *math_content = xstrdup("");
            const char *after = line + 2;
            const char *close_p = strstr(after, "$$");
            if (close_p && close_p > after) {
                char *inner = xstrdup_len(after, close_p - after);
                append_str(&math_content, inner);
                free(inner);
                i += 1;
            } else {
                if (*after != '\0') {
                    append_str(&math_content, after);
                    append_str(&math_content, "\n");
                }
                i += 1;
                while (i < count) {
                    char *cur = lines[i].text;
                    const char *end_m = strstr(cur, "$$");
                    if (end_m) {
                        char *inner = xstrdup_len(cur, end_m - cur);
                        append_str(&math_content, inner);
                        free(inner);
                        i += 1;
                        break;
                    }
                    append_str(&math_content, cur);
                    append_str(&math_content, "\n");
                    i += 1;
                }
            }
            char *escaped_math = xstrdup("");
            escape_html_append(&escaped_math, math_content);
            append_str(&html, "<div class=\"math-block\">$$\n");
            append_str(&html, escaped_math);
            if (escaped_math[0] != '\0' && escaped_math[strlen(escaped_math) - 1] != '\n') {
                append_str(&html, "\n");
            }
            append_str(&html, "$$</div>\n");
            free(escaped_math);
            free(math_content);
            continue;
        }

        /* Definition List: term followed by : definition */
        if (i + 1 < count) {
            const char *next_l = lines[i + 1].text;
            while (*next_l == ' ' || *next_l == '\t') next_l++;
            if (next_l[0] == ':' && (next_l[1] == ' ' || next_l[1] == '\t')) {
                append_str(&html, "<dl>\n");
                while (i < count) {
                    char *term = lines[i].text;
                    if (is_blank_line(term)) break;
                    if (i + 1 >= count) break;
                    char *def_line = lines[i + 1].text;
                    while (*def_line == ' ' || *def_line == '\t') def_line++;
                    if (def_line[0] != ':' || (def_line[1] != ' ' && def_line[1] != '\t')) {
                        break;
                    }
                    def_line += 2;
                    while (*def_line == ' ' || *def_line == '\t') def_line++;

                    char *term_html = render_inline_fragment(term, src, lines[i].offset, st);
                    char *def_html = render_inline_fragment(def_line, src, lines[i + 1].offset, st);

                    append_str(&html, "<dt>");
                    append_str(&html, term_html ? term_html : "");
                    append_str(&html, "</dt>\n");

                    append_str(&html, "<dd>");
                    append_str(&html, def_html ? def_html : "");
                    append_str(&html, "</dd>\n");

                    free(term_html);
                    free(def_html);
                    i += 2;

                    size_t next_i = i;
                    while (next_i < count && is_blank_line(lines[next_i].text)) {
                        next_i++;
                    }
                    if (next_i < count && next_i + 1 < count) {
                        const char *peek_def = lines[next_i + 1].text;
                        while (*peek_def == ' ' || *peek_def == '\t') peek_def++;
                        if (peek_def[0] == ':' && (peek_def[1] == ' ' || peek_def[1] == '\t')) {
                            i = next_i;
                            continue;
                        }
                    }
                    break;
                }
                append_str(&html, "</dl>\n");
                continue;
            }
        }

        /* Table Block */
        size_t col_count = 0;
        if (i + 1 < count && match_table_delimiter(lines[i + 1].text, &col_count)) {
            char **headers = NULL;
            size_t header_count = 0;
            if (split_table_row(line, &headers, &header_count)) {
                append_str(&html, "<table>\n<thead>\n<tr>\n");
                for (size_t h = 0; h < header_count; ++h) {
                    char *formatted = render_inline_and_check(headers[h], src, lines[i].offset, st);
                    append_str(&html, "<th>");
                    append_str(&html, formatted ? formatted : "");
                    append_str(&html, "</th>\n");
                    free(formatted);
                }
                append_str(&html, "</tr>\n</thead>\n<tbody>\n");
                free_string_array(headers, header_count);

                i += 2;

                while (i < count) {
                    char *cur_line = lines[i].text;
                    if (is_blank_line(cur_line)) break;
                    if (match_horizontal_rule(cur_line) || match_heading(cur_line, NULL, NULL) ||
                        starts_code_fence(cur_line) || starts_blockquote(cur_line)) {
                        break;
                    }

                    char **row_cells = NULL;
                    size_t row_cell_count = 0;
                    if (!split_table_row(cur_line, &row_cells, &row_cell_count)) {
                        break;
                    }

                    append_str(&html, "<tr>\n");
                    for (size_t c = 0; c < row_cell_count; ++c) {
                        char *formatted = render_inline_and_check(row_cells[c], src, lines[i].offset, st);
                        append_str(&html, "<td>");
                        append_str(&html, formatted ? formatted : "");
                        append_str(&html, "</td>\n");
                        free(formatted);
                    }
                    append_str(&html, "</tr>\n");
                    free_string_array(row_cells, row_cell_count);
                    i += 1;
                }
                append_str(&html, "</tbody>\n</table>\n");
                continue;
            }
        }

        if (match_horizontal_rule(line)) {
            append_str(&html, "<hr />\n");
            i += 1;
            continue;
        }

        int heading_level = 0;
        char *heading_content = NULL;
        if (match_heading(line, &heading_level, &heading_content)) {
            char *formatted = render_inline_and_check(heading_content, src, lines[i].offset + (size_t)(strchr(line, '#') - line) + heading_level + 1, st);
            if (!formatted) {
                free(heading_content);
                free_lines(lines, count);
                free(html);
                return NULL;
            }
            append_formatted(&html, "<h%d>", heading_level);
            append_str(&html, formatted);
            append_formatted(&html, "</h%d>\n", heading_level);
            free(formatted);
            free(heading_content);
            i += 1;
            continue;
        }

        /* Indented code block: 4+ leading spaces */
        if (strncmp(line, "    ", 4) == 0 && !match_list_item(line, NULL, NULL)) {
            char *code = xstrdup("");
            while (i < count) {
                char *cur = lines[i].text;
                if (strncmp(cur, "    ", 4) == 0) {
                    /* Strip 4 leading spaces */
                    char *escaped_line = xstrdup("");
                    escape_html_append(&escaped_line, cur + 4);
                    append_str(&code, escaped_line);
                    append_str(&code, "\n");
                    free(escaped_line);
                    i += 1;
                } else if (is_blank_line(cur)) {
                    /* Blank line continues code block if followed by indented line */
                    if (i + 1 < count && strncmp(lines[i + 1].text, "    ", 4) == 0) {
                        append_str(&code, "\n");
                        i += 1;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
            append_str(&html, "<pre><code>");
            append_str(&html, code);
            append_str(&html, "</code></pre>\n");
            free(code);
            continue;
        }

        code_fence_info_t fence_info = {0};
        if (parse_code_fence_open(line, &fence_info)) {
            char *code = xstrdup("");
            i += 1;
            while (i < count) {
                if (is_code_fence_close(lines[i].text, fence_info.fence_char, fence_info.fence_len)) {
                    break;
                }
                const char *content_line = lines[i].text;
                /* Strip up to fence_info.indent leading spaces */
                int stripped_spaces = 0;
                while (stripped_spaces < fence_info.indent && *content_line == ' ') {
                    stripped_spaces++;
                    content_line++;
                }
                char *segment = join_strings(code, content_line);
                free(code);
                code = segment;
                char *with_new = join_strings(code, "\n");
                free(code);
                code = with_new;
                i += 1;
            }
            if (i >= count) {
                parser_set_error(st, src, lines[i - 1].offset, "unterminated code fence");
                free(code);
                free(fence_info.info);
                free_lines(lines, count);
                free(html);
                return NULL;
            }
            char *escaped = xstrdup("");
            escape_html_append(&escaped, code);
            append_str(&html, "<pre><code");
            if (fence_info.info && fence_info.info[0] != '\0') {
                if (strncmp(fence_info.info, "language-", 9) == 0) {
                    append_formatted(&html, " class=\"%s\"", fence_info.info);
                } else {
                    append_formatted(&html, " class=\"language-%s\"", fence_info.info);
                }
            }
            append_str(&html, ">");
            append_str(&html, escaped);
            append_str(&html, "</code></pre>\n");
            free(escaped);
            free(code);
            free(fence_info.info);
            i += 1;
            continue;
        }

        bool ordered = false;
        int marker_indent = 0, content_indent = 0;
        char bullet_char = 0;
        if (match_list_item_full(line, &ordered, &marker_indent, &content_indent,
                                  &bullet_char, NULL, NULL)) {
            /* --- CommonMark list collection --- */
            bool is_loose = false;

            /* Each item is represented as a heap-string: its body lines joined by '\n' */
            size_t items_cap = 8;
            char **items = malloc(items_cap * sizeof(char *));
            bool *item_was_followed_by_blank = calloc(items_cap, sizeof(bool));
            size_t items_count = 0;
            int list_start_num = 1;
            bool list_ordered = ordered;

            /* First item's start number for ordered lists */
            match_list_item_full(line, NULL, NULL, NULL, NULL, &list_start_num, NULL);
            if (list_start_num < 0) list_start_num = 1;

            bool prev_blank = false;
            while (i < count) {
                char *cur_line = lines[i].text;

                /* Thematic break always terminates a list */
                if (match_horizontal_rule(cur_line)) break;

                bool cur_ordered = false;
                int cur_mi = 0, cur_ci = 0;
                char cur_bc = 0;
                bool cur_is_item = match_list_item_full(cur_line, &cur_ordered,
                                                         &cur_mi, &cur_ci, &cur_bc, NULL, NULL);

                /* New list item with same type and same bullet/delimiter */
                if (cur_is_item && cur_ordered == list_ordered) {
                    if (prev_blank) {
                        size_t bks = 0;
                        size_t bi = i;
                        while (bi > 0 && is_blank_line(lines[bi - 1].text)) {
                            bks++;
                            bi--;
                        }
                        if (bks >= 2) {
                            break;
                        }
                        if (items_count > 0) {
                            item_was_followed_by_blank[items_count - 1] = true;
                            is_loose = true;
                        }
                    }
                    prev_blank = false;

                    /* Collect this item's body */
                    char *item_body = xstrdup("");
                    /* First line: content after marker */
                    char *first_content = NULL;
                    match_list_item_full(cur_line, NULL, NULL, NULL, NULL, NULL, &first_content);
                    if (first_content) {
                        append_str(&item_body, first_content);
                        free(first_content);
                    }
                    i++;

                    /* Continuation lines */
                    while (i < count) {
                        char *next = lines[i].text;
                        if (is_blank_line(next)) {
                            /* Blank line may continue item if next non-blank is indented */
                            size_t j = i;
                            size_t blank_count = 0;
                            while (j < count && is_blank_line(lines[j].text)) {
                                blank_count++;
                                j++;
                            }
                            if (blank_count >= 2) {
                                prev_blank = true;
                                break;
                            }
                            if (j < count) {
                                /* Check if next non-blank continues item */
                                const char *nb = lines[j].text;
                                int nb_spaces = 0;
                                while (*nb == ' ') { nb++; nb_spaces++; }
                                bool nb_is_new_item = false;
                                {
                                    bool tob = false; char tbc = 0;
                                    nb_is_new_item = match_list_item_full(lines[j].text, &tob,
                                                                           NULL, NULL, &tbc, NULL, NULL)
                                                     && tob == list_ordered;
                                }
                                if (nb_spaces >= content_indent || nb_is_new_item) {
                                    /* Blank line(s) within item body make it loose */
                                    is_loose = true;
                                    append_str(&item_body, "\n");
                                    i = j;
                                    continue;
                                }
                            }
                            /* Blank line followed by non-continuing content: item ends */
                            prev_blank = true;
                            break;
                        }
                        /* Check if this is a new list item or block marker */
                        bool ni_ordered = false; char ni_bc = 0;
                        int ni_mi = 0;
                        bool ni_is = match_list_item_full(next, &ni_ordered, &ni_mi, NULL, &ni_bc, NULL, NULL);
                        if (ni_is && ni_ordered == list_ordered && ni_mi < content_indent) break;
                        if (match_heading(next, NULL, NULL) || match_horizontal_rule(next) ||
                            starts_code_fence(next) || starts_blockquote(next)) {
                            break;
                        }

                        /* Continuation: must be indented >= content_indent OR be a lazy line */
                        int next_spaces = 0;
                        const char *p = next;
                        while (*p == ' ') { p++; next_spaces++; }
                        if (next_spaces < content_indent && !is_blank_line(next)) {
                            /* Lazy continuation: only valid if no blank line preceded */
                            if (prev_blank) break;
                        }

                        /* Strip content_indent leading spaces from continuation line */
                        const char *stripped = next;
                        int stripped_count = 0;
                        while (*stripped == ' ' && stripped_count < content_indent) {
                            stripped++; stripped_count++;
                        }
                        append_str(&item_body, "\n");
                        append_str(&item_body, stripped);
                        i++;
                    }

                    /* Store item */
                    if (items_count == items_cap) {
                        items_cap *= 2;
                        items = realloc(items, items_cap * sizeof(char *));
                        item_was_followed_by_blank = realloc(item_was_followed_by_blank,
                                                              items_cap * sizeof(bool));
                        memset(item_was_followed_by_blank + items_count, 0,
                               (items_cap - items_count) * sizeof(bool));
                    }
                    items[items_count++] = item_body;
                    /* Update content_indent for subsequent items (use first item's) */
                    content_indent = cur_ci;
                    continue;
                }

                if (is_blank_line(cur_line)) {
                    prev_blank = true;
                    i++;
                    continue;
                }

                /* Any other non-blank non-item line that isn't indented: list ends */
                {
                    int ns = 0;
                    const char *tp = cur_line;
                    while (*tp == ' ') { tp++; ns++; }
                    if (ns < content_indent) break;
                    /* Indented continuation of last item */
                    if (items_count > 0) {
                        const char *stripped = cur_line;
                        int sc = 0;
                        while (*stripped == ' ' && sc < content_indent) { stripped++; sc++; }
                        append_str(&items[items_count - 1], "\n");
                        append_str(&items[items_count - 1], stripped);
                        i++;
                        continue;
                    }
                }
                break;
            }

            /* Determine final loose: only if an item has internal paragraphs */

            /* Emit HTML */
            if (list_ordered) {
                if (list_start_num == 1) {
                    append_str(&html, "<ol>\n");
                } else {
                    append_formatted(&html, "<ol start=\"%d\">\n", list_start_num);
                }
            } else {
                append_str(&html, "<ul>\n");
            }

            for (size_t k = 0; k < items_count; k++) {
                char *item_html = parse_block_html_with_state(items[k], st);
                free(items[k]);
                if (!item_html) {
                    /* cleanup remaining */
                    for (size_t m = k + 1; m < items_count; m++) free(items[m]);
                    free(items); free(item_was_followed_by_blank);
                    free_lines(lines, count);
                    free(html);
                    return NULL;
                }
                if (item_html[0] == '\0') {
                    append_str(&html, "<li>");
                } else if (is_loose) {
                    append_str(&html, "<li>\n");
                    append_str(&html, item_html);
                } else {
                    /* Tight: strip wrapping <p>...</p> from the leading paragraph */
                    char *ih = item_html;
                    size_t ihlen = strlen(ih);
                    while (ihlen > 0 && (ih[ihlen-1] == '\n' || ih[ihlen-1] == '\r')) ihlen--;
                    ih[ihlen] = '\0';
                    if (strncmp(ih, "<p>", 3) == 0) {
                        const char *close_p = strstr(ih, "</p>");
                        if (close_p) {
                            size_t p_content_len = close_p - (ih + 3);
                            char *p_content = xstrdup_len(ih + 3, p_content_len);
                            const char *rest = close_p + 4;
                            append_str(&html, "<li>");
                            append_str(&html, p_content);
                            if (*rest != '\0') {
                                append_str(&html, rest);
                            }
                            free(p_content);
                        } else {
                            append_str(&html, "<li>");
                            append_str(&html, ih);
                        }
                    } else {
                        if (ih[0] == '<' && (strncmp(ih, "<pre>", 5) == 0 || strncmp(ih, "<blockquote>", 12) == 0 ||
                                             strncmp(ih, "<ul>", 4) == 0 || strncmp(ih, "<ol>", 4) == 0 || strncmp(ih, "<hr", 3) == 0)) {
                            append_str(&html, "<li>\n");
                        } else {
                            append_str(&html, "<li>");
                        }
                        append_str(&html, ih);
                    }
                }
                free(item_html);
                append_str(&html, "</li>\n");
            }

            free(items);
            free(item_was_followed_by_blank);
            append_str(&html, list_ordered ? "</ol>\n" : "</ul>\n");
            continue;
        }

        if (starts_blockquote(line)) {
            /* Collect blockquote lines, strip '>' prefix, join, recurse */
            char *quote_body = xstrdup("");
            bool first_bq_line = true;
            while (i < count) {
                char *raw = lines[i].text;
                /* Blockquote continuation: either starts with '>' (with optional spaces)
                   or is a non-blank lazy continuation line */
                bool is_bq_marker = starts_blockquote(raw);
                bool is_blank = is_blank_line(raw);
                if (!is_bq_marker && !is_blank) {
                    /* Lazy continuation: non-blank line without > can continue BQ content */
                    if (match_heading(raw, NULL, NULL) || match_horizontal_rule(raw) ||
                        starts_code_fence(raw) || match_list_item(raw, NULL, NULL)) {
                        break;
                    }
                    if (!first_bq_line) append_str(&quote_body, "\n");
                    append_str(&quote_body, raw);
                    first_bq_line = false;
                    i++;
                    continue;
                }
                if (is_blank) {
                    break;
                }
                /* Strip leading spaces and '>' */
                while (*raw == ' ' || *raw == '\t') raw++;
                if (*raw == '>') {
                    raw++;
                    if (*raw == ' ') raw++;
                }
                if (!first_bq_line) append_str(&quote_body, "\n");
                append_str(&quote_body, raw);
                first_bq_line = false;
                i++;
            }
            /* Check for GitHub-Style Alerts: [!NOTE], [!TIP], [!IMPORTANT], [!WARNING], [!CAUTION] */
            const char *alert_type = NULL;
            const char *alert_title = NULL;
            const char *alert_icon = NULL;
            const char *body_p = quote_body;
            while (*body_p == ' ' || *body_p == '\t') body_p++;

            if (ci_strncasecmp(body_p, "[!NOTE]", 7) == 0) {
                alert_type = "note"; alert_title = "Note"; alert_icon = "ℹ";
                body_p += 7;
            } else if (ci_strncasecmp(body_p, "[!TIP]", 6) == 0) {
                alert_type = "tip"; alert_title = "Tip"; alert_icon = "💡";
                body_p += 6;
            } else if (ci_strncasecmp(body_p, "[!IMPORTANT]", 12) == 0) {
                alert_type = "important"; alert_title = "Important"; alert_icon = "💬";
                body_p += 12;
            } else if (ci_strncasecmp(body_p, "[!WARNING]", 10) == 0) {
                alert_type = "warning"; alert_title = "Warning"; alert_icon = "⚠️";
                body_p += 10;
            } else if (ci_strncasecmp(body_p, "[!CAUTION]", 10) == 0) {
                alert_type = "caution"; alert_title = "Caution"; alert_icon = "🛑";
                body_p += 10;
            }

            if (alert_type) {
                while (*body_p == ' ' || *body_p == '\t' || *body_p == '\r') body_p++;
                if (*body_p == '\n') body_p++;
                while (*body_p == ' ' || *body_p == '\t' || *body_p == '\r' || *body_p == '\n') body_p++;

                char *inner_html = parse_block_html_with_state(body_p, st);
                free(quote_body);
                if (!inner_html) {
                    free_lines(lines, count);
                    free(html);
                    return NULL;
                }
                append_formatted(&html, "<div class=\"markdown-alert markdown-alert-%s\">\n", alert_type);
                append_formatted(&html, "<p class=\"markdown-alert-title\"><span class=\"alert-icon\">%s</span> %s</p>\n", alert_icon, alert_title);
                append_str(&html, inner_html);
                append_str(&html, "</div>\n");
                free(inner_html);
                continue;
            }

            /* Recursively parse standard blockquote body */
            char *inner_html = parse_block_html_with_state(quote_body, st);
            free(quote_body);
            if (!inner_html) {
                free_lines(lines, count);
                free(html);
                return NULL;
            }
            append_str(&html, "<blockquote>\n");
            append_str(&html, inner_html);
            free(inner_html);
            append_str(&html, "</blockquote>\n");
            continue;
        }

        char *paragraph_text = xstrdup("");
        size_t paragraph_start = i;
        int setext_level = 0;
        while (i < count) {
            char *current_line = lines[i].text;
            if (is_blank_line(current_line)) {
                break;
            }
            if (i + 1 < count && match_table_delimiter(lines[i + 1].text, NULL)) {
                break;
            }
            /* Check for setext underline on the NEXT line */
            if (i + 1 < count && paragraph_text[0] != '\0') {
                int sl = match_setext_underline(lines[i + 1].text);
                if (sl > 0) {
                    /* Include current line in paragraph_text, then stop */
                    char *line_text = trim_paragraph_line(current_line);
                    if (paragraph_text[0] != '\0') {
                        char *combined = join_strings(paragraph_text, "\n");
                        free(paragraph_text);
                        paragraph_text = combined;
                    }
                    char *temp = join_strings(paragraph_text, line_text);
                    free(paragraph_text);
                    paragraph_text = temp;
                    free(line_text);
                    i += 1; /* consumed current line */
                    setext_level = sl;
                    i += 1; /* skip underline */
                    break;
                }
            }
            /* Also check if THIS is the first line and next is setext underline */
            if (paragraph_text[0] == '\0' && i + 1 < count) {
                int sl = match_setext_underline(lines[i + 1].text);
                if (sl > 0) {
                    char *line_text = trim_paragraph_line(current_line);
                    char *temp = join_strings(paragraph_text, line_text);
                    free(paragraph_text);
                    paragraph_text = temp;
                    free(line_text);
                    i += 1; /* consumed current line */
                    setext_level = sl;
                    i += 1; /* skip underline */
                    break;
                }
            }
            if (match_horizontal_rule(current_line) || match_heading(current_line, &heading_level, NULL) ||
                starts_code_fence(current_line) || match_list_item_can_interrupt_paragraph(current_line) || starts_blockquote(current_line) ||
                is_html_block_line(current_line) || is_footnote_def_line(current_line) || strncmp(current_line, "$$", 2) == 0) {
                break;
            }
            char *line_text = trim_paragraph_line(current_line);
            if (paragraph_text[0] != '\0') {
                char *combined = join_strings(paragraph_text, "\n");
                free(paragraph_text);
                paragraph_text = combined;
            }
            char *temp = join_strings(paragraph_text, line_text);
            free(paragraph_text);
            paragraph_text = temp;
            free(line_text);
            i += 1;
        }

        trim_in_place(paragraph_text);
        if (paragraph_text[0] == '\0') {
            free(paragraph_text);
            continue;
        }

        char *formatted = render_inline_and_check(paragraph_text, src, lines[paragraph_start].offset, st);
        free(paragraph_text);
        if (!formatted) {
            free_lines(lines, count);
            free(html);
            return NULL;
        }
        trim_in_place(formatted);
        if (formatted[0] != '\0') {
            if (setext_level > 0) {
                append_formatted(&html, "<h%d>", setext_level);
                append_str(&html, formatted);
                append_formatted(&html, "</h%d>\n", setext_level);
            } else {
                append_str(&html, "<p>");
                append_str(&html, formatted);
                append_str(&html, "</p>\n");
            }
        }
        free(formatted);
    }

    if (is_top_level && st->footnotes_count > 0) {
        bool has_used = false;
        for (size_t f = 0; f < st->footnotes_count; f++) {
            if (st->footnotes[f].used) { has_used = true; break; }
        }
        if (has_used) {
            append_str(&html, "<section class=\"footnotes\">\n<hr />\n<ol class=\"footnotes-list\">\n");
            for (size_t f = 0; f < st->footnotes_count; f++) {
                if (st->footnotes[f].used) {
                    char *formatted_content = render_inline_fragment(st->footnotes[f].content, src, 0, st);
                    append_formatted(&html, "<li id=\"fn-%s\"><p>%s <a href=\"#fnref-%s\" class=\"footnote-backref\">&#x21a9;&#xfe0e;</a></p></li>\n",
                                     st->footnotes[f].label, formatted_content ? formatted_content : "", st->footnotes[f].label);
                    free(formatted_content);
                }
            }
            append_str(&html, "</ol>\n</section>\n");
        }
    }

    free_lines(lines, count);
    if (st->has_error) {
        free(html);
        return NULL;
    }
    return html;
}

/* Parses a sub-document body string using the given parser_state_t (link refs, etc.
   are shared). Returns a new heap-allocated HTML string. */
static char *parse_block_html_with_state(const char *body_src, parser_state_t *st) {
    if (!body_src) return xstrdup("");
    /* Temporarily swap the src so offsets still work within error reporting */
    const char *saved_src = st->src;
    size_t saved_len = st->src_len;
    /* We cannot change src (offsets would be wrong) but error reporting is best-effort
       for sub-documents. Just call build_html_for_document with body as standalone src. */
    (void)saved_src; (void)saved_len;
    size_t blen = strlen(body_src);
    char *result = build_html_for_document(body_src, blen, st, false);
    return result ? result : xstrdup("");
}

md_parse_result_t md_to_html(const char *md_src, size_t md_len) {
    md_parse_result_t result;
    memset(&result, 0, sizeof(result));

    if (!md_src || md_len == 0) {
        result.success = true;
        result.html = xstrdup("");
        return result;
    }

    char *source = xstrdup_len(md_src, md_len);
    parser_state_t st = {0};
    st.src = source;
    st.src_len = md_len;

    
    st.html = build_html_for_document(st.src, st.src_len, &st, true);

    // Cleanup references and footnotes table helper macro/code
#define CLEANUP_REFS \
    for (size_t i = 0; i < st.refs_count; i++) { \
        free(st.refs[i].label_normalized); \
        free(st.refs[i].url); \
        free(st.refs[i].title); \
    } \
    free(st.refs); \
    for (size_t i = 0; i < st.footnotes_count; i++) { \
        free(st.footnotes[i].label); \
        free(st.footnotes[i].label_normalized); \
        free(st.footnotes[i].content); \
    } \
    free(st.footnotes);

    if (st.has_error) {
        result.success = false;
        result.error_msg = st.error_msg ? st.error_msg : xstrdup("parse error");
        result.line = st.error_line;
        result.col = st.error_col;
        result.caret_snippet = error_report_create(source, result.line, result.col, result.error_msg);
        st.error_msg = NULL;
        free(st.html);
        CLEANUP_REFS
        free(source);
        return result;
    }

    if (st.error_msg) {
        free(st.error_msg);
        st.error_msg = NULL;
    }
    CLEANUP_REFS

    if (!st.html) {
        result.success = true;
        result.html = xstrdup("");
        free(source);
        return result;
    }

    result.success = true;
    result.html = st.html;
    free(source);
    return result;
}

void md_parse_result_free(md_parse_result_t *res) {
    if (!res) {
        return;
    }
    free(res->html);
    free(res->error_msg);
    free(res->caret_snippet);
    res->html = NULL;
    res->error_msg = NULL;
    res->caret_snippet = NULL;
}
