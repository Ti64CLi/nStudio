/*
 * syntax.h
 * ARM assembly tokenisation and highlighted line rendering.
 *
 * nasm syntax notes (see ../nasm/README.md):
 *   - labels sit in column 0, instructions must be indented
 *   - comments start at ';' (outside quotes)
 *   - directives are bare words (ALIGN, DCD, EQU, ...), never dot-prefixed
 */
#ifndef SYNTAX_H_INCLUDED
#define SYNTAX_H_INCLUDED

#include <stdint.h>

/* Case-insensitive compare of at most n chars */
int strncaseeq(const char *a, const char *b, int n);

/* Word-slice classifiers (word need not be NUL-terminated); all are
   backed by the vendored optab.c tables shared with nasm. */
int syn_is_mnem(const char *word, int wlen);
int syn_is_reg(const char *word, int wlen);
int syn_is_shift(const char *word, int wlen);
int syn_is_directive(const char *word, int wlen);
int syn_is_label_word(const char *word, int wlen);
int is_branch_base(const char *w, int wlen);

/* SWI/SVC line scanner shared by the renderer and the cheat sheet */
int syntax_scan_swi(const char *line, int len, long *out_num);

void render_line_highlighted(const char *line_buf, int px, int py,
                             uint16_t bg, int col_off, int max_w);

#endif /* SYNTAX_H_INCLUDED */
