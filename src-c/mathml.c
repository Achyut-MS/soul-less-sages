#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

/* Forward declarations */
static void mathml_parse_expr(const char **p, char **out, size_t *cap, size_t *len);
static void mathml_parse_atom(const char **p, char **out, size_t *cap, size_t *len);
static void mathml_parse_group(const char **p, char **out, size_t *cap, size_t *len);

/* Helper string builder */
static void mml_append(char **out, size_t *cap, size_t *len, const char *str) {
    size_t slen = strlen(str);
    if (*len + slen + 1 > *cap) {
        *cap = (*len + slen + 1) * 2;
        if (*cap < 128) *cap = 128;
        char *new_out = realloc(*out, *cap);
        if (!new_out) return;
        *out = new_out;
    }
    memcpy(*out + *len, str, slen);
    *len += slen;
    (*out)[*len] = '\0';
}

static void mml_append_char(char **out, size_t *cap, size_t *len, char c) {
    char buf[2] = {c, '\0'};
    mml_append(out, cap, len, buf);
}

/* Map of Greek letters and operators */
typedef struct {
    const char *cmd;
    const char *mml;
} MathMLCmdMap;

static const MathMLCmdMap mathml_greek_map[] = {
    {"alpha", "\xCE\xB1"}, {"beta", "\xCE\xB2"}, {"gamma", "\xCE\xB3"}, {"delta", "\xCE\xB4"},
    {"epsilon", "\xCE\xB5"}, {"theta", "\xCE\xB8"}, {"lambda", "\xCE\xBB"}, {"mu", "\xCE\xBC"},
    {"pi", "\xCF\x80"}, {"sigma", "\xCF\x83"}, {"phi", "\xCF\x86"}, {"omega", "\xCF\x89"},
    {"Gamma", "\xCE\x93"}, {"Delta", "\xCE\x94"}, {"Theta", "\xCE\x98"}, {"Lambda", "\xCE\x9B"},
    {"Pi", "\xCE\xA0"}, {"Sigma", "\xCE\xA3"}, {"Phi", "\xCE\xA6"}, {"Omega", "\xCE\xA9"},
    {"infty", "\xE2\x88\x9E"},
    {NULL, NULL}
};

static const MathMLCmdMap mathml_op_map[] = {
    {"sum", "\xE2\x88\x91"}, {"prod", "\xE2\x88\x8F"}, {"int", "\xE2\x88\xAB"}, {"partial", "\xE2\x88\x82"},
    {"nabla", "\xE2\x88\x87"}, {"pm", "\xC2\xB1"}, {"mp", "\xE2\x88\x93"}, {"times", "\xC3\x97"},
    {"div", "\xC3\xB7"}, {"cdot", "\xE2\x8B\x85"}, {"leq", "\xE2\x89\xA4"}, {"geq", "\xE2\x89\xA5"},
    {"neq", "\xE2\x89\xA0"}, {"approx", "\xE2\x89\x88"}, {"equiv", "\xE2\x89\xA1"},
    {"rightarrow", "\xE2\x86\x92"}, {"leftarrow", "\xE2\x86\x90"},
    {"Rightarrow", "\xE2\x87\x92"}, {"Leftarrow", "\xE2\x87\x90"},
    {"ldots", "\xE2\x80\xA6"}, {"cdots", "\xE2\x8B\xAF"},
    {NULL, NULL}
};

static const MathMLCmdMap mathml_space_map[] = {
    {",", "<mspace width=\"0.167em\" />"},
    {";", "<mspace width=\"0.278em\" />"},
    {"quad", "<mspace width=\"1em\" />"},
    {"qquad", "<mspace width=\"2em\" />"},
    {NULL, NULL}
};

static const char *mathml_lookup_map(const MathMLCmdMap *map, const char *cmd, size_t cmd_len) {
    for (int i = 0; map[i].cmd != NULL; i++) {
        if (strlen(map[i].cmd) == cmd_len && strncmp(map[i].cmd, cmd, cmd_len) == 0) {
            return map[i].mml;
        }
    }
    return NULL;
}

static void mathml_skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

/* Consume exactly one element (atom, group, or single char) */
static void mathml_parse_single(const char **p, char **out, size_t *cap, size_t *len) {
    mathml_skip_ws(p);
    if (**p == '\0') return;

    if (**p == '{') {
        mathml_parse_group(p, out, cap, len);
    } else {
        mathml_parse_atom(p, out, cap, len);
    }
}

static void mathml_parse_group(const char **p, char **out, size_t *cap, size_t *len) {
    if (**p != '{') return;
    (*p)++;

    char *inner = NULL;
    size_t icap = 0, ilen = 0;

    while (**p && **p != '}') {
        mathml_skip_ws(p);
        if (**p == '}') break;
        mathml_parse_expr(p, &inner, &icap, &ilen);
    }

    if (**p == '}') (*p)++;

    if (inner && ilen > 0) {
        mml_append(out, cap, len, "<mrow>");
        mml_append(out, cap, len, inner);
        mml_append(out, cap, len, "</mrow>");
    }
    free(inner);
}

static void mathml_parse_atom(const char **p, char **out, size_t *cap, size_t *len) {
    mathml_skip_ws(p);
    if (**p == '\0') return;

    if (**p == '\\') {
        (*p)++;
        if (**p == '\0') return;

        const char *cmd_start = *p;
        if (isalpha((unsigned char)**p)) {
            while (**p && isalpha((unsigned char)**p)) (*p)++;
        } else {
            (*p)++;
        }
        size_t cmd_len = (size_t)(*p - cmd_start);

        /* Space commands */
        if (cmd_len == 1 && (cmd_start[0] == ',' || cmd_start[0] == ';')) {
            const char *space = mathml_lookup_map(mathml_space_map, cmd_start, cmd_len);
            if (space) mml_append(out, cap, len, space);
            return;
        }

        if (cmd_len == 4 && strncmp(cmd_start, "quad", 4) == 0) {
            mml_append(out, cap, len, mathml_space_map[2].mml);
            return;
        }
        if (cmd_len == 5 && strncmp(cmd_start, "qquad", 5) == 0) {
            mml_append(out, cap, len, mathml_space_map[3].mml);
            return;
        }

        /* Greek letters */
        const char *greek = mathml_lookup_map(mathml_greek_map, cmd_start, cmd_len);
        if (greek) {
            mml_append(out, cap, len, "<mi>");
            mml_append(out, cap, len, greek);
            mml_append(out, cap, len, "</mi>");
            return;
        }

        /* Operators */
        const char *op = mathml_lookup_map(mathml_op_map, cmd_start, cmd_len);
        if (op) {
            mml_append(out, cap, len, "<mo>");
            mml_append(out, cap, len, op);
            mml_append(out, cap, len, "</mo>");
            return;
        }

        /* \text{...} */
        if (cmd_len == 4 && strncmp(cmd_start, "text", 4) == 0) {
            mathml_skip_ws(p);
            if (**p == '{') {
                (*p)++;
                mml_append(out, cap, len, "<mtext>");
                while (**p && **p != '}') {
                    if (**p == '<') mml_append(out, cap, len, "&lt;");
                    else if (**p == '>') mml_append(out, cap, len, "&gt;");
                    else mml_append_char(out, cap, len, **p);
                    (*p)++;
                }
                if (**p == '}') (*p)++;
                mml_append(out, cap, len, "</mtext>");
            }
            return;
        }

        /* \mathrm{...} */
        if (cmd_len == 6 && strncmp(cmd_start, "mathrm", 6) == 0) {
            mathml_skip_ws(p);
            if (**p == '{') {
                (*p)++;
                mml_append(out, cap, len, "<mi mathvariant=\"normal\">");
                while (**p && **p != '}') {
                    if (**p == '<') mml_append(out, cap, len, "&lt;");
                    else if (**p == '>') mml_append(out, cap, len, "&gt;");
                    else mml_append_char(out, cap, len, **p);
                    (*p)++;
                }
                if (**p == '}') (*p)++;
                mml_append(out, cap, len, "</mi>");
            }
            return;
        }

        /* \frac{num}{den} */
        if (cmd_len == 4 && strncmp(cmd_start, "frac", 4) == 0) {
            mml_append(out, cap, len, "<mfrac>");
            mathml_parse_single(p, out, cap, len);
            mathml_parse_single(p, out, cap, len);
            mml_append(out, cap, len, "</mfrac>");
            return;
        }

        /* \sqrt{...} or \sqrt[n]{...} */
        if (cmd_len == 4 && strncmp(cmd_start, "sqrt", 4) == 0) {
            mathml_skip_ws(p);
            if (**p == '[') {
                (*p)++;
                char *degree = NULL;
                size_t dcap = 0, dlen = 0;
                while (**p && **p != ']') {
                    mathml_parse_expr(p, &degree, &dcap, &dlen);
                }
                if (**p == ']') (*p)++;

                mml_append(out, cap, len, "<mroot>");
                mathml_parse_single(p, out, cap, len);
                if (degree) {
                    mml_append(out, cap, len, degree);
                    free(degree);
                }
                mml_append(out, cap, len, "</mroot>");
            } else {
                mml_append(out, cap, len, "<msqrt>");
                mathml_parse_single(p, out, cap, len);
                mml_append(out, cap, len, "</msqrt>");
            }
            return;
        }

        /* Unrecognized command: fallback to \command as mtext */
        mml_append(out, cap, len, "<mtext>\\");
        char cmd_buf[64];
        size_t copy_len = cmd_len > 63 ? 63 : cmd_len;
        memcpy(cmd_buf, cmd_start, copy_len);
        cmd_buf[copy_len] = '\0';
        mml_append(out, cap, len, cmd_buf);
        mml_append(out, cap, len, "</mtext>");

    } else if (isdigit((unsigned char)**p)) {
        mml_append(out, cap, len, "<mn>");
        while (**p && (isdigit((unsigned char)**p) || **p == '.')) {
            mml_append_char(out, cap, len, **p);
            (*p)++;
        }
        mml_append(out, cap, len, "</mn>");
    } else if (isalpha((unsigned char)**p)) {
        mml_append(out, cap, len, "<mi>");
        mml_append_char(out, cap, len, **p);
        mml_append(out, cap, len, "</mi>");
        (*p)++;
    } else if (**p == '~') {
        mml_append(out, cap, len, "<mspace width=\"0.333em\" />");
        (*p)++;
    } else {
        char c = **p;
        (*p)++;

        /* Don't emit ^ or _ as operators; they're handled in parse_expr */
        if (c == '^' || c == '_') {
            return;
        }

        mml_append(out, cap, len, "<mo>");
        if (c == '<') mml_append(out, cap, len, "&lt;");
        else if (c == '>') mml_append(out, cap, len, "&gt;");
        else if (c == '&') mml_append(out, cap, len, "&amp;");
        else mml_append_char(out, cap, len, c);
        mml_append(out, cap, len, "</mo>");
    }
}

static void mathml_parse_expr(const char **p, char **out, size_t *cap, size_t *len) {
    mathml_skip_ws(p);
    if (**p == '\0' || **p == '}' || **p == ']') return;

    char *base = NULL;
    size_t bcap = 0, blen = 0;

    if (**p == '{') {
        mathml_parse_group(p, &base, &bcap, &blen);
    } else {
        mathml_parse_atom(p, &base, &bcap, &blen);
    }

    mathml_skip_ws(p);

    char *sup = NULL;
    size_t scap = 0, slen = 0;
    char *sub = NULL;
    size_t sbcap = 0, sblen = 0;

    while (**p == '^' || **p == '_') {
        if (**p == '^' && !sup) {
            (*p)++;
            mathml_parse_single(p, &sup, &scap, &slen);
        } else if (**p == '_' && !sub) {
            (*p)++;
            mathml_parse_single(p, &sub, &sbcap, &sblen);
        } else {
            break;
        }
        mathml_skip_ws(p);
    }

    if (sup && sub) {
        mml_append(out, cap, len, "<msubsup>");
        if (base) mml_append(out, cap, len, base);
        mml_append(out, cap, len, sub);
        mml_append(out, cap, len, sup);
        mml_append(out, cap, len, "</msubsup>");
    } else if (sup) {
        mml_append(out, cap, len, "<msup>");
        if (base) mml_append(out, cap, len, base);
        mml_append(out, cap, len, sup);
        mml_append(out, cap, len, "</msup>");
    } else if (sub) {
        mml_append(out, cap, len, "<msub>");
        if (base) mml_append(out, cap, len, base);
        mml_append(out, cap, len, sub);
        mml_append(out, cap, len, "</msub>");
    } else {
        if (base) mml_append(out, cap, len, base);
    }

    free(base);
    free(sup);
    free(sub);
}

static char *latex_to_mathml(const char *latex, bool display_mode) {
    if (!latex) return NULL;

    char *out = NULL;
    size_t cap = 0, len = 0;

    if (display_mode) {
        mml_append(&out, &cap, &len, "<math display=\"block\">");
    } else {
        mml_append(&out, &cap, &len, "<math>");
    }

    const char *p = latex;
    while (*p) {
        mathml_skip_ws(&p);
        if (*p == '\0') break;
        mathml_parse_expr(&p, &out, &cap, &len);
    }

    mml_append(&out, &cap, &len, "</math>");
    return out;
}
