/*
 * textdiff.c
 * Minimal changed span between two versions of a text buffer.
 * See textdiff.h for what the result means.
 */

#include "gapbuf.h"
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

void buf_diff_span_gap(const char *a, int alen, const GapBuf *g, int *out_pos,
                       int *out_alen, int *out_blen) {
  int blen = gb_len(g);
  if (alen < 0)
    alen = 0;

  /*
   * The buffer is two contiguous runs - [0, gap_lo) and the tail living at
   * gap_hi - so both walks below step through plain pointers rather than
   * calling gb_get() per byte.  That matters: the per-byte branch costs more
   * than the copy this function exists to avoid, which makes the naive version
   * slower than flattening the buffer first.
   */
  const char *run1 = g->buf;                    /* logical [0, gap_lo)      */
  const char *run2 = g->buf + g->gap_hi - g->gap_lo; /* logical [gap_lo, blen) */

  int lim = alen < blen ? alen : blen;
  int pos = 0;
  int end1 = g->gap_lo < lim ? g->gap_lo : lim;
  while (pos < end1 && a[pos] == run1[pos])
    pos++;
  if (pos == end1)
    while (pos < lim && a[pos] == run2[pos])
      pos++;

  int max_suf = (alen - pos) < (blen - pos) ? (alen - pos) : (blen - pos);
  int suf = 0;
  /* Walking back from the end starts in the tail run and may cross into the
     head run once the logical index drops below gap_lo. */
  int tail_n = blen - g->gap_lo;
  if (tail_n > max_suf)
    tail_n = max_suf;
  while (suf < tail_n && a[alen - 1 - suf] == run2[blen - 1 - suf])
    suf++;
  if (suf == tail_n)
    while (suf < max_suf && a[alen - 1 - suf] == run1[blen - 1 - suf])
      suf++;

  *out_pos = pos;
  *out_alen = alen - pos - suf;
  *out_blen = blen - pos - suf;
}
