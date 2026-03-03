/*
 * editor.c
 * ARM assembly text editor for TI-Nspire CX / CX II (Ndless)
 *
 * Architecture
 * ------------
 * Text is stored in a gap buffer:  [before_gap | ---gap--- | after_gap]
 * The cursor is always at the start of the gap.
 * A line-start table is rebuilt after every edit (cheap for files up to
 * a few thousand lines).
 *
 * Syntax highlighting (per-line, stateless)
 * -----------------------------------------
 * Each line is tokenised left-to-right:
 *   LABEL   word followed by ':'           -> yellow
 *   MNEM    known ARM mnemonic             -> cyan
 *   REG     r0-r15 / sp / lr / pc / cpsr   -> green
 *   IMM     #... or 0x...                  -> orange
 *   COMMENT ; to end of line               -> grey
 *   DIRECT  . or % prefixed word           -> magenta
 *   STRING  "..." or '...'                 -> orange (same as imm)
 *   OTHER                                  -> white
 */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <keys.h>
#include <libndls.h>

#include "editor.h"
#include "gfx.h"
#include "settings.h"

/* ================================================================
 * Layout constants
 * ================================================================ */
#define GUTTER_W 4
#define GUTTER_PX (GUTTER_W * GFX_CHAR_W + 2)
#define STATUS_H 10
#define EDIT_X GUTTER_PX
#define EDIT_Y 0
#define EDIT_W (GFX_W - EDIT_X)
#define EDIT_H (GFX_H - STATUS_H)
#define COLS_VIS (EDIT_W / GFX_CHAR_W)
#define ROWS_VIS (EDIT_H / GFX_FONT_H)

/* ================================================================
 * Colours — Dynamically mapped to the unified UI settings
 * ================================================================ */
#define C_BG settings_col(g_settings.ui_bg)
#define C_FG settings_col(g_settings.ui_fg)
#define C_GUTTER_BG settings_col(g_settings.ui_item_bg)
#define C_GUTTER_FG settings_col(g_settings.syn.comment)
#define C_CURSOR_BG settings_col(g_settings.ui_fg)
#define C_CURSOR_FG settings_col(g_settings.ui_bg)
#define C_CURLINE_BG settings_col(g_settings.ui_item_bg)
#define C_STATUS_BG settings_col(g_settings.ui_title_bg)
#define C_STATUS_FG settings_col(g_settings.ui_title_fg)
#define C_MODIFIED settings_col(g_settings.syn.label)

#define C_MNEM settings_col(g_settings.syn.mnem)
#define C_REG settings_col(g_settings.syn.reg)
#define C_IMM settings_col(g_settings.syn.imm)
#define C_LABEL settings_col(g_settings.syn.label)
#define C_CMT settings_col(g_settings.syn.comment)
#define C_DIR settings_col(g_settings.syn.directive)
#define C_STR settings_col(g_settings.syn.string)

/* ================================================================
 * Gap buffer
 * ================================================================ */
#define GAP_INIT 4096
#define GAP_GROW 2048

typedef struct {
  char *buf;  /* the whole allocation                          */
  int size;   /* total allocation size                         */
  int gap_lo; /* first byte of gap (= cursor position)         */
  int gap_hi; /* first byte after gap                          */
} GapBuf;

static void gb_init(GapBuf *g) {
  g->buf = (char *)malloc(GAP_INIT);
  g->size = GAP_INIT;
  g->gap_lo = 0;
  g->gap_hi = GAP_INIT;
  if (g->buf)
    memset(g->buf, 0, GAP_INIT);
}

static void gb_free(GapBuf *g) {
  free(g->buf);
  g->buf = NULL;
}

static int gb_len(const GapBuf *g) { return g->size - (g->gap_hi - g->gap_lo); }

static int gb_phys(const GapBuf *g, int idx) {
  return idx < g->gap_lo ? idx : idx + (g->gap_hi - g->gap_lo);
}

static char gb_get(const GapBuf *g, int idx) { return g->buf[gb_phys(g, idx)]; }

static void gb_ensure(GapBuf *g, int need) {
  int have = g->gap_hi - g->gap_lo;
  if (have >= need)
    return;
  int extra = need - have + GAP_GROW;
  int newsize = g->size + extra;
  char *nb = (char *)realloc(g->buf, newsize);
  if (!nb)
    return;
  int after = g->size - g->gap_hi;
  memmove(nb + newsize - after, nb + g->gap_hi, after);
  g->buf = nb;
  g->gap_hi = newsize - after;
  g->size = newsize;
}

static void gb_move(GapBuf *g, int pos) {
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

static void gb_insert(GapBuf *g, char c) {
  gb_ensure(g, 1);
  g->buf[g->gap_lo++] = c;
}

static void gb_inserts(GapBuf *g, const char *s) {
  while (*s)
    gb_insert(g, *s++);
}

static void gb_backspace(GapBuf *g) {
  if (g->gap_lo > 0)
    g->gap_lo--;
}

static void gb_delete(GapBuf *g) {
  if (g->gap_hi < g->size)
    g->gap_hi++;
}

/* ================================================================
 * Line table
 * ================================================================ */
#define MAX_LINES 4096

static GapBuf g_buf;
static int line_starts[MAX_LINES];
static int num_lines;

static void rebuild_lines(const GapBuf *g) {
  int len = gb_len(g);
  num_lines = 0;
  line_starts[num_lines++] = 0;
  int i;
  for (i = 0; i < len && num_lines < MAX_LINES; i++) {
    if (gb_get(g, i) == '\n')
      line_starts[num_lines++] = i + 1;
  }
}

static int line_len(const GapBuf *g, int line) {
  int start = line_starts[line];
  int end = (line + 1 < num_lines) ? line_starts[line + 1] - 1 : gb_len(g);
  return end - start;
}

/* ================================================================
 * Cursor management
 * ================================================================ */
static int cursor_pos;
static int cursor_row;
static int cursor_col;
static int scroll_row;
static int scroll_col;

static void cursor_sync_pos(void) {
  int i;
  cursor_row = 0;
  for (i = 1; i < num_lines; i++) {
    if (line_starts[i] <= cursor_pos)
      cursor_row = i;
    else
      break;
  }
  cursor_col = cursor_pos - line_starts[cursor_row];
}

static void cursor_sync_rowcol(void) {
  int ll = line_len(&g_buf, cursor_row);
  if (cursor_col > ll)
    cursor_col = ll;
  cursor_pos = line_starts[cursor_row] + cursor_col;
}

static void scroll_to_cursor(void) {
  if (cursor_row < scroll_row)
    scroll_row = cursor_row;
  if (cursor_row >= scroll_row + ROWS_VIS)
    scroll_row = cursor_row - ROWS_VIS + 1;
  if (cursor_col < scroll_col)
    scroll_col = cursor_col;
  if (cursor_col >= scroll_col + COLS_VIS)
    scroll_col = cursor_col - COLS_VIS + 1;
}

/* ================================================================
 * ARM syntax highlighting
 * ================================================================ */

/* ARM mnemonics (uppercase, base only — condition codes stripped on match) */
static const char *arm_mnems[] = {
    "ADC",   "ADD",   "AND",   "B",    "BIC",   "BL",    "BX",   "CLZ",
    "CMN",   "CMP",   "EOR",   "LDM",  "LDR",   "MCR",   "MLA",  "MOV",
    "MRC",   "MRS",   "MSR",   "MUL",  "MVN",   "ORR",   "RSB",  "RSC",
    "SBC",   "SMLAL", "SMULL", "STM",  "STR",   "SUB",   "SWI",  "SVC",
    "SWP",   "SWPB",  "TEQ",   "TST",  "UMLAL", "UMULL", "LDRB", "LDRH",
    "LDRSB", "LDRSH", "STRB",  "STRH", "LDRBT", "STRBT", "LDRT", "STRT",
    "ADR",   NULL};

static const char *arm_regs[] = {
    "R0",       "R1",       "R2",       "R3",       "R4",       "R5",
    "R6",       "R7",       "R8",       "R9",       "R10",      "R11",
    "R12",      "R13",      "R14",      "R15",      "SP",       "LR",
    "PC",       "CPSR",     "SPSR",     "CPSR_ALL", "CPSR_FLG", "CPSR_CTL",
    "SPSR_ALL", "SPSR_FLG", "SPSR_CTL", NULL};

static const char *arm_directives[] = {
    "ALIGN", "DCD", "DCDU", "DCW", "DCWU", "DCB", "INCBIN", "INCLUDE", NULL};

/* Case-insensitive compare of at most n chars */
static int strncaseeq(const char *a, const char *b, int n) {
  int i;
  for (i = 0; i < n; i++) {
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
      return 0;
    if (!a[i])
      return 1;
  }
  return 1;
}

/* Check if `word` (length wlen, uppercase) matches any entry in table */
static int in_table(const char *word, int wlen, const char **table) {
  int i;
  for (i = 0; table[i]; i++) {
    int tlen = (int)strlen(table[i]);
    if (tlen == wlen && strncaseeq(word, table[i], wlen))
      return 1;
  }
  return 0;
}

static int is_mnem(const char *word, int wlen) {
  static const char *cc[] = {"EQ", "NE", "CS", "CC", "MI", "PL",
                             "VS", "VC", "HI", "LS", "GE", "LT",
                             "GT", "LE", "AL", "HS", "LO", NULL};
  static const char *am[] = {"IA", "IB", "DA", "DB", "FD",
                             "FA", "ED", "EA", NULL};

  int lens[8];
  int nlens = 0;

#define PUSH(l)                                                                \
  do {                                                                         \
    if ((l) > 0)                                                               \
      lens[nlens++] = (l);                                                     \
  } while (0)

  PUSH(wlen);

  int no_s = wlen;
  if (wlen > 1 && toupper((unsigned char)word[wlen - 1]) == 'S')
    no_s = wlen - 1;
  if (no_s != wlen)
    PUSH(no_s);

  int base_count = nlens;
  for (int ti = 0; ti < base_count; ti++) {
    int l = lens[ti];
    if (l > 2) {
      for (int i = 0; cc[i]; i++) {
        if (strncaseeq(word + l - 2, cc[i], 2))
          PUSH(l - 2);
      }
    }
  }

  int after_cc_count = nlens;
  for (int ti = 0; ti < after_cc_count; ti++) {
    int l = lens[ti];
    if (l > 2) {
      for (int i = 0; am[i]; i++) {
        if (strncaseeq(word + l - 2, am[i], 2))
          PUSH(l - 2);
      }
    }
  }

#undef PUSH

  for (int ti = 0; ti < nlens; ti++) {
    if (in_table(word, lens[ti], arm_mnems))
      return 1;
  }
  return 0;
}

static int is_reg(const char *word, int wlen) {
  return in_table(word, wlen, arm_regs);
}

static int is_directive(const char *word, int wlen) {
  return in_table(word, wlen, arm_directives);
}

static const char *arm_shifts[] = {"LSL", "LSR", "ASR", "ROR", "RRX", NULL};

static int is_shift(const char *word, int wlen) {
  return in_table(word, wlen, arm_shifts);
}

typedef enum {
  TOK_OTHER,
  TOK_MNEM,
  TOK_REG,
  TOK_IMM,
  TOK_LABEL,
  TOK_CMT,
  TOK_DIR,
  TOK_STR
} TokType;

static uint16_t tok_colour(TokType t) {
  if (!g_settings.syntax_highlight)
    return C_FG;
  switch (t) {
  case TOK_MNEM:
    return C_MNEM;
  case TOK_REG:
    return C_REG;
  case TOK_IMM:
    return C_IMM;
  case TOK_LABEL:
    return C_LABEL;
  case TOK_CMT:
    return C_CMT;
  case TOK_DIR:
    return C_DIR;
  case TOK_STR:
    return C_STR;
  default:
    return C_FG;
  }
}

static void render_line_highlighted(const char *line_buf, int px, int py,
                                    uint16_t bg, int col_off, int max_w) {
  int len = (int)strlen(line_buf);
  int i = 0;
  int draw_x = px - col_off * GFX_CHAR_W;

#define DRAWC(ch, fg)                                                          \
  do {                                                                         \
    if (draw_x >= px && draw_x + GFX_CHAR_W <= px + max_w)                     \
      gfx_drawchar(draw_x, py, (ch), (fg), bg);                                \
    else if (draw_x >= px + max_w)                                             \
      goto done_line;                                                          \
    draw_x += GFX_CHAR_W;                                                      \
  } while (0)

  while (i < len) {
    char c = line_buf[i];

    if (c == ';') {
      while (i < len) {
        DRAWC(line_buf[i], C_CMT);
        i++;
      }
      break;
    }

    if (c == '"' || c == '\'') {
      char delim = c;
      DRAWC(c, C_STR);
      i++;
      while (i < len && line_buf[i] != delim) {
        DRAWC(line_buf[i], C_STR);
        i++;
      }
      if (i < len) {
        DRAWC(line_buf[i], C_STR);
        i++;
      }
      continue;
    }

    if (c == '#' || (c == '0' && i + 1 < len &&
                     (line_buf[i + 1] == 'x' || line_buf[i + 1] == 'X'))) {
      while (i < len && !isspace((unsigned char)line_buf[i]) &&
             line_buf[i] != ',' && line_buf[i] != ']' && line_buf[i] != ')') {
        DRAWC(line_buf[i], C_IMM);
        i++;
      }
      continue;
    }

    if ((c == '.' || c == '%') && i + 1 < len &&
        (isalpha((unsigned char)line_buf[i + 1]) || line_buf[i + 1] == '_')) {
      int start = i;
      while (i < len && (isalnum((unsigned char)line_buf[i]) ||
                         line_buf[i] == '.' || line_buf[i] == '_'))
        i++;
      int wlen = i - start;
      TokType t = is_directive(line_buf + start, wlen) ? TOK_DIR : TOK_OTHER;
      for (int j = start; j < i; j++) {
        DRAWC(line_buf[j], tok_colour(t));
      }
      continue;
    }

    if (isalpha((unsigned char)c) || c == '_') {
      int start = i;
      while (i < len &&
             (isalnum((unsigned char)line_buf[i]) || line_buf[i] == '_'))
        i++;
      int wlen = i - start;

      TokType t = TOK_OTHER;
      /* A label is a bare word at column 0 that is not a known
         mnemonic, register, shift operator, or directive. */
      if (start == 0 && !is_reg(line_buf + start, wlen) &&
          !is_mnem(line_buf + start, wlen) &&
          !is_shift(line_buf + start, wlen) &&
          !is_directive(line_buf + start, wlen)) {
        t = TOK_LABEL;
      } else if (is_reg(line_buf + start, wlen))
        t = TOK_REG;
      else if (is_mnem(line_buf + start, wlen))
        t = TOK_MNEM;
      else if (is_shift(line_buf + start, wlen))
        t = TOK_MNEM;
      else if (is_directive(line_buf + start, wlen))
        t = TOK_DIR;

      for (int j = start; j < i; j++) {
        DRAWC(line_buf[j], tok_colour(t));
      }
      continue;
    }

    DRAWC(c, C_FG);
    i++;
  }

done_line:
  if (draw_x < px + max_w) {
    int fill_x = draw_x < px ? px : draw_x;
    if (fill_x < px + max_w)
      gfx_fillrect(fill_x, py, px + max_w - fill_x, GFX_FONT_H, bg);
  }
#undef DRAWC
}

/* ================================================================
 * File I/O
 * ================================================================ */
static char g_filepath[512];
static int g_modified;

static int load_file(const char *path) {
  FILE *f = fopen(path, "rb");
  gb_init(&g_buf);
  if (!f) {
    rebuild_lines(&g_buf);
    return 0;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  if (sz > 0) {
    char *tmp = (char *)malloc(sz + 1);
    if (tmp) {
      size_t br = fread(tmp, 1, sz, f);
      tmp[br] = '\0';
      gb_inserts(&g_buf, tmp);
      free(tmp);
    }
  }
  fclose(f);
  gb_move(&g_buf, 0);
  rebuild_lines(&g_buf);
  return 1;
}

static int save_file(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return 0;
  int len = gb_len(&g_buf);
  for (int i = 0; i < len; i++)
    fputc(gb_get(&g_buf, i), f);
  fclose(f);
  return 1;
}

/* ================================================================
 * Render
 * ================================================================ */
static char line_scratch[1024];

static void extract_line(int row) {
  int start = line_starts[row];
  int ll = line_len(&g_buf, row);
  if (ll > (int)sizeof(line_scratch) - 1)
    ll = (int)sizeof(line_scratch) - 1;
  for (int i = 0; i < ll; i++)
    line_scratch[i] = gb_get(&g_buf, start + i);
  line_scratch[ll] = '\0';
}

/* ================================================================
 * Selection
 *
 * A selection is active when sel_anchor != sel_active.
 * Both are logical positions in the gap buffer.
 * sel_anchor is where the selection started (fixed end).
 * sel_active follows the cursor (moving end).
 * ================================================================ */
/* Forward declaration — render_all is defined after render_editor_core,
   but is needed by the search helpers which live above both. */
static void render_all(void);

static int sel_anchor = 0; /* -1 = no selection */
static int sel_active = 0;
#define SEL_NONE (-1)

static int sel_lo(void) {
  if (sel_anchor == SEL_NONE)
    return cursor_pos;
  return sel_anchor < sel_active ? sel_anchor : sel_active;
}
static int sel_hi(void) {
  if (sel_anchor == SEL_NONE)
    return cursor_pos;
  return sel_anchor < sel_active ? sel_active : sel_anchor;
}
static int sel_active_flag(void) {
  return sel_anchor != SEL_NONE && sel_anchor != sel_active;
}

static void sel_clear(void) {
  sel_anchor = SEL_NONE;
  sel_active = 0;
}

/* Start or extend a selection. Call AFTER moving cursor_pos. */
static void sel_extend(int old_pos) {
  if (sel_anchor == SEL_NONE)
    sel_anchor = old_pos; /* fix the anchor where we started */
  sel_active = cursor_pos;
}

/* Delete the selected region, leave cursor at sel_lo. */
static void sel_delete_region(void) {
  if (!sel_active_flag())
    return;
  int lo = sel_lo(), hi = sel_hi();
  gb_move(&g_buf, lo);
  for (int i = 0; i < hi - lo; i++)
    gb_delete(&g_buf);
  cursor_pos = lo;
  rebuild_lines(&g_buf);
  cursor_sync_pos();
  g_modified = 1;
  sel_clear();
}

/* ================================================================
 * Clipboard
 * ================================================================ */
static char *g_clipboard = NULL;
static int g_clipboard_len = 0;

static void clipboard_copy(int cut) {
  if (!sel_active_flag())
    return;
  int lo = sel_lo(), hi = sel_hi(), len = hi - lo;
  free(g_clipboard);
  g_clipboard = (char *)malloc(len + 1);
  if (!g_clipboard) {
    g_clipboard_len = 0;
    return;
  }
  for (int i = 0; i < len; i++)
    g_clipboard[i] = gb_get(&g_buf, lo + i);
  g_clipboard[len] = '\0';
  g_clipboard_len = len;
  if (cut)
    sel_delete_region();
  else
    sel_clear();
}

static void clipboard_paste(void) {
  if (!g_clipboard || g_clipboard_len == 0)
    return;
  if (sel_active_flag())
    sel_delete_region();
  gb_move(&g_buf, cursor_pos);
  for (int i = 0; i < g_clipboard_len; i++) {
    gb_insert(&g_buf, g_clipboard[i]);
    cursor_pos++;
  }
  rebuild_lines(&g_buf);
  cursor_sync_pos();
  g_modified = 1;
}

/* ================================================================
 * Undo / Redo
 *
 * Each snapshot stores: full logical buffer, its length, and the
 * cursor position at the time of the snapshot.  We take a snapshot
 * before every mutating operation.  The ring holds UNDO_MAX entries.
 * ================================================================ */
#define UNDO_MAX 32

typedef struct {
  char *buf; /* malloc'd copy of the logical text */
  int len;
  int cursor;
  int s_anchor;
  int s_active;
} UndoSnap;

static UndoSnap g_undo_ring[UNDO_MAX];
static int g_undo_head = 0; /* next write slot */
static int g_undo_count = 0;
static int g_redo_head = 0;
static int g_redo_count = 0;
static UndoSnap g_redo_ring[UNDO_MAX];

static void snap_free(UndoSnap *s) {
  free(s->buf);
  s->buf = NULL;
  s->len = 0;
}

static void undo_push(void) {
  int len = gb_len(&g_buf);
  UndoSnap *s = &g_undo_ring[g_undo_head % UNDO_MAX];
  snap_free(s);
  s->buf = (char *)malloc(len + 1);
  if (!s->buf)
    return;
  for (int i = 0; i < len; i++)
    s->buf[i] = gb_get(&g_buf, i);
  s->buf[len] = '\0';
  s->len = len;
  s->cursor = cursor_pos;
  s->s_anchor = sel_anchor;
  s->s_active = sel_active;
  g_undo_head = (g_undo_head + 1) % UNDO_MAX;
  if (g_undo_count < UNDO_MAX)
    g_undo_count++;
  for (int i = 0; i < g_redo_count; i++)
    snap_free(&g_redo_ring[i]);
  g_redo_count = 0;
  g_redo_head = 0;
}

static void snap_restore(UndoSnap *s) {
  gb_free(&g_buf);
  gb_init(&g_buf);
  gb_inserts(&g_buf, s->buf);
  gb_move(&g_buf, 0);
  rebuild_lines(&g_buf);
  cursor_pos = s->cursor;
  sel_anchor = s->s_anchor;
  sel_active = s->s_active;
  if (cursor_pos > gb_len(&g_buf))
    cursor_pos = gb_len(&g_buf);
  cursor_sync_pos();
  g_modified = 1;
}

static void do_undo(void) {
  if (g_undo_count == 0)
    return;
  int len = gb_len(&g_buf);
  UndoSnap *r = &g_redo_ring[g_redo_head % UNDO_MAX];
  snap_free(r);
  r->buf = (char *)malloc(len + 1);
  if (r->buf) {
    for (int i = 0; i < len; i++)
      r->buf[i] = gb_get(&g_buf, i);
    r->buf[len] = '\0';
    r->len = len;
    r->cursor = cursor_pos;
    r->s_anchor = sel_anchor;
    r->s_active = sel_active;
    g_redo_head = (g_redo_head + 1) % UNDO_MAX;
    if (g_redo_count < UNDO_MAX)
      g_redo_count++;
  }
  g_undo_head = (g_undo_head - 1 + UNDO_MAX) % UNDO_MAX;
  g_undo_count--;
  snap_restore(&g_undo_ring[g_undo_head]);
}

static void do_redo(void) {
  if (g_redo_count == 0)
    return;
  g_redo_head = (g_redo_head - 1 + UNDO_MAX) % UNDO_MAX;
  g_redo_count--;
  undo_push();
  g_undo_count--; /* undo_push incremented it; balance */
  snap_restore(&g_redo_ring[g_redo_head]);
}

/* Convenience: take snapshot then perform a mutating action.
   All mutating do_* call undo_push() internally via these macros. */
#define EDIT_BEGIN() undo_push()

/* ================================================================
 * Search / Search-and-Replace
 * ================================================================ */
#define SEARCH_MAX 128

static char g_search_pat[SEARCH_MAX] = "";
static char g_replace_str[SEARCH_MAX] = "";

/* Case-insensitive substring search in the logical buffer.
   Returns logical position of match start, or -1. */
static int buf_find(int from, const char *pat, int patlen) {
  int buflen = gb_len(&g_buf);
  if (patlen == 0 || from > buflen - patlen)
    return -1;
  for (int i = from; i <= buflen - patlen; i++) {
    int ok = 1;
    for (int j = 0; j < patlen && ok; j++)
      if (tolower((unsigned char)gb_get(&g_buf, i + j)) !=
          tolower((unsigned char)pat[j]))
        ok = 0;
    if (ok)
      return i;
  }
  return -1;
}

/* Highlight the match [lo, hi) as the selection and scroll to it. */
static void search_select(int lo, int hi) {
  sel_anchor = lo;
  sel_active = hi;
  cursor_pos = hi;
  cursor_sync_pos();
  scroll_to_cursor();
}

/* Show a themed search-result status line briefly drawn over the editor.
   This is just a one-line overlay at the bottom; it disappears on next
   full render. */
static void search_draw_status(const char *msg) {
  int sy = GFX_H - STATUS_H;
  gfx_fillrect(0, sy, GFX_W, STATUS_H, g_default_theme.title_bg);
  gfx_drawstr_clipped(2, sy + 1, msg, g_default_theme.accent,
                      g_default_theme.title_bg, GFX_W - 4);
  gfx_flip();
}

/* Interactive search loop.
   find_next = 1 to start from cursor+1 (so repeated Enter keeps moving).
   Returns 1 if something was found+selected, 0 if not found / cancelled. */
static int editor_search_loop(void) {
  int patlen = (int)strlen(g_search_pat);
  if (patlen == 0)
    return 0;

  int start = cursor_pos + 1;
  int found = buf_find(start, g_search_pat, patlen);
  if (found < 0)
    found = buf_find(0, g_search_pat, patlen); /* wrap */
  if (found < 0) {
    search_draw_status("Not found.");
    msleep(800);
    return 0;
  }
  search_select(found, found + patlen);
  render_all();

  while (any_key_pressed())
    msleep(20);
  for (;;) {
    while (!any_key_pressed()) {
      msleep(16);
      idle();
    }
    if (isKeyPressed(KEY_NSPIRE_ESC)) {
      while (any_key_pressed())
        msleep(20);
      sel_clear();
      break;
    }
    if (isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_CLICK)) {
      while (any_key_pressed())
        msleep(20);
      int next = buf_find(cursor_pos + 1, g_search_pat, patlen);
      if (next < 0)
        next = buf_find(0, g_search_pat, patlen);
      if (next < 0) {
        search_draw_status("No more matches.");
        msleep(600);
        break;
      }
      search_select(next, next + patlen);
      render_all();
    } else {
      msleep(20);
    }
  }
  return 1;
}

static void editor_search(void) {
  char tmp[SEARCH_MAX];
  strncpy(tmp, g_search_pat, SEARCH_MAX);
  if (!gfx_input_filename("Search", "Find:", tmp, SEARCH_MAX))
    return;
  strncpy(g_search_pat, tmp, SEARCH_MAX);
  editor_search_loop();
}

static void editor_search_replace(void) {
  char tmp_pat[SEARCH_MAX], tmp_rep[SEARCH_MAX];
  strncpy(tmp_pat, g_search_pat, SEARCH_MAX);
  strncpy(tmp_rep, g_replace_str, SEARCH_MAX);
  if (!gfx_input_filename("Search", "Find:", tmp_pat, SEARCH_MAX))
    return;
  if (!gfx_input_filename("Replace", "Replace with:", tmp_rep, SEARCH_MAX))
    return;
  strncpy(g_search_pat, tmp_pat, SEARCH_MAX);
  strncpy(g_replace_str, tmp_rep, SEARCH_MAX);

  int patlen = (int)strlen(g_search_pat);
  int replen = (int)strlen(g_replace_str);
  if (patlen == 0)
    return;

  int replaced = 0;
  int search_from = 0; /* always advance forward, never re-scan replaced text */

  while (any_key_pressed())
    msleep(20);

  for (;;) {
    int pos = buf_find(search_from, g_search_pat, patlen);
    if (pos < 0)
      break;

    search_select(pos, pos + patlen);
    scroll_to_cursor();
    render_all();
    search_draw_status("Enter:replace  Tab:skip  Esc:done");

    while (!any_key_pressed()) {
      msleep(16);
      idle();
    }

    if (isKeyPressed(KEY_NSPIRE_ESC)) {
      while (any_key_pressed())
        msleep(20);
      sel_clear();
      break;
    } else if (isKeyPressed(KEY_NSPIRE_ENTER) ||
               isKeyPressed(KEY_NSPIRE_CLICK)) {
      while (any_key_pressed())
        msleep(20);
      undo_push();
      gb_move(&g_buf, pos);
      for (int i = 0; i < patlen; i++)
        gb_delete(&g_buf);
      for (int i = 0; i < replen; i++)
        gb_insert(&g_buf, g_replace_str[i]);
      cursor_pos = pos + replen;
      rebuild_lines(&g_buf);
      cursor_sync_pos();
      g_modified = 1;
      sel_clear();
      replaced++;
      search_from = pos + replen;
    } else if (isKeyPressed(KEY_NSPIRE_TAB)) {
      while (any_key_pressed())
        msleep(20);
      sel_clear();
      search_from = pos + patlen;
    } else {
      msleep(20);
      continue;
    }
    render_all();
  }

  sel_clear();
  char msg[48];
  snprintf(msg, sizeof(msg), "Replaced %d occurrence(s).", replaced);
  const char *body[1] = {msg};
  if (replaced > 0)
    gfx_window_alert("Replace Done", body, 1, "OK");
}

static void render_editor_core(void) {
  int visible = num_lines - scroll_row;
  if (visible > ROWS_VIS)
    visible = ROWS_VIS;

  for (int r = 0; r < ROWS_VIS; r++) {
    int row = scroll_row + r;
    int py = EDIT_Y + r * GFX_FONT_H;
    int is_cur = (row == cursor_row);
    uint16_t bg = is_cur ? C_CURLINE_BG : C_BG;

    gfx_fillrect(0, py, GUTTER_PX, GFX_FONT_H, C_GUTTER_BG);
    if (row < num_lines) {
      char gnum[12];
      snprintf(gnum, sizeof(gnum), "%4d", row + 1);
      gfx_drawstr(0, py, gnum, is_cur ? C_FG : C_GUTTER_FG, C_GUTTER_BG);
    }

    if (row < num_lines) {
      extract_line(row);
      render_line_highlighted(line_scratch, EDIT_X, py, bg, scroll_col, EDIT_W);
    } else {
      gfx_fillrect(EDIT_X, py, EDIT_W, GFX_FONT_H, C_BG);
    }
  }

  {
    int text_bottom = EDIT_Y + ROWS_VIS * GFX_FONT_H;
    int status_top = GFX_H - STATUS_H;
    if (text_bottom < status_top)
      gfx_fillrect(0, text_bottom, GFX_W, status_top - text_bottom, C_BG);
  }

  /* ── Selection highlight pass ──
     Walk every visible row; for each character inside [sel_lo, sel_hi)
     overdraw it with the selection background and foreground colours.  */
  if (sel_active_flag()) {
    int lo = sel_lo(), hi = sel_hi();
    uint16_t SEL_BG = g_default_theme.accent;
    uint16_t SEL_FG = g_default_theme.accent_text;

    for (int r = 0; r < ROWS_VIS; r++) {
      int row = scroll_row + r;
      if (row >= num_lines)
        break;

      int row_start = line_starts[row];
      int ll = line_len(&g_buf, row);
      int py = EDIT_Y + r * GFX_FONT_H;

      int row_end = row_start + ll; /* exclusive; newline not included */
      if (lo >= row_end + 1 || hi <= row_start)
        continue;

      extract_line(row);
      for (int col = 0; col < ll; col++) {
        int buf_pos = row_start + col;
        if (buf_pos < lo || buf_pos >= hi)
          continue;
        int scr_col = col - scroll_col;
        if (scr_col < 0 || scr_col >= COLS_VIS)
          continue;
        int cx = EDIT_X + scr_col * GFX_CHAR_W;
        gfx_drawchar(cx, py, line_scratch[col], SEL_FG, SEL_BG);
      }
      if (hi > row_end) {
        int scr_col = ll - scroll_col;
        if (scr_col >= 0 && scr_col < COLS_VIS) {
          int cx = EDIT_X + scr_col * GFX_CHAR_W;
          gfx_fillrect(cx, py, GFX_CHAR_W, GFX_FONT_H, SEL_BG);
        }
      }
    }
  }

  int cur_screen_row = cursor_row - scroll_row;
  int cur_screen_col = cursor_col - scroll_col;
  if (cur_screen_row >= 0 && cur_screen_row < ROWS_VIS && cur_screen_col >= 0 &&
      cur_screen_col < COLS_VIS) {
    int cx = EDIT_X + cur_screen_col * GFX_CHAR_W;
    int cy = cur_screen_row * GFX_FONT_H;
    char ch = ' ';
    if (cursor_col < line_len(&g_buf, cursor_row))
      ch = gb_get(&g_buf, cursor_pos);
    gfx_drawchar(cx, cy, ch, C_CURSOR_FG, C_CURSOR_BG);
  }

  {
    int sy = GFX_H - STATUS_H;
    gfx_fillrect(0, sy, GFX_W, STATUS_H, C_STATUS_BG);
    char status[600];

    const char *fname;
    if (g_filepath[0] == '\0') {
      fname = "Untitled";
    } else {
      fname = strrchr(g_filepath, '/');
      fname = fname ? fname + 1 : g_filepath;
    }

    snprintf(status, sizeof(status), " %s%s  Ln %d/%d  Col %d", fname,
             g_modified ? "*" : "", cursor_row + 1, num_lines, cursor_col + 1);
    gfx_drawstr_clipped(0, sy + 1, status,
                        g_modified ? C_MODIFIED : C_STATUS_FG, C_STATUS_BG,
                        GFX_W);
  }
}

static void render_all(void) {
  render_editor_core();
  gfx_flip();
}

/* ================================================================
 * Keyboard input
 * ================================================================ */

#define KEYMAP_SIZE 55

typedef struct {
  t_key key;
  char normal;
  char shifted;
  char ctrl;
} KeyMap;

static const KeyMap keymap[KEYMAP_SIZE] = {
    {KEY_NSPIRE_A, 'a', 'A', 0},           {KEY_NSPIRE_B, 'b', 'B', 0},
    {KEY_NSPIRE_C, 'c', 'C', 0},           {KEY_NSPIRE_D, 'd', 'D', 0},
    {KEY_NSPIRE_E, 'e', 'E', 0},           {KEY_NSPIRE_F, 'f', 'F', 0},
    {KEY_NSPIRE_G, 'g', 'G', 0},           {KEY_NSPIRE_H, 'h', 'H', 0},
    {KEY_NSPIRE_I, 'i', 'I', 0},           {KEY_NSPIRE_J, 'j', 'J', 0},
    {KEY_NSPIRE_K, 'k', 'K', 0},           {KEY_NSPIRE_L, 'l', 'L', 0},
    {KEY_NSPIRE_M, 'm', 'M', 0},           {KEY_NSPIRE_N, 'n', 'N', 0},
    {KEY_NSPIRE_O, 'o', 'O', 0},           {KEY_NSPIRE_P, 'p', 'P', 0},
    {KEY_NSPIRE_Q, 'q', 'Q', 0},           {KEY_NSPIRE_R, 'r', 'R', 0},
    {KEY_NSPIRE_S, 's', 'S', 0},           {KEY_NSPIRE_T, 't', 'T', 0},
    {KEY_NSPIRE_U, 'u', 'U', 0},           {KEY_NSPIRE_V, 'v', 'V', 0},
    {KEY_NSPIRE_W, 'w', 'W', 0},           {KEY_NSPIRE_X, 'x', 'X', 0},
    {KEY_NSPIRE_Y, 'y', 'Y', 0},           {KEY_NSPIRE_Z, 'z', 'Z', 0},
    {KEY_NSPIRE_0, '0', ')', 0},           {KEY_NSPIRE_1, '1', '!', 0},
    {KEY_NSPIRE_2, '2', '@', 0},           {KEY_NSPIRE_3, '3', '#', 0},
    {KEY_NSPIRE_4, '4', '$', 0},           {KEY_NSPIRE_5, '5', '%', 0},
    {KEY_NSPIRE_6, '6', '^', 0},           {KEY_NSPIRE_7, '7', '&', 0},
    {KEY_NSPIRE_8, '8', '*', 0},           {KEY_NSPIRE_9, '9', '(', 0},
    {KEY_NSPIRE_COMMA, ',', '<', 0},       {KEY_NSPIRE_PERIOD, '.', ':', 0},
    {KEY_NSPIRE_COLON, ':', ';', 0},       {KEY_NSPIRE_DIVIDE, '/', '?', 0},
    {KEY_NSPIRE_MINUS, '-', ';', 0},       {KEY_NSPIRE_PLUS, '+', '=', 0},
    {KEY_NSPIRE_LP, '(', '[', '{'},        {KEY_NSPIRE_RP, ')', ']', '}'},
    {KEY_NSPIRE_SPACE, ' ', ' ', 0},       {KEY_NSPIRE_EXP, '^', '~', 0},
    {KEY_NSPIRE_BAR, '|', '\\', 0},        {KEY_NSPIRE_QUOTE, '"', '"', 0},
    {KEY_NSPIRE_APOSTROPHE, '\'', '`', 0}, {KEY_NSPIRE_MULTIPLY, '*', '*', 0},
    {KEY_NSPIRE_EQU, '=', '+', 0},         {KEY_NSPIRE_NEGATIVE, '~', '~', 0},
    {KEY_NSPIRE_GTHAN, '>', ',', 0},       {KEY_NSPIRE_LTHAN, '<', '{', 0},
    {KEY_NSPIRE_QUES, '?', '?', 0},
};

#define ACT_NONE (-1)
#define ACT_ENTER (-2)
#define ACT_BS (-3)
#define ACT_DEL (-4)
#define ACT_LEFT (-5)
#define ACT_RIGHT (-6)
#define ACT_UP (-7)
#define ACT_DOWN (-8)
#define ACT_HOME (-9)
#define ACT_END (-10)
#define ACT_PGUP (-11)
#define ACT_PGDN (-12)
#define ACT_SAVE (-13)
#define ACT_ESC (-14)
#define ACT_TAB (-15)
#define ACT_CHARMAP (-16)
#define ACT_WORD_LEFT (-17)
#define ACT_WORD_RIGHT (-18)
#define ACT_FILE_TOP (-19)
#define ACT_FILE_BOT (-20)
#define ACT_CATALOG (-21)
#define ACT_JUMP_LABEL (-22)
/* Selection-extending movements */
#define ACT_SEL_LEFT (-23)
#define ACT_SEL_RIGHT (-24)
#define ACT_SEL_UP (-25)
#define ACT_SEL_DOWN (-26)
#define ACT_SEL_HOME (-27)
#define ACT_SEL_END (-28)
#define ACT_SEL_WORD_LEFT (-29)
#define ACT_SEL_WORD_RIGHT (-30)
#define ACT_SEL_FILE_TOP (-31)
#define ACT_SEL_FILE_BOT (-32)
/* Clipboard / undo / search */
#define ACT_COPY (-33)
#define ACT_CUT (-34)
#define ACT_PASTE (-35)
#define ACT_UNDO (-36)
#define ACT_REDO (-37)
#define ACT_SEARCH (-38)
#define ACT_REPLACE (-39)
#define ACT_SEL_ALL (-40)
#define ACT_CHEATSHEET (-41)
#define ACT_OPEN (-42)
#define ACT_SAVE_AS (-43)
#define ACT_GOTO_LINE (-44)
#define ACT_GOTO_LABEL (-45)

static int last_action = ACT_NONE;
static int repeat_timer = 0;
#define REPEAT_DELAY 18
#define REPEAT_RATE 4

static int poll_key(void) {
  int shift = isKeyPressed(KEY_NSPIRE_SHIFT);
  int ctrl = isKeyPressed(KEY_NSPIRE_CTRL);

  if (isKeyPressed(KEY_NSPIRE_ENTER))
    return ctrl ? ACT_JUMP_LABEL : ACT_ENTER;
  if (isKeyPressed(KEY_NSPIRE_ESC))
    return ACT_ESC;
  if (isKeyPressed(KEY_NSPIRE_TAB))
    return ACT_TAB;

  if (isKeyPressed(KEY_NSPIRE_LEFT)) {
    if (shift && ctrl)
      return ACT_SEL_WORD_LEFT;
    if (shift)
      return ACT_SEL_LEFT;
    if (ctrl)
      return ACT_WORD_LEFT;
    return ACT_LEFT;
  }
  if (isKeyPressed(KEY_NSPIRE_RIGHT)) {
    if (shift && ctrl)
      return ACT_SEL_WORD_RIGHT;
    if (shift)
      return ACT_SEL_RIGHT;
    if (ctrl)
      return ACT_WORD_RIGHT;
    return ACT_RIGHT;
  }
  if (isKeyPressed(KEY_NSPIRE_UP)) {
    if (shift && ctrl)
      return ACT_SEL_FILE_TOP;
    if (shift)
      return ACT_SEL_UP;
    if (ctrl)
      return ACT_PGUP;
    return ACT_UP;
  }
  if (isKeyPressed(KEY_NSPIRE_DOWN)) {
    if (shift && ctrl)
      return ACT_SEL_FILE_BOT;
    if (shift)
      return ACT_SEL_DOWN;
    if (ctrl)
      return ACT_PGDN;
    return ACT_DOWN;
  }

  if (isKeyPressed(KEY_NSPIRE_HOME)) {
    if (shift)
      return ACT_SEL_HOME;
    if (ctrl)
      return ACT_FILE_TOP;
    return ACT_HOME;
  }
  if (isKeyPressed(KEY_NSPIRE_MENU) && ctrl)
    return ACT_FILE_BOT;

  if (isKeyPressed(KEY_NSPIRE_DEL))
    return shift ? ACT_DEL : ACT_BS;

  if (ctrl && isKeyPressed(KEY_NSPIRE_O))
    return ACT_OPEN;
  if (ctrl && !shift && isKeyPressed(KEY_NSPIRE_S))
    return ACT_SAVE;
  if (ctrl && shift && isKeyPressed(KEY_NSPIRE_S))
    return ACT_SAVE_AS;

  if (ctrl && isKeyPressed(KEY_NSPIRE_G))
    return ACT_GOTO_LINE;
  if (ctrl && isKeyPressed(KEY_NSPIRE_L))
    return ACT_GOTO_LABEL;

  if (ctrl && isKeyPressed(KEY_NSPIRE_CAT))
    return ACT_CHARMAP;
  if (ctrl && isKeyPressed(KEY_NSPIRE_Z))
    return ACT_UNDO;
  if (ctrl && isKeyPressed(KEY_NSPIRE_Y))
    return ACT_REDO;
  if (ctrl && isKeyPressed(KEY_NSPIRE_C))
    return ACT_COPY;
  if (ctrl && isKeyPressed(KEY_NSPIRE_X))
    return ACT_CUT;
  if (ctrl && isKeyPressed(KEY_NSPIRE_V))
    return ACT_PASTE;
  if (ctrl && isKeyPressed(KEY_NSPIRE_F))
    return ACT_SEARCH;
  if (ctrl && isKeyPressed(KEY_NSPIRE_H))
    return ACT_REPLACE;
  if (ctrl && isKeyPressed(KEY_NSPIRE_A))
    return ACT_SEL_ALL;
  if (ctrl && isKeyPressed(KEY_NSPIRE_TRIG))
    return ACT_CHEATSHEET;

  if (isKeyPressed(KEY_NSPIRE_CAT))
    return ACT_CATALOG;

  for (int i = 0; i < KEYMAP_SIZE; i++) {
    if (!isKeyPressed(keymap[i].key))
      continue;
    if (ctrl)
      return keymap[i].ctrl ? (unsigned char)keymap[i].ctrl : ACT_NONE;
    if (shift)
      return (unsigned char)keymap[i].shifted;
    return (unsigned char)keymap[i].normal;
  }
  return ACT_NONE;
}

/* ================================================================
 * Editor actions
 * ================================================================ */

static void do_insert_char(char c) {
  undo_push();
  if (sel_active_flag())
    sel_delete_region();

  gb_move(&g_buf, cursor_pos);
  gb_insert(&g_buf, c);
  cursor_pos++;
  rebuild_lines(&g_buf);
  cursor_sync_pos();
  g_modified = 1;
}

static void do_enter(void) {
  undo_push();
  if (sel_active_flag())
    sel_delete_region();

  int indent = 0;
  int line_start = line_starts[cursor_row];
  int ll = line_len(&g_buf, cursor_row);

  /* Only look for indentation spaces that are BEFORE the cursor */
  int max_indent = cursor_pos - line_start;

  if (g_settings.auto_indent) {
    while (indent < ll && indent < max_indent) {
      char c = gb_get(&g_buf, line_start + indent);
      if (c == ' ' || c == '\t')
        indent++;
      else
        break;
    }
  }

  gb_move(&g_buf, cursor_pos);
  gb_insert(&g_buf, '\n');
  cursor_pos++;

  int i;
  for (i = 0; i < indent; i++) {
    /* Safe to read from line_start + i because it is strictly before the
     * insertion point */
    gb_insert(&g_buf, gb_get(&g_buf, line_start + i));
    cursor_pos++;
  }

  rebuild_lines(&g_buf);
  cursor_sync_pos();
  g_modified = 1;
}

static void do_backspace(void) {
  if (sel_active_flag()) {
    undo_push();
    sel_delete_region();
    return;
  }
  if (cursor_pos == 0)
    return;
  undo_push();
  gb_move(&g_buf, cursor_pos);
  gb_backspace(&g_buf);
  cursor_pos--;
  rebuild_lines(&g_buf);
  cursor_sync_pos();
  g_modified = 1;
}

static void do_delete(void) {
  if (sel_active_flag()) {
    undo_push();
    sel_delete_region();
    return;
  }
  if (cursor_pos >= gb_len(&g_buf))
    return;
  undo_push();
  gb_move(&g_buf, cursor_pos);
  gb_delete(&g_buf);
  rebuild_lines(&g_buf);
  cursor_sync_pos();
  g_modified = 1;
}

static void do_tab(void) {
  undo_push();
  if (sel_active_flag())
    sel_delete_region();
  for (int i = 0; i < g_settings.tab_width; i++) {
    gb_move(&g_buf, cursor_pos);
    gb_insert(&g_buf, ' ');
    cursor_pos++;
  }
  rebuild_lines(&g_buf);
  cursor_sync_pos();
  g_modified = 1;
}

static void do_left(void) {
  if (sel_active_flag()) {
    cursor_pos = sel_lo();
    sel_clear();
    cursor_sync_pos();
    return;
  }
  if (cursor_pos > 0) {
    cursor_pos--;
    cursor_sync_pos();
  }
}

static void do_right(void) {
  if (sel_active_flag()) {
    cursor_pos = sel_hi();
    sel_clear();
    cursor_sync_pos();
    return;
  }
  if (cursor_pos < gb_len(&g_buf)) {
    cursor_pos++;
    cursor_sync_pos();
  }
}

static void do_up(void) {
  sel_clear();
  if (cursor_row > 0) {
    cursor_row--;
    cursor_sync_rowcol();
  }
}

static void do_down(void) {
  sel_clear();
  if (cursor_row < num_lines - 1) {
    cursor_row++;
    cursor_sync_rowcol();
  }
}

static void do_home(void) {
  sel_clear();
  cursor_col = 0;
  cursor_sync_rowcol();
}

static void do_end(void) {
  sel_clear();
  cursor_col = line_len(&g_buf, cursor_row);
  cursor_sync_rowcol();
}

static void do_pgup(void) {
  sel_clear();
  cursor_row -= ROWS_VIS;
  if (cursor_row < 0)
    cursor_row = 0;
  cursor_sync_rowcol();
}

static void do_pgdn(void) {
  sel_clear();
  cursor_row += ROWS_VIS;
  if (cursor_row >= num_lines)
    cursor_row = num_lines - 1;
  cursor_sync_rowcol();
}

#define IS_WORD(c) (isalnum((unsigned char)(c)) || (c) == '_')
#define IS_HSPACE(c) ((c) == ' ' || (c) == '\t')

static void do_word_left(void) {
  sel_clear();
  if (cursor_pos == 0)
    return;
  while (cursor_pos > 0 && IS_HSPACE(gb_get(&g_buf, cursor_pos - 1)))
    cursor_pos--;
  if (cursor_pos == 0) {
    cursor_sync_pos();
    return;
  }
  if (IS_WORD(gb_get(&g_buf, cursor_pos - 1))) {
    while (cursor_pos > 0 && IS_WORD(gb_get(&g_buf, cursor_pos - 1)))
      cursor_pos--;
  } else {
    cursor_pos--;
  }
  cursor_sync_pos();
}

static void do_word_right(void) {
  sel_clear();
  int len = gb_len(&g_buf);
  if (cursor_pos >= len)
    return;
  if (IS_WORD(gb_get(&g_buf, cursor_pos))) {
    while (cursor_pos < len && IS_WORD(gb_get(&g_buf, cursor_pos)))
      cursor_pos++;
  } else if (!IS_HSPACE(gb_get(&g_buf, cursor_pos))) {
    cursor_pos++;
  }
  while (cursor_pos < len && IS_HSPACE(gb_get(&g_buf, cursor_pos)))
    cursor_pos++;
  cursor_sync_pos();
}

static void do_file_top(void) {
  sel_clear();
  cursor_pos = 0;
  cursor_row = 0;
  cursor_col = 0;
  scroll_row = 0;
  scroll_col = 0;
}

static void do_file_bot(void) {
  sel_clear();
  cursor_row = num_lines - 1;
  cursor_col = line_len(&g_buf, cursor_row);
  cursor_sync_rowcol();
}

/* ── Selection-extending movement variants ──
   Each saves old cursor_pos as anchor (first call), then moves
   and updates sel_active.                                        */
static void do_sel_left(void) {
  int old = cursor_pos;
  if (cursor_pos > 0) {
    cursor_pos--;
    cursor_sync_pos();
  }
  sel_extend(old);
}
static void do_sel_right(void) {
  int old = cursor_pos;
  if (cursor_pos < gb_len(&g_buf)) {
    cursor_pos++;
    cursor_sync_pos();
  }
  sel_extend(old);
}
static void do_sel_up(void) {
  int old = cursor_pos;
  if (cursor_row > 0) {
    cursor_row--;
    cursor_sync_rowcol();
  }
  sel_extend(old);
}
static void do_sel_down(void) {
  int old = cursor_pos;
  if (cursor_row < num_lines - 1) {
    cursor_row++;
    cursor_sync_rowcol();
  }
  sel_extend(old);
}
static void do_sel_home(void) {
  int old = cursor_pos;
  cursor_col = 0;
  cursor_sync_rowcol();
  sel_extend(old);
}
static void do_sel_end(void) {
  int old = cursor_pos;
  cursor_col = line_len(&g_buf, cursor_row);
  cursor_sync_rowcol();
  sel_extend(old);
}
static void do_sel_word_left(void) {
  int old = cursor_pos;
  if (cursor_pos > 0) {
    while (cursor_pos > 0 && IS_HSPACE(gb_get(&g_buf, cursor_pos - 1)))
      cursor_pos--;
    if (cursor_pos > 0) {
      if (IS_WORD(gb_get(&g_buf, cursor_pos - 1))) {
        while (cursor_pos > 0 && IS_WORD(gb_get(&g_buf, cursor_pos - 1)))
          cursor_pos--;
      } else
        cursor_pos--;
    }
    cursor_sync_pos();
  }
  sel_extend(old);
}
static void do_sel_word_right(void) {
  int old = cursor_pos;
  int len = gb_len(&g_buf);
  if (cursor_pos < len) {
    if (IS_WORD(gb_get(&g_buf, cursor_pos))) {
      while (cursor_pos < len && IS_WORD(gb_get(&g_buf, cursor_pos)))
        cursor_pos++;
    } else if (!IS_HSPACE(gb_get(&g_buf, cursor_pos)))
      cursor_pos++;
    while (cursor_pos < len && IS_HSPACE(gb_get(&g_buf, cursor_pos)))
      cursor_pos++;
    cursor_sync_pos();
  }
  sel_extend(old);
}
static void do_sel_file_top(void) {
  int old = cursor_pos;
  cursor_pos = 0;
  cursor_row = 0;
  cursor_col = 0;
  scroll_row = 0;
  scroll_col = 0;
  sel_extend(old);
}
static void do_sel_file_bot(void) {
  int old = cursor_pos;
  cursor_row = num_lines - 1;
  cursor_col = line_len(&g_buf, cursor_row);
  cursor_sync_rowcol();
  sel_extend(old);
}
static void do_select_all(void) {
  sel_anchor = 0;
  sel_active = gb_len(&g_buf);
  cursor_pos = sel_active;
  cursor_sync_pos();
}

/* ================================================================
 * Character map picker
 * Ctrl+Cat opens a modal grid of special/ASCII characters.
 * Returns the selected char (>0), or 0 if cancelled.
 * ================================================================ */

static const char charmap_chars[] = "()[]{}<>"
                                    "+-*/\\%^&|~!"
                                    "=<>!?"
                                    ".,;:@#$"
                                    "\"'`"
                                    "_"
                                    "0123456789ABCDEF"
                                    "#@!^";

#define CM_CELL_W (GFX_CHAR_W + 4)
#define CM_CELL_H (GFX_FONT_H + 4)
#define CM_COLS 16
#define CM_WIN_PAD 6
#define CM_TITLE_H 12
#define CM_BTN_H 12

static char charmap_pick(void) {
  static char chars[128];
  int nchars = 0;
  for (int i = 0; charmap_chars[i] && nchars < 127; i++) {
    char c = charmap_chars[i];
    int dup = 0;
    for (int j = 0; j < nchars; j++)
      if (chars[j] == c) {
        dup = 1;
        break;
      }
    if (!dup)
      chars[nchars++] = c;
  }

  int nrows = (nchars + CM_COLS - 1) / CM_COLS;
  int grid_w = CM_COLS * CM_CELL_W + CM_WIN_PAD * 2;
  int grid_h = nrows * CM_CELL_H + CM_WIN_PAD * 2;
  int win_w = grid_w + 2;
  int win_h = CM_TITLE_H + grid_h + CM_BTN_H + 2;

  if (win_w > GFX_W - 4)
    win_w = GFX_W - 4;
  if (win_h > GFX_H - 4)
    win_h = GFX_H - 4;

  int wx = (GFX_W - win_w) / 2;
  int wy = (GFX_H - win_h) / 2;
  int gx = wx + 1 + CM_WIN_PAD;
  int gy = wy + 1 + CM_TITLE_H + CM_WIN_PAD;
  int sel = 0;

  while (any_key_pressed())
    msleep(20);

  int redraw = 1;
  for (;;) {
    if (redraw) {
      gfx_fillrect(wx + 3, wy + 3, win_w, win_h, g_default_theme.border_dark);
      gfx_borderrect(wx, wy, win_w, win_h, g_default_theme.bg,
                     g_default_theme.border_light);
      gfx_fillrect(wx + 1, wy + 1, win_w - 2, CM_TITLE_H,
                   g_default_theme.title_bg);
      gfx_drawstr_clipped(wx + 4, wy + 1 + (CM_TITLE_H - GFX_FONT_H) / 2,
                          "Special characters", g_default_theme.title_fg,
                          g_default_theme.title_bg, win_w - 8);
      gfx_fillrect(wx + 1, wy + 1 + CM_TITLE_H, win_w - 2,
                   win_h - CM_TITLE_H - CM_BTN_H - 1, g_default_theme.bg);
      int hy = wy + win_h - CM_BTN_H - 1;
      gfx_hline(wx + 1, hy, win_w - 2, g_default_theme.border_light);
      gfx_fillrect(wx + 1, hy + 1, win_w - 2, CM_BTN_H - 1, g_default_theme.bg);
      gfx_drawstr_clipped(wx + 4, hy + 2,
                          "Arrows:move  Enter:insert  Esc:cancel",
                          g_default_theme.fg, g_default_theme.bg, win_w - 8);

      for (int i = 0; i < nchars; i++) {
        int row = i / CM_COLS;
        int col = i % CM_COLS;
        int cx = gx + col * CM_CELL_W;
        int cy = gy + row * CM_CELL_H;
        int is_sel = (i == sel);
        uint16_t fg = is_sel ? g_default_theme.accent_text : g_default_theme.fg;
        uint16_t cbg =
            is_sel ? g_default_theme.accent : g_default_theme.item_bg;
        gfx_fillrect(cx, cy, CM_CELL_W - 1, CM_CELL_H - 1, cbg);
        int char_x = cx + (CM_CELL_W - 1 - GFX_CHAR_W) / 2;
        int char_y = cy + (CM_CELL_H - 1 - GFX_FONT_H) / 2;
        gfx_drawchar(char_x, char_y, chars[i], fg, cbg);
      }

      {
        char preview[4] = {'\'', chars[sel], '\'', '\0'};
        int px = wx + win_w - 4 - (int)strlen(preview) * GFX_CHAR_W;
        gfx_drawstr(px, wy + 1 + (CM_TITLE_H - GFX_FONT_H) / 2, preview,
                    g_default_theme.accent, g_default_theme.title_bg);
      }

      gfx_flip();
      redraw = 0;
    }

    NavAction nav = gfx_poll_nav();
    if (nav == NAV_NONE) {
      msleep(16);
      idle();
      continue;
    }

    if (nav == NAV_UP) {
      if (sel >= CM_COLS)
        sel -= CM_COLS;
      redraw = 1;
    } else if (nav == NAV_DOWN) {
      if (sel + CM_COLS < nchars)
        sel += CM_COLS;
      else
        sel = nchars - 1;
      redraw = 1;
    } else if (nav == NAV_LEFT) {
      if (sel > 0)
        sel--;
      redraw = 1;
    } else if (nav == NAV_RIGHT) {
      if (sel < nchars - 1)
        sel++;
      redraw = 1;
    } else if (nav == NAV_ENTER) {
      while (any_key_pressed())
        msleep(20);
      return chars[sel];
    } else if (nav == NAV_ESC) {
      while (any_key_pressed())
        msleep(20);
      return 0;
    }
  }
}

/* ================================================================
 * ARM Mnemonic Catalog
 * Opened by KEY_NSPIRE_CAT (alone) or from the editor menu.
 * Shows categorized mnemonics in a scrollable list with collapsible
 * category headers.  Selecting a mnemonic inserts it at cursor.
 * Returns the selected mnemonic string, or NULL if cancelled.
 * ================================================================ */

/* ================================================================
 * ARM Mnemonic database
 * Each entry carries: lowercase name, argument signature, description.
 * ================================================================ */
typedef struct {
  const char *name;  /* lowercase, inserted on Enter   */
  const char *args;  /* short signature shown on row   */
  const char *desc;  /* full description for popup     */
  const char *flags; /* CPSR flag effects (N,Z,C,V)    */
} MnemInfo;

static const MnemInfo db_move[] = {
    {"mov", "Rd, Op2", "Move: Rd = Op2.",
     "With 'S': updates N,Z, C from shifter, V unchanged."},
    {"mvn", "Rd, Op2", "Move NOT: Rd = ~Op2.",
     "With 'S': updates N,Z, C from shifter, V unchanged."},
    {"mrs", "Rd, cpsr|spsr", "Move PSR to Register: Rd = CPSR or SPSR.",
     "No flags modified."},
    {"msr", "cpsr|spsr_<flg>, Op",
     "Move Register to PSR: CPSR/SPSR = Rm or #imm.",
     "Updates CPSR flags directly if fields include 'f'."},
};
static const MnemInfo db_arith[] = {
    {"add", "Rd, Rn, Op2", "Add: Rd = Rn + Op2.", "With 'S': updates N,Z,C,V."},
    {"adc", "Rd, Rn, Op2", "Add with Carry: Rd = Rn + Op2 + C.",
     "With 'S': updates N,Z,C,V."},
    {"sub", "Rd, Rn, Op2", "Subtract: Rd = Rn - Op2.",
     "With 'S': updates N,Z,C,V."},
    {"sbc", "Rd, Rn, Op2", "Subtract with Carry: Rd = Rn - Op2 - NOT(C).",
     "With 'S': updates N,Z,C,V."},
    {"rsb", "Rd, Rn, Op2", "Reverse Subtract: Rd = Op2 - Rn.",
     "With 'S': updates N,Z,C,V."},
    {"rsc", "Rd, Rn, Op2",
     "Reverse Subtract with Carry: Rd = Op2 - Rn - NOT(C).",
     "With 'S': updates N,Z,C,V."},
    {"mul", "Rd, Rm, Rs", "Multiply (32-bit): Rd = Rm * Rs.",
     "With 'S': updates N,Z. C,V unpredictable."},
    {"mla", "Rd, Rm, Rs, Rn", "Multiply Accumulate: Rd = (Rm * Rs) + Rn.",
     "With 'S': updates N,Z. C,V unpredictable."},
    {"umull", "RdLo, RdHi, Rm, Rs",
     "Unsigned Long Multiply: {RdHi,RdLo} = Rm * Rs.",
     "With 'S': updates N,Z. C,V unpredictable."},
    {"umlal", "RdLo, RdHi, Rm, Rs",
     "Unsigned Long Multiply Accum: {RdHi,RdLo} += Rm * Rs.",
     "With 'S': updates N,Z. C,V unpredictable."},
    {"smull", "RdLo, RdHi, Rm, Rs",
     "Signed Long Multiply: {RdHi,RdLo} = Rm * Rs.",
     "With 'S': updates N,Z. C,V unpredictable."},
    {"smlal", "RdLo, RdHi, Rm, Rs",
     "Signed Long Multiply Accum: {RdHi,RdLo} += Rm * Rs.",
     "With 'S': updates N,Z. C,V unpredictable."},
    {"clz", "Rd, Rm", "Count Leading Zeros: Rd = number of 0s at MSB of Rm.",
     "No flags modified."},
};
static const MnemInfo db_logic[] = {
    {"and", "Rd, Rn, Op2", "Bitwise AND: Rd = Rn & Op2.",
     "With 'S': updates N,Z, C from shifter, V unchanged."},
    {"orr", "Rd, Rn, Op2", "Bitwise OR: Rd = Rn | Op2.",
     "With 'S': updates N,Z, C from shifter, V unchanged."},
    {"eor", "Rd, Rn, Op2", "Bitwise Exclusive OR: Rd = Rn ^ Op2.",
     "With 'S': updates N,Z, C from shifter, V unchanged."},
    {"bic", "Rd, Rn, Op2", "Bit Clear: Rd = Rn & ~Op2.",
     "With 'S': updates N,Z, C from shifter, V unchanged."},
};
static const MnemInfo db_cmp[] = {
    {"cmp", "Rn, Op2", "Compare: computes Rn - Op2.",
     "Always updates N,Z,C,V."},
    {"cmn", "Rn, Op2", "Compare Negative: computes Rn + Op2.",
     "Always updates N,Z,C,V."},
    {"tst", "Rn, Op2", "Test: computes Rn & Op2.",
     "Always updates N,Z, C from shifter, V unchanged."},
    {"teq", "Rn, Op2", "Test Equivalence: computes Rn ^ Op2.",
     "Always updates N,Z, C from shifter, V unchanged."},
};
static const MnemInfo db_branch[] = {
    {"b", "label", "Branch: PC = label.", "No flags modified."},
    {"bl", "label", "Branch with Link: LR = PC + 4, PC = label.",
     "No flags modified."},
    {"bx", "Rm", "Branch and Exchange: PC = Rm, switch to Thumb if Rm[0]=1.",
     "Updates CPSR T-bit if switching modes."},
};
static const MnemInfo db_ldr[] = {
    {"ldr", "Rd, [Rn, Op]", "Load Word: Rd = [mem32].", "No flags modified."},
    {"ldrb", "Rd, [Rn, Op]", "Load Byte: Rd = ZeroExt([mem8]).",
     "No flags modified."},
    {"ldrh", "Rd, [Rn, Op]", "Load Halfword: Rd = ZeroExt([mem16]).",
     "No flags modified."},
    {"ldrsb", "Rd, [Rn, Op]", "Load Signed Byte: Rd = SignExt([mem8]).",
     "No flags modified."},
    {"ldrsh", "Rd, [Rn, Op]", "Load Signed Halfword: Rd = SignExt([mem16]).",
     "No flags modified."},
    {"ldrt", "Rd, [Rn]", "Load Word Unprivileged: Rd = [mem32].",
     "No flags modified."},
    {"ldrbt", "Rd, [Rn]", "Load Byte Unprivileged: Rd = ZeroExt([mem8]).",
     "No flags modified."},
};
static const MnemInfo db_str[] = {
    {"str", "Rd, [Rn, Op]", "Store Word: [mem32] = Rd.", "No flags modified."},
    {"strb", "Rd, [Rn, Op]", "Store Byte: [mem8] = Rd[7:0].",
     "No flags modified."},
    {"strh", "Rd, [Rn, Op]", "Store Halfword: [mem16] = Rd[15:0].",
     "No flags modified."},
    {"strt", "Rd, [Rn]", "Store Word Unprivileged: [mem32] = Rd.",
     "No flags modified."},
    {"strbt", "Rd, [Rn]", "Store Byte Unprivileged: [mem8] = Rd[7:0].",
     "No flags modified."},
};
static const MnemInfo db_ldm[] = {
    {"ldm", "Rn{!}, reglist",
     "Load Multiple: load registers from [Rn] (default IA).",
     "No flags modified (unless PC loaded with ^: restores CPSR)."},
    {"ldmia", "Rn{!}, reglist", "Load Multiple, Increment After (same as LDM).",
     "No flags modified."},
    {"ldmib", "Rn{!}, reglist", "Load Multiple, Increment Before.",
     "No flags modified."},
    {"ldmda", "Rn{!}, reglist", "Load Multiple, Decrement After.",
     "No flags modified."},
    {"ldmdb", "Rn{!}, reglist", "Load Multiple, Decrement Before.",
     "No flags modified."},
    {"ldmfd", "Rn{!}, reglist",
     "Load Multiple, Full Descending (alias for LDMIA).", "No flags modified."},
    {"ldmed", "Rn{!}, reglist",
     "Load Multiple, Empty Descending (alias for LDMIB).",
     "No flags modified."},
    {"ldmfa", "Rn{!}, reglist",
     "Load Multiple, Full Ascending (alias for LDMDA).", "No flags modified."},
    {"ldmea", "Rn{!}, reglist",
     "Load Multiple, Empty Ascending (alias for LDMDB).", "No flags modified."},
};
static const MnemInfo db_stm[] = {
    {"stm", "Rn{!}, reglist",
     "Store Multiple: store registers to [Rn] (default IA).",
     "No flags modified."},
    {"stmia", "Rn{!}, reglist",
     "Store Multiple, Increment After (same as STM).", "No flags modified."},
    {"stmib", "Rn{!}, reglist", "Store Multiple, Increment Before.",
     "No flags modified."},
    {"stmda", "Rn{!}, reglist", "Store Multiple, Decrement After.",
     "No flags modified."},
    {"stmdb", "Rn{!}, reglist", "Store Multiple, Decrement Before.",
     "No flags modified."},
    {"stmfd", "Rn{!}, reglist",
     "Store Multiple, Full Descending (alias for STMDB).",
     "No flags modified."},
    {"stmed", "Rn{!}, reglist",
     "Store Multiple, Empty Descending (alias for STMDA).",
     "No flags modified."},
    {"stmfa", "Rn{!}, reglist",
     "Store Multiple, Full Ascending (alias for STMIB).", "No flags modified."},
    {"stmea", "Rn{!}, reglist",
     "Store Multiple, Empty Ascending (alias for STMIA).",
     "No flags modified."},
};
static const MnemInfo db_cop[] = {
    {"mcr", "cp, op, Rd, CRn, CRm, op2",
     "Move to Coprocessor from ARM Register.", "No ARM flags modified."},
    {"mrc", "cp, op, Rd, CRn, CRm, op2",
     "Move to ARM Register from Coprocessor.",
     "If Rd=R15, updates ARM N,Z,C,V flags."},
    {"swp", "Rd, Rm, [Rn]", "Swap Word: atomic memory read and write.",
     "No flags modified."},
    {"swpb", "Rd, Rm, [Rn]", "Swap Byte: atomic memory read and write.",
     "No flags modified."},
};
static const MnemInfo db_misc[] = {
    {"swi", "#imm", "Software Interrupt (legacy): triggers SVC exception.",
     "No flags modified."},
    {"svc", "#imm", "Supervisor Call: triggers SVC exception.",
     "No flags modified."},
    {"adr", "Rd, label", "Load PC-relative address into Rd.",
     "No flags modified."},
};
static const MnemInfo db_shifts[] = {
    {"lsl", "Rm, #n|Rs", "Logical Shift Left: Rd = Rm << Op.",
     "Used as Op2: updates C. With 'S': updates N,Z,C."},
    {"lsr", "Rm, #n|Rs", "Logical Shift Right: Rd = Rm >> Op.",
     "Used as Op2: updates C. With 'S': updates N,Z,C."},
    {"asr", "Rm, #n|Rs",
     "Arithmetic Shift Right: Rd = Rm >> Op (sign-extended).",
     "Used as Op2: updates C. With 'S': updates N,Z,C."},
    {"ror", "Rm, #n|Rs", "Rotate Right: Rd = Rm rotated by Op.",
     "Used as Op2: updates C. With 'S': updates N,Z,C."},
    {"rrx", "Rm", "Rotate Right Extended: Rd = (C << 31) | (Rm >> 1).",
     "Used as Op2: updates C. With 'S': updates N,Z,C."},
};

typedef struct {
  const char *name;
  const MnemInfo *mnems;
  int count;
  int expanded;
} CatalogCat;

#define NCATS 12
static CatalogCat g_cats[NCATS];
static int g_cats_init = 0;

#define ASIZE(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void catalog_init_cats(void) {
  if (g_cats_init)
    return;
  int i = 0;
  g_cats[i++] = (CatalogCat){"Data Transfer", db_move, ASIZE(db_move), 1};
  g_cats[i++] = (CatalogCat){"Arithmetic", db_arith, ASIZE(db_arith), 1};
  g_cats[i++] = (CatalogCat){"Logic", db_logic, ASIZE(db_logic), 1};
  g_cats[i++] = (CatalogCat){"Comparison", db_cmp, ASIZE(db_cmp), 1};
  g_cats[i++] = (CatalogCat){"Branch", db_branch, ASIZE(db_branch), 1};
  g_cats[i++] = (CatalogCat){"Load (single)", db_ldr, ASIZE(db_ldr), 1};
  g_cats[i++] = (CatalogCat){"Store (single)", db_str, ASIZE(db_str), 1};
  g_cats[i++] = (CatalogCat){"Load (multiple)", db_ldm, ASIZE(db_ldm), 0};
  g_cats[i++] = (CatalogCat){"Store (multiple)", db_stm, ASIZE(db_stm), 0};
  g_cats[i++] = (CatalogCat){"Coprocessor", db_cop, ASIZE(db_cop), 0};
  g_cats[i++] = (CatalogCat){"Miscellaneous", db_misc, ASIZE(db_misc), 1};
  g_cats[i++] = (CatalogCat){"Shift Operators", db_shifts, ASIZE(db_shifts), 1};
  g_cats_init = 1;
}

#define CAT_MAX_ROWS 256

typedef struct {
  int is_cat;
  int cat_idx;
  int mnem_idx;
} CatRow;

static CatRow cat_rows[CAT_MAX_ROWS];
static int cat_nrows;

static void catalog_build_rows(void) {
  cat_nrows = 0;
  for (int c = 0; c < NCATS && cat_nrows < CAT_MAX_ROWS; c++) {
    cat_rows[cat_nrows++] = (CatRow){1, c, 0};
    if (g_cats[c].expanded) {
      for (int m = 0; m < g_cats[c].count && cat_nrows < CAT_MAX_ROWS; m++)
        cat_rows[cat_nrows++] = (CatRow){0, c, m};
    }
  }
}

#define CAT_WIN_X 10
#define CAT_WIN_Y 6
#define CAT_WIN_W (GFX_W - 20)
#define CAT_WIN_H (GFX_H - 12)
#define CAT_TITLE_H 12
#define CAT_HINT_H 11
#define CAT_ROW_H 9
#define CAT_LIST_Y (CAT_WIN_Y + 1 + CAT_TITLE_H)
#define CAT_LIST_H (CAT_WIN_H - CAT_TITLE_H - CAT_HINT_H - 2)
#define CAT_ROWS_VIS (CAT_LIST_H / CAT_ROW_H)
#define CAT_INDENT 10
/* Column where the args string starts (in pixels from window left interior) */
#define CAT_ARGS_X (CAT_WIN_X + 1 + CAT_INDENT + 7 * GFX_CHAR_W)

/* Draw the mnemonic description popup over the catalog.
   Wraps the description text across multiple lines.           */
static void catalog_show_desc(const MnemInfo *mi) {
  const int PW = 220, TITLE_H = 12, PAD = 5, BTN_H = 12;
  int line_w = (PW - 2 * PAD) / GFX_CHAR_W;
  if (line_w < 1)
    line_w = 1;

  const char *desc = mi->desc;
  int dlen = (int)strlen(desc);
  char lines[6][64];
  int nlines = 0;
  int pos = 0;
  while (pos < dlen && nlines < 6) {
    int take = dlen - pos;
    if (take > line_w) {
      take = line_w;
      while (take > 1 && desc[pos + take - 1] != ' ')
        take--;
      if (take <= 1)
        take = line_w;
    }
    int copy = take < 63 ? take : 63;
    strncpy(lines[nlines], desc + pos, copy);
    lines[nlines][copy] = '\0';
    int tl = (int)strlen(lines[nlines]);
    while (tl > 0 && lines[nlines][tl - 1] == ' ')
      lines[nlines][--tl] = '\0';
    nlines++;
    pos += take;
    while (pos < dlen && desc[pos] == ' ')
      pos++;
  }

  int body_lines = nlines + (mi->args[0] ? 2 : 0); /* args + blank + desc */
  int body_h = body_lines * GFX_FONT_H + 2 * PAD;
  int win_h = TITLE_H + body_h + BTN_H + 2;
  int win_w = PW;
  int wx = (GFX_W - win_w) / 2;
  int wy = (GFX_H - win_h) / 2;

  gfx_fillrect(wx + 3, wy + 3, win_w, win_h, g_default_theme.border_dark);
  gfx_borderrect(wx, wy, win_w, win_h, g_default_theme.bg,
                 g_default_theme.border_light);
  gfx_fillrect(wx + 1, wy + 1, win_w - 2, TITLE_H, g_default_theme.title_bg);

  char title[32];
  snprintf(title, sizeof(title), "%s", mi->name);
  gfx_drawstr_clipped(wx + PAD, wy + 1 + (TITLE_H - GFX_FONT_H) / 2, title,
                      g_default_theme.title_fg, g_default_theme.title_bg,
                      win_w - 2 * PAD);

  gfx_fillrect(wx + 1, wy + 1 + TITLE_H, win_w - 2, body_h, g_default_theme.bg);

  int ty = wy + 1 + TITLE_H + PAD;

  if (mi->args[0]) {
    char arg_line[80];
    snprintf(arg_line, sizeof(arg_line), "  %s", mi->args);
    gfx_drawstr_clipped(wx + PAD, ty, arg_line, g_default_theme.accent,
                        g_default_theme.bg, win_w - 2 * PAD);
    ty += GFX_FONT_H + 2; /* small gap */
  }

  for (int i = 0; i < nlines; i++) {
    gfx_drawstr_clipped(wx + PAD, ty, lines[i], g_default_theme.fg,
                        g_default_theme.bg, win_w - 2 * PAD);
    ty += GFX_FONT_H;
  }

  int hy = wy + win_h - BTN_H - 1;
  gfx_hline(wx + 1, hy, win_w - 2, g_default_theme.border_light);
  gfx_fillrect(wx + 1, hy + 1, win_w - 2, BTN_H - 1, g_default_theme.bg);
  gfx_drawstr_clipped(wx + PAD, hy + 2, "Any key: close", g_default_theme.fg,
                      g_default_theme.bg, win_w - 2 * PAD);

  gfx_flip();

  while (!any_key_pressed()) {
    msleep(16);
    idle();
  }
  while (any_key_pressed())
    msleep(20);
}

static void catalog_draw(int sel, int scroll) {
  uint16_t WIN_BG = g_default_theme.bg;
  uint16_t WIN_FG = g_default_theme.fg;
  uint16_t TITLE_BG = g_default_theme.title_bg;
  uint16_t TITLE_FG = g_default_theme.title_fg;
  uint16_t SEL_BG = g_default_theme.accent;
  uint16_t SEL_FG = g_default_theme.accent_text;
  uint16_t CAT_FG = g_default_theme.accent;
  uint16_t CAT_BG = g_default_theme.item_bg;
  uint16_t BORDER = g_default_theme.border_light;
  uint16_t SHADOW = g_default_theme.border_dark;
  uint16_t ARGS_FG = g_default_theme.accent; /* args colour when not selected */

  gfx_fillrect(CAT_WIN_X + 3, CAT_WIN_Y + 3, CAT_WIN_W, CAT_WIN_H, SHADOW);
  gfx_borderrect(CAT_WIN_X, CAT_WIN_Y, CAT_WIN_W, CAT_WIN_H, WIN_BG, BORDER);
  gfx_fillrect(CAT_WIN_X + 1, CAT_WIN_Y + 1, CAT_WIN_W - 2, CAT_TITLE_H,
               TITLE_BG);
  gfx_drawstr_clipped(
      CAT_WIN_X + 4, CAT_WIN_Y + 1 + (CAT_TITLE_H - GFX_FONT_H) / 2,
      "ARM Instruction Catalog", TITLE_FG, TITLE_BG, CAT_WIN_W - 8);

  for (int vi = 0; vi < CAT_ROWS_VIS; vi++) {
    int ri = scroll + vi;
    int row_y = CAT_LIST_Y + vi * CAT_ROW_H;

    if (ri >= cat_nrows) {
      gfx_fillrect(CAT_WIN_X + 1, row_y, CAT_WIN_W - 2, CAT_ROW_H, WIN_BG);
      continue;
    }

    CatRow *cr = &cat_rows[ri];
    int is_sel = (ri == sel);

    if (cr->is_cat) {
      uint16_t bg = is_sel ? SEL_BG : CAT_BG;
      uint16_t fg = is_sel ? SEL_FG : CAT_FG;
      gfx_fillrect(CAT_WIN_X + 1, row_y, CAT_WIN_W - 2, CAT_ROW_H, bg);
      char indicator = g_cats[cr->cat_idx].expanded ? '-' : '+';
      gfx_drawchar(CAT_WIN_X + 3, row_y + 1, indicator, fg, bg);
      gfx_drawstr_clipped(CAT_WIN_X + 3 + GFX_CHAR_W + 2, row_y + 1,
                          g_cats[cr->cat_idx].name, fg, bg, CAT_WIN_W - 14);
    } else {
      const MnemInfo *mi = &g_cats[cr->cat_idx].mnems[cr->mnem_idx];
      uint16_t bg = is_sel ? SEL_BG : WIN_BG;
      uint16_t fg = is_sel ? SEL_FG : WIN_FG;
      uint16_t afg = is_sel ? SEL_FG : ARGS_FG;
      gfx_fillrect(CAT_WIN_X + 1, row_y, CAT_WIN_W - 2, CAT_ROW_H, bg);
      gfx_drawstr_clipped(CAT_WIN_X + 1 + CAT_INDENT, row_y + 1, mi->name, fg,
                          bg, CAT_ARGS_X - (CAT_WIN_X + 1 + CAT_INDENT) - 2);
      if (mi->args[0])
        gfx_drawstr_clipped(CAT_ARGS_X, row_y + 1, mi->args, afg, bg,
                            CAT_WIN_X + CAT_WIN_W - 6 - CAT_ARGS_X);
    }
  }

  if (cat_nrows > CAT_ROWS_VIS) {
    int bar_total = CAT_LIST_H;
    int bar_h = bar_total * CAT_ROWS_VIS / cat_nrows;
    if (bar_h < 4)
      bar_h = 4;
    int ms = cat_nrows - CAT_ROWS_VIS;
    int bar_y = CAT_LIST_Y + (bar_total - bar_h) * scroll / (ms > 0 ? ms : 1);
    gfx_fillrect(CAT_WIN_X + CAT_WIN_W - 4, CAT_LIST_Y, 3, bar_total, CAT_BG);
    gfx_fillrect(CAT_WIN_X + CAT_WIN_W - 4, bar_y, 3, bar_h, BORDER);
  }

  int hy = CAT_WIN_Y + CAT_WIN_H - CAT_HINT_H - 1;
  gfx_hline(CAT_WIN_X + 1, hy, CAT_WIN_W - 2, BORDER);
  gfx_fillrect(CAT_WIN_X + 1, hy + 1, CAT_WIN_W - 2, CAT_HINT_H - 1, WIN_BG);
  gfx_drawstr_clipped(CAT_WIN_X + 4, hy + 2,
                      "Enter:insert  Shift:desc  +/-:expand  Esc:close", WIN_FG,
                      WIN_BG, CAT_WIN_W - 8);

  gfx_flip();
}

static const char *catalog_pick(void) {
  catalog_init_cats();
  catalog_build_rows();
  int sel = 0, scroll = 0;

  while (any_key_pressed())
    msleep(20);
  catalog_draw(sel, scroll);

  for (;;) {
    NavAction nav = gfx_poll_nav();
    if (nav == NAV_NONE) {
      msleep(16);
      idle();
      continue;
    }

    int shift = isKeyPressed(KEY_NSPIRE_SHIFT);

    if (nav == NAV_ESC || nav == NAV_CAT) {
      while (any_key_pressed())
        msleep(20);
      return NULL;
    } else if (nav == NAV_UP) {
      if (sel > 0) {
        sel--;
        if (sel < scroll)
          scroll = sel;
      }
    } else if (nav == NAV_DOWN) {
      if (sel < cat_nrows - 1) {
        sel++;
        if (sel >= scroll + CAT_ROWS_VIS)
          scroll = sel - CAT_ROWS_VIS + 1;
      }
    } else if (nav == NAV_LEFT) {
      CatRow *cr = &cat_rows[sel];
      if (g_cats[cr->cat_idx].expanded) {
        g_cats[cr->cat_idx].expanded = 0;
        catalog_build_rows();
        for (int i = 0; i < cat_nrows; i++)
          if (cat_rows[i].is_cat && cat_rows[i].cat_idx == cr->cat_idx) {
            sel = i;
            break;
          }
        if (sel < scroll)
          scroll = sel;
      }
    } else if (nav == NAV_RIGHT) {
      CatRow *cr = &cat_rows[sel];
      if (!g_cats[cr->cat_idx].expanded) {
        g_cats[cr->cat_idx].expanded = 1;
        catalog_build_rows();
      }
    } else if (nav == NAV_ENTER) {
      CatRow *cr = &cat_rows[sel];
      if (cr->is_cat) {
        g_cats[cr->cat_idx].expanded = !g_cats[cr->cat_idx].expanded;
        int save_cat = cr->cat_idx;
        catalog_build_rows();
        for (int i = 0; i < cat_nrows; i++)
          if (cat_rows[i].is_cat && cat_rows[i].cat_idx == save_cat) {
            sel = i;
            break;
          }
        if (sel >= cat_nrows)
          sel = cat_nrows - 1;
        if (sel < scroll)
          scroll = sel;
        if (sel >= scroll + CAT_ROWS_VIS)
          scroll = sel - CAT_ROWS_VIS + 1;
      } else {
        if (shift) {
          catalog_show_desc(&g_cats[cr->cat_idx].mnems[cr->mnem_idx]);
        } else {
          return g_cats[cr->cat_idx].mnems[cr->mnem_idx].name;
        }
      }
    }
    catalog_draw(sel, scroll);
  }
}

/* ================================================================
 * Editor settings (re-uses the nstudio settings form)
 * Forward-declared here; identical logic to action_settings() in
 * nstudio.c but embedded in the editor so the editor can call it
 * without coupling to nstudio.c.
 * ================================================================ */

typedef struct {
  NStudioSettings original;
  NStudioSettings current_frame;
} EditorSettingsCtx;

static void editor_settings_tick(GfxWindow *win) {
  EditorSettingsCtx *ctx = (EditorSettingsCtx *)win->user_data;

  if (g_settings.theme != ctx->current_frame.theme) {
    if (g_settings.theme == 0)
      settings_theme_dark(&g_settings);
    else if (g_settings.theme == 1)
      settings_theme_light(&g_settings);
  } else if (g_settings.theme != 2) {
    NStudioSettings *c = &g_settings;
    NStudioSettings *p = &ctx->current_frame;
    if (c->ui_bg != p->ui_bg || c->ui_fg != p->ui_fg ||
        c->ui_border_light != p->ui_border_light ||
        c->ui_border_dark != p->ui_border_dark ||
        c->ui_title_bg != p->ui_title_bg || c->ui_title_fg != p->ui_title_fg ||
        c->ui_accent != p->ui_accent ||
        c->ui_accent_text != p->ui_accent_text ||
        c->ui_item_bg != p->ui_item_bg || c->syn.mnem != p->syn.mnem ||
        c->syn.reg != p->syn.reg || c->syn.imm != p->syn.imm ||
        c->syn.label != p->syn.label || c->syn.comment != p->syn.comment ||
        c->syn.directive != p->syn.directive ||
        c->syn.string != p->syn.string || c->syn.normal != p->syn.normal) {
      g_settings.theme = 2;
    }
  }

  settings_apply_theme();

  int changed =
      (memcmp(&g_settings, &ctx->original, sizeof(NStudioSettings)) != 0);
  win->children[win->num_children - 2]->disabled = !changed;
  win->children[win->num_children - 1]->disabled = !changed;

  ctx->current_frame = g_settings;
}

static void editor_open_settings(void) {
  static const char *theme_opts[] = {"Dark", "Light", "Custom"};
  const char *color_opts[32];
  for (int i = 0; i < g_palette_size && i < 32; i++)
    color_opts[i] = g_palette[i].name;

  const int row_h = 24, col1 = 10, col2 = 140, ww = 110;
  int yy = 10;

  GfxWidget *w_beh_hdr =
      widget_create_heading(col1, yy, 280 - col1 * 2, row_h, "Behaviour");
  yy += row_h;
  GfxWidget *w_tab_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Tab Width:");
  GfxWidget *w_tab_val =
      widget_create_number(col2, yy, ww, row_h, &g_settings.tab_width, 1, 8);
  yy += row_h;
  GfxWidget *w_ind_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Auto Indent:");
  GfxWidget *w_ind_val =
      widget_create_toggle(col2, yy, ww, row_h, &g_settings.auto_indent);
  yy += row_h;
  GfxWidget *w_syn_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Syntax Colors:");
  GfxWidget *w_syn_val =
      widget_create_toggle(col2, yy, ww, row_h, &g_settings.syntax_highlight);
  yy += row_h;
  GfxWidget *w_ext_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "ASM Extension:");
  GfxWidget *w_ext_val = widget_create_text(
      col2, yy, ww, row_h, g_settings.asm_extension, 16, "Edit ASM Extension:");
  yy += row_h;
  yy += 10;

  GfxWidget *w_app_hdr =
      widget_create_heading(col1, yy, 280 - col1 * 2, row_h, "Appearance");
  yy += row_h;
  GfxWidget *w_thm_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Base Theme:");
  GfxWidget *w_thm_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.theme, theme_opts, 3);
  yy += row_h;
  GfxWidget *w_bg_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Background:");
  GfxWidget *w_bg_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.ui_bg, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_fg_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Text:");
  GfxWidget *w_fg_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.ui_fg, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_acc_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Accent:");
  GfxWidget *w_acc_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.ui_accent, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_atx_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Accent Text:");
  GfxWidget *w_atx_val =
      widget_create_dropdown(col2, yy, ww, row_h, &g_settings.ui_accent_text,
                             color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_ttb_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Title Bar:");
  GfxWidget *w_ttb_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.ui_title_bg, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_ttf_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Title Text:");
  GfxWidget *w_ttf_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.ui_title_fg, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_itm_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Gutter/Shade:");
  GfxWidget *w_itm_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.ui_item_bg, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_brl_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Border Light:");
  GfxWidget *w_brl_val =
      widget_create_dropdown(col2, yy, ww, row_h, &g_settings.ui_border_light,
                             color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_brd_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Border Dark:");
  GfxWidget *w_brd_val =
      widget_create_dropdown(col2, yy, ww, row_h, &g_settings.ui_border_dark,
                             color_opts, g_palette_size);
  yy += row_h;
  yy += 10;

  GfxWidget *w_syx_hdr =
      widget_create_heading(col1, yy, 280 - col1 * 2, row_h, "Syntax");
  yy += row_h;
  GfxWidget *w_nrm_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Normal Text:");
  GfxWidget *w_nrm_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.normal, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_mnm_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Mnemonics:");
  GfxWidget *w_mnm_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.mnem, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_reg_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Registers:");
  GfxWidget *w_reg_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.reg, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_imm_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Immediates:");
  GfxWidget *w_imm_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.imm, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_lbl_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Labels:");
  GfxWidget *w_lbl_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.label, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_cmt_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Comments:");
  GfxWidget *w_cmt_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.comment, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_dir_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Directives:");
  GfxWidget *w_dir_val =
      widget_create_dropdown(col2, yy, ww, row_h, &g_settings.syn.directive,
                             color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_str_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Strings:");
  GfxWidget *w_str_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.string, color_opts, g_palette_size);
  yy += row_h + 14;

  GfxWidget *btn_apply = widget_create_button(10, yy, 125, row_h, "Apply");
  GfxWidget *btn_restore = widget_create_button(145, yy, 125, row_h, "Restore");

  GfxWidget *children[] = {
      w_beh_hdr,   w_tab_lbl, w_tab_val, w_ind_lbl, w_ind_val, w_syn_lbl,
      w_syn_val,   w_ext_lbl, w_ext_val, w_app_hdr, w_thm_lbl, w_thm_val,
      w_bg_lbl,    w_bg_val,  w_fg_lbl,  w_fg_val,  w_acc_lbl, w_acc_val,
      w_atx_lbl,   w_atx_val, w_ttb_lbl, w_ttb_val, w_ttf_lbl, w_ttf_val,
      w_itm_lbl,   w_itm_val, w_brl_lbl, w_brl_val, w_brd_lbl, w_brd_val,
      w_syx_hdr,   w_nrm_lbl, w_nrm_val, w_mnm_lbl, w_mnm_val, w_reg_lbl,
      w_reg_val,   w_imm_lbl, w_imm_val, w_lbl_lbl, w_lbl_val, w_cmt_lbl,
      w_cmt_val,   w_dir_lbl, w_dir_val, w_str_lbl, w_str_val, btn_apply,
      btn_restore,
  };
  int num_widgets = (int)(sizeof(children) / sizeof(GfxWidget *));

  EditorSettingsCtx ctx;
  ctx.original = g_settings;
  ctx.current_frame = g_settings;

  GfxWindow win;
  win.x = (GFX_W - 280) / 2;
  win.y = (GFX_H - 220) / 2;
  win.w = 280;
  win.h = 220;
  win.title = "Preferences";
  win.theme = NULL;
  win.children = children;
  win.num_children = num_widgets;
  win.focused_idx = 2;
  win.scroll_y = 0;
  win.user_data = &ctx;
  win.on_tick = editor_settings_tick;

  for (;;) {
    int result = gfx_window_exec(&win);

    if (result == num_widgets - 2) {
      ctx.original = g_settings;
      settings_save();
      win.focused_idx = 2;
    } else if (result == num_widgets - 1) {
      g_settings = ctx.original;
      ctx.current_frame = g_settings;
      settings_apply_theme();
      win.focused_idx = 2;
    } else {
      int changed =
          (memcmp(&g_settings, &ctx.original, sizeof(NStudioSettings)) != 0);
      if (changed) {
        static const char *body[] = {"You have unapplied changes.",
                                     "What would you like to do?"};
        int choice = gfx_window_confirm3("Unsaved Changes", body, 2, "Apply",
                                         "Discard", "Cancel");
        if (choice == 0) {
          ctx.original = g_settings;
          settings_save();
          break;
        } else if (choice == 1) {
          g_settings = ctx.original;
          settings_apply_theme();
          break;
        }
      } else {
        break;
      }
    }
  }

  for (int i = 0; i < num_widgets; i++)
    free(children[i]);
}

/* ================================================================
 * Cheat sheet
 *
 * Reads the word under the cursor, looks it up in the MnemInfo
 * database (case-insensitive), and shows a themed modal popup with:
 *   - Instruction name (title bar)
 *   - Syntax / argument signature
 *   - Description
 *   - CPSR flag effects
 * ================================================================ */

/* Look up a mnemonic name (any case, with optional condition-code suffix)
   in the full database.  Returns the MnemInfo, or NULL if not found.    */
static const MnemInfo *cheatsheet_lookup(const char *word, int wlen) {
  catalog_init_cats();
  for (int c = 0; c < NCATS; c++) {
    for (int m = 0; m < g_cats[c].count; m++) {
      const MnemInfo *mi = &g_cats[c].mnems[m];
      int nl = (int)strlen(mi->name);
      if (nl == wlen && strncaseeq(mi->name, word, wlen))
        return mi;
    }
  }
  /* Try stripping a 2-char condition-code suffix (e.g. MOVEQ -> MOV) */
  static const char *cc[] = {"EQ", "NE", "CS", "CC", "MI", "PL",
                             "VS", "VC", "HI", "LS", "GE", "LT",
                             "GT", "LE", "AL", "HS", "LO", NULL};
  if (wlen > 2) {
    for (int i = 0; cc[i]; i++) {
      if (strncaseeq(word + wlen - 2, cc[i], 2)) {
        int base_len = wlen - 2;
        for (int c = 0; c < NCATS; c++) {
          for (int m = 0; m < g_cats[c].count; m++) {
            const MnemInfo *mi = &g_cats[c].mnems[m];
            int nl = (int)strlen(mi->name);
            if (nl == base_len && strncaseeq(mi->name, word, base_len))
              return mi;
          }
        }
      }
    }
  }
  /* Also try stripping 'S' suffix (e.g. ADDS -> ADD) */
  if (wlen > 1 && (word[wlen - 1] == 'S' || word[wlen - 1] == 's')) {
    int base_len = wlen - 1;
    for (int c = 0; c < NCATS; c++) {
      for (int m = 0; m < g_cats[c].count; m++) {
        const MnemInfo *mi = &g_cats[c].mnems[m];
        int nl = (int)strlen(mi->name);
        if (nl == base_len && strncaseeq(mi->name, word, base_len))
          return mi;
      }
    }
    /* Condition code + S (e.g. ADDEQS) */
    if (wlen > 3) {
      for (int i = 0; cc[i]; i++) {
        if (strncaseeq(word + wlen - 3, cc[i], 2)) {
          int base_len2 = wlen - 3;
          for (int c = 0; c < NCATS; c++) {
            for (int m = 0; m < g_cats[c].count; m++) {
              const MnemInfo *mi = &g_cats[c].mnems[m];
              int nl = (int)strlen(mi->name);
              if (nl == base_len2 && strncaseeq(mi->name, word, base_len2))
                return mi;
            }
          }
        }
      }
    }
  }
  return NULL;
}

/* Wrap text at word boundaries and draw into the popup body area.
   Returns the number of lines consumed.                           */
static int cheatsheet_draw_wrapped(const char *text, int wx, int ty, int max_w,
                                   int max_lines, uint16_t fg, uint16_t bg) {
  int line_chars = max_w / GFX_CHAR_W;
  if (line_chars < 1)
    line_chars = 1;
  int pos = 0, nlines = 0;
  int len = (int)strlen(text);
  while (pos < len && nlines < max_lines) {
    int take = len - pos;
    if (take > line_chars) {
      take = line_chars;
      while (take > 1 && text[pos + take - 1] != ' ')
        take--;
      if (take <= 1)
        take = line_chars;
    }
    while (take > 0 && text[pos] == ' ') {
      pos++;
      take--;
    }
    char linebuf[128];
    int copy = take < 127 ? take : 127;
    strncpy(linebuf, text + pos, copy);
    linebuf[copy] = '\0';
    gfx_drawstr_clipped(wx, ty + nlines * GFX_FONT_H, linebuf, fg, bg, max_w);
    pos += take;
    nlines++;
  }
  return nlines;
}

static void editor_cheatsheet(void) {
  if (cursor_row >= num_lines)
    return;
  extract_line(cursor_row);
  const char *line = line_scratch;
  int len = (int)strlen(line);

  int col = cursor_col;
  if (col > len)
    col = len;
  int ws = col;
  while (ws > 0 &&
         (isalnum((unsigned char)line[ws - 1]) || line[ws - 1] == '_'))
    ws--;
  int we = col;
  while (we < len && (isalnum((unsigned char)line[we]) || line[we] == '_'))
    we++;
  int wlen = we - ws;

  if (wlen == 0) {
    const char *body[] = {"No instruction found under the cursor.",
                          "Place cursor on a mnemonic and try again."};
    gfx_window_alert("Cheat Sheet", body, 2, "OK");
    return;
  }

  const MnemInfo *mi = cheatsheet_lookup(line + ws, wlen);
  if (!mi) {
    char msg[80];
    snprintf(msg, sizeof(msg), "'%.*s' is not a known ARM instruction.",
             wlen > 32 ? 32 : wlen, line + ws);
    const char *body[] = {msg};
    gfx_window_alert("Cheat Sheet", body, 1, "OK");
    return;
  }

  const int WIN_W = GFX_W - 16;
  const int WIN_X = 8;
  const int WIN_Y = 8;
  const int TITLE_H = 12;
  const int PAD = 5;
  const int HINT_H = 11;
  const int INNER_W = WIN_W - 2 * PAD - 2;
  const int LINE_H = GFX_FONT_H;
  const int SEC_GAP = 3; /* pixels between sections */

  int body_h = PAD + LINE_H + SEC_GAP /* "Syntax:" label */
               + LINE_H + SEC_GAP     /* args value */
               + LINE_H + SEC_GAP     /* "Description:" label */
               + 3 * LINE_H + SEC_GAP /* desc (up to 3 wrapped lines) */
               + LINE_H + SEC_GAP     /* "Flags:" label */
               + 3 * LINE_H           /* flags (up to 3 wrapped lines) */
               + PAD;
  int WIN_H = TITLE_H + body_h + HINT_H + 2;
  if (WIN_H > GFX_H - 8)
    WIN_H = GFX_H - 8;

  gfx_fillrect(WIN_X + 3, WIN_Y + 3, WIN_W, WIN_H, g_default_theme.border_dark);
  gfx_borderrect(WIN_X, WIN_Y, WIN_W, WIN_H, g_default_theme.bg,
                 g_default_theme.border_light);

  gfx_fillrect(WIN_X + 1, WIN_Y + 1, WIN_W - 2, TITLE_H,
               g_default_theme.title_bg);
  char title[32];
  snprintf(title, sizeof(title), "%s", mi->name);
  for (int i = 0; title[i]; i++)
    title[i] = toupper((unsigned char)title[i]);
  gfx_drawstr_clipped(WIN_X + PAD, WIN_Y + 1 + (TITLE_H - GFX_FONT_H) / 2,
                      title, g_default_theme.title_fg, g_default_theme.title_bg,
                      WIN_W - 2 * PAD);

  int body_top = WIN_Y + 1 + TITLE_H;
  int body_bot = WIN_Y + WIN_H - HINT_H - 1;
  gfx_fillrect(WIN_X + 1, body_top, WIN_W - 2, body_bot - body_top,
               g_default_theme.bg);

  int tx = WIN_X + 1 + PAD;
  int ty = body_top + PAD;
  uint16_t BG = g_default_theme.bg;
  uint16_t FG = g_default_theme.fg;
  uint16_t LABEL = g_default_theme.accent;

  gfx_drawstr_clipped(tx, ty, "Syntax:", LABEL, BG, INNER_W);
  ty += LINE_H;
  if (mi->args[0]) {
    char argbuf[80];
    snprintf(argbuf, sizeof(argbuf), "%s  %s", mi->name, mi->args);
    int ni = 0;
    while (argbuf[ni] && argbuf[ni] != ' ') {
      argbuf[ni] = toupper((unsigned char)argbuf[ni]);
      ni++;
    }
    gfx_drawstr_clipped(tx, ty, argbuf, FG, BG, INNER_W);
  } else {
    gfx_drawstr_clipped(tx, ty, mi->name, FG, BG, INNER_W);
  }
  ty += LINE_H + SEC_GAP;

  gfx_drawstr_clipped(tx, ty, "Description:", LABEL, BG, INNER_W);
  ty += LINE_H;
  int used = cheatsheet_draw_wrapped(mi->desc, tx, ty, INNER_W, 4, FG, BG);
  ty += used * LINE_H + SEC_GAP;

  if (ty + LINE_H < body_bot) {
    gfx_drawstr_clipped(tx, ty, "CPSR Flags:", LABEL, BG, INNER_W);
    ty += LINE_H;
    cheatsheet_draw_wrapped(mi->flags, tx, ty, INNER_W, 3, FG, BG);
  }

  int hy = WIN_Y + WIN_H - HINT_H - 1;
  gfx_hline(WIN_X + 1, hy, WIN_W - 2, g_default_theme.border_light);
  gfx_fillrect(WIN_X + 1, hy + 1, WIN_W - 2, HINT_H - 1, BG);
  gfx_drawstr_clipped(WIN_X + PAD, hy + 2, "Any key: close", FG, BG,
                      WIN_W - 2 * PAD);

  gfx_flip();
  while (!any_key_pressed()) {
    msleep(16);
    idle();
  }
  while (any_key_pressed())
    msleep(20);
}

/* ================================================================
 * File browser
 *
 * filebrowser_pick_file(start_dir, out, outsz)
 *   Shows a scrollable directory navigator.  The user navigates into
 *   subdirectories with Enter / click, and selects a file with Enter.
 *   Returns 1 with the full path in `out`, or 0 on cancel.
 *
 * filebrowser_pick_dir(start_dir, out, outsz)
 *   Same, but Enter on a directory *selects* it rather than entering.
 *   Used by Save As to pick the destination folder.
 *
 * Both share the same drawing and navigation core.
 * ================================================================ */

#define FB_MAX_ENTRIES 256
#define FB_NAME_MAX 64
#define FB_ROW_H 10
#define FB_ROWS_VIS 18
#define FB_WIN_W (GFX_W - 20)
#define FB_WIN_X 10
#define FB_WIN_Y 4
#define FB_TITLE_H 12
#define FB_HINT_H 11
#define FB_LIST_H (FB_ROWS_VIS * FB_ROW_H)
#define FB_WIN_H (FB_TITLE_H + FB_LIST_H + FB_HINT_H + 4)
#define FB_LIST_Y (FB_WIN_Y + 1 + FB_TITLE_H)

typedef struct {
  char name[FB_NAME_MAX];
  int is_dir;
} FBEntry;

static FBEntry fb_entries[FB_MAX_ENTRIES];
static int fb_nentries;

/* strcmp comparator for FBEntry: dirs first, then files, both alpha */
static int fb_cmp(const void *a, const void *b) {
  const FBEntry *ea = (const FBEntry *)a;
  const FBEntry *eb = (const FBEntry *)b;
  if (ea->is_dir != eb->is_dir)
    return eb->is_dir - ea->is_dir;
  return strcmp(ea->name, eb->name);
}

static int fb_load_dir(const char *path, int pick_dir, int filter_asm) {
  fb_nentries = 0;
  DIR *d = opendir(path);
  if (!d)
    return 0;

  struct dirent *de;
  while ((de = readdir(d)) != NULL && fb_nentries < FB_MAX_ENTRIES) {
    if (de->d_name[0] == '.')
      continue;

    char full[512];
    snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
    struct stat st;
    int is_dir = 0;
    if (stat(full, &st) == 0) {
      is_dir = S_ISDIR(st.st_mode);
    }

    if (pick_dir && !is_dir) {
      continue;
    }

    if (!is_dir && !pick_dir && filter_asm) {
      int nlen = (int)strlen(de->d_name);
      int elen = (int)strlen(g_settings.asm_extension);
      if (nlen < elen + 5)
        continue; /* must be at least . + ext + .tns */

      const char *suffix = de->d_name + nlen - (elen + 5);
      if (suffix[0] != '.')
        continue;
      if (strncmp(suffix + 1, g_settings.asm_extension, elen) != 0)
        continue;
      if (strcmp(suffix + 1 + elen, ".tns") != 0)
        continue;
    }

    FBEntry *e = &fb_entries[fb_nentries];
    strncpy(e->name, de->d_name, FB_NAME_MAX - 1);
    e->name[FB_NAME_MAX - 1] = '\0';
    e->is_dir = is_dir;

    fb_nentries++;
  }
  closedir(d);

  qsort(fb_entries, fb_nentries, sizeof(FBEntry), fb_cmp);
  return 1;
}

static void fb_draw(const char *cwd, int sel, int scroll, int pick_dir,
                    int has_parent, int filter_asm) {
  uint16_t BG = g_default_theme.bg;
  uint16_t FG = g_default_theme.fg;
  uint16_t SEL_BG = g_default_theme.accent;
  uint16_t SEL_FG = g_default_theme.accent_text;
  uint16_t DIR_FG = g_default_theme.accent;
  uint16_t DIM_FG = g_default_theme.border_light;
  uint16_t BD = g_default_theme.border_light;

  gfx_fillrect(FB_WIN_X + 3, FB_WIN_Y + 3, FB_WIN_W, FB_WIN_H,
               g_default_theme.border_dark);
  gfx_borderrect(FB_WIN_X, FB_WIN_Y, FB_WIN_W, FB_WIN_H, BG, BD);

  gfx_fillrect(FB_WIN_X + 1, FB_WIN_Y + 1, FB_WIN_W - 2, FB_TITLE_H,
               g_default_theme.title_bg);

  char disp_path[512];
  const char *clean_cwd = cwd;
  while (clean_cwd[0] == '/' && clean_cwd[1] == '/')
    clean_cwd++;
  snprintf(disp_path, sizeof(disp_path), "/%s", clean_cwd);

  gfx_drawstr_clipped(
      FB_WIN_X + 4, FB_WIN_Y + 1 + (FB_TITLE_H - GFX_FONT_H) / 2, disp_path,
      g_default_theme.title_fg, g_default_theme.title_bg, FB_WIN_W - 8);

  gfx_fillrect(FB_WIN_X + 1, FB_LIST_Y, FB_WIN_W - 2, FB_LIST_H, BG);

  int base = 0;
  if (has_parent) {
    int ry = FB_LIST_Y;
    int is_sel = (sel == -1);
    uint16_t rbg = is_sel ? SEL_BG : BG;
    uint16_t rfg = is_sel ? SEL_FG : DIR_FG;
    gfx_fillrect(FB_WIN_X + 1, ry, FB_WIN_W - 2, FB_ROW_H, rbg);
    gfx_drawstr_clipped(FB_WIN_X + 4, ry + 1, "../  (parent)", rfg, rbg,
                        FB_WIN_W - 8);
    base = 1;
  }

  for (int vi = 0; vi < FB_ROWS_VIS - base; vi++) {
    int ri = scroll + vi;
    int ry = FB_LIST_Y + (vi + base) * FB_ROW_H;
    if (ri >= fb_nentries) {
      gfx_fillrect(FB_WIN_X + 1, ry, FB_WIN_W - 2, FB_ROW_H, BG);
      continue;
    }
    int is_sel = (sel == ri);
    uint16_t rbg = is_sel ? SEL_BG : BG;
    uint16_t rfg = is_sel ? SEL_FG : (fb_entries[ri].is_dir ? DIR_FG : FG);

    gfx_fillrect(FB_WIN_X + 1, ry, FB_WIN_W - 2, FB_ROW_H, rbg);
    gfx_drawstr_clipped(FB_WIN_X + 4, ry + 1, fb_entries[ri].name, rfg, rbg,
                        FB_WIN_W - 8);
  }

  if (fb_nentries > FB_ROWS_VIS - base) {
    int bt = FB_LIST_H - base * FB_ROW_H;
    int vis = FB_ROWS_VIS - base;
    int bh = bt * vis / fb_nentries;
    if (bh < 4)
      bh = 4;
    int ms = fb_nentries - vis;
    int by =
        FB_LIST_Y + base * FB_ROW_H + (bt - bh) * scroll / (ms > 0 ? ms : 1);
    gfx_fillrect(FB_WIN_X + FB_WIN_W - 5, FB_LIST_Y + base * FB_ROW_H, 3, bt,
                 g_default_theme.item_bg);
    gfx_fillrect(FB_WIN_X + FB_WIN_W - 5, by, 3, bh, BD);
  }

  int hy = FB_WIN_Y + FB_WIN_H - FB_HINT_H - 1;
  gfx_hline(FB_WIN_X + 1, hy, FB_WIN_W - 2, BD);
  gfx_fillrect(FB_WIN_X + 1, hy + 1, FB_WIN_W - 2, FB_HINT_H - 1, BG);

  char hint_buf[128];
  const char *hint;
  if (pick_dir) {
    hint = "Tab:save here  Enter:open  Esc:cancel";
  } else if (filter_asm) {
    hint = "Tab:all files  Enter:open  Esc:cancel";
  } else {
    snprintf(hint_buf, sizeof(hint_buf), "Tab:*.%s.tns  Enter:open  Esc:cancel",
             g_settings.asm_extension);
    hint = hint_buf;
  }
  gfx_drawstr_clipped(FB_WIN_X + 4, hy + 2, hint, DIM_FG, BG, FB_WIN_W - 8);

  gfx_flip();
}

/* Internal core: pick_dir=0 -> pick file, pick_dir=1 -> pick directory.
   Returns 1 and fills `out` on success, 0 on cancel.                   */
static int fb_run(const char *start, char *out, int outsz, int pick_dir) {
  char cwd[512];
  strncpy(cwd, start, sizeof(cwd) - 1);
  cwd[sizeof(cwd) - 1] = '\0';

  int filter_asm = 1;

  fb_load_dir(cwd, pick_dir, filter_asm);

  int has_parent = (strcmp(cwd, "/") != 0);
  int vis_rows = FB_ROWS_VIS - (has_parent ? 1 : 0);

  int sel = has_parent ? -1 : 0;
  int scroll = 0;

  while (any_key_pressed())
    msleep(20);
  fb_draw(cwd, sel, scroll, pick_dir, has_parent, filter_asm);

  for (;;) {
    NavAction nav = gfx_poll_nav();
    if (nav == NAV_NONE) {
      msleep(16);
      idle();
      continue;
    }

    if (nav == NAV_ESC) {
      while (any_key_pressed())
        msleep(20);
      return 0;

    } else if (nav == NAV_TAB) {
      if (pick_dir) {
        while (any_key_pressed())
          msleep(20);
        strncpy(out, cwd, outsz - 1);
        out[outsz - 1] = '\0';
        return 1;
      } else {
        filter_asm = !filter_asm;
        fb_load_dir(cwd, pick_dir, filter_asm);
        has_parent = (strcmp(cwd, "/") != 0);
        vis_rows = FB_ROWS_VIS - (has_parent ? 1 : 0);
        sel = has_parent ? -1 : 0;
        scroll = 0;
      }

    } else if (nav == NAV_UP) {
      if (sel == 0 && has_parent)
        sel = -1;
      else if (sel > 0)
        sel--;
      if (sel >= 0 && sel < scroll)
        scroll = sel;

    } else if (nav == NAV_DOWN) {
      if (sel == -1) {
        if (fb_nentries > 0)
          sel = 0;
      } else if (sel < fb_nentries - 1) {
        sel++;
      }
      if (sel >= scroll + vis_rows)
        scroll = sel - vis_rows + 1;

    } else if (nav == NAV_ENTER) {
      while (any_key_pressed())
        msleep(20);

      if (sel == -1) {
        char *slash = strrchr(cwd, '/');
        if (slash && slash != cwd)
          *slash = '\0';
        else
          strncpy(cwd, "/", sizeof(cwd) - 1);
        fb_load_dir(cwd, pick_dir, filter_asm);
        has_parent = (strcmp(cwd, "/") != 0);
        vis_rows = FB_ROWS_VIS - (has_parent ? 1 : 0);
        sel = has_parent ? -1 : 0;
        scroll = 0;
        fb_draw(cwd, sel, scroll, pick_dir, has_parent, filter_asm);
        continue;
      }

      if (sel < 0 || sel >= fb_nentries)
        continue;

      if (fb_entries[sel].is_dir) {
        if (strcmp(cwd, "/") == 0) {
          snprintf(cwd, sizeof(cwd), "/%s", fb_entries[sel].name);
        } else {
          snprintf(cwd + strlen(cwd), sizeof(cwd) - strlen(cwd), "/%s",
                   fb_entries[sel].name);
        }
        fb_load_dir(cwd, pick_dir, filter_asm);
        has_parent = (strcmp(cwd, "/") != 0);
        vis_rows = FB_ROWS_VIS - (has_parent ? 1 : 0);
        sel = has_parent ? -1 : 0;
        scroll = 0;

      } else if (!pick_dir) {
        if (strcmp(cwd, "/") == 0)
          snprintf(out, outsz, "/%s", fb_entries[sel].name);
        else
          snprintf(out, outsz, "%s/%s", cwd, fb_entries[sel].name);
        return 1;
      }
    }
    fb_draw(cwd, sel, scroll, pick_dir, has_parent, filter_asm);
  }
}

/* Public wrappers */
static int filebrowser_pick_file(const char *start, char *out, int outsz) {
  return fb_run(start, out, outsz, 0);
}
static int filebrowser_pick_dir(const char *start, char *out, int outsz) {
  return fb_run(start, out, outsz, 1);
}

/* ================================================================
 * File operations: Open and Save As
 *
 * Forward declaration needed because prompt_unsaved is defined later.
 * ================================================================ */
static int prompt_unsaved(void);

static void editor_open_file(void) {
  if (g_modified) {
    int choice = prompt_unsaved();
    if (choice == 1) {
      save_file(g_filepath);
      g_modified = 0;
    } else if (choice == 0)
      return; /* cancel */
  }

  char start_dir[512];
  strncpy(start_dir, g_filepath, sizeof(start_dir) - 1);
  start_dir[sizeof(start_dir) - 1] = '\0';
  char *slash = strrchr(start_dir, '/');
  if (slash)
    *slash = '\0';
  else
    strncpy(start_dir, "/documents", sizeof(start_dir) - 1);

  char newpath[1024] = "";
  if (!filebrowser_pick_file(start_dir, newpath, sizeof(newpath)))
    return;

  strncpy(g_filepath, newpath, sizeof(g_filepath) - 1);
  g_filepath[sizeof(g_filepath) - 1] = '\0';
  gb_free(&g_buf);
  load_file(g_filepath);
  cursor_pos = 0;
  cursor_row = 0;
  cursor_col = 0;
  scroll_row = 0;
  scroll_col = 0;
  sel_anchor = SEL_NONE;
  sel_active = 0;
  for (int i = 0; i < UNDO_MAX; i++) {
    snap_free(&g_undo_ring[i]);
    snap_free(&g_redo_ring[i]);
  }
  g_undo_head = 0;
  g_undo_count = 0;
  g_redo_head = 0;
  g_redo_count = 0;
  g_modified = 0;
}

/* Save As: pick a directory, then enter a filename; saves as dir/name.tns */
static void editor_save_as(void) {
  char start_dir[512];
  strncpy(start_dir, g_filepath, sizeof(start_dir) - 1);
  start_dir[sizeof(start_dir) - 1] = '\0';
  char *slash = strrchr(start_dir, '/');
  if (slash)
    *slash = '\0';
  else
    strncpy(start_dir, "/documents", sizeof(start_dir) - 1);

  char destdir[1024] = "";
  if (!filebrowser_pick_dir(start_dir, destdir, sizeof(destdir)))
    return;

  char fname[128] = "";
  {
    const char *base = strrchr(g_filepath, '/');
    base = base ? base + 1 : g_filepath;
    strncpy(fname, base, sizeof(fname) - 1);
    char *dot = strstr(fname, ".tns");
    if (dot)
      *dot = '\0';
  }
  if (!gfx_input_filename("Save As", "Filename (no extension):", fname,
                          sizeof(fname)))
    return;

  if (fname[0] == '\0') {
    const char *body[] = {"Filename cannot be empty."};
    gfx_window_alert("Save As", body, 1, "OK");
    return;
  }

  char newpath[2048];

  if (!strchr(fname, '.')) {
    snprintf(newpath, sizeof(newpath), "%s/%s.%s.tns", destdir, fname,
             g_settings.asm_extension);
  } else {
    int len = strlen(fname);
    if (len >= 4 && strcmp(fname + len - 4, ".tns") == 0) {
      snprintf(newpath, sizeof(newpath), "%s/%s", destdir, fname);
    } else {
      snprintf(newpath, sizeof(newpath), "%s/%s.tns", destdir, fname);
    }
  }

  if (save_file(newpath)) {
    strncpy(g_filepath, newpath, sizeof(g_filepath) - 1);
    g_filepath[sizeof(g_filepath) - 1] = '\0';
    g_modified = 0;
  } else {
    const char *body[] = {"Could not write to the specified path.",
                          "Check that the directory exists and is writable."};
    gfx_window_alert("Save As Failed", body, 2, "OK");
  }
}

/* ================================================================
 * Goto Line
 * ================================================================ */
static void editor_goto_line(void) {
  char buf[16];
  buf[0] = '\0';
  char prompt[48];
  snprintf(prompt, sizeof(prompt), "Line (1-%d):", num_lines);
  if (!gfx_input_filename("Go to Line", prompt, buf, sizeof(buf)))
    return;
  int ln = atoi(buf);
  if (ln < 1)
    ln = 1;
  if (ln > num_lines)
    ln = num_lines;
  cursor_row = ln - 1;
  cursor_col = 0;
  cursor_sync_rowcol();
  scroll_to_cursor();
}

/* ================================================================
 * Label browser
 *
 * Scans the gap buffer for label definitions (word immediately
 * followed by ':') and presents them in a scrollable picker.
 * Selecting a label moves the cursor to its definition line.
 *
 * Also provides label-lookup by name (used by jump-to-label).
 * ================================================================ */

#define MAX_LABELS 256
#define MAX_LABEL_LEN 64

typedef struct {
  char name[MAX_LABEL_LEN];
  int line; /* 0-based line index */
} LabelEntry;

static LabelEntry g_labels[MAX_LABELS];
static int g_nlabels;

/* Rebuild label table from the gap buffer. */
static void labels_scan(void) {
  g_nlabels = 0;
  for (int row = 0; row < num_lines && g_nlabels < MAX_LABELS; row++) {
    int start = line_starts[row];
    int ll = line_len(&g_buf, row);
    if (ll == 0)
      continue;
    char first = gb_get(&g_buf, start);
    if (!isalpha((unsigned char)first) && first != '_')
      continue;
    int i = 0;
    while (i < ll && (isalnum((unsigned char)gb_get(&g_buf, start + i)) ||
                      gb_get(&g_buf, start + i) == '_'))
      i++;
    int wlen = i;
    if (wlen == 0 || wlen >= MAX_LABEL_LEN)
      continue;
    char tmp[MAX_LABEL_LEN];
    for (int k = 0; k < wlen; k++)
      tmp[k] = gb_get(&g_buf, start + k);
    tmp[wlen] = '\0';
    if (is_reg(tmp, wlen) || is_mnem(tmp, wlen) || is_shift(tmp, wlen) ||
        is_directive(tmp, wlen))
      continue;
    memcpy(g_labels[g_nlabels].name, tmp, wlen + 1);
    g_labels[g_nlabels].line = row;
    g_nlabels++;
  }
}

/* Return the line (0-based) of the label with the given name,
   case-insensitive.  Returns -1 if not found. */
static int label_find(const char *name) {
  for (int i = 0; i < g_nlabels; i++) {
    if (strncaseeq(g_labels[i].name, name, MAX_LABEL_LEN))
      return g_labels[i].line;
  }
  return -1;
}

#define LBL_WIN_X 20
#define LBL_WIN_Y 10
#define LBL_WIN_W (GFX_W - 40)
#define LBL_WIN_H (GFX_H - 20)
#define LBL_TITLE_H 12
#define LBL_HINT_H 11
#define LBL_ROW_H 10
#define LBL_LIST_Y (LBL_WIN_Y + 1 + LBL_TITLE_H)
#define LBL_LIST_H (LBL_WIN_H - LBL_TITLE_H - LBL_HINT_H - 2)
#define LBL_ROWS_VIS (LBL_LIST_H / LBL_ROW_H)

static void labels_draw(int sel, int scroll) {
  uint16_t WIN_BG = g_default_theme.bg;
  uint16_t WIN_FG = g_default_theme.fg;
  uint16_t TIT_BG = g_default_theme.title_bg;
  uint16_t TIT_FG = g_default_theme.title_fg;
  uint16_t SEL_BG = g_default_theme.accent;
  uint16_t SEL_FG = g_default_theme.accent_text;
  uint16_t DIM_FG = g_default_theme.border_light;
  uint16_t BORDER = g_default_theme.border_light;
  uint16_t SHADOW = g_default_theme.border_dark;

  gfx_fillrect(LBL_WIN_X + 3, LBL_WIN_Y + 3, LBL_WIN_W, LBL_WIN_H, SHADOW);
  gfx_borderrect(LBL_WIN_X, LBL_WIN_Y, LBL_WIN_W, LBL_WIN_H, WIN_BG, BORDER);
  gfx_fillrect(LBL_WIN_X + 1, LBL_WIN_Y + 1, LBL_WIN_W - 2, LBL_TITLE_H,
               TIT_BG);

  char title[48];
  snprintf(title, sizeof(title), "Labels  (%d defined)", g_nlabels);
  gfx_drawstr_clipped(LBL_WIN_X + 4,
                      LBL_WIN_Y + 1 + (LBL_TITLE_H - GFX_FONT_H) / 2, title,
                      TIT_FG, TIT_BG, LBL_WIN_W - 8);

  if (g_nlabels == 0) {
    gfx_fillrect(LBL_WIN_X + 1, LBL_LIST_Y, LBL_WIN_W - 2,
                 LBL_WIN_H - LBL_TITLE_H - LBL_HINT_H - 2, WIN_BG);
    gfx_drawstr_clipped(LBL_WIN_X + 8, LBL_LIST_Y + 10,
                        "No labels defined in this file.", DIM_FG, WIN_BG,
                        LBL_WIN_W - 16);
  }

  for (int vi = 0; vi < LBL_ROWS_VIS; vi++) {
    int ri = scroll + vi;
    int row_y = LBL_LIST_Y + vi * LBL_ROW_H;

    if (ri >= g_nlabels) {
      gfx_fillrect(LBL_WIN_X + 1, row_y, LBL_WIN_W - 2, LBL_ROW_H, WIN_BG);
      continue;
    }

    int is_sel = (ri == sel);
    uint16_t bg = is_sel ? SEL_BG : WIN_BG;
    uint16_t fg = is_sel ? SEL_FG : WIN_FG;
    uint16_t lfg = is_sel ? SEL_FG : DIM_FG;

    gfx_fillrect(LBL_WIN_X + 1, row_y, LBL_WIN_W - 2, LBL_ROW_H, bg);

    gfx_drawstr_clipped(LBL_WIN_X + 4, row_y + 1, g_labels[ri].name, fg, bg,
                        LBL_WIN_W - 50);

    char lnbuf[16];
    snprintf(lnbuf, sizeof(lnbuf), "Ln %d", g_labels[ri].line + 1);
    int lnw = (int)strlen(lnbuf) * GFX_CHAR_W;
    gfx_drawstr(LBL_WIN_X + LBL_WIN_W - 6 - lnw, row_y + 1, lnbuf, lfg, bg);
  }

  if (g_nlabels > LBL_ROWS_VIS) {
    int bt = LBL_LIST_H;
    int bh = bt * LBL_ROWS_VIS / g_nlabels;
    if (bh < 4)
      bh = 4;
    int ms = g_nlabels - LBL_ROWS_VIS;
    int by = LBL_LIST_Y + (bt - bh) * scroll / (ms > 0 ? ms : 1);
    gfx_fillrect(LBL_WIN_X + LBL_WIN_W - 4, LBL_LIST_Y, 3, bt,
                 g_default_theme.item_bg);
    gfx_fillrect(LBL_WIN_X + LBL_WIN_W - 4, by, 3, bh, BORDER);
  }

  int hy = LBL_WIN_Y + LBL_WIN_H - LBL_HINT_H - 1;
  gfx_hline(LBL_WIN_X + 1, hy, LBL_WIN_W - 2, BORDER);
  gfx_fillrect(LBL_WIN_X + 1, hy + 1, LBL_WIN_W - 2, LBL_HINT_H - 1, WIN_BG);
  gfx_drawstr_clipped(LBL_WIN_X + 4, hy + 2, "Enter:jump  Esc:close", WIN_FG,
                      WIN_BG, LBL_WIN_W - 8);

  gfx_flip();
}

/* Open the label browser.  Returns 1 if cursor was moved. */
static int editor_label_browser(void) {
  labels_scan();

  int sel = 0;
  int scroll = 0;

  if (g_nlabels > 0) {
    int best = 0, bestd = abs(g_labels[0].line - cursor_row);
    for (int i = 1; i < g_nlabels; i++) {
      int d = abs(g_labels[i].line - cursor_row);
      if (d < bestd) {
        bestd = d;
        best = i;
      }
    }
    sel = best;
    scroll = sel - LBL_ROWS_VIS / 2;
    if (scroll < 0)
      scroll = 0;
    if (scroll > g_nlabels - LBL_ROWS_VIS && g_nlabels > LBL_ROWS_VIS)
      scroll = g_nlabels - LBL_ROWS_VIS;
  }

  while (any_key_pressed())
    msleep(20);
  labels_draw(sel, scroll);

  for (;;) {
    NavAction nav = gfx_poll_nav();
    if (nav == NAV_NONE) {
      msleep(16);
      idle();
      continue;
    }

    if (nav == NAV_ESC) {
      while (any_key_pressed())
        msleep(20);
      return 0;
    } else if (nav == NAV_UP) {
      if (sel > 0) {
        sel--;
        if (sel < scroll)
          scroll = sel;
      }
    } else if (nav == NAV_DOWN) {
      if (sel < g_nlabels - 1) {
        sel++;
        if (sel >= scroll + LBL_ROWS_VIS)
          scroll = sel - LBL_ROWS_VIS + 1;
      }
    } else if (nav == NAV_ENTER) {
      while (any_key_pressed())
        msleep(20);
      if (g_nlabels > 0) {
        cursor_row = g_labels[sel].line;
        cursor_col = 0;
        cursor_sync_rowcol();
        scroll_to_cursor();
        return 1;
      }
      return 0;
    }
    labels_draw(sel, scroll);
  }
}

/* ================================================================
 * Jump to label under cursor (for branch instructions)
 *
 * Called when the user presses ctrl+Enter while the cursor is on a
 * line whose first non-space token is a branch mnemonic
 * (B, BL, BX, BLX and their condition-code variants).
 *
 * If the target label is found the cursor jumps to it.
 * If the line is not a branch, or no label operand is found,
 * or the label does not exist, an appropriate error popup is shown.
 * ================================================================ */

/* Return 1 if the word (case-insensitive, length wlen) is a branch base. */
static int is_branch_base(const char *w, int wlen) {
  static const char *bases[] = {"BLX", "BL", "BX", "B", NULL};
  for (int i = 0; bases[i]; i++) {
    int bl = (int)strlen(bases[i]);
    if (wlen >= bl && strncaseeq(w, bases[i], bl)) {
      int rem = wlen - bl;
      if (rem == 0)
        return 1;
      if (rem == 2) {
        static const char *cc[] = {"EQ", "NE", "CS", "CC", "MI", "PL",
                                   "VS", "VC", "HI", "LS", "GE", "LT",
                                   "GT", "LE", "AL", "HS", "LO", NULL};
        for (int j = 0; cc[j]; j++)
          if (strncaseeq(w + bl, cc[j], 2))
            return 1;
      }
    }
  }
  return 0;
}

static void editor_jump_to_label(void) {
  extract_line(cursor_row);
  const char *line = line_scratch;
  int len = (int)strlen(line);

  int i = 0;
  while (i < len && (line[i] == ' ' || line[i] == '\t'))
    i++;

  int mstart = i;
  while (i < len && (isalnum((unsigned char)line[i]) || line[i] == '_'))
    i++;
  int mlen = i - mstart;

  if (mlen == 0 || !is_branch_base(line + mstart, mlen)) {
    static const char *body[] = {"This line is not a branch instruction.",
                                 "Place the cursor on a B/BL/BX/BLX line."};
    gfx_window_alert("Not a Branch", body, 2, "OK");
    return;
  }

  while (i < len && (line[i] == ' ' || line[i] == '\t'))
    i++;

  int ostart = i;
  while (i < len && line[i] != ' ' && line[i] != '\t' && line[i] != ',' &&
         line[i] != ';' && line[i] != '\0')
    i++;
  int olen = i - ostart;

  if (olen == 0) {
    static const char *body[] = {
        "No operand found on this branch instruction."};
    gfx_window_alert("No Operand", body, 1, "OK");
    return;
  }

  char opbuf[MAX_LABEL_LEN];
  if (olen >= MAX_LABEL_LEN)
    olen = MAX_LABEL_LEN - 1;
  strncpy(opbuf, line + ostart, olen);
  opbuf[olen] = '\0';

  if (is_reg(opbuf, olen)) {
    static const char *body[] = {"This branch uses a register operand,",
                                 "not a label — cannot jump to definition."};
    gfx_window_alert("Register Branch", body, 2, "OK");
    return;
  }

  labels_scan();
  int target = label_find(opbuf);
  if (target < 0) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Label \"%s\" not found in this file.", opbuf);
    const char *body[1] = {msg};
    gfx_window_alert("Label Not Found", body, 1, "OK");
    return;
  }

  cursor_row = target;
  cursor_col = 0;
  cursor_sync_rowcol();
  scroll_to_cursor();
}

/* ================================================================
 * Vertical popup menu
 * ================================================================ */

typedef struct {
  const char *label;
  const char **items;
  int nitems;
} MenuDef;

static const char *menu_file_items[] = {
    "Save (Ctrl+S)", "Save As (Ctrl+Shift+S)", "Open (Ctrl+O)", "---", "Close"};
static const char *menu_edit_items[] = {
    "Undo (Ctrl+Z)",       "Redo (Ctrl+Y)", "---",
    "Cut (Ctrl+X)",        "Copy (Ctrl+C)", "Paste (Ctrl+P)",
    "Select All (Ctrl+A)", "---",           "Search (Ctrl+F)",
    "Replace (Ctrl+H)"};
static const char *menu_nav_items[] = {
    "Go to Line (Ctrl+G)", "Go to Label (Ctrl+L)", "File Top", "File Bottom"};
static const char *menu_view_items[] = {"ARM Catalog", "Label Browser",
                                        "Instruction Help (Ctrl+Trig)"};
static const char *menu_settings_items[] = {"Preferences"};

static const MenuDef g_menus[] = {
    {"File", menu_file_items, 5},         {"Edit", menu_edit_items, 10},
    {"Navigate", menu_nav_items, 4},      {"View", menu_view_items, 3},
    {"Settings", menu_settings_items, 1},
};
#define NMENU ((int)(sizeof(g_menus) / sizeof(g_menus[0])))

#define MENU_ROW_H 10
#define MENU_PAD_X 4
#define MENU_ORIGIN_X 0
#define MENU_ORIGIN_Y 0

static int menu_panel_w(int max_label_len) {
  return (max_label_len + 4) * GFX_CHAR_W;
}

static int menu_is_sep(const char *item) {
  return item[0] == '-' && item[1] == '-' && item[2] == '-';
}

static void menu_draw_panel(int px, int py, int pw, const char **items,
                            int nitems, int sel_row, int active) {
  int ph = nitems * MENU_ROW_H + 2;
  gfx_fillrect(px + 3, py + 3, pw, ph, g_default_theme.border_dark);
  gfx_borderrect(px, py, pw, ph, g_default_theme.bg,
                 g_default_theme.border_light);

  for (int i = 0; i < nitems; i++) {
    int ry = py + 1 + i * MENU_ROW_H;
    if (menu_is_sep(items[i])) {
      gfx_fillrect(px + 1, ry, pw - 2, MENU_ROW_H, g_default_theme.bg);
      int mid = ry + MENU_ROW_H / 2;
      gfx_hline(px + 2, mid, pw - 4, g_default_theme.border_light);
    } else if (i == sel_row) {
      uint16_t bg = active ? g_default_theme.accent : g_default_theme.item_bg;
      uint16_t fg = active ? g_default_theme.accent_text : g_default_theme.fg;
      gfx_fillrect(px + 1, ry, pw - 2, MENU_ROW_H, bg);
      gfx_drawstr(px + 1 + MENU_PAD_X, ry + 1, items[i], fg, bg);
    } else {
      gfx_fillrect(px + 1, ry, pw - 2, MENU_ROW_H, g_default_theme.bg);
      gfx_drawstr(px + 1 + MENU_PAD_X, ry + 1, items[i], g_default_theme.fg,
                  g_default_theme.bg);
    }
  }
}

static int menu_toplevel_w(void) {
  int maxlen = 0;
  for (int i = 0; i < NMENU; i++) {
    int l = (int)strlen(g_menus[i].label);
    if (l > maxlen)
      maxlen = l;
  }
  return menu_panel_w(maxlen);
}

static int menu_sub_w(int idx) {
  int maxlen = 0;
  for (int i = 0; i < g_menus[idx].nitems; i++) {
    int l = (int)strlen(g_menus[idx].items[i]);
    if (l > maxlen)
      maxlen = l;
  }
  return menu_panel_w(maxlen);
}

static void menu_draw(int top_sel, int sub_sel, int in_sub) {
  render_editor_core();

  int lw = menu_toplevel_w();

  const char *top_labels[NMENU];
  for (int i = 0; i < NMENU; i++)
    top_labels[i] = g_menus[i].label;

  menu_draw_panel(MENU_ORIGIN_X, MENU_ORIGIN_Y, lw, top_labels, NMENU, top_sel,
                  !in_sub);

  {
    int ry = MENU_ORIGIN_Y + 1 + top_sel * MENU_ROW_H;
    uint16_t bg = !in_sub ? g_default_theme.accent : g_default_theme.item_bg;
    uint16_t fg = !in_sub ? g_default_theme.accent_text : g_default_theme.fg;
    gfx_drawchar(MENU_ORIGIN_X + lw - 2 - GFX_CHAR_W, ry + 1, '>', fg, bg);
  }

  int sw = menu_sub_w(top_sel);
  int sx = MENU_ORIGIN_X + lw;
  if (sx + sw > GFX_W)
    sx = MENU_ORIGIN_X - sw;
  int sy = MENU_ORIGIN_Y + top_sel * MENU_ROW_H;

  menu_draw_panel(sx, sy, sw, g_menus[top_sel].items, g_menus[top_sel].nitems,
                  in_sub ? sub_sel : -1, in_sub);

  gfx_flip();
}

/* ── Menu action dispatch ──
   Returns 1 = action done (close menu + redraw),
           2 = close editor requested.              */
static int menu_dispatch(int top, int sub) {
  if (top == 0) {
    if (sub == 0) {
      save_file(g_filepath);
      g_modified = 0;
      return 1;
    }
    if (sub == 1) {
      editor_save_as();
      return 1;
    }
    if (sub == 2) {
      editor_open_file();
      return 1;
    }
    if (sub == 4)
      return 2;
  }

  if (top == 1) {
    if (sub == 0) {
      do_undo();
      return 1;
    }
    if (sub == 1) {
      do_redo();
      return 1;
    }
    if (sub == 3) {
      undo_push();
      clipboard_copy(1);
      return 1;
    }
    if (sub == 4) {
      clipboard_copy(0);
      return 1;
    }
    if (sub == 5) {
      clipboard_paste();
      return 1;
    }
    if (sub == 6) {
      do_select_all();
      return 1;
    }
    if (sub == 8) {
      editor_search();
      return 1;
    }
    if (sub == 9) {
      editor_search_replace();
      return 1;
    }
  }

  if (top == 2) {
    if (sub == 0) {
      editor_goto_line();
      return 1;
    }
    if (sub == 1) {
      editor_label_browser();
      return 1;
    }
    if (sub == 2) {
      do_file_top();
      return 1;
    }
    if (sub == 3) {
      do_file_bot();
      return 1;
    }
  }

  if (top == 3) {
    if (sub == 0) {
      const char *m = catalog_pick();
      if (m)
        for (const char *p = m; *p; p++)
          do_insert_char(*p);
      return 1;
    }
    if (sub == 1) {
      editor_label_browser();
      return 1;
    }
  }

  if (top == 4) {
    if (sub == 0) {
      editor_open_settings();
      return 1;
    }
  }

  return 1;
}

/* Returns 0 normally, 1 if editor should close (File > Close chosen) */
static int menu_run(void) {
  int top_sel = 0;
  int sub_sel = 0;
  int in_sub = 0;
  int result = 0;

  while (any_key_pressed())
    msleep(20);

  menu_draw(top_sel, sub_sel, in_sub);

  for (;;) {
    NavAction nav = gfx_poll_nav();
    if (nav == NAV_NONE) {
      msleep(16);
      idle();
      continue;
    }

    if (nav == NAV_ESC) {
      while (any_key_pressed())
        msleep(20);
      break;
    } else if (nav == NAV_UP) {
      if (in_sub) {
        int n = g_menus[top_sel].nitems;
        do {
          sub_sel = (sub_sel - 1 + n) % n;
        } while (menu_is_sep(g_menus[top_sel].items[sub_sel]));
      } else {
        top_sel = (top_sel - 1 + NMENU) % NMENU;
        sub_sel = 0;
      }
    } else if (nav == NAV_DOWN) {
      if (in_sub) {
        int n = g_menus[top_sel].nitems;
        do {
          sub_sel = (sub_sel + 1) % n;
        } while (menu_is_sep(g_menus[top_sel].items[sub_sel]));
      } else {
        top_sel = (top_sel + 1) % NMENU;
        sub_sel = 0;
      }
    } else if (nav == NAV_RIGHT) {
      if (!in_sub) {
        in_sub = 1;
        sub_sel = 0;
        while (menu_is_sep(g_menus[top_sel].items[sub_sel]) &&
               sub_sel < g_menus[top_sel].nitems - 1)
          sub_sel++;
      }
    } else if (nav == NAV_LEFT) {
      if (in_sub)
        in_sub = 0;
    } else if (nav == NAV_ENTER) {
      if (!in_sub) {
        in_sub = 1;
        sub_sel = 0;
        while (any_key_pressed())
          msleep(20);
      } else {
        while (any_key_pressed())
          msleep(20);
        int r = menu_dispatch(top_sel, sub_sel);
        if (r == 2) {
          result = 1;
          break;
        }
        if (r == 1)
          break;
      }
    }
    menu_draw(top_sel, sub_sel, in_sub);
  }

  render_all();
  return result;
}

/* ================================================================
 * Unsaved-changes prompt
 * ================================================================ */
static int prompt_unsaved(void) {
  static const char *body[] = {"This file has unsaved changes.",
                               "What would you like to do?"};
  int choice = gfx_window_confirm3("Unsaved Changes", body, 2, "Save",
                                   "Don't Save", "Cancel");
  if (choice == 0)
    return 1;
  if (choice == 1)
    return 2;
  return 0;
}

/* ================================================================
 * Key-repeat helper
 * ================================================================ */
static int key_repeat_poll(void) {
  int action = poll_key();

  if (action == ACT_NONE) {
    last_action = ACT_NONE;
    repeat_timer = 0;
    return ACT_NONE;
  }

  if (action != last_action) {
    last_action = action;
    repeat_timer = 0;
    return action;
  }

  repeat_timer++;
  if (repeat_timer < REPEAT_DELAY)
    return ACT_NONE;
  if ((repeat_timer - REPEAT_DELAY) % REPEAT_RATE != 0)
    return ACT_NONE;
  return action;
}

/* ================================================================
 * Public entry point
 * ================================================================ */
int editor_open(const char *path) {
  if (path && path[0] != '\0') {
    strncpy(g_filepath, path, sizeof(g_filepath) - 1);
    g_filepath[sizeof(g_filepath) - 1] = '\0';
    load_file(path);
  } else {
    g_filepath[0] = '\0';
    gb_init(&g_buf);
    rebuild_lines(&g_buf);
  }

  g_modified = 0;

  load_file(path);

  cursor_pos = 0;
  cursor_row = 0;
  cursor_col = 0;
  scroll_row = 0;
  scroll_col = 0;

  sel_anchor = SEL_NONE;
  sel_active = 0;
  for (int i = 0; i < UNDO_MAX; i++) {
    snap_free(&g_undo_ring[i]);
    snap_free(&g_redo_ring[i]);
  }
  g_undo_head = 0;
  g_undo_count = 0;
  g_redo_head = 0;
  g_redo_count = 0;

  render_all();

  while (any_key_pressed())
    msleep(20);

  int saved = 0;
  int running = 1;

  while (running) {
    if (isKeyPressed(KEY_NSPIRE_MENU)) {
      int close = menu_run();
      if (close) {
        if (g_modified) {
          int choice = prompt_unsaved();
          if (choice == 1) {
            save_file(g_filepath);
            saved = 1;
          }
          if (choice != 0)
            running = 0;
        } else {
          running = 0;
        }
      }
      continue;
    }

    if (isKeyPressed(KEY_NSPIRE_CAT) && !isKeyPressed(KEY_NSPIRE_CTRL)) {
      while (any_key_pressed())
        msleep(20);
      const char *m = catalog_pick();
      if (m) {
        for (const char *p = m; *p; p++)
          do_insert_char(*p);
      }
      scroll_to_cursor();
      render_all();
      continue;
    }

    int act = key_repeat_poll();

    if (act == ACT_NONE) {
      msleep(16);
      idle();
      continue;
    }

    switch (act) {
    case ACT_ESC:
      if (g_modified) {
        int choice = prompt_unsaved();
        if (choice == 1) { /* Save */
          if (g_filepath[0] == '\0') {
            editor_save_as();
            if (!g_modified) {
              saved = 1;
              running = 0;
            }
          } else {
            save_file(g_filepath);
            g_modified = 0;
            saved = 1;
            running = 0;
          }
        } else if (choice == 2) { /* Discard */
          running = 0;
        }
      } else {
        running = 0;
      }
      break;

    case ACT_OPEN:
      editor_open_file();
      break;
    case ACT_SAVE:
      if (g_filepath[0] == '\0') {
        editor_save_as();
      } else {
        save_file(g_filepath);
        g_modified = 0;
        saved = 1;
      }
      break;
    case ACT_SAVE_AS:
      editor_save_as();
      break;

    case ACT_GOTO_LINE:
      editor_goto_line();
      break;
    case ACT_GOTO_LABEL:
      editor_label_browser();
      break;

    case ACT_ENTER:
      do_enter();
      break;
    case ACT_BS:
      do_backspace();
      break;
    case ACT_DEL:
      do_delete();
      break;
    case ACT_TAB:
      do_tab();
      break;
    case ACT_LEFT:
      do_left();
      break;
    case ACT_RIGHT:
      do_right();
      break;
    case ACT_UP:
      do_up();
      break;
    case ACT_DOWN:
      do_down();
      break;
    case ACT_HOME:
      do_home();
      break;
    case ACT_END:
      do_end();
      break;
    case ACT_PGUP:
      do_pgup();
      break;
    case ACT_PGDN:
      do_pgdn();
      break;
    case ACT_WORD_LEFT:
      do_word_left();
      break;
    case ACT_WORD_RIGHT:
      do_word_right();
      break;
    case ACT_FILE_TOP:
      do_file_top();
      break;
    case ACT_FILE_BOT:
      do_file_bot();
      break;
    case ACT_SEL_LEFT:
      do_sel_left();
      break;
    case ACT_SEL_RIGHT:
      do_sel_right();
      break;
    case ACT_SEL_UP:
      do_sel_up();
      break;
    case ACT_SEL_DOWN:
      do_sel_down();
      break;
    case ACT_SEL_HOME:
      do_sel_home();
      break;
    case ACT_SEL_END:
      do_sel_end();
      break;
    case ACT_SEL_WORD_LEFT:
      do_sel_word_left();
      break;
    case ACT_SEL_WORD_RIGHT:
      do_sel_word_right();
      break;
    case ACT_SEL_FILE_TOP:
      do_sel_file_top();
      break;
    case ACT_SEL_FILE_BOT:
      do_sel_file_bot();
      break;
    case ACT_COPY:
      clipboard_copy(0);
      break;
    case ACT_CUT:
      undo_push();
      clipboard_copy(1);
      break;
    case ACT_PASTE:
      clipboard_paste();
      break;
    case ACT_SEL_ALL:
      do_select_all();
      break;
    case ACT_UNDO:
      do_undo();
      break;
    case ACT_REDO:
      do_redo();
      break;

    case ACT_SEARCH:
      editor_search();
      break;
    case ACT_REPLACE:
      editor_search_replace();
      break;

    case ACT_CHARMAP: {
      char picked = charmap_pick();
      if (picked)
        do_insert_char(picked);
      break;
    }

    case ACT_CATALOG: {
      const char *m = catalog_pick();
      if (m) {
        for (const char *p = m; *p; p++)
          do_insert_char(*p);
      }
      break;
    }

    case ACT_JUMP_LABEL:
      editor_jump_to_label();
      break;

    case ACT_CHEATSHEET:
      editor_cheatsheet();
      break;

    default:
      if (act > 0 && act < 128)
        do_insert_char((char)act);
      break;
    }

    if (running) {
      scroll_to_cursor();
      render_all();
    }
  }

  gb_free(&g_buf);
  return saved;
}
