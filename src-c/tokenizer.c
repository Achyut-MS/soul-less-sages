#include "tokenizer.h"

void tokenizer_init(tokenizer_t *t, const char *src, size_t len) {
    t->src = src;
    t->len = len;
    t->pos = 0;
    t->line = 1;
    t->col = 1;
}

bool tokenizer_eof(const tokenizer_t *t) {
    return t->pos >= t->len;
}

char tokenizer_peek(const tokenizer_t *t) {
    if (tokenizer_eof(t)) {
        return '\0';
    }
    return t->src[t->pos];
}

char tokenizer_peek_offset(const tokenizer_t *t, size_t offset) {
    if (t->pos + offset >= t->len) {
        return '\0';
    }
    return t->src[t->pos + offset];
}

void tokenizer_advance(tokenizer_t *t) {
    if (tokenizer_eof(t)) {
        return;
    }

    if (t->src[t->pos] == '\n') {
        t->line += 1;
        t->col = 1;
    } else {
        t->col += 1;
    }

    t->pos += 1;
}

void tokenizer_get_position(const tokenizer_t *t, size_t *line, size_t *col) {
    if (line) {
        *line = t->line;
    }
    if (col) {
        *col = t->col;
    }
}
