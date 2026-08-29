#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    size_t line;
    size_t col;
} tokenizer_t;

void tokenizer_init(tokenizer_t *t, const char *src, size_t len);
bool tokenizer_eof(const tokenizer_t *t);
char tokenizer_peek(const tokenizer_t *t);
char tokenizer_peek_offset(const tokenizer_t *t, size_t offset);
void tokenizer_advance(tokenizer_t *t);
void tokenizer_get_position(const tokenizer_t *t, size_t *line, size_t *col);

#endif
