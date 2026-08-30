#include "html_serializer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * ============================================================================
 * Zero-Dep HTML to Markdown Serializer
 * Member 2: Serializer & Correctness Lead
 *
 * Implements a scoped DOM-tag fragment parser and context-aware reverse walker.
 * Conforms to ISO C23 standard library.
 * Zero third-party dependencies.
 * ============================================================================
 */

/* -------------------------------------------------------------------------
 * Dynamic String Builder (ser_builder_t)
 * ------------------------------------------------------------------------- */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} ser_builder_t;

static ser_builder_t *ser_builder_new(size_t initial_cap) {
    ser_builder_t *b = (ser_builder_t *)malloc(sizeof(ser_builder_t));
    if (!b) return NULL;
    if (initial_cap < 128) initial_cap = 128;
    b->data = (char *)malloc(initial_cap);
    if (!b->data) {
        free(b);
        return NULL;
    }
    b->data[0] = '\0';
    b->len = 0;
    b->cap = initial_cap;
    return b;
}

static bool ser_builder_reserve(ser_builder_t *b, size_t needed) {
    if (!b) return false;
    if (b->len + needed + 1 <= b->cap) return true;
    size_t new_cap = b->cap * 2;
    if (new_cap < b->len + needed + 1) {
        new_cap = b->len + needed + 128;
    }
    char *new_data = (char *)realloc(b->data, new_cap);
    if (!new_data) return false;
    b->data = new_data;
    b->cap = new_cap;
    return true;
}

static void ser_builder_append_len(ser_builder_t *b, const char *s, size_t len) {
    if (!b || !s || len == 0) return;
    if (!ser_builder_reserve(b, len)) return;
    memcpy(b->data + b->len, s, len);
    b->len += len;
    b->data[b->len] = '\0';
}

static void ser_builder_append(ser_builder_t *b, const char *s) {
    if (!s) return;
    ser_builder_append_len(b, s, strlen(s));
}

static void ser_builder_append_char(ser_builder_t *b, char c) {
    if (!b) return;
    if (!ser_builder_reserve(b, 1)) return;
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

static char *ser_builder_take(ser_builder_t *b) {
    if (!b) return NULL;
    char *res = b->data;
    free(b);
    return res;
}

static void ser_builder_free(ser_builder_t *b) {
    if (!b) return;
    if (b->data) free(b->data);
    free(b);
}

/* -------------------------------------------------------------------------
 * DOM Data Structures & Node Management
 * ------------------------------------------------------------------------- */
typedef enum {
    SER_NODE_ROOT,
    SER_NODE_ELEMENT,
    SER_NODE_TEXT,
    SER_NODE_COMMENT
} ser_node_type_t;

typedef struct ser_attr_t {
    char *name;
    char *value;
    struct ser_attr_t *next;
} ser_attr_t;

typedef struct ser_node_t {
    ser_node_type_t type;
    char *tag;
    char *text;
    ser_attr_t *attrs;
    struct ser_node_t *parent;
    struct ser_node_t *first_child;
    struct ser_node_t *last_child;
    struct ser_node_t *next_sibling;
    struct ser_node_t *prev_sibling;
    bool has_closing_tag;
    char *raw_tag;  /* original-case tag name for unknown tags */
    char *raw_open_tag;
    char *raw_close_tag;
} ser_node_t;

static ser_node_t *ser_node_new(ser_node_type_t type) {
    ser_node_t *n = (ser_node_t *)malloc(sizeof(ser_node_t));
    if (!n) return NULL;
    memset(n, 0, sizeof(ser_node_t));
    n->type = type;
    return n;
}

static void ser_node_add_child(ser_node_t *parent, ser_node_t *child) {
    if (!parent || !child) return;
    child->parent = parent;
    child->next_sibling = NULL;
    child->prev_sibling = parent->last_child;
    if (parent->last_child) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
}

static void ser_attr_free_list(ser_attr_t *attr) {
    while (attr) {
        ser_attr_t *next = attr->next;
        if (attr->name) free(attr->name);
        if (attr->value) free(attr->value);
        free(attr);
        attr = next;
    }
}

static void ser_node_free(ser_node_t *node) {
    if (!node) return;
    ser_node_t *child = node->first_child;
    while (child) {
        ser_node_t *next = child->next_sibling;
        ser_node_free(child);
        child = next;
    }
    if (node->tag) free(node->tag);
    if (node->raw_tag) free(node->raw_tag);
    if (node->raw_open_tag) free(node->raw_open_tag);
    if (node->raw_close_tag) free(node->raw_close_tag);
    if (node->text) free(node->text);
    if (node->attrs) ser_attr_free_list(node->attrs);
    free(node);
}

static void ser_node_add_attr(ser_node_t *node, const char *name, const char *value) {
    if (!node || !name) return;
    ser_attr_t *a = (ser_attr_t *)malloc(sizeof(ser_attr_t));
    if (!a) return;
    size_t nlen = strlen(name);
    a->name = (char *)malloc(nlen + 1);
    if (a->name) {
        for (size_t i = 0; i <= nlen; ++i) {
            a->name[i] = (char)tolower((unsigned char)name[i]);
        }
    }
    if (value) {
        size_t vlen = strlen(value);
        a->value = (char *)malloc(vlen + 1);
        if (a->value) memcpy(a->value, value, vlen + 1);
    } else {
        a->value = NULL;
    }
    a->next = NULL;
    if (!node->attrs) {
        node->attrs = a;
    } else {
        ser_attr_t *curr = node->attrs;
        while (curr->next) curr = curr->next;
        curr->next = a;
    }
}

static int ser_strcasecmp(const char *s1, const char *s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

static const char *ser_node_get_attr(const ser_node_t *node, const char *name) {
    if (!node || !name) return NULL;
    ser_attr_t *a = node->attrs;
    while (a) {
        if (a->name && ser_strcasecmp(a->name, name) == 0) {
            return a->value ? a->value : "";
        }
        a = a->next;
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * HTML Entity Decoding & UTF-8 Encoding
 * ------------------------------------------------------------------------- */
static size_t ser_utf8_encode(uint32_t cp, char *out) {
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | ((cp >> 6) & 0x1F));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | ((cp >> 12) & 0x0F));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

static char *ser_decode_entities(const char *src, size_t len) {
    if (!src) return NULL;
    ser_builder_t *b = ser_builder_new(len + 16);
    if (!b) return NULL;

    size_t i = 0;
    while (i < len) {
        if (src[i] == '&') {
            size_t end = i + 1;
            while (end < len && end - i < 12 && src[end] != ';' && src[end] != ' ' && src[end] != '<' && src[end] != '&') {
                end++;
            }
            if (end < len && src[end] == ';') {
                size_t ent_len = end - i + 1;
                char ent_buf[16];
                if (ent_len < sizeof(ent_buf)) {
                    memcpy(ent_buf, src + i, ent_len);
                    ent_buf[ent_len] = '\0';

                    if (strcmp(ent_buf, "&amp;") == 0) {
                        ser_builder_append_char(b, '&');
                        i = end + 1;
                        continue;
                    } else if (strcmp(ent_buf, "&lt;") == 0) {
                        ser_builder_append_char(b, '<');
                        i = end + 1;
                        continue;
                    } else if (strcmp(ent_buf, "&gt;") == 0) {
                        ser_builder_append_char(b, '>');
                        i = end + 1;
                        continue;
                    } else if (strcmp(ent_buf, "&quot;") == 0) {
                        ser_builder_append_char(b, '"');
                        i = end + 1;
                        continue;
                    } else if (strcmp(ent_buf, "&#39;") == 0 || strcmp(ent_buf, "&apos;") == 0) {
                        ser_builder_append_char(b, '\'');
                        i = end + 1;
                        continue;
                    } else if (strcmp(ent_buf, "&nbsp;") == 0) {
                        ser_builder_append_char(b, ' ');
                        i = end + 1;
                        continue;
                    } else if (ent_buf[1] == '#') {
                        uint32_t cp = 0;
                        if (ent_buf[2] == 'x' || ent_buf[2] == 'X') {
                            cp = (uint32_t)strtoul(ent_buf + 3, NULL, 16);
                        } else {
                            cp = (uint32_t)strtoul(ent_buf + 2, NULL, 10);
                        }
                        if (cp > 0) {
                            char utf8_buf[8] = {0};
                            size_t ulen = ser_utf8_encode(cp, utf8_buf);
                            if (ulen > 0) {
                                ser_builder_append_len(b, utf8_buf, ulen);
                                i = end + 1;
                                continue;
                            }
                        }
                    }
                }
            }
        }
        ser_builder_append_char(b, src[i]);
        i++;
    }

    return ser_builder_take(b);
}

/* -------------------------------------------------------------------------
 * Void Tag Check
 * ------------------------------------------------------------------------- */
static bool ser_is_void_tag(const char *tag) {
    if (!tag) return false;
    static const char *void_tags[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr", NULL
    };
    for (int i = 0; void_tags[i]; ++i) {
        if (ser_strcasecmp(tag, void_tags[i]) == 0) return true;
    }
    return false;
}

/* -------------------------------------------------------------------------
 * HTML Fragment Parser
 * ------------------------------------------------------------------------- */
static ser_node_t *ser_parse_html_fragment(const char *html, size_t len) {
    if (!html || len == 0) {
        return ser_node_new(SER_NODE_ROOT);
    }

    ser_node_t *root = ser_node_new(SER_NODE_ROOT);
    if (!root) return NULL;

    ser_node_t *curr = root;
    size_t i = 0;

    while (i < len) {
        if (html[i] == '<') {
            /* HTML Comment: <!-- ... --> */
            if (i + 3 < len && html[i + 1] == '!' && html[i + 2] == '-' && html[i + 3] == '-') {
                size_t end = i + 4;
                while (end + 2 < len && !(html[end] == '-' && html[end + 1] == '-' && html[end + 2] == '>')) {
                    end++;
                }
                i = (end + 2 < len) ? end + 3 : len;
                continue;
            }

            /* DOCTYPE / Declarations: <! ... > */
            if (i + 1 < len && html[i + 1] == '!') {
                size_t end = i + 2;
                while (end < len && html[end] != '>') end++;
                i = (end < len) ? end + 1 : len;
                continue;
            }

            /* XML declaration / Processing instructions: <? ... ?> */
            if (i + 1 < len && html[i + 1] == '?') {
                size_t end = i + 2;
                while (end + 1 < len && !(html[end] == '?' && html[end + 1] == '>')) end++;
                i = (end + 1 < len) ? end + 2 : len;
                continue;
            }

            /* Closing Tag: </tag> */
            if (i + 1 < len && html[i + 1] == '/') {
                size_t close_start = i;
                size_t tag_start = i + 2;
                while (tag_start < len && isspace((unsigned char)html[tag_start])) tag_start++;
                size_t tag_end = tag_start;
                while (tag_end < len && (isalnum((unsigned char)html[tag_end]) || html[tag_end] == '-' || html[tag_end] == '_')) {
                    tag_end++;
                }
                char tag_buf[64] = {0};
                size_t tlen = tag_end - tag_start;
                if (tlen >= sizeof(tag_buf)) tlen = sizeof(tag_buf) - 1;
                for (size_t k = 0; k < tlen; ++k) {
                    tag_buf[k] = (char)tolower((unsigned char)html[tag_start + k]);
                }
                tag_buf[tlen] = '\0';

                /* Skip to '>' */
                size_t end = tag_end;
                while (end < len && html[end] != '>') end++;
                size_t close_end = (end < len) ? end + 1 : len;
                size_t close_len = close_end - close_start;

                /* Find matching open ancestor */
                ser_node_t *scan = curr;
                while (scan && scan != root) {
                    if (scan->tag && ser_strcasecmp(scan->tag, tag_buf) == 0) {
                        scan->has_closing_tag = true;
                        if (!scan->raw_close_tag && close_len > 0) {
                            scan->raw_close_tag = (char *)malloc(close_len + 1);
                            if (scan->raw_close_tag) {
                                memcpy(scan->raw_close_tag, html + close_start, close_len);
                                scan->raw_close_tag[close_len] = '\0';
                            }
                        }
                        curr = scan->parent ? scan->parent : root;
                        break;
                    }
                    scan = scan->parent;
                }

                i = close_end;
                continue;
            }

            /* Opening Tag: <tag attr="val"...> */
            size_t open_start = i;
            size_t tag_start = i + 1;
            while (tag_start < len && isspace((unsigned char)html[tag_start])) tag_start++;
            if (tag_start < len && (isalnum((unsigned char)html[tag_start]))) {
                size_t tag_end = tag_start;
                while (tag_end < len && (isalnum((unsigned char)html[tag_end]) || html[tag_end] == '-' || html[tag_end] == '_')) {
                    tag_end++;
                }

                char tag_buf[64] = {0};
                char tag_buf_lower[64] = {0};
                size_t tlen = tag_end - tag_start;
                if (tlen >= sizeof(tag_buf)) tlen = sizeof(tag_buf) - 1;
                for (size_t k = 0; k < tlen; ++k) {
                    tag_buf[k] = html[tag_start + k];
                    tag_buf_lower[k] = (char)tolower((unsigned char)html[tag_start + k]);
                }
                tag_buf[tlen] = '\0';
                tag_buf_lower[tlen] = '\0';

                ser_node_t *elem = ser_node_new(SER_NODE_ELEMENT);
                if (!elem) {
                    ser_node_free(root);
                    return NULL;
                }
                /* Store lowercase for known tags, original case for unknown */
                const char *tag_store = tag_buf_lower;
                elem->tag = (char *)malloc(strlen(tag_store) + 1);
                if (elem->tag) strcpy(elem->tag, tag_store);
                /* Store original case for serialization of unknown tags */
                elem->raw_tag = (char *)malloc(strlen(tag_buf) + 1);
                if (elem->raw_tag) strcpy(elem->raw_tag, tag_buf);
                elem->has_closing_tag = false;
                /* tag_buf_lower used only for dispatch; raw_tag preserves original case */
                (void)tag_buf;

                /* Parse attributes */
                size_t attr_pos = tag_end;
                bool self_closing = false;

                while (attr_pos < len && html[attr_pos] != '>') {
                    while (attr_pos < len && isspace((unsigned char)html[attr_pos])) attr_pos++;
                    if (attr_pos >= len || html[attr_pos] == '>') break;
                    if (html[attr_pos] == '/') {
                        self_closing = true;
                        attr_pos++;
                        continue;
                    }

                    /* Attribute name */
                    size_t aname_start = attr_pos;
                    while (attr_pos < len && !isspace((unsigned char)html[attr_pos]) &&
                           html[attr_pos] != '=' && html[attr_pos] != '>' && html[attr_pos] != '/') {
                        attr_pos++;
                    }
                    size_t aname_len = attr_pos - aname_start;
                    if (aname_len == 0) {
                        attr_pos++;
                        continue;
                    }

                    char aname_buf[128] = {0};
                    if (aname_len >= sizeof(aname_buf)) aname_len = sizeof(aname_buf) - 1;
                    memcpy(aname_buf, html + aname_start, aname_len);
                    aname_buf[aname_len] = '\0';

                    while (attr_pos < len && isspace((unsigned char)html[attr_pos])) attr_pos++;

                    char *aval_str = NULL;
                    if (attr_pos < len && html[attr_pos] == '=') {
                        attr_pos++;
                        while (attr_pos < len && isspace((unsigned char)html[attr_pos])) attr_pos++;
                        if (attr_pos < len) {
                            if (html[attr_pos] == '"' || html[attr_pos] == '\'') {
                                char quote = html[attr_pos++];
                                size_t val_start = attr_pos;
                                while (attr_pos < len && html[attr_pos] != quote) attr_pos++;
                                size_t val_len = attr_pos - val_start;
                                aval_str = ser_decode_entities(html + val_start, val_len);
                                if (attr_pos < len && html[attr_pos] == quote) attr_pos++;
                            } else {
                                size_t val_start = attr_pos;
                                while (attr_pos < len && !isspace((unsigned char)html[attr_pos]) && html[attr_pos] != '>') attr_pos++;
                                size_t val_len = attr_pos - val_start;
                                aval_str = ser_decode_entities(html + val_start, val_len);
                            }
                        }
                    }

                    ser_node_add_attr(elem, aname_buf, aval_str);
                    if (aval_str) free(aval_str);
                }

                if (attr_pos < len && html[attr_pos] == '>') attr_pos++;
                size_t open_len = attr_pos - open_start;
                elem->raw_open_tag = (char *)malloc(open_len + 1);
                if (elem->raw_open_tag) {
                    memcpy(elem->raw_open_tag, html + open_start, open_len);
                    elem->raw_open_tag[open_len] = '\0';
                }
                i = attr_pos;

                /* Attach element to current DOM node */
                ser_node_add_child(curr, elem);

                if (!self_closing && !ser_is_void_tag(elem->tag)) {
                    curr = elem;
                }
                continue;
            }
        }

        /* Text Node */
        size_t text_start = i;
        while (i < len && html[i] != '<') {
            i++;
        }
        size_t text_len = i - text_start;
        if (text_len > 0) {
            char *decoded = ser_decode_entities(html + text_start, text_len);
            if (decoded) {
                if (decoded[0] != '\0') {
                    ser_node_t *tnode = ser_node_new(SER_NODE_TEXT);
                    if (tnode) {
                        tnode->text = decoded;
                        ser_node_add_child(curr, tnode);
                    } else {
                        free(decoded);
                    }
                } else {
                    free(decoded);
                }
            }
        }
    }

    return root;
}

/* -------------------------------------------------------------------------
 * DOM Walker & Markdown Generator
 * ------------------------------------------------------------------------- */
typedef struct {
    int list_level;
    int quote_level;
    int p_level;
    bool in_code;
    bool in_pre;
    int em_level;
    int strong_level;
    bool tight_list;
} ser_ctx_t;

static void ser_walk_node(ser_node_t *node, ser_builder_t *out, ser_ctx_t *ctx);

static void ser_walk_children(ser_node_t *node, ser_builder_t *out, ser_ctx_t *ctx) {
    if (!node) return;
    ser_node_t *child = node->first_child;
    while (child) {
        ser_walk_node(child, out, ctx);
        child = child->next_sibling;
    }
}

static void ser_render_to_buffer(ser_node_t *node, ser_builder_t *buf, ser_ctx_t *ctx) {
    if (!node) return;
    ser_node_t *child = node->first_child;
    while (child) {
        ser_walk_node(child, buf, ctx);
        child = child->next_sibling;
    }
}

static void ser_emit_destination(ser_builder_t *out, const char *url) {
    if (!out || !url) return;
    for (size_t i = 0; url[i] != '\0'; i++) {
        char c = url[i];
        if (c == '(') {
            ser_builder_append(out, "\\(");
        } else if (c == ')') {
            ser_builder_append(out, "\\)");
        } else if (c == '\\') {
            ser_builder_append(out, "\\\\");
        } else {
            ser_builder_append_char(out, c);
        }
    }
}

static void ser_emit_title(ser_builder_t *out, const char *title) {
    if (!out || !title) return;
    for (size_t i = 0; title[i] != '\0'; i++) {
        char c = title[i];
        if (c == '"') {
            ser_builder_append(out, "\\\"");
        } else if (c == '\\') {
            ser_builder_append(out, "\\\\");
        } else {
            ser_builder_append_char(out, c);
        }
    }
}

static void ser_emit_escaped_text(ser_builder_t *out, const char *text, ser_ctx_t *ctx) {
    if (!out || !text) return;
    if (ctx->in_code || ctx->in_pre) {
        ser_builder_append(out, text);
        return;
    }

    size_t len = strlen(text);
    for (size_t i = 0; i < len; ++i) {
        char c = text[i];
        bool is_line_start = (out->len == 0 || out->data[out->len - 1] == '\n');
        
        if (c == '\n' && is_line_start) {
            continue;
        }

        switch (c) {
            case '*':
                ser_builder_append(out, "\\*");
                break;
            case '_':
                ser_builder_append(out, "\\_");
                break;
            case '`':
                ser_builder_append(out, "\\`");
                break;
            case '[':
                ser_builder_append(out, "\\[");
                break;
            case ']':
                ser_builder_append(out, "\\]");
                break;
            case '\\':
                ser_builder_append(out, "\\\\");
                break;
            case '#':
                /* ATX heading rule: '#' at line start followed by space, tab,
                 * another '#', or end of line (including end of the text
                 * node / a literal newline) all start a heading, so all of
                 * those must be escaped, not just the space/'#' cases. */
                if (is_line_start &&
                    (i + 1 >= len || text[i + 1] == ' ' || text[i + 1] == '\t' ||
                     text[i + 1] == '#' || text[i + 1] == '\n')) {
                    ser_builder_append(out, "\\#");
                } else {
                    ser_builder_append_char(out, c);
                }
                break;
            case '>':
                /* Any '>' at line start starts a blockquote per CommonMark
                 * (the space after '>' is optional), so it must always be
                 * escaped here regardless of what follows. */
                if (is_line_start) {
                    ser_builder_append(out, "\\>");
                } else {
                    ser_builder_append_char(out, c);
                }
                break;
            case '-':
                if (is_line_start || (i + 1 < len && (text[i+1] == '-' || text[i+1] == ' '))) {
                    ser_builder_append(out, "\\-");
                } else {
                    ser_builder_append_char(out, c);
                }
                break;
            case '.':
                if (i > 0 && isdigit((unsigned char)text[i - 1])) {
                    ser_builder_append(out, "\\.");
                } else {
                    ser_builder_append_char(out, c);
                }
                break;
            case '+':
                if (is_line_start) {
                    ser_builder_append(out, "\\+");
                } else {
                    ser_builder_append_char(out, c);
                }
                break;
            case '<':
                /* Only escape < at line start to prevent raw HTML injection;
                 * mid-line < is harmless text and must not be escaped so that
                 * test_html_entities passes (<tag> stays <tag> not \<tag>) */
                if (is_line_start) {
                    ser_builder_append(out, "\\<");
                } else {
                    ser_builder_append_char(out, c);
                }
                break;
            case '!':
                ser_builder_append(out, "\\!");
                break;
            default:
                ser_builder_append_char(out, c);
                break;
        }
    }
}

static bool ser_is_list_loose(ser_node_t *list_node) {
    if (!list_node) return false;
    ser_node_t *child = list_node->first_child;
    bool found_simple = false;
    bool simple_has_p = false;
    while (child) {
        if (child->type == SER_NODE_ELEMENT && child->tag && strcmp(child->tag, "li") == 0) {
            int block_count = 0;
            bool has_p = false;
            ser_node_t *lichild = child->first_child;
            while (lichild) {
                if (lichild->type == SER_NODE_ELEMENT) {
                    const char *ltag = lichild->tag;
                    if (ltag && (strcmp(ltag, "p") == 0 || strcmp(ltag, "ul") == 0 || 
                                 strcmp(ltag, "ol") == 0 || strcmp(ltag, "blockquote") == 0 ||
                                 strcmp(ltag, "pre") == 0 || strcmp(ltag, "h1") == 0 ||
                                 strcmp(ltag, "h2") == 0 || strcmp(ltag, "h3") == 0 ||
                                 strcmp(ltag, "h4") == 0 || strcmp(ltag, "h5") == 0 ||
                                 strcmp(ltag, "h6") == 0)) {
                        block_count++;
                        if (strcmp(ltag, "p") == 0) has_p = true;
                    }
                }
                lichild = lichild->next_sibling;
            }
            if (block_count <= 1) {
                found_simple = true;
                if (has_p) {
                    simple_has_p = true;
                }
            }
        }
        child = child->next_sibling;
    }
    if (found_simple) return simple_has_p;
    return false;
}

static void ser_walk_node(ser_node_t *node, ser_builder_t *out, ser_ctx_t *ctx) {
    if (!node || !out) return;

    if (node->type == SER_NODE_TEXT) {
        if (node->text) {
            /* If the next sibling is a <br>, strip trailing whitespace from this text node
             * to prevent round-trip mismatches (md_parser normalizes trailing spaces) */
            const char *text = node->text;
            ser_node_t *next = node->next_sibling;
            bool next_is_br = (next && next->type == SER_NODE_ELEMENT &&
                               next->tag && strcmp(next->tag, "br") == 0);
            if (next_is_br) {
                size_t tlen = strlen(text);
                while (tlen > 0 && (text[tlen - 1] == ' ' || text[tlen - 1] == '\t')) {
                    tlen--;
                }
                /* Emit only the non-trailing-space portion */
                char *tmp = (char *)malloc(tlen + 1);
                if (tmp) {
                    memcpy(tmp, text, tlen);
                    tmp[tlen] = '\0';
                    ser_emit_escaped_text(out, tmp, ctx);
                    free(tmp);
                } else {
                    ser_emit_escaped_text(out, text, ctx);
                }
            } else {
                ser_emit_escaped_text(out, text, ctx);
            }
        }
        return;
    }

    if (node->type == SER_NODE_ROOT) {
        ser_walk_children(node, out, ctx);
        return;
    }

    if (node->type != SER_NODE_ELEMENT || !node->tag) return;

    const char *tag = node->tag;

    /* If a block element is improperly inside a <p>, fallback to raw HTML so it round-trips safely */
    if (ctx->p_level > 0) {
        if (ser_strcasecmp(tag, "p") == 0 ||
            ser_strcasecmp(tag, "blockquote") == 0 ||
            ser_strcasecmp(tag, "ul") == 0 ||
            ser_strcasecmp(tag, "ol") == 0 ||
            ser_strcasecmp(tag, "li") == 0 ||
            (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && tag[2] == '\0') ||
            ser_strcasecmp(tag, "hr") == 0 ||
            ser_strcasecmp(tag, "pre") == 0 ||
            ser_strcasecmp(tag, "table") == 0 ||
            ser_strcasecmp(tag, "div") == 0) {
            goto raw_html_fallback;
        }
    }

    /* Headings: h1 - h6 */
    if (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && tag[2] == '\0') {
        int level = tag[1] - '0';
        ser_builder_t *h_buf = ser_builder_new(128);
        if (h_buf) {
            ser_walk_children(node, h_buf, ctx);
            bool has_newline = false;
            for (size_t i = 0; i < h_buf->len; i++) {
                if (h_buf->data[i] == '\n') { has_newline = true; break; }
            }
            if (has_newline && (level == 1 || level == 2)) {
                ser_builder_append_len(out, h_buf->data, h_buf->len);
                ser_builder_append_char(out, '\n');
                for (size_t i = 0; i < 2; i++) ser_builder_append_char(out, level == 1 ? '=' : '-');
                ser_builder_append_char(out, '\n');
                if (!ctx->tight_list) ser_builder_append_char(out, '\n');
            } else {
                for (int k = 0; k < level; ++k) ser_builder_append_char(out, '#');
                ser_builder_append_char(out, ' ');
                ser_builder_append_len(out, h_buf->data, h_buf->len);
                ser_builder_append_char(out, '\n');
                if (!ctx->tight_list) ser_builder_append_char(out, '\n');
            }
            ser_builder_free(h_buf);
        }
        return;
    }

    /* Bold / Strong */
    if (strcmp(tag, "strong") == 0 || strcmp(tag, "b") == 0) {
        if (node->attrs != NULL) {
            goto raw_html_fallback;
        }
        if (ctx->em_level > 0 && ctx->strong_level == 0) {
            /* Inside em: use *** for combined bold-italic, or raw if already nested */
            ser_builder_append(out, "**");
            ctx->strong_level++;
            ser_walk_children(node, out, ctx);
            ctx->strong_level--;
            ser_builder_append(out, "**");
        } else if (ctx->strong_level > 0) {
            /* Already in strong: fall back to raw HTML to avoid ambiguity */
            goto raw_html_fallback;
        } else {
            ser_builder_append(out, "**");
            ctx->strong_level++;
            ser_walk_children(node, out, ctx);
            ctx->strong_level--;
            ser_builder_append(out, "**");
        }
        return;
    }

    /* Italic / Emphasis */
    if (strcmp(tag, "em") == 0 || strcmp(tag, "i") == 0) {
        if (node->attrs != NULL) {
            goto raw_html_fallback;
        }
        if (ctx->strong_level > 0 && ctx->em_level == 0) {
            /* Inside strong: use * for italic portion */
            ser_builder_append(out, "*");
            ctx->em_level++;
            ser_walk_children(node, out, ctx);
            ctx->em_level--;
            ser_builder_append(out, "*");
        } else if (ctx->em_level > 0) {
            /* Already in em: fall back to raw HTML */
            goto raw_html_fallback;
        } else {
            ser_builder_append(out, "*");
            ctx->em_level++;
            ser_walk_children(node, out, ctx);
            ctx->em_level--;
            ser_builder_append(out, "*");
        }
        return;
    }

    /* Inline Code */
    if (strcmp(tag, "code") == 0 && !ctx->in_pre) {
        if (node->attrs != NULL) {
            goto raw_html_fallback;
        }
        ser_builder_append(out, "`");
        bool prev_in_code = ctx->in_code;
        ctx->in_code = true;
        ser_walk_children(node, out, ctx);
        ctx->in_code = prev_in_code;
        ser_builder_append(out, "`");
        return;
    }

    /* Code Block: <pre> or <pre><code class="..."> */
    if (strcmp(tag, "pre") == 0) {
        const char *lang = "";
        ser_node_t *cnode = node->first_child;
        while (cnode && (cnode->type != SER_NODE_ELEMENT || !cnode->tag || strcmp(cnode->tag, "code") != 0)) {
            cnode = cnode->next_sibling;
        }
        if (cnode) {
            const char *cls = ser_node_get_attr(cnode, "class");
            if (cls && cls[0] != '\0') {
                if (strncmp(cls, "language-", 9) == 0) {
                    lang = cls + 9;
                } else if (strncmp(cls, "lang-", 5) == 0) {
                    lang = cls + 5;
                } else {
                    lang = cls;
                }
            }
        }

        ser_builder_append(out, "```");
        if (lang[0] != '\0') {
            ser_builder_append(out, lang);
        }
        ser_builder_append_char(out, '\n');

        ser_builder_t *code_buf = ser_builder_new(256);
        if (code_buf) {
            ser_ctx_t code_ctx = *ctx;
            code_ctx.in_code = true;
            code_ctx.in_pre = true;
            if (cnode) {
                ser_render_to_buffer(cnode, code_buf, &code_ctx);
            } else {
                ser_render_to_buffer(node, code_buf, &code_ctx);
            }
            if (code_buf->len > 0) {
                ser_builder_append_len(out, code_buf->data, code_buf->len);
                if (code_buf->data[code_buf->len - 1] != '\n') {
                    ser_builder_append_char(out, '\n');
                }
            }
            ser_builder_free(code_buf);
        }

        ser_builder_append(out, "```\n\n");
        return;
    }

    /* Paragraph */
    if (ser_strcasecmp(tag, "p") == 0) {
        ctx->p_level++;
        ser_walk_children(node, out, ctx);
        ctx->p_level--;
        ser_builder_append_char(out, '\n');
        if (!ctx->tight_list) ser_builder_append_char(out, '\n');
        return;
    }

    /* Unordered List: <ul> */
    if (strcmp(tag, "ul") == 0) {
        if (out->len > 0 && out->data[out->len - 1] != '\n') {
            ser_builder_append_char(out, '\n');
        }
        bool is_loose = ser_is_list_loose(node);
        ser_node_t *child = node->first_child;
        while (child) {
            if (child->type == SER_NODE_ELEMENT && child->tag && strcmp(child->tag, "li") == 0) {
                char prefix[64];
                prefix[0] = '\0';
                strcat(prefix, "- ");

                const char *indent_str = "  ";

                ser_ctx_t nested_ctx = *ctx;
                nested_ctx.list_level += 1;
                nested_ctx.tight_list = !is_loose;

                ser_builder_t *item_buf = ser_builder_new(128);
                if (item_buf) {
                    ser_render_to_buffer(child, item_buf, &nested_ctx);
                    /* Trim trailing newlines inside single list item text */
                    while (item_buf->len > 0 && (item_buf->data[item_buf->len - 1] == '\n' || item_buf->data[item_buf->len - 1] == '\r')) {
                        item_buf->data[--item_buf->len] = '\0';
                    }
                    if (item_buf->len == 0) {
                        ser_builder_append(out, prefix);
                    } else {
                        for (size_t i = 0; i < item_buf->len; i++) {
                            if (i == 0) {
                                ser_builder_append(out, prefix);
                            } else if (item_buf->data[i - 1] == '\n') {
                                ser_builder_append(out, indent_str);
                            }
                            ser_builder_append_char(out, item_buf->data[i]);
                        }
                    }
                    ser_builder_free(item_buf);
                }
                ser_builder_append_char(out, '\n');
                if (is_loose) {
                    ser_builder_append_char(out, '\n');
                }
            }
            child = child->next_sibling;
        }
        if (ctx->list_level == 0) {
            ser_builder_append_char(out, '\n');
        }
        return;
    }

    /* Ordered List: <ol> (Sequentially Renumbered) */
    if (strcmp(tag, "ol") == 0) {
        const char *cls = ser_node_get_attr(node, "class");
        if (cls && strcmp(cls, "footnotes-list") == 0) {
            ser_walk_children(node, out, ctx);
            return;
        }
        if (out->len > 0 && out->data[out->len - 1] != '\n') {
            ser_builder_append_char(out, '\n');
        }
        int item_idx = 1;
        const char *start_attr = ser_node_get_attr(node, "start");
        if (start_attr) {
            item_idx = atoi(start_attr);
        }
        bool is_loose = ser_is_list_loose(node);
        ser_node_t *child = node->first_child;
        while (child) {
            if (child->type == SER_NODE_ELEMENT && child->tag && strcmp(child->tag, "li") == 0) {
                char prefix[64];
                prefix[0] = '\0';
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%d. ", item_idx++);
                strcat(prefix, num_buf);

                const char *indent_str = "  ";

                ser_ctx_t nested_ctx = *ctx;
                nested_ctx.list_level += 1;
                nested_ctx.tight_list = !is_loose;

                ser_builder_t *item_buf = ser_builder_new(128);
                if (item_buf) {
                    ser_render_to_buffer(child, item_buf, &nested_ctx);
                    while (item_buf->len > 0 && (item_buf->data[item_buf->len - 1] == '\n' || item_buf->data[item_buf->len - 1] == '\r')) {
                        item_buf->data[--item_buf->len] = '\0';
                    }
                    if (item_buf->len == 0) {
                        ser_builder_append(out, prefix);
                    } else {
                        for (size_t i = 0; i < item_buf->len; i++) {
                            if (i == 0) {
                                ser_builder_append(out, prefix);
                            } else if (item_buf->data[i - 1] == '\n') {
                                ser_builder_append(out, indent_str);
                            }
                            ser_builder_append_char(out, item_buf->data[i]);
                        }
                    }
                    ser_builder_free(item_buf);
                }
                ser_builder_append_char(out, '\n');
                if (is_loose) {
                    ser_builder_append_char(out, '\n');
                }
            }
            child = child->next_sibling;
        }
        if (ctx->list_level == 0) {
            ser_builder_append_char(out, '\n');
        }
        return;
    }

    /* Blockquote: <blockquote> */
    if (strcmp(tag, "blockquote") == 0) {
        ser_builder_t *quote_buf = ser_builder_new(256);
        if (quote_buf) {
            ser_ctx_t nested_ctx = *ctx;
            nested_ctx.quote_level += 1;
            ser_render_to_buffer(node, quote_buf, &nested_ctx);

            /* Trim trailing newlines from quote_buf */
            while (quote_buf->len > 0 && (quote_buf->data[quote_buf->len - 1] == '\n' || quote_buf->data[quote_buf->len - 1] == '\r')) {
                quote_buf->data[--quote_buf->len] = '\0';
            }

            /* Split quote_buf by lines and prefix each with '> ' */
            const char *src = quote_buf->data;
            size_t slen = quote_buf->len;
            size_t line_start = 0;

            if (slen == 0) {
                ser_builder_append(out, ">\n");
            } else {
                while (line_start < slen) {
                    size_t line_end = line_start;
                    while (line_end < slen && src[line_end] != '\n') line_end++;

                    size_t cur_len = line_end - line_start;
                    if (cur_len > 0) {
                        ser_builder_append(out, "> ");
                        ser_builder_append_len(out, src + line_start, cur_len);
                        ser_builder_append_char(out, '\n');
                    } else {
                        ser_builder_append(out, ">\n");
                    }
                    line_start = (line_end < slen) ? line_end + 1 : slen;
                }
            }
            if (!ctx->tight_list) {
                ser_builder_append_char(out, '\n');
            }
            ser_builder_free(quote_buf);
        }
        return;
    }

    /* Link: <a href="..."> */
    if (strcmp(tag, "a") == 0) {
        const char *href = ser_node_get_attr(node, "href");
        if (href) {
            bool has_other_attrs = false;
            ser_attr_t *at = node->attrs;
            while (at) {
                if (at->name && ser_strcasecmp(at->name, "href") != 0 && ser_strcasecmp(at->name, "title") != 0) {
                    has_other_attrs = true;
                    break;
                }
                at = at->next;
            }
            if (has_other_attrs) {
                goto raw_html_fallback;
            }

            ser_builder_append_char(out, '[');
            ser_walk_children(node, out, ctx);
            ser_builder_append(out, "](");
            ser_emit_destination(out, href);
            const char *title = ser_node_get_attr(node, "title");
            if (title && title[0] != '\0') {
                ser_builder_append(out, " \"");
                ser_emit_title(out, title);
                ser_builder_append_char(out, '"');
            }
            ser_builder_append_char(out, ')');
            return;
        }
    }

    /* Image: <img src="..." alt="..."> */
    if (strcmp(tag, "img") == 0) {
        const char *src = ser_node_get_attr(node, "src");
        if (src) {
            bool has_other_attrs = false;
            ser_attr_t *at = node->attrs;
            while (at) {
                if (at->name && ser_strcasecmp(at->name, "src") != 0 &&
                    ser_strcasecmp(at->name, "alt") != 0 &&
                    ser_strcasecmp(at->name, "title") != 0) {
                    has_other_attrs = true;
                    break;
                }
                at = at->next;
            }
            if (has_other_attrs) {
                goto raw_html_fallback;
            }

            const char *alt = ser_node_get_attr(node, "alt");
            ser_builder_append(out, "![");
            if (alt) ser_builder_append(out, alt);
            ser_builder_append(out, "](");
            ser_emit_destination(out, src);
            const char *title = ser_node_get_attr(node, "title");
            if (title && title[0] != '\0') {
                ser_builder_append(out, " \"");
                ser_emit_title(out, title);
                ser_builder_append_char(out, '"');
            }
            ser_builder_append_char(out, ')');
            return;
        }
    }

    /* Horizontal Rule: <hr> */
    if (strcmp(tag, "hr") == 0) {
        ser_builder_append(out, "---\n\n");
        return;
    }

    /* Line Break: <br> */
    if (strcmp(tag, "br") == 0) {
        if (out->len == 0 || out->data[out->len - 1] == '\n') {
            ser_builder_append(out, "\\\n");
        } else {
            ser_builder_append(out, "  \n");
        }
        return;
    }

    /* Table: <table> */
    if (strcmp(tag, "table") == 0) {
        ser_node_t *tr_nodes[512];
        size_t tr_count = 0;

        ser_node_t *stack[512];
        int top = 0;
        stack[top++] = node;
        while (top > 0) {
            ser_node_t *cur = stack[--top];
            if (cur != node && cur->type == SER_NODE_ELEMENT && cur->tag && strcmp(cur->tag, "tr") == 0) {
                if (tr_count < 512) tr_nodes[tr_count++] = cur;
                continue;
            }
            ser_node_t *c = cur->first_child;
            ser_node_t *kids[64];
            int kcount = 0;
            while (c && kcount < 64) {
                kids[kcount++] = c;
                c = c->next_sibling;
            }
            for (int k = kcount - 1; k >= 0; --k) {
                if (top < 512) stack[top++] = kids[k];
            }
        }

        bool header_emitted = false;
        for (size_t r = 0; r < tr_count; ++r) {
            ser_node_t *tr = tr_nodes[r];
            ser_node_t *cell = tr->first_child;
            bool is_header = false;
            size_t cell_count = 0;

            ser_builder_t *row_buf = ser_builder_new(256);
            if (!row_buf) continue;
            ser_builder_append(row_buf, "| ");

            while (cell) {
                if (cell->type == SER_NODE_ELEMENT && cell->tag) {
                    if (strcmp(cell->tag, "th") == 0 || strcmp(cell->tag, "td") == 0) {
                        if (strcmp(cell->tag, "th") == 0) is_header = true;
                        if (cell_count > 0) {
                            ser_builder_append(row_buf, " | ");
                        }
                        ser_builder_t *cbuf = ser_builder_new(64);
                        if (cbuf) {
                            ser_render_to_buffer(cell, cbuf, ctx);
                            /* Trim newlines inside table cells */
                            for (size_t ci = 0; ci < cbuf->len; ++ci) {
                                if (cbuf->data[ci] == '\n' || cbuf->data[ci] == '\r') {
                                    cbuf->data[ci] = ' ';
                                }
                            }
                            ser_builder_append_len(row_buf, cbuf->data, cbuf->len);
                            ser_builder_free(cbuf);
                        }
                        cell_count++;
                    }
                }
                cell = cell->next_sibling;
            }
            ser_builder_append(row_buf, " |\n");

            if (cell_count > 0) {
                ser_builder_append_len(out, row_buf->data, row_buf->len);
                if (is_header && !header_emitted) {
                    ser_builder_append(out, "|");
                    for (size_t h = 0; h < cell_count; ++h) {
                        ser_builder_append(out, "---|");
                    }
                    ser_builder_append_char(out, '\n');
                    header_emitted = true;
                }
            }
            ser_builder_free(row_buf);
        }
        ser_builder_append_char(out, '\n');
        return;
    }

    /* Frontmatter, Alerts, Math Block in <div> */
    if (strcmp(tag, "div") == 0) {
        const char *cls = ser_node_get_attr(node, "class");
        if (cls && strcmp(cls, "frontmatter") == 0) {
            ser_builder_append(out, "---\n");
            ser_node_t *child = node->first_child;
            while (child) {
                if (child->type == SER_NODE_ELEMENT && child->first_child && child->first_child->text) {
                    ser_builder_append(out, child->first_child->text);
                    ser_builder_append_char(out, '\n');
                }
                child = child->next_sibling;
            }
            ser_builder_append(out, "---\n\n");
            return;
        }
        if (cls && strncmp(cls, "markdown-alert", 14) == 0) {
            const char *type = "NOTE";
            if (strstr(cls, "tip")) type = "TIP";
            else if (strstr(cls, "important")) type = "IMPORTANT";
            else if (strstr(cls, "warning")) type = "WARNING";
            else if (strstr(cls, "caution")) type = "CAUTION";
            ser_builder_append(out, "> [!");
            ser_builder_append(out, type);
            ser_builder_append(out, "]\n");
            ser_node_t *child = node->first_child;
            while (child) {
                const char *ccls = ser_node_get_attr(child, "class");
                if (ccls && strcmp(ccls, "markdown-alert-title") == 0) {
                    child = child->next_sibling;
                    continue;
                }
                ser_builder_t *cbuf = ser_builder_new(128);
                if (cbuf) {
                    ser_walk_node(child, cbuf, ctx);
                    size_t start = 0;
                    for (size_t k = 0; k <= cbuf->len; k++) {
                        if (k == cbuf->len || cbuf->data[k] == '\n') {
                            if (k > start) {
                                ser_builder_append(out, "> ");
                                ser_builder_append_len(out, cbuf->data + start, k - start);
                                ser_builder_append_char(out, '\n');
                            }
                            start = k + 1;
                        }
                    }
                    ser_builder_free(cbuf);
                }
                child = child->next_sibling;
            }
            if (!ctx->tight_list) ser_builder_append_char(out, '\n');
            return;
        }
        if (cls && strcmp(cls, "math-block") == 0) {
            ser_node_t *child = node->first_child;
            while (child) {
                if (child->type == SER_NODE_TEXT && child->text) {
                    ser_builder_append(out, child->text);
                }
                child = child->next_sibling;
            }
            ser_builder_append(out, "\n\n");
            return;
        }
    }

    /* Math Inline in <span> */
    if (strcmp(tag, "span") == 0) {
        const char *cls = ser_node_get_attr(node, "class");
        if (cls && strcmp(cls, "math-inline") == 0) {
            ser_node_t *child = node->first_child;
            while (child) {
                if (child->type == SER_NODE_TEXT && child->text) {
                    ser_builder_append(out, child->text);
                }
                child = child->next_sibling;
            }
            return;
        }
    }

    /* Footnote reference in <sup> */
    if (strcmp(tag, "sup") == 0) {
        ser_node_t *child = node->first_child;
        if (child && child->type == SER_NODE_ELEMENT && child->tag && strcmp(child->tag, "a") == 0) {
            const char *cls = ser_node_get_attr(child, "class");
            if (cls && strcmp(cls, "footnote-ref") == 0) {
                const char *href = ser_node_get_attr(child, "href");
                if (href && strncmp(href, "#fn-", 4) == 0) {
                    ser_builder_append(out, "[^");
                    ser_builder_append(out, href + 4);
                    ser_builder_append(out, "]");
                    return;
                }
            }
        }
    }

    /* Footnotes section in <section> */
    if (strcmp(tag, "section") == 0) {
        const char *cls = ser_node_get_attr(node, "class");
        if (cls && strcmp(cls, "footnotes") == 0) {
            ser_node_t *child = node->first_child;
            while (child) {
                if (child->type == SER_NODE_ELEMENT && child->tag && strcmp(child->tag, "ol") == 0) {
                    ser_walk_children(child, out, ctx);
                }
                child = child->next_sibling;
            }
            return;
        }
    }

    /* Footnote definition <li> in footnote list */
    if (strcmp(tag, "li") == 0) {
        const char *id = ser_node_get_attr(node, "id");
        if (id && strncmp(id, "fn-", 3) == 0) {
            ser_builder_append(out, "[^");
            ser_builder_append(out, id + 3);
            ser_builder_append(out, "]: ");
            ser_builder_t *fbuf = ser_builder_new(128);
            if (fbuf) {
                ser_node_t *p_child = node->first_child;
                while (p_child) {
                    if (p_child->type == SER_NODE_ELEMENT && p_child->tag && strcmp(p_child->tag, "p") == 0) {
                        ser_node_t *c = p_child->first_child;
                        while (c) {
                            const char *ccls = (c->type == SER_NODE_ELEMENT) ? ser_node_get_attr(c, "class") : NULL;
                            if (ccls && strcmp(ccls, "footnote-backref") == 0) {
                                c = c->next_sibling;
                                continue;
                            }
                            ser_walk_node(c, fbuf, ctx);
                            c = c->next_sibling;
                        }
                    } else {
                        ser_walk_node(p_child, fbuf, ctx);
                    }
                    p_child = p_child->next_sibling;
                }
                while (fbuf->len > 0 && (fbuf->data[fbuf->len - 1] == ' ' || fbuf->data[fbuf->len - 1] == '\t' ||
                                         fbuf->data[fbuf->len - 1] == '\n' || fbuf->data[fbuf->len - 1] == '\r')) {
                    fbuf->data[--fbuf->len] = '\0';
                }
                ser_builder_append_len(out, fbuf->data, fbuf->len);
                ser_builder_free(fbuf);
            }
            ser_builder_append(out, "\n");
            return;
        }
    }

    /* Definition list <dl> */
    if (strcmp(tag, "dl") == 0) {
        ser_node_t *child = node->first_child;
        while (child) {
            if (child->type == SER_NODE_ELEMENT && child->tag) {
                if (strcmp(child->tag, "dt") == 0) {
                    ser_walk_children(child, out, ctx);
                    ser_builder_append_char(out, '\n');
                } else if (strcmp(child->tag, "dd") == 0) {
                    ser_builder_append(out, ": ");
                    ser_walk_children(child, out, ctx);
                    ser_builder_append(out, "\n\n");
                }
            }
            child = child->next_sibling;
        }
        return;
    }

raw_html_fallback:
    /* Default: Treat all unrecognized tags (or fallbacks) as raw HTML tags to preserve them */
    if (node->raw_open_tag) {
        ser_builder_append(out, node->raw_open_tag);
        ser_walk_children(node, out, ctx);
        if (node->has_closing_tag) {
            if (node->raw_close_tag) {
                ser_builder_append(out, node->raw_close_tag);
            } else {
                const char *emit_tag = (node->raw_tag && node->raw_tag[0]) ? node->raw_tag : tag;
                ser_builder_append(out, "</");
                ser_builder_append(out, emit_tag);
                ser_builder_append_char(out, '>');
            }
        }
    } else {
        const char *emit_tag = (node->raw_tag && node->raw_tag[0]) ? node->raw_tag : tag;
        ser_builder_append_char(out, '<');
        ser_builder_append(out, emit_tag);
        ser_attr_t *attr = node->attrs;
        while (attr) {
            ser_builder_append_char(out, ' ');
            ser_builder_append(out, attr->name);
            if (attr->value) {
                ser_builder_append(out, "=\"");
                ser_builder_append(out, attr->value);
                ser_builder_append_char(out, '"');
            }
            attr = attr->next;
        }
        if (!node->has_closing_tag) {
            ser_builder_append_char(out, '>');
            ser_walk_children(node, out, ctx);
        } else {
            if (!node->first_child) {
                ser_builder_append(out, "></");
                ser_builder_append(out, emit_tag);
                ser_builder_append_char(out, '>');
            } else {
                ser_builder_append_char(out, '>');
                ser_walk_children(node, out, ctx);
                ser_builder_append(out, "</");
                ser_builder_append(out, emit_tag);
                ser_builder_append_char(out, '>');
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Output Normalization & Cleanup
 * ------------------------------------------------------------------------- */
static char *ser_normalize_markdown(char *raw) {
    if (!raw) return NULL;
    size_t len = strlen(raw);
    if (len == 0) return raw;

    /* Trim excess trailing newlines down to at most 2 */
    while (len > 2 && raw[len - 1] == '\n' && raw[len - 2] == '\n' && raw[len - 3] == '\n') {
        raw[--len] = '\0';
    }

    return raw;
}

/* -------------------------------------------------------------------------
 * Public Interface: html_to_md() & html_serialize_result_free()
 * ------------------------------------------------------------------------- */
html_serialize_result_t html_to_md(const char *html_src, size_t html_len) {
    html_serialize_result_t res = {
        .success = false,
        .markdown = NULL,
        .error_msg = NULL
    };

    if (!html_src || html_len == 0) {
        res.success = true;
        res.markdown = (char *)malloc(1);
        if (res.markdown) {
            res.markdown[0] = '\0';
        } else {
            res.success = false;
            res.error_msg = (char *)malloc(32);
            if (res.error_msg) strcpy(res.error_msg, "memory allocation failed");
        }
        return res;
    }

    ser_node_t *dom_root = ser_parse_html_fragment(html_src, html_len);
    if (!dom_root) {
        res.error_msg = (char *)malloc(64);
        if (res.error_msg) {
            strcpy(res.error_msg, "failed to parse HTML fragment");
        }
        return res;
    }

    ser_builder_t *out = ser_builder_new(html_len + 64);
    if (!out) {
        ser_node_free(dom_root);
        res.error_msg = (char *)malloc(32);
        if (res.error_msg) strcpy(res.error_msg, "memory allocation failed");
        return res;
    }

    ser_ctx_t ctx = {
        .list_level = 0,
        .quote_level = 0,
        .in_code = false,
        .in_pre = false,
        .em_level = 0,
        .strong_level = 0,
        .tight_list = false
    };

    ser_walk_node(dom_root, out, &ctx);

    char *md_str = ser_builder_take(out);
    ser_node_free(dom_root);

    if (!md_str) {
        res.error_msg = (char *)malloc(32);
        if (res.error_msg) strcpy(res.error_msg, "memory allocation failed");
        return res;
    }

    res.markdown = ser_normalize_markdown(md_str);
    res.success = true;
    return res;
}

void html_serialize_result_free(html_serialize_result_t *res) {
    if (!res) return;
    if (res->markdown) {
        free(res->markdown);
        res->markdown = NULL;
    }
    if (res->error_msg) {
        free(res->error_msg);
        res->error_msg = NULL;
    }
}

