#include "md_parser.h"

#include "error_report.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const char *src;
    size_t src_len;
    char *html;
    bool has_error;
    char *error_msg;
    size_t error_line;
    size_t error_col;
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
            case '\'': append_str(dst, "&#39;"); break;
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
    return start;
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

static size_t find_marker_index(const char *text, const char *marker) {
    const char *pos = strstr(text, marker);
    return pos ? (size_t)(pos - text) : SIZE_MAX;
}

static void parser_set_error(parser_state_t *st, const char *src, size_t offset, const char *message) {
    if (!st || !message) {
        return;
    }
    st->has_error = true;
    st->error_msg = xstrdup(message);
    compute_line_col(src, offset, &st->error_line, &st->error_col);
    st->error_line = st->error_line ? st->error_line : 1;
    st->error_col = st->error_col ? st->error_col : 1;
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

static char *render_inline_fragment(const char *text, const char *src, size_t base_offset, parser_state_t *st) {
    if (!text) {
        return xstrdup("");
    }

    char *out = xstrdup("");
    size_t len = strlen(text);
    for (size_t i = 0; i < len; ) {
        if (text[i] == '\\' && i + 1 < len) {
            append_char(&out, text[i + 1]);
            i += 2;
            continue;
        }

        if (strncmp(text + i, "***", 3) == 0) {
            size_t close = find_marker_index(text + i + 3, "***");
            if (close == SIZE_MAX) {
                parser_set_error(st, src, base_offset + i, "unmatched '***'");
                free(out);
                return NULL;
            }
            size_t inner_len = close;
            char *inner = xstrdup_len(text + i + 3, inner_len);
            char *inner_html = render_inline_fragment(inner, src, base_offset + i + 3, st);
            free(inner);
            if (!inner_html) {
                free(out);
                return NULL;
            }
            append_str(&out, "<strong><em>");
            append_str(&out, inner_html);
            append_str(&out, "</em></strong>");
            free(inner_html);
            i += 3 + inner_len + 3;
            continue;
        }

        if (strncmp(text + i, "**", 2) == 0) {
            size_t close = find_marker_index(text + i + 2, "**");
            if (close == SIZE_MAX) {
                parser_set_error(st, src, base_offset + i, "unmatched '**'");
                free(out);
                return NULL;
            }
            size_t inner_len = close;
            char *inner = xstrdup_len(text + i + 2, inner_len);
            char *inner_html = render_inline_fragment(inner, src, base_offset + i + 2, st);
            free(inner);
            if (!inner_html) {
                free(out);
                return NULL;
            }
            append_str(&out, "<strong>");
            append_str(&out, inner_html);
            append_str(&out, "</strong>");
            free(inner_html);
            i += 2 + inner_len + 2;
            continue;
        }

        if (text[i] == '*') {
            size_t close = find_marker_index(text + i + 1, "*");
            if (close == SIZE_MAX) {
                parser_set_error(st, src, base_offset + i, "unmatched '*'");
                free(out);
                return NULL;
            }
            size_t inner_len = close;
            char *inner = xstrdup_len(text + i + 1, inner_len);
            char *inner_html = render_inline_fragment(inner, src, base_offset + i + 1, st);
            free(inner);
            if (!inner_html) {
                free(out);
                return NULL;
            }
            append_str(&out, "<em>");
            append_str(&out, inner_html);
            append_str(&out, "</em>");
            free(inner_html);
            i += 1 + inner_len + 1;
            continue;
        }

        if (text[i] == '`') {
            size_t close = find_marker_index(text + i + 1, "`");
            if (close == SIZE_MAX) {
                parser_set_error(st, src, base_offset + i, "unmatched '`'");
                free(out);
                return NULL;
            }
            size_t inner_len = close;
            char *inner = xstrdup_len(text + i + 1, inner_len);
            char *escaped = xstrdup(inner);
            free(inner);
            char *coded = xstrdup("");
            escape_html_append(&coded, escaped);
            free(escaped);
            append_str(&out, "<code>");
            append_str(&out, coded);
            append_str(&out, "</code>");
            free(coded);
            i += 1 + inner_len + 1;
            continue;
        }

        if (text[i] == '[') {
            size_t close_bracket = SIZE_MAX;
            for (size_t j = i + 1; j < len; ++j) {
                if (text[j] == ']') {
                    close_bracket = j;
                    break;
                }
            }
            if (close_bracket == SIZE_MAX) {
                parser_set_error(st, src, base_offset + i, "expected ']' after link text");
                free(out);
                return NULL;
            }
            if (close_bracket + 1 >= len || text[close_bracket + 1] != '(') {
                parser_set_error(st, src, base_offset + i, "expected '(' after link text");
                free(out);
                return NULL;
            }
            size_t close_paren = SIZE_MAX;
            for (size_t j = close_bracket + 2; j < len; ++j) {
                if (text[j] == ')') {
                    close_paren = j;
                    break;
                }
            }
            if (close_paren == SIZE_MAX) {
                parser_set_error(st, src, base_offset + i, "expected ')' after link target");
                free(out);
                return NULL;
            }

            size_t label_len = close_bracket - (i + 1);
            size_t url_len = close_paren - (close_bracket + 2);
            char *label = xstrdup_len(text + i + 1, label_len);
            char *url = xstrdup_len(text + close_bracket + 2, url_len);
            char *label_html = render_inline_fragment(label, src, base_offset + i + 1, st);
            free(label);
            if (!label_html) {
                free(url);
                free(out);
                return NULL;
            }
            char *href = xstrdup("");
            escape_html_append(&href, url);
            free(url);
            append_str(&out, "<a href=\"");
            append_str(&out, href);
            append_str(&out, "\">");
            append_str(&out, label_html);
            append_str(&out, "</a>");
            free(href);
            free(label_html);
            i = close_paren + 1;
            continue;
        }

        char buf[2];
        buf[0] = text[i];
        buf[1] = '\0';
        char *escaped = xstrdup("");
        escape_html_append(&escaped, buf);
        append_str(&out, escaped);
        free(escaped);
        i += 1;
    }

    return out;
}

static void split_lines(const char *src, line_record_t **lines_out, size_t *count_out) {
    size_t cap = 16;
    line_record_t *lines = (line_record_t *)calloc(cap, sizeof(line_record_t));
    size_t count = 0;
    const char *start = src;
    const char *p = src;
    while (*p != '\0') {
        if (*p == '\n') {
            size_t len = (size_t)(p - start);
            if (len > 0 && start[len - 1] == '\r') {
                len -= 1;
            }
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
            lines[count].text = xstrdup_len(start, len);
            lines[count].line = count + 1;
            lines[count].offset = (size_t)(start - src);
            count += 1;
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
        if (last_len > 0 && start[last_len - 1] == '\r') {
            last_len -= 1;
        }
        lines[count].text = xstrdup_len(start, last_len);
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
    if (!line || *line == '\0') {
        return false;
    }
    const char *cursor = line;
    int level = 0;
    while (*cursor == '#') {
        level += 1;
        cursor += 1;
        if (level > 6) {
            return false;
        }
    }
    if (level == 0) {
        return false;
    }
    while (*cursor == ' ' || *cursor == '\t') {
        cursor += 1;
    }
    if (*cursor == '\0') {
        return false;
    }
    if (level_out) {
        *level_out = level;
    }
    if (content_out) {
        *content_out = xstrdup(cursor);
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
    while (*line == ' ' || *line == '\t') {
        line += 1;
    }
    return strncmp(line, "```", 3) == 0;
}

static char *render_inline_and_check(const char *text, const char *src, size_t base_offset, parser_state_t *st) {
    char *result = render_inline_fragment(text, src, base_offset, st);
    if (st->has_error) {
        return NULL;
    }
    return result;
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

    char *html = xstrdup("");
    size_t i = 0;
    while (i < count) {
        char *line = lines[i].text;
        if (is_blank_line(line)) {
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
                append_formatted(&html, " class=\"%s\"", fence_lang);
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
        char *list_item = NULL;
        if (match_list_item(line, &ordered, &list_item)) {
            append_str(&html, ordered ? "<ol>\n" : "<ul>\n");
            while (i < count) {
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
            free(list_item);
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
            char *formatted = render_inline_and_check(quote_body, src, lines[i - 1].offset, st);
            if (!formatted) {
                free(quote_body);
                free_lines(lines, count);
                free(html);
                return NULL;
            }
            append_str(&html, "<blockquote><p>");
            append_str(&html, formatted);
            append_str(&html, "</p></blockquote>\n");
            free(formatted);
            free(quote_body);
            continue;
        }

        char *paragraph_text = xstrdup("");
        size_t paragraph_start = i;
        while (i < count) {
            char *current_line = lines[i].text;
            if (is_blank_line(current_line)) {
                break;
            }
            if (match_heading(current_line, &heading_level, &heading_content) || starts_code_fence(current_line) ||
                match_list_item(current_line, &ordered, &list_item) || starts_blockquote(current_line)) {
                break;
            }
            char *line_text = xstrdup(current_line);
            trim_in_place(line_text);
            if (paragraph_text[0] != '\0') {
                char *combined = join_strings(paragraph_text, " ");
                free(paragraph_text);
                paragraph_text = combined;
            }
            char *temp = join_strings(paragraph_text, line_text);
            free(paragraph_text);
            paragraph_text = temp;
            free(line_text);
            i += 1;
        }

        if (paragraph_text[0] == '\0') {
            free(paragraph_text);
            continue;
        }

        char *formatted = render_inline_and_check(paragraph_text, src, lines[paragraph_start].offset, st);
        if (!formatted) {
            free(paragraph_text);
            free_lines(lines, count);
            free(html);
            return NULL;
        }
        append_str(&html, "<p>");
        append_str(&html, formatted);
        append_str(&html, "</p>\n");
        free(formatted);
        free(paragraph_text);
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
    st.html = build_html_for_document(source, md_len, &st);

    if (st.has_error) {
        result.success = false;
        result.error_msg = st.error_msg ? xstrdup(st.error_msg) : xstrdup("parse error");
        result.line = st.error_line;
        result.col = st.error_col;
        result.caret_snippet = error_report_create(source, result.line, result.col, result.error_msg);
        free(st.error_msg);
        free(source);
        return result;
    }

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
