/*
 * syntax.h
 * ARM assembly tokenisation and highlighted line rendering.
 *
 * nasm syntax notes (see nasm's README):
 *   - labels sit in column 0, instructions must be indented
 *   - comments start at ';' (outside quotes)
 *   - directives are bare words (ALIGN, DCD, EQU, ...), never dot-prefixed
 */
#ifndef SYNTAX_H_INCLUDED
#define SYNTAX_H_INCLUDED

#include <stdint.h>

#include "asmlang.h" /* the word classifiers the highlighter is built on */

/* SWI/SVC line scanner shared by the renderer and the cheat sheet */
int syntax_scan_swi(const char *line, int len, long *out_num);

void render_line_highlighted(const char *line_buf, int px, int py,
                             uint16_t bg, int col_off, int max_w);

#endif /* SYNTAX_H_INCLUDED */
