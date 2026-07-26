/*
 * gapbuf.h
 * Gap-buffer text storage:  [before_gap | ---gap--- | after_gap]
 * The editing cursor is always at the start of the gap.
 * A line-start table is rebuilt after every edit.
 */
#ifndef GAPBUF_H_INCLUDED
#define GAPBUF_H_INCLUDED

typedef struct {
  char *buf;  /* the whole allocation                          */
  int size;   /* total allocation size                         */
  int gap_lo; /* first byte of gap (= cursor position)         */
  int gap_hi; /* first byte after gap                          */
} GapBuf;

/* Set when an edit had to be dropped because memory ran out.  The main
   editor loop checks it and alerts the user once. */
extern int g_gb_oom;

void gb_init(GapBuf *g);
void gb_free(GapBuf *g);
int gb_ensure(GapBuf *g, int need);
void gb_move(GapBuf *g, int pos);
/* Returns 1 if the character was inserted, 0 if it had to be dropped. */
int gb_insert(GapBuf *g, char c);
void gb_inserts(GapBuf *g, const char *s);
/* Bulk insert of exactly n bytes (NUL-safe).  Returns bytes inserted. */
int gb_insert_n(GapBuf *g, const char *s, int n);

static inline int gb_len(const GapBuf *g) {
  return g->size - (g->gap_hi - g->gap_lo);
}
static inline int gb_phys(const GapBuf *g, int idx) {
  return idx < g->gap_lo ? idx : idx + (g->gap_hi - g->gap_lo);
}
static inline char gb_get(const GapBuf *g, int idx) {
  return g->buf[gb_phys(g, idx)];
}
static inline void gb_backspace(GapBuf *g) {
  if (g->gap_lo > 0)
    g->gap_lo--;
}
static inline void gb_delete(GapBuf *g) {
  if (g->gap_hi < g->size)
    g->gap_hi++;
}

/* ------------------------------------------------------------------ */
/* Line table                                                         */
/* ------------------------------------------------------------------ */
/*
 * Byte offset of the first character of each line.  The table grows on demand,
 * so the number of lines a file may have is limited only by memory; it is never
 * NULL, falling back to a single static entry if even the first allocation
 * fails, so line_starts[0] is always readable.
 */
extern int *line_starts;
extern int num_lines;

/* Set when the table could not grow to cover the whole buffer, i.e. memory ran
   out: the lines past that point exist in the text but are not indexed. */
extern int g_lines_truncated;

void rebuild_lines(const GapBuf *g);

/* Release the line table (back to the static fallback). */
void lines_free(void);
int line_len(const GapBuf *g, int line);

/*
 * Update the line table for an edit at `pos` that changed the buffer length by
 * `delta` bytes WITHOUT adding or removing a line break: line starts after
 * `pos` slide by `delta` and the line count is unchanged.
 *
 * This is the cheap path for ordinary typing - it walks the line table instead
 * of rescanning the whole buffer, as rebuild_lines() does.  Any edit that
 * inserts or deletes a '\n' (Enter, joining two lines, pasting, block indent)
 * changes the structure of the table and must still call rebuild_lines().
 */
void lines_shift(int pos, int delta);

#endif /* GAPBUF_H_INCLUDED */
