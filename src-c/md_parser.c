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
    const char *src;
    size_t src_len;
    char *html;
    bool has_error;
    char *error_msg;
    size_t error_line;
    size_t error_col;
    link_ref_t *refs;
    size_t refs_count;
} parser_state_t;

typedef struct {
    char *text;
    size_t line;
    size_t offset;
} line_record_t;

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
        if (j == 0) {
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
        } else if (isalnum(c) || strchr("-_.~:/?#[]@!$&'()*+,;=", c) != NULL) {
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
                    allocated_title = xstrdup_len(t_start, p - t_start);
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
        
        char *encoded_url = percent_encode_url(url ? url : "");
        char *escaped_url = xstrdup("");
        escape_html_append(&escaped_url, encoded_url ? encoded_url : "");
        free(encoded_url);
        char *escaped_title = xstrdup("");
        escape_html_append(&escaped_title, title ? title : "");
        
        char *result = xstrdup("");
        if (is_img) {
            append_str(&result, "<img src=\"");
            append_str(&result, escaped_url);
            append_str(&result, "\" alt=\"");
            append_str(&result, inner_html);
            append_str(&result, "\"");
            if (title && title[0] != '\0') {
                append_str(&result, " title=\"");
                append_str(&result, escaped_title);
                append_str(&result, "\"");
            }
            append_str(&result, " />");
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
    
    if (p + 3 < end && strncmp(p, "!--", 3) == 0) {
        p += 3;
        while (p + 2 < end) {
            if (strncmp(p, "-->", 3) == 0) {
                *tag_len = p + 3 - start;
                return start;
            }
            p++;
        }
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
        sprintf(href, "mailto:%s", escaped_url);
    } else {
        href = xstrdup(escaped_url);
    }
    
    char *escaped_content = xstrdup("");
    escape_html_append(&escaped_content, url);
    
    char *html = malloc(strlen(href) + strlen(escaped_content) + 16);
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
    sprintf(html, "<code>%s</code>", escaped);
    free(escaped);
    return html;
}

static inline_token_t *tokenize_inline(const char *text, size_t len, parser_state_t *st, size_t *tokens_count_out) {
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
        
        if (text[i] == '*' || text[i] == '_') {
            char c = text[i];
            size_t run_len = 0;
            while (i + run_len < len && text[i + run_len] == c) {
                run_len++;
            }
            
            bool left = is_left_flanking(text, len, i, run_len);
            bool right = is_right_flanking(text, len, i, run_len);
            
            bool can_open = false;
            bool can_close = false;
            if (c == '*') {
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
        while (i < len && text[i] != '\\' && text[i] != '`' && text[i] != '[' && text[i] != '!' && text[i] != '*' && text[i] != '_' && text[i] != '\n' && text[i] != '<') {
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
    return tokens;
}

static char *process_emphasis_tokens(inline_token_t *tokens, size_t count) {
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
                    
                    if (tokens[opener_idx].can_close || tokens[closer_idx].can_open) {
                        if ((tokens[opener_idx].len + tokens[closer_idx].len) % 3 == 0 &&
                            !(tokens[opener_idx].len % 3 == 0 && tokens[closer_idx].len % 3 == 0)) {
                            continue;
                        }
                    }
                    found_opener = true;
                    break;
                }
            }
            
            if (found_opener) {
                size_t num = (tokens[opener_idx].len >= 2 && tokens[closer_idx].len >= 2) ? 2 : 1;
                const char *open_tag = (num == 2) ? "<strong>" : "<em>";
                const char *close_tag = (num == 2) ? "</strong>" : "</em>";
                
                // Deactivate all delimiters between opener and closer
                for (size_t k = opener_idx + 1; k < closer_idx; k++) {
                    if (tokens[k].type == TOK_DELIM) {
                        tokens[k].can_open = false;
                        tokens[k].can_close = false;
                    }
                }
                
                if (tokens[opener_idx].len > num) {
                    tokens[opener_idx].len -= num;
                    inline_token_t tag_tok = {TOK_HTML_TAG, xstrdup(open_tag), strlen(open_tag), 0, false, false};
                    size_t cap = count + 4;
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
                    inline_token_t tag_tok = {TOK_HTML_TAG, xstrdup(close_tag), strlen(close_tag), 0, false, false};
                    size_t cap = count + 4;
                    insert_token_at(&tokens, &count, &cap, closer_idx, tag_tok);
                    closer_idx++;
                } else {
                    free(tokens[closer_idx].val);
                    tokens[closer_idx].type = TOK_HTML_TAG;
                    tokens[closer_idx].val = xstrdup(close_tag);
                    tokens[closer_idx].len = strlen(close_tag);
                }
                continue;
            }
        }
        closer_idx++;
    }
    
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
    inline_token_t *tokens = tokenize_inline(text, strlen(text), st, &count);
    if (!tokens) {
        return xstrdup("");
    }
    char *result = process_emphasis_tokens(tokens, count);
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

static bool match_list_item(const char *line, bool *ordered_out, char **content_out) {
    if (!line || *line == '\0') {
        return false;
    }
    const char *cursor = line;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor += 1;
    }
    if (isdigit((unsigned char)*cursor)) {
        const char *num = cursor;
        while (isdigit((unsigned char)*num)) {
            num += 1;
        }
        if (*num == '.' && (num[1] == ' ' || num[1] == '\t')) {
            if (ordered_out) {
                *ordered_out = true;
            }
            if (content_out) {
                *content_out = xstrdup(num + 2);
            }
            return true;
        }
    }
    if (*cursor == '-' || *cursor == '*') {
        const char *rest = cursor + 1;
        if (*rest == ' ' || *rest == '\t') {
            if (ordered_out) {
                *ordered_out = false;
            }
            if (content_out) {
                *content_out = xstrdup(rest + 1);
            }
            return true;
        }
    }
    return false;
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

static bool starts_code_fence(const char *line) {
    if (!line) {
        return false;
    }
    int spaces = 0;
    while (*line == ' ' || *line == '\t') {
        if (*line == ' ') spaces++;
        else spaces += 4 - (spaces % 4);
        line++;
    }
    if (spaces >= 4) return false;
    
    char c = *line;
    if (c != '`' && c != '~') {
        return false;
    }
    
    size_t count = 0;
    while (*line == c) {
        count++;
        line++;
    }
    if (count < 3) {
        return false;
    }
    
    if (c == '`') {
        if (strchr(line, '`') != NULL) {
            return false;
        }
    }
    return true;
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
    int spaces = 0;
    while ((*line == ' ' || *line == '\t') && spaces < 3) {
        if (*line == ' ') spaces++;
        else spaces += 4 - (spaces % 4);
        line++;
    }
    return xstrdup(line);
}

static char *build_html_for_document(const char *src, size_t src_len, parser_state_t *st) {
    (void)src_len;
    line_record_t *lines = NULL;
    size_t count = 0;
    split_lines(src, &lines, &count);
    if (!lines) {
        parser_set_error(st, src, 0, "out of memory while parsing document");
        return NULL;
    }

    parse_all_link_refs_from_lines(lines, count, st);

    char *html = xstrdup("");
    size_t i = 0;
    while (i < count) {
        char *line = lines[i].text;
        if (is_blank_line(line)) {
            i += 1;
            continue;
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

        if (starts_code_fence(line)) {
            size_t fence_start = i;
            char *fence_lang = xstrdup(line + 3);
            trim_in_place(fence_lang);
            char *code = xstrdup("");
            i += 1;
            while (i < count) {
                if (starts_code_fence(lines[i].text)) {
                    break;
                }
                char *segment = join_strings(code, lines[i].text);
                free(code);
                code = segment;
                char *with_new = join_strings(code, "\n");
                free(code);
                code = with_new;
                i += 1;
            }
            if (i >= count) {
                parser_set_error(st, src, lines[fence_start].offset, "unterminated code fence");
                free(code);
                free(fence_lang);
                free_lines(lines, count);
                free(html);
                return NULL;
            }

            char *escaped = xstrdup("");
            escape_html_append(&escaped, code);
            append_str(&html, "<pre><code");
            if (fence_lang[0] != '\0') {
                if (strncmp(fence_lang, "language-", 9) == 0) {
                    append_formatted(&html, " class=\"%s\"", fence_lang);
                } else {
                    append_formatted(&html, " class=\"language-%s\"", fence_lang);
                }
            }
            append_str(&html, ">");
            append_str(&html, escaped);
            append_str(&html, "</code></pre>\n");
            free(escaped);
            free(code);
            free(fence_lang);
            i += 1;
            continue;
        }

        bool ordered = false;
        if (match_list_item(line, &ordered, NULL)) {
            append_str(&html, ordered ? "<ol>\n" : "<ul>\n");
            while (i < count) {
                if (match_horizontal_rule(lines[i].text)) {
                    break;
                }
                bool item_ordered = false;
                char *item_text = NULL;
                if (!match_list_item(lines[i].text, &item_ordered, &item_text)) {
                    break;
                }
                if (item_ordered != ordered) {
                    free(item_text);
                    break;
                }
                char *formatted = render_inline_and_check(item_text, src, lines[i].offset, st);
                if (!formatted) {
                    free(item_text);
                    free_lines(lines, count);
                    free(html);
                    return NULL;
                }
                append_str(&html, "<li>");
                append_str(&html, formatted);
                append_str(&html, "</li>\n");
                free(formatted);
                free(item_text);
                i += 1;
            }
            append_str(&html, ordered ? "</ol>\n" : "</ul>\n");
            continue;
        }

        if (starts_blockquote(line)) {
            char *quote_body = xstrdup("");
            while (i < count && starts_blockquote(lines[i].text)) {
                char *raw = lines[i].text;
                while (*raw == ' ' || *raw == '\t') {
                    raw += 1;
                }
                if (*raw == '>') {
                    raw += 1;
                    if (*raw == ' ') {
                        raw += 1;
                    }
                }
                char *segment = join_strings(quote_body, raw);
                free(quote_body);
                quote_body = segment;
                if (i + 1 < count && starts_blockquote(lines[i + 1].text)) {
                    char *nl = join_strings(quote_body, "\n");
                    free(quote_body);
                    quote_body = nl;
                }
                i += 1;
            }
            trim_in_place(quote_body);
            char *formatted = render_inline_and_check(quote_body, src, lines[i - 1].offset, st);
            if (!formatted) {
                free(quote_body);
                free_lines(lines, count);
                free(html);
                return NULL;
            }
            trim_in_place(formatted);
            if (formatted[0] == '\0') {
                append_str(&html, "<blockquote>\n</blockquote>\n");
            } else {
                append_str(&html, "<blockquote>\n<p>");
                append_str(&html, formatted);
                append_str(&html, "</p>\n</blockquote>\n");
            }
            free(formatted);
            free(quote_body);
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
                starts_code_fence(current_line) || match_list_item(current_line, &ordered, NULL) || starts_blockquote(current_line)) {
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

    free_lines(lines, count);
    if (st->has_error) {
        free(html);
        return NULL;
    }
    return html;
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

    
    st.html = build_html_for_document(st.src, st.src_len, &st);

    // Cleanup references table helper macro/code
#define CLEANUP_REFS \
    for (size_t i = 0; i < st.refs_count; i++) { \
        free(st.refs[i].label_normalized); \
        free(st.refs[i].url); \
        free(st.refs[i].title); \
    } \
    free(st.refs);

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
