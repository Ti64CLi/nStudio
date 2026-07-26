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
/* Entries the table starts at, then doubles from.  Big enough that ordinary
   sources never reallocate, small enough to be free for a scratch buffer. */
#define LINES_INIT 512

/* Last-resort table, used only if even the first allocation fails.  It keeps
   line_starts non-NULL and line_starts[0] valid, so the editor can still show
   and navigate the first line rather than crash. */
static int lines_fallback[1];

int *line_starts = lines_fallback;
static int line_cap = 1;
int num_lines;
int g_lines_truncated; /* set when the table could not cover the whole buffer */

/* Grow the table to hold at least `need` entries.  Returns 1 on success, 0 if
   the allocation failed (leaving the existing table intact and usable).
   Copies rather than reallocs, since the fallback is not heap memory. */
static int lines_reserve(int need) {
  if (need <= line_cap)
    return 1;

  /* Ceiling on entries, so the byte size below cannot overflow.  Far beyond
     what the calculator's memory could hold anyway; it exists to keep the
     arithmetic honest rather than to impose a limit. */
  const int cap_max = (int)(0x7fffffffu / sizeof(int));
  if (need > cap_max)
    return 0;

  int cap = line_cap < LINES_INIT ? LINES_INIT : line_cap;
  while (cap < need) {
    if (cap > cap_max / 2) {
      cap = need; /* one last exact-size step instead of overflowing */
      break;
    }
    cap *= 2;
  }

  int *nb = (int *)malloc((size_t)cap * sizeof(int));
  if (!nb)
    return 0;

  memcpy(nb, line_starts, (size_t)num_lines * sizeof(int));
  if (line_starts != lines_fallback)
    free(line_starts);
  line_starts = nb;
  line_cap = cap;
  return 1;
}

void lines_free(void) {
  if (line_starts != lines_fallback)
    free(line_starts);
  line_starts = lines_fallback;
  line_cap = 1;
  num_lines = 0;
  g_lines_truncated = 0;
}

void rebuild_lines(const GapBuf *g) {
  int len = gb_len(g);
  num_lines = 0;
  g_lines_truncated = 0;

  line_starts[num_lines++] = 0; /* line 0 always fits: cap is at least 1 */

  for (int i = 0; i < len; i++) {
    if (gb_get(g, i) != '\n')
      continue;
    if (num_lines >= line_cap && !lines_reserve(num_lines + 1)) {
      /* Out of memory: index what we have and say so.  The remaining text is
         still in the buffer, it just has no line entries. */
      g_lines_truncated = 1;
      return;
    }
    line_starts[num_lines++] = i + 1;
  }
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

