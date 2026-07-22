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
#define MAX_LINES 4096

extern int line_starts[MAX_LINES];
extern int num_lines;
extern int g_lines_truncated; /* set when the file exceeds MAX_LINES lines */

void rebuild_lines(const GapBuf *g);
int line_len(const GapBuf *g, int line);

#endif /* GAPBUF_H_INCLUDED */
