/*
 * textdiff.c
 * Minimal changed span between two versions of a text buffer.
 * See textdiff.h for what the result means.
 */

#include "textdiff.h"

void buf_diff_span(const char *a, int alen, const char *b, int blen,
                   int *out_pos, int *out_alen, int *out_blen) {
  if (alen < 0)
    alen = 0;
  if (blen < 0)
    blen = 0;

  /* Longest common prefix. */
  int pos = 0;
  while (pos < alen && pos < blen && a[pos] == b[pos])
    pos++;

  /*
   * Longest common suffix, stopping before the prefix in either buffer.  That
   * bound is what makes the result well formed when one buffer is an extension
   * of the other: for "aa" -> "aaa" the prefix already covers all of "aa", so
   * no suffix is taken and the span is a single inserted byte at the end,
   * rather than a region that overlaps itself.
   */
  int suf = 0;
  while (suf < alen - pos && suf < blen - pos &&
         a[alen - 1 - suf] == b[blen - 1 - suf])
    suf++;

  *out_pos = pos;
  *out_alen = alen - pos - suf;
  *out_blen = blen - pos - suf;
}
