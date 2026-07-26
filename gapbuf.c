/*
 * gapbuf.c
 * Gap-buffer text storage and line-start table for the nStudio editor.
 */

#include <stdlib.h>
#include <string.h>

#include "gapbuf.h"

/* ================================================================
 * Gap buffer
 * ================================================================ */
#define GAP_INIT 4096
#define GAP_GROW 2048

/* Set when an edit had to be dropped because memory ran out.  The main
   editor loop checks it and alerts the user once. */
int g_gb_oom = 0;

void gb_init(GapBuf *g) {
  g->buf = (char *)malloc(GAP_INIT);
  if (g->buf) {
    g->size = GAP_INIT;
    g->gap_hi = GAP_INIT;
    memset(g->buf, 0, GAP_INIT);
  } else {
    /* Start empty: gb_ensure will try to grow via realloc(NULL, ...) */
    g->size = 0;
    g->gap_hi = 0;
  }
  g->gap_lo = 0;
}

void gb_free(GapBuf *g) {
  free(g->buf);
  g->buf = NULL;
}

int gb_ensure(GapBuf *g, int need) {
  int have = g->gap_hi - g->gap_lo;
  if (have >= need)
    return 1;
  int extra = need - have + GAP_GROW;
  int newsize = g->size + extra;
  char *nb = (char *)realloc(g->buf, newsize);
  if (!nb)
    return 0;
  int after = g->size - g->gap_hi;
  memmove(nb + newsize - after, nb + g->gap_hi, after);
  g->buf = nb;
  g->gap_hi = newsize - after;
  g->size = newsize;
  return 1;
}

void gb_move(GapBuf *g, int pos) {
  if (pos == g->gap_lo)
    return;
  int gaplen = g->gap_hi - g->gap_lo;
  if (pos < g->gap_lo) {
    int n = g->gap_lo - pos;
    memmove(g->buf + g->gap_hi - n, g->buf + pos, n);
    g->gap_lo = pos;
    g->gap_hi = pos + gaplen;
  } else {
    int n = pos - g->gap_lo;
    memmove(g->buf + g->gap_lo, g->buf + g->gap_hi, n);
    g->gap_lo = pos;
    g->gap_hi = pos + gaplen;
  }
}

/* Returns 1 if the character was inserted, 0 if it had to be dropped.
   Never writes when the gap is empty, so a failed grow cannot corrupt
   the buffer contents. */
int gb_insert(GapBuf *g, char c) {
  if (!gb_ensure(g, 1) || g->gap_lo >= g->gap_hi) {
    g_gb_oom = 1;
    return 0;
  }
  g->buf[g->gap_lo++] = c;
  return 1;
}

void gb_inserts(GapBuf *g, const char *s) {
  while (*s) {
    if (!gb_insert(g, *s++))
      break;
  }
}

/* Bulk insert of exactly n bytes (NUL bytes included, so binary-ish
   content survives a load/save round trip).  Returns the number of
   bytes actually inserted (0 on allocation failure). */
int gb_insert_n(GapBuf *g, const char *s, int n) {
  if (n <= 0)
    return 0;
  if (!gb_ensure(g, n)) {
    g_gb_oom = 1;
    return 0;
  }
  memcpy(g->buf + g->gap_lo, s, n);
  g->gap_lo += n;
  return n;
}

/* ================================================================
 * Line table
 * ================================================================ */
int line_starts[MAX_LINES];
int num_lines;
int g_lines_truncated; /* set when the file has more than MAX_LINES lines */

void rebuild_lines(const GapBuf *g) {
  int len = gb_len(g);
  num_lines = 0;
  line_starts[num_lines++] = 0;
  int i;
  for (i = 0; i < len && num_lines < MAX_LINES; i++) {
    if (gb_get(g, i) == '\n')
      line_starts[num_lines++] = i + 1;
  }
  /* If we stopped because the table filled while bytes remain, the file
     has more lines than we can index. */
  g_lines_truncated = (num_lines >= MAX_LINES && i < len);
}

/*
 * Cheap line-table maintenance for an edit that did not add or remove a line
 * break: `delta` bytes were inserted (positive) or removed (negative) at `pos`.
 *
 * Only the offsets move - every line starting after `pos` slides by `delta`,
 * and the number of lines is unchanged - so this replaces rebuild_lines()'s
 * scan of the whole buffer with a walk over the table alone.  That is the
 * difference between touching every byte of the file and touching one int per
 * line on each keystroke.
 *
 * A line starting exactly at `pos` must NOT move: inserting at the head of a
 * line leaves that line starting where it did.  Deletions cannot strand a line
 * start inside the removed range, because a start always follows a '\n' and by
 * contract no '\n' was removed - callers that touch a line break (Enter,
 * joining lines, pasting) must still call rebuild_lines().
 */
void lines_shift(int pos, int delta) {
  if (delta == 0)
    return;

  /* First line starting strictly after `pos`; everything below it is fixed. */
  int lo = 1, hi = num_lines, first = num_lines;
  while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    if (line_starts[mid] > pos) {
      first = mid;
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }

  for (int i = first; i < num_lines; i++)
    line_starts[i] += delta;
}

int line_len(const GapBuf *g, int line) {
  int start = line_starts[line];
  int end = (line + 1 < num_lines) ? line_starts[line + 1] - 1 : gb_len(g);
  return end - start;
}

