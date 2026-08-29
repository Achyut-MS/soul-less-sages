#ifndef HTML_SERIALIZER_H
#define HTML_SERIALIZER_H

#include <stddef.h>
#include <stdbool.h>

/**
 * @struct html_serialize_result_t
 * @brief Structured serialization output for HTML-to-Markdown serializer.
 *
 * Implements the reverse conversion contract, ensuring that HTML preview changes
 * can be losslessly and predictably translated back into canonical Markdown source.
 */
typedef struct {
    bool  success;
    char *markdown;       // Allocated string on success; NULL on error
    char *error_msg;      // Descriptive error message if fragment is malformed
} html_serialize_result_t;

/**
 * @brief Converts rendered HTML back into clean, canonical Markdown.
 * @param html_src Pointer to the HTML source string.
 * @param html_len Length of the HTML source string.
 * @return html_serialize_result_t struct containing the serializer output.
 */
html_serialize_result_t html_to_md(const char *html_src, size_t html_len);

/**
 * @brief Frees all dynamic allocations within an html_serialize_result_t structure.
 * @param res Pointer to the result struct to clean up.
 */
void html_serialize_result_free(html_serialize_result_t *res);

#endif
