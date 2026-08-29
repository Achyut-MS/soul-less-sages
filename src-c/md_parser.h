#ifndef MD_PARSER_H
#define MD_PARSER_H

#include <stddef.h>
#include <stdbool.h>

/**
 * @struct md_parse_result_t
 * @brief Structured parse output for Markdown-to-HTML parser.
 *
 * Designed to return either a successfully rendered HTML string or detailed
 * compiler-style error diagnostics specifying the exact position of failure.
 */
typedef struct {
    bool   success;
    char  *html;          // Allocated string on success; NULL on error
    char  *error_msg;     // e.g. "unmatched '**' delimiter"
    char  *caret_snippet; // Compiler-style formatted 3-line error snippet
    size_t line;          // 1-indexed
    size_t col;           // 1-indexed
} md_parse_result_t;

/**
 * @brief Parses a Markdown buffer and returns HTML or structured error context.
 * @param md_src Pointer to the source Markdown character buffer.
 * @param md_len Length of the source Markdown buffer.
 * @return md_parse_result_t struct containing the parser result.
 */
md_parse_result_t md_to_html(const char *md_src, size_t md_len);

/**
 * @brief Frees all internal dynamic allocations within an md_parse_result_t structure.
 * @param res Pointer to the result struct to clean up.
 */
void md_parse_result_free(md_parse_result_t *res);

#endif
