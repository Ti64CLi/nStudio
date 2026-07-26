/*
 * editor.c
 * ARM assembly text editor for TI-Nspire CX / CX II (Ndless)
 *
 * Architecture
 * ------------
 * Text is stored in a gap buffer (gapbuf.c):
 *   [before_gap | ---gap--- | after_gap]
 * The cursor is always at the start of the gap.
 * A line-start table is rebuilt after every edit (cheap for files up to
 * a few thousand lines).
 *
 * Syntax highlighting lives in syntax.c and follows the nasm assembler
 * exactly (classification is driven by nasm's own optab.c tables).
 * Note the nasm label rule: a label is any valid identifier in column 0
 * (letter first) - there is NO trailing colon, and instructions must be
 * indented.
 *
 * Modal dialogs that only browse the static reference tables - the character
 * map and the instruction and syscall catalogs - live in editor_ui.c.  They
 * return a selection and know nothing about the buffer or the cursor, so
 * acting on what they return (and the read-only confirmation that implies)
 * happens here.  Dialogs that do read editor state, such as the cheat sheet
 * and the label browser, are still below.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <keys.h>
#include <libndls.h>

#include "asmdb.h"
#include "asmdiag.h"
#include "browser.h"
#include "editor.h"
#include "editor_ui.h"
#include "fileio.h"
#include "gapbuf.h"
#include "gfx.h"
#include "settings.h"
#include "syntax.h"
#include "undo.h"
#include "util.h"

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
 * Colours: mapped to the UI settings
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


/* ================================================================
 * Document buffer (gap buffer + line table live in gapbuf.c)
 * ================================================================ */
static GapBuf g_buf;

/* ================================================================
 * Cursor management
 * ================================================================ */
static int cursor_pos;
static int cursor_row;
static int cursor_col;
static int scroll_row;
static int scroll_col;
/* Column the cursor tries to keep while moving vertically, so up/down
   across short lines does not permanently lose the horizontal position. */
static int cursor_goal_col;

static void cursor_sync_pos(void) {
  int i;
  int len = gb_len(&g_buf);
  if (cursor_pos > len)
    cursor_pos = len;
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
 * Jump-back stack
 *
 * Anything that moves the cursor somewhere the user did not navigate to by
 * hand - a label definition, a browser pick, go-to-line, a diagnostic's
 * related location - first records where they were, so they can step back
 * the way they came.  Positions are logical offsets and are clamped when
 * popped, so editing after a jump leaves them approximate rather than wrong;
 * that is friendlier than discarding the trail on every keystroke.
 *
 * Error cycling (Ctrl+N / Ctrl+P) deliberately does not push: it already
 * moves in both directions, and recording each step would bury the position
 * the user actually wants to come back to.
 * ================================================================ */
#define JUMP_STACK_MAX 16

static int g_jump_stack[JUMP_STACK_MAX];
static int g_jump_count;

/* Remember the current position as a place to come back to. */
static void jump_push(void) {
  if (g_jump_count == JUMP_STACK_MAX) {
    /* Full: drop the oldest so the most recent trail is the one kept. */
    memmove(g_jump_stack, g_jump_stack + 1,
            sizeof(g_jump_stack[0]) * (JUMP_STACK_MAX - 1));
    g_jump_count--;
  }
  g_jump_stack[g_jump_count++] = cursor_pos;
}

static void jump_stack_clear(void) { g_jump_count = 0; }

/* ================================================================
 * File I/O
 * ================================================================ */
static char g_filepath[512];
static int g_modified;
static int g_saved; /* set once any save succeeds during this editor session */
static int g_readonly; /* file opened read-only (not an asm source) */
static int g_crlf;     /* 0 = LF line endings, 1 = CRLF (write \r\n) */
/* One-line transient message shown in the status bar (e.g. the first assemble
   error after a jump); "" when none.  Dismissed on the next keypress. */
static char g_diag_status[224] = "";

/* Set whenever the buffer changes after a successful assemble, i.e. the built
   program no longer matches the source.  The calculator's filesystem does not
   report a usable modification time, so this - not the timestamps - is what
   makes the "older than the source" warning reliable.  It only tracks the
   current session: after opening a file we cannot know whether an existing
   build matches, so we start out not warning. */
static int g_build_stale;

/* Assemble diagnostics retained for next/previous-error navigation.  Populated
   after each assemble: g_diags holds the whole parsed batch, g_diag_nav holds
   the indices of those that land in the file currently open (line >= 1), and
   g_diag_cur is the current position within g_diag_nav (-1 when none).  Any
   buffer edit clears this (see diag_nav_reset callers), so navigation can
   never jump to a line the diagnostics no longer describe. */
static AsmDiag g_diags[ASMDIAG_MAX];
static int g_ndiags = 0;
static int g_diag_nav[ASMDIAG_MAX];
static int g_diag_nav_count = 0;
static int g_diag_cur = -1;

static void diag_nav_reset(void) {
  g_ndiags = 0;
  g_diag_nav_count = 0;
  g_diag_cur = -1;
}

static int path_is_asm_source(const char *path);

static int load_file(const char *path) {
  FILE *f = fopen(path, "rb");
  gb_init(&g_buf);
  g_crlf = 0;
  if (!f) {
    rebuild_lines(&g_buf);
    return 0;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  if (sz > 0) {
    char *tmp = (char *)malloc(sz);
    if (tmp) {
      size_t br = fread(tmp, 1, sz, f);
      if (path_is_asm_source(path)) {
        /* Editable source: normalise CRLF -> LF in the buffer and
           remember the file's style so it round-trips on save. */
        int out = 0;
        for (size_t i = 0; i < br; i++) {
          if (tmp[i] == '\r' && i + 1 < br && tmp[i + 1] == '\n') {
            g_crlf = 1;
            continue;
          }
          tmp[out++] = tmp[i];
        }
        gb_insert_n(&g_buf, tmp, out);
      } else {
        /* Byte-exact insert: NUL bytes survive, so an accidentally
           opened binary is not truncated and round-trips on save. */
        gb_insert_n(&g_buf, tmp, (int)br);
      }
      free(tmp);
    }
  }
  fclose(f);
  gb_move(&g_buf, 0);
  rebuild_lines(&g_buf);
  return 1;
}

/* Write a run of bytes, expanding '\n' to "\r\n" when in CRLF mode.
   Returns 1 on success, 0 if the stream reported a short write / error. */
static int write_bytes(FILE *f, const char *p, int n) {
  if (!g_crlf)
    return fwrite(p, 1, (size_t)n, f) == (size_t)n;

  for (int i = 0; i < n; i++) {
    if (p[i] == '\n' && fputc('\r', f) == EOF)
      return 0;
    if (fputc((unsigned char)p[i], f) == EOF)
      return 0;
  }
  return 1;
}

/* Writer for write_file_atomic: emit the gap buffer's two contiguous halves,
   translating line endings for CRLF files.  Returns 0 on a short write. */
static int gapbuf_writer(FILE *f, void *ctx) {
  (void)ctx;
  int ok = 1;
  if (g_buf.gap_lo > 0)
    ok = write_bytes(f, g_buf.buf, g_buf.gap_lo);
  int after_len = g_buf.size - g_buf.gap_hi;
  if (ok && after_len > 0)
    ok = write_bytes(f, g_buf.buf + g_buf.gap_hi, after_len);
  return ok;
}

/*
 * Save the buffer to `path` atomically: write_file_atomic() writes the content
 * to a verified sibling temporary and only then replaces the destination, so a
 * failed save can never truncate or corrupt existing work.
 *
 * Returns 1 only when `path` holds the fully written new content.
 */
static int save_file(const char *path) {
  return write_file_atomic(path, gapbuf_writer, NULL);
}

/* Does this filename look like an assembler source?
   Matches ".<ext>.tns" or bare ".<ext>", the same rule the nasm
   assembler applies to decide what it can build. */
static int path_is_asm_source(const char *path) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  int nlen = (int)strlen(base);
  int elen = (int)strlen(g_settings.asm_extension);
  if (elen == 0)
    return 1; /* no extension configured: treat everything as editable */

  char suf[64];
  snprintf(suf, sizeof(suf), ".%s.tns", g_settings.asm_extension);
  int sl = (int)strlen(suf);
  if (nlen >= sl && strcasecmp(base + nlen - sl, suf) == 0)
    return 1;

  snprintf(suf, sizeof(suf), ".%s", g_settings.asm_extension);
  sl = (int)strlen(suf);
  if (nlen >= sl && strcasecmp(base + nlen - sl, suf) == 0)
    return 1;

  return 0;
}

/* Ask before the first modification of a read-only file.
   Returns 1 if editing may proceed. */
static int editor_confirm_rw(void) {
  if (!g_readonly)
    return 1;
  static const char *body[] = {
      "This file was opened read-only because it",
      "does not match your ASM source extension.",
      "Enable editing anyway?"};
  if (gfx_window_confirm2("Read-only", body, 3, "Enable editing", "Cancel") ==
      0) {
    g_readonly = 0;
    return 1;
  }
  return 0;
}

/* ================================================================
 * Render
 * ================================================================ */
/* Scratch text for the current line.  Common lines use a fixed fast-path
   buffer; unusually long lines spill into a dynamic buffer grown on demand
   (up to LINE_SCRATCH_MAX), so the editor is not tied to a fixed line length.
   line_scratch always points at whichever buffer holds the current line and
   is never NULL.  line_dyn is retained across lines and files as a cache -
   it is only ever allocated when a genuinely long line is encountered. */
#define LINE_SCRATCH_STATIC 1024
#define LINE_SCRATCH_MAX 65536 /* safety bound, far beyond any real asm line */

static char line_static[LINE_SCRATCH_STATIC];
static char *line_dyn = NULL;
static int line_dyn_cap = 0;
static char *line_scratch = line_static;

/* Point line_scratch at a buffer sized for a line of `need` chars and return
   the greatest length it can hold.  Normally that is `need`; it is clamped
   only when an allocation fails or the safety bound is hit, so an over-long
   or pathological line degrades gracefully instead of over-allocating. */
static int line_scratch_reserve(int need) {
  if (need > LINE_SCRATCH_MAX)
    need = LINE_SCRATCH_MAX; /* the most we will ever hold */
  if (need < LINE_SCRATCH_STATIC) {
    line_scratch = line_static;
    return LINE_SCRATCH_STATIC - 1;
  }
  /* need is now in [LINE_SCRATCH_STATIC, LINE_SCRATCH_MAX]; grow the dynamic
     buffer to hold need+1 bytes, never past the safety ceiling. */
  if (need + 1 > line_dyn_cap) {
    int newcap = line_dyn_cap ? line_dyn_cap : LINE_SCRATCH_STATIC;
    while (newcap < need + 1)
      newcap *= 2;
    if (newcap > LINE_SCRATCH_MAX + 1)
      newcap = LINE_SCRATCH_MAX + 1;
    char *nb = (char *)realloc(line_dyn, (size_t)newcap);
    if (!nb) {
      line_scratch = line_static; /* grow failed: clamp to the static buffer */
      return LINE_SCRATCH_STATIC - 1;
    }
    line_dyn = nb;
    line_dyn_cap = newcap;
  }
  line_scratch = line_dyn;
  /* Never report more than `need` (the clamped line length) or the buffer. */
  return (need < line_dyn_cap) ? need : line_dyn_cap - 1;
}

static void extract_line(int row) {
  int start = line_starts[row];
  int ll = line_len(&g_buf, row);
  int maxll = line_scratch_reserve(ll);
  if (ll > maxll)
    ll = maxll;

  /* Copy directly from the gap buffer's two contiguous halves. */
  int out = 0;
  if (start < g_buf.gap_lo) {
    int take = g_buf.gap_lo - start;
    if (take > ll)
      take = ll;
    memcpy(line_scratch, g_buf.buf + start, take);
    out += take;
  }
  if (out < ll) {
    int phys_start = (start < g_buf.gap_lo)
                         ? g_buf.gap_hi
                         : g_buf.gap_hi + (start - g_buf.gap_lo);
    memcpy(line_scratch + out, g_buf.buf + phys_start, ll - out);
  }
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
/* Forward declaration: render_all is defined after render_editor_core,
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

typedef enum { UK_NONE, UK_INSERT, UK_DELETE, UK_OTHER } UndoKind;
static void undo_checkpoint(UndoKind kind);

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
  if (cut) {
    undo_checkpoint(UK_OTHER);
    sel_delete_region();
  } else {
    sel_clear();
  }
}

static void clipboard_paste(void) {
  if (!g_clipboard || g_clipboard_len == 0)
    return;
  undo_checkpoint(UK_OTHER);
  if (sel_active_flag())
    sel_delete_region();
  gb_move(&g_buf, cursor_pos);
  for (int i = 0; i < g_clipboard_len; i++) {
    if (!gb_insert(&g_buf, g_clipboard[i]))
      break;
    cursor_pos++;
  }
  rebuild_lines(&g_buf);
  cursor_sync_pos();
  g_modified = 1;
}

/* ================================================================
 * Undo / Redo
 *
 * The history (undo.c) stores only the region each edit changed, so its
 * cost follows how much was edited rather than how large the file is - a
 * ring of whole-buffer copies would need more memory than the calculator
 * has on a large source, and undo would quietly stop working there.
 *
 * Deriving a delta needs the text from both before and after an edit, but
 * undo_checkpoint() runs before the mutation, so recording lags by one
 * step: g_last holds the text as of the last checkpoint, and the next
 * flush diffs it against the buffer.  Coalescing then costs nothing - a
 * run of same-kind edits simply does not flush, so the whole run is
 * recorded as the single entry it should be.
 * ================================================================ */

/* Text as of the last checkpoint, with the editor state from that moment. */
static char *g_last;
static int g_last_len;
static int g_last_cursor, g_last_anchor, g_last_active;

/* Flatten the gap buffer into a fresh array.  Returns NULL on failure. */
static char *buf_snapshot(int *out_len) {
  int len = gb_len(&g_buf);
  char *nb = (char *)malloc((size_t)len + 1);
  if (!nb)
    return NULL;
  memcpy(nb, g_buf.buf, (size_t)g_buf.gap_lo);
  int after = g_buf.size - g_buf.gap_hi;
  memcpy(nb + g_buf.gap_lo, g_buf.buf + g_buf.gap_hi, (size_t)after);
  nb[len] = '\0';
  *out_len = len;
  return nb;
}

/* Adopt the current buffer as the reference for the next delta. */
static void undo_mark(void) {
  int len;
  char *snap = buf_snapshot(&len);
  if (!snap)
    return; /* keep the old reference; the next flush simply spans more */
  free(g_last);
  g_last = snap;
  g_last_len = len;
  g_last_cursor = cursor_pos;
  g_last_anchor = sel_anchor;
  g_last_active = sel_active;
}

/* Record whatever has changed since the last mark as one history entry. */
static void undo_flush(void) {
  if (!g_last)
    return;
  int len;
  char *now = buf_snapshot(&len);
  if (!now)
    return;
  undo_record(g_last, g_last_len, now, len, g_last_cursor, g_last_anchor,
              g_last_active, cursor_pos, sel_anchor, sel_active);
  free(now);
  undo_mark();
}

/* Drop the history entirely (a new file, or leaving the editor). */
static void undo_forget(void) {
  undo_reset();
  free(g_last);
  g_last = NULL;
  g_last_len = 0;
}

/* Undo coalescing: consecutive same-kind edits share one entry, so undo
   steps back a word or an edit at a time rather than a keystroke.
   Movement and other operations break the group. */
static UndoKind g_undo_kind = UK_NONE;

/* Force the next edit to start a fresh undo group. */
static void undo_break(void) { g_undo_kind = UK_NONE; }

/* Put the buffer back the way `d` describes and restore the editor state
   that went with it.  `remove`/`insert` pick the direction. */
static void undo_apply(int pos, int remove_len, const char *insert,
                       int insert_len, int cursor, int anchor, int active) {
  gb_move(&g_buf, pos);
  for (int i = 0; i < remove_len; i++)
    gb_delete(&g_buf);
  if (insert_len > 0)
    gb_insert_n(&g_buf, insert, insert_len);
  rebuild_lines(&g_buf);

  cursor_pos = cursor;
  if (cursor_pos > gb_len(&g_buf))
    cursor_pos = gb_len(&g_buf);
  if (cursor_pos < 0)
    cursor_pos = 0;
  sel_anchor = anchor;
  sel_active = active;
  cursor_sync_pos();
  g_modified = 1;
  diag_nav_reset(); /* the buffer moved; drop stale assemble diagnostics */
  g_build_stale = 1;

  undo_mark(); /* the buffer is now the reference for the next delta */
}

static void do_undo(void) {
  undo_flush(); /* fold in an edit still in progress */
  const UndoDelta *d = undo_take_undo();
  if (!d)
    return;
  undo_apply(d->pos, d->new_len, d->old_text, d->old_len, d->cursor_before,
             d->anchor_before, d->active_before);
  undo_break();
}

static void do_redo(void) {
  /* Fold in an edit still in progress first.  If there is one, recording it
     discards the redo path - editing after an undo has always done that - and
     the take below correctly finds nothing.  Skipping this would apply a delta
     describing a state the buffer no longer holds, corrupting the text. */
  undo_flush();
  const UndoDelta *d = undo_take_redo();
  if (!d)
    return;
  undo_apply(d->pos, d->old_len, d->new_text, d->new_len, d->cursor_after,
             d->anchor_after, d->active_after);
  undo_break();
}

/* Called before a mutation.  Runs of the same kind coalesce into one entry;
   everything else closes the previous entry first. */
static void undo_checkpoint(UndoKind kind) {
  diag_nav_reset(); /* an edit invalidates the retained assemble diagnostics */
  g_build_stale = 1; /* ...and leaves any built program behind the source */
  int coalesce =
      (kind == g_undo_kind) && (kind == UK_INSERT || kind == UK_DELETE);
  if (!coalesce) {
    if (g_last)
      undo_flush();
    else
      undo_mark(); /* first edit of the session: establish the reference */
  }
  g_undo_kind = kind;
}

/* ================================================================
 * Search / Search-and-Replace
 * ================================================================ */
#define SEARCH_MAX 128

static char g_search_pat[SEARCH_MAX] = "";
static char g_replace_str[SEARCH_MAX] = "";
static int g_search_case = 0; /* 0 = case-insensitive, 1 = case-sensitive */

/* Substring search in the logical buffer, honouring g_search_case.
   Returns logical position of match start, or -1. */
static int buf_find(int from, const char *pat, int patlen) {
  int buflen = gb_len(&g_buf);
  if (patlen == 0)
    return -1;
  if (from < 0)
    from = 0;
  for (int i = from; i <= buflen - patlen; i++) {
    int ok = 1;
    for (int j = 0; j < patlen && ok; j++) {
      char a = gb_get(&g_buf, i + j);
      char b = pat[j];
      if (g_search_case) {
        if (a != b)
          ok = 0;
      } else if (tolower((unsigned char)a) != tolower((unsigned char)b)) {
        ok = 0;
      }
    }
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

static void search_status_hint(const char *action) {
  char msg[96];
  snprintf(msg, sizeof(msg), "%s   Tab:case[%s]  Esc:done", action,
           g_search_case ? "Aa" : "aa");
  search_draw_status(msg);
}

/* Interactive search loop.  Enter jumps to the next match (wrapping),
   Tab toggles case sensitivity, Esc ends.
   Returns 1 if something was found+selected, 0 if not found / cancelled. */
static int editor_search_loop(void) {
  int patlen = (int)strlen(g_search_pat);
  if (patlen == 0)
    return 0;

  int found = buf_find(cursor_pos + 1, g_search_pat, patlen);
  if (found < 0)
    found = buf_find(0, g_search_pat, patlen); /* wrap */
  if (found < 0) {
    search_draw_status("Not found.");
    msleep(800);
    return 0;
  }
  search_select(found, found + patlen);
  render_all();
  search_status_hint("Enter:next");

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
    if (isKeyPressed(KEY_NSPIRE_TAB)) {
      while (any_key_pressed())
        msleep(20);
      g_search_case = !g_search_case;
      /* Re-anchor the search on the current match under the new mode. */
      int here = buf_find(cursor_pos - patlen, g_search_pat, patlen);
      if (here < 0)
        here = buf_find(0, g_search_pat, patlen);
      if (here >= 0) {
        search_select(here, here + patlen);
        render_all();
      }
      search_status_hint("Enter:next");
      continue;
    }
    if (isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_CLICK)) {
      while (any_key_pressed())
        msleep(20);
      int next = buf_find(cursor_pos + 1, g_search_pat, patlen);
      if (next < 0)
        next = buf_find(0, g_search_pat, patlen);
      if (next < 0) {
        sel_clear();
        render_all();
        search_draw_status("No more matches.");
        msleep(600);
        break;
      }
      search_select(next, next + patlen);
      render_all();
      search_status_hint("Enter:next");
    } else {
      msleep(20);
    }
  }
  return 1;
}

static void editor_search(void) {
  char tmp[SEARCH_MAX];
  strncpy(tmp, g_search_pat, SEARCH_MAX);
  tmp[SEARCH_MAX - 1] = '\0';
  if (!gfx_input_filename("Search", "Find:", tmp, SEARCH_MAX))
    return;
  strncpy(g_search_pat, tmp, SEARCH_MAX);
  g_search_pat[SEARCH_MAX - 1] = '\0';
  editor_search_loop();
}

/* Replace the patlen bytes at pos with g_replace_str.  Assumes the gap
   ops are done by the caller's snapshot policy.  Returns the number of
   replacement bytes actually written. */
static int do_one_replace(int pos, int patlen, const char *rep, int replen) {
  gb_move(&g_buf, pos);
  for (int i = 0; i < patlen; i++)
    gb_delete(&g_buf);
  int inserted = 0;
  for (int i = 0; i < replen; i++) {
    if (!gb_insert(&g_buf, rep[i]))
      break;
    inserted++;
  }
  rebuild_lines(&g_buf);
  g_modified = 1;
  return inserted;
}

/* Replace every match in the file as a single undo step. */
static int replace_all(int patlen, int replen) {
  undo_checkpoint(UK_OTHER);
  int from = 0, count = 0;
  for (;;) {
    int pos = buf_find(from, g_search_pat, patlen);
    if (pos < 0)
      break;
    int inserted = do_one_replace(pos, patlen, g_replace_str, replen);
    from = pos + inserted;
    count++;
    if (g_gb_oom)
      break;
  }
  cursor_pos = 0;
  cursor_sync_pos();
  return count;
}

static void editor_search_replace(void) {
  char tmp_pat[SEARCH_MAX], tmp_rep[SEARCH_MAX];
  strncpy(tmp_pat, g_search_pat, SEARCH_MAX);
  tmp_pat[SEARCH_MAX - 1] = '\0';
  strncpy(tmp_rep, g_replace_str, SEARCH_MAX);
  tmp_rep[SEARCH_MAX - 1] = '\0';
  if (!gfx_input_filename("Search", "Find:", tmp_pat, SEARCH_MAX))
    return;
  if (!gfx_input_filename("Replace", "Replace with:", tmp_rep, SEARCH_MAX))
    return;
  strncpy(g_search_pat, tmp_pat, SEARCH_MAX);
  g_search_pat[SEARCH_MAX - 1] = '\0';
  strncpy(g_replace_str, tmp_rep, SEARCH_MAX);
  g_replace_str[SEARCH_MAX - 1] = '\0';

  int patlen = (int)strlen(g_search_pat);
  int replen = (int)strlen(g_replace_str);
  if (patlen == 0)
    return;

  int replaced = 0;

  /* Interactive stepping from the cursor, wrapping once through the top. */
  int origin = cursor_pos;
  int wrapped = 0;
  int from = origin;

  while (any_key_pressed())
    msleep(20);

  for (;;) {
    int pos = buf_find(from, g_search_pat, patlen);
    if (pos < 0 || (wrapped && pos >= origin)) {
      if (!wrapped) {
        wrapped = 1;
        from = 0;
        continue;
      }
      break; /* whole file scanned */
    }

    search_select(pos, pos + patlen);
    scroll_to_cursor();
    render_all();
    search_status_hint("Enter:replace  Tab:skip  A:all");

    while (!any_key_pressed()) {
      msleep(16);
      idle();
    }

    if (isKeyPressed(KEY_NSPIRE_ESC)) {
      while (any_key_pressed())
        msleep(20);
      break;
    } else if (isKeyPressed(KEY_NSPIRE_A)) {
      while (any_key_pressed())
        msleep(20);
      replaced += replace_all(patlen, replen);
      break;
    } else if (isKeyPressed(KEY_NSPIRE_ENTER) ||
               isKeyPressed(KEY_NSPIRE_CLICK)) {
      while (any_key_pressed())
        msleep(20);
      undo_checkpoint(UK_OTHER);
      int inserted = do_one_replace(pos, patlen, g_replace_str, replen);
      cursor_pos = pos + inserted;
      cursor_sync_pos();
      sel_clear();
      replaced++;
      /* A replacement shifts everything after pos by this delta. */
      int delta = inserted - patlen;
      if (wrapped)
        origin += delta;
      from = pos + inserted;
    } else if (isKeyPressed(KEY_NSPIRE_TAB)) {
      while (any_key_pressed())
        msleep(20);
      from = pos + patlen;
    } else {
      msleep(20);
      continue;
    }
    render_all();
  }

  sel_clear();
  render_all();
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

  /* Selection highlight pass:
   Overdraw characters inside [sel_lo, sel_hi) with the selection colours. */
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
      /* extract_line truncates to the scratch buffer size; never read
         line_scratch beyond what was actually extracted. */
      int avail = (int)strlen(line_scratch);
      for (int col = 0; col < avail; col++) {
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

    if (g_diag_status[0]) {
      /* A transient message (e.g. the first assemble error) takes over the
         status bar until the next keypress. */
      gfx_drawstr_clipped(0, sy + 1, g_diag_status, g_default_theme.accent,
                          C_STATUS_BG, GFX_W);
    } else {
      snprintf(status, sizeof(status), " %s%s%s  Ln %d/%d  Col %d  %s", fname,
               g_modified ? "*" : "", g_readonly ? " [RO]" : "", cursor_row + 1,
               num_lines, cursor_col + 1, g_crlf ? "CRLF" : "LF");
      gfx_drawstr_clipped(0, sy + 1, status,
                          g_modified ? C_MODIFIED : C_STATUS_FG, C_STATUS_BG,
                          GFX_W);
    }
  }
}

static void render_all(void) {
  render_editor_core();
  gfx_flip();
}

/* ================================================================
 * Keyboard input
 * ================================================================ */

/* Character key table lives in gfx.c (shared with the input dialogs). */

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
#define ACT_ASSEMBLE (-46)
#define ACT_SYSCALL_CATALOG (-47)
#define ACT_BS_WORD (-48)
#define ACT_DEL_WORD (-49)
#define ACT_UNTAB (-50)
#define ACT_DIAG_NEXT (-51)
#define ACT_DIAG_PREV (-52)
#define ACT_DIAG_DETAIL (-53)
#define ACT_RUN (-54)
#define ACT_JUMP_BACK (-55)
#define ACT_FIND_REFS (-56)

/* Whether an action counts as a buffer edit for undo-coalescing: a run of
   same-kind edits shares one undo group and any non-edit action ends the
   group (see the main loop).  This is intentionally broader than
   act_needs_write below - it also covers undo/redo and the pickers - because
   it is about grouping, not permission. */
static int act_is_edit(int act) {
  if (act > 0)
    return 1; /* printable character insert */
  switch (act) {
  case ACT_ENTER:
  case ACT_BS:
  case ACT_DEL:
  case ACT_BS_WORD:
  case ACT_DEL_WORD:
  case ACT_TAB:
  case ACT_UNTAB:
  case ACT_CUT:
  case ACT_PASTE:
  case ACT_UNDO:
  case ACT_REDO:
  case ACT_REPLACE:
  case ACT_CHARMAP:
  case ACT_CATALOG:
  case ACT_SYSCALL_CATALOG:
    return 1;
  default:
    return 0;
  }
}

/* Actions that unconditionally mutate the buffer and therefore require write
   permission up front (the read-only prompt).  Defined as the edit set minus
   two groups that must not prompt eagerly, so the two lists cannot drift:
     - undo/redo: no-ops when there is nothing to revert (and if there is, the
       edits that created it already enabled editing);
     - the char map / instruction / syscall pickers: view-only until something
       is chosen, so they gate at the actual point of insertion instead. */
static int act_needs_write(int act) {
  switch (act) {
  case ACT_UNDO:
  case ACT_REDO:
  case ACT_CHARMAP:
  case ACT_CATALOG:
  case ACT_SYSCALL_CATALOG:
    return 0;
  default:
    return act_is_edit(act);
  }
}

static GfxRepeat g_key_repeat = {ACT_NONE, 0};

static int poll_key(void) {
  int shift = isKeyPressed(KEY_NSPIRE_SHIFT);
  int ctrl = isKeyPressed(KEY_NSPIRE_CTRL);

  if (isKeyPressed(KEY_NSPIRE_ENTER)) {
    if (ctrl && shift)
      return ACT_JUMP_BACK;
    return ctrl ? ACT_JUMP_LABEL : ACT_ENTER;
  }
  if (isKeyPressed(KEY_NSPIRE_ESC))
    return ACT_ESC;
  if (isKeyPressed(KEY_NSPIRE_TAB))
    return shift ? ACT_UNTAB : ACT_TAB;

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

  /* The Nspire has no End key: Doc jumps to end of line. */
  if (isKeyPressed(KEY_NSPIRE_DOC)) {
    if (shift)
      return ACT_SEL_END;
    return ACT_END;
  }

  if (isKeyPressed(KEY_NSPIRE_DEL)) {
    if (ctrl && shift)
      return ACT_DEL_WORD;
    if (ctrl)
      return ACT_BS_WORD;
    if (shift)
      return ACT_DEL;
    return ACT_BS;
  }

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
  if (ctrl && isKeyPressed(KEY_NSPIRE_B))
    return ACT_ASSEMBLE;
  if (ctrl && isKeyPressed(KEY_NSPIRE_N))
    return ACT_DIAG_NEXT;
  if (ctrl && isKeyPressed(KEY_NSPIRE_P))
    return ACT_DIAG_PREV;
  if (ctrl && isKeyPressed(KEY_NSPIRE_D))
    return ACT_DIAG_DETAIL;
  if (ctrl && isKeyPressed(KEY_NSPIRE_R))
    return ACT_RUN;
  if (ctrl && isKeyPressed(KEY_NSPIRE_U))
    return ACT_FIND_REFS;
  if (ctrl && isKeyPressed(KEY_NSPIRE_TRIG))
    return ACT_CHEATSHEET;

  if (isKeyPressed(KEY_NSPIRE_CAT)) {
    if (shift)
      return ACT_SYSCALL_CATALOG;
    return ACT_CATALOG;
  }

  for (int i = 0; i < gfx_char_keymap_size; i++) {
    if (!isKeyPressed(gfx_char_keymap[i].key))
      continue;
    if (ctrl)
      return gfx_char_keymap[i].ctrl ? (unsigned char)gfx_char_keymap[i].ctrl
                                     : ACT_NONE;
    if (shift)
      return (unsigned char)gfx_char_keymap[i].shifted;
    return (unsigned char)gfx_char_keymap[i].normal;
  }
  return ACT_NONE;
}

/* ================================================================
 * Editor actions
 * ================================================================ */

/* ================================================================
 * Automatic bracket completion
 *
 * Applies only to brackets typed directly on the keyboard (routed through
 * editor_type_char).  Programmatic insertion via the catalog, syscall and
 * character-map pickers still goes straight through do_insert_char, so it
 * is never auto-completed.
 * ================================================================ */
static char bracket_close_for(char open) {
  switch (open) {
  case '(':
    return ')';
  case '[':
    return ']';
  case '{':
    return '}';
  default:
    return 0;
  }
}

static int is_close_bracket(char c) {
  return c == ')' || c == ']' || c == '}';
}

/* True when (open, close) form an empty pair the cursor sits between. */
static int is_auto_pair(char open, char close) {
  return close != 0 && bracket_close_for(open) == close;
}

/* Auto-close an opener only when the character it would sit in front of is
   a natural boundary, so typing "(" before a word does not wedge a ")" into
   the middle of it. */
static int autopair_context_ok(void) {
  int len = gb_len(&g_buf);
  if (cursor_pos >= len)
    return 1; /* end of line / buffer */
  char n = gb_get(&g_buf, cursor_pos);
  return n == ' ' || n == '\t' || n == '\n' || n == '\r' ||
         is_close_bracket(n) || n == ',' || n == ';';
}

/* Insert an "open close" pair and leave the cursor between them, as a
   single undo step. */
static void insert_pair(char open, char close) {
  undo_checkpoint(UK_OTHER);
  int pos = cursor_pos;
  int before = gb_len(&g_buf);
  gb_move(&g_buf, cursor_pos);
  if (gb_insert(&g_buf, open)) {
    cursor_pos++;
    gb_move(&g_buf, cursor_pos);
    gb_insert(&g_buf, close); /* cursor stays in front of the closer */
  }
  /* Brackets only: no line break, so the table just slides. */
  lines_shift(pos, gb_len(&g_buf) - before);
  cursor_sync_pos();
  g_modified = 1;
}

static void do_insert_char(char c) {
  int wordy = (isalnum((unsigned char)c) || c == '_');
  undo_checkpoint(UK_INSERT);
  if (sel_active_flag())
    sel_delete_region();

  int pos = cursor_pos;
  int before = gb_len(&g_buf);
  gb_move(&g_buf, cursor_pos);
  if (gb_insert(&g_buf, c))
    cursor_pos++;
  if (c == '\n')
    rebuild_lines(&g_buf); /* a new line: the table's shape changes */
  else
    lines_shift(pos, gb_len(&g_buf) - before);
  cursor_sync_pos();
  g_modified = 1;
  if (!wordy)
    undo_break(); /* whitespace / punctuation ends the typing group */
}

/* Handle a directly typed printable character: apply bracket auto-completion
   where it feels natural, otherwise fall back to a plain insert.  Only
   keyboard typing routes through here. */
static void editor_type_char(char c) {
  /* Typing a closer where the same closer already sits: step over it rather
     than doubling it (makes auto-closed pairs painless to finish). */
  if (is_close_bracket(c) && !sel_active_flag() && cursor_pos < gb_len(&g_buf) &&
      gb_get(&g_buf, cursor_pos) == c) {
    cursor_pos++;
    cursor_sync_pos();
    undo_break(); /* a cursor move ends the current typing group */
    return;
  }

  /* Auto-close an opener in a safe context.  Never while a selection is
     active, so the existing type-over-selection behaviour is preserved. */
  char close = bracket_close_for(c);
  if (close && !sel_active_flag() && autopair_context_ok()) {
    insert_pair(c, close);
    return;
  }

  do_insert_char(c);
}

static void do_enter(void) {
  undo_checkpoint(UK_OTHER);
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
  if (gb_insert(&g_buf, '\n'))
    cursor_pos++;

  int i;
  for (i = 0; i < indent; i++) {
    /* Safe to read from line_start + i because it is strictly before the
     * insertion point */
    if (!gb_insert(&g_buf, gb_get(&g_buf, line_start + i)))
      break;
    cursor_pos++;
  }

  rebuild_lines(&g_buf);
  cursor_sync_pos();
  g_modified = 1;
}

static void do_backspace(void) {
  if (sel_active_flag()) {
    undo_checkpoint(UK_OTHER);
    sel_delete_region();
    return;
  }
  if (cursor_pos == 0)
    return;

  /* Backspacing the opener of an empty auto-pair removes the closer too. */
  if (cursor_pos < gb_len(&g_buf) &&
      is_auto_pair(gb_get(&g_buf, cursor_pos - 1), gb_get(&g_buf, cursor_pos))) {
    undo_checkpoint(UK_OTHER);
    gb_move(&g_buf, cursor_pos);
    gb_delete(&g_buf);    /* the closer, at the cursor */
    gb_backspace(&g_buf); /* the opener, before the cursor */
    cursor_pos--;
    lines_shift(cursor_pos, -2); /* both are brackets, never a line break */
    cursor_sync_pos();
    g_modified = 1;
    return;
  }

  undo_checkpoint(UK_DELETE);
  int erased_nl = (gb_get(&g_buf, cursor_pos - 1) == '\n');
  gb_move(&g_buf, cursor_pos);
  gb_backspace(&g_buf);
  cursor_pos--;
  if (erased_nl)
    rebuild_lines(&g_buf); /* two lines join: the table loses an entry */
  else
    lines_shift(cursor_pos, -1);
  cursor_sync_pos();
  g_modified = 1;
}

static void do_delete(void) {
  if (sel_active_flag()) {
    undo_checkpoint(UK_OTHER);
    sel_delete_region();
    return;
  }
  if (cursor_pos >= gb_len(&g_buf))
    return;
  undo_checkpoint(UK_DELETE);
  int erased_nl = (gb_get(&g_buf, cursor_pos) == '\n');
  gb_move(&g_buf, cursor_pos);
  gb_delete(&g_buf);
  if (erased_nl)
    rebuild_lines(&g_buf); /* two lines join: the table loses an entry */
  else
    lines_shift(cursor_pos, -1);
  cursor_sync_pos();
  g_modified = 1;
}

/* Indent (outdent=0) or outdent (outdent=1) every line touched by the
   current selection by one tab width, keeping the whole block selected. */
static void indent_selection(int outdent) {
  int tw = g_settings.tab_width;
  int lo = sel_lo(), hi = sel_hi();

  int r0 = 0, r1 = 0;
  for (int r = 0; r < num_lines; r++) {
    if (line_starts[r] <= lo)
      r0 = r;
    if (line_starts[r] <= hi)
      r1 = r;
  }
  /* If the selection ends exactly at a line start, that trailing line is
     not really included. */
  if (r1 > r0 && line_starts[r1] == hi)
    r1--;

  undo_checkpoint(UK_OTHER);
  for (int r = r0; r <= r1; r++) {
    int start = line_starts[r];
    if (outdent) {
      int ll = line_len(&g_buf, r);
      int rem = 0;
      if (ll > 0 && gb_get(&g_buf, start) == '\t') {
        rem = 1;
      } else {
        while (rem < tw && rem < ll && gb_get(&g_buf, start + rem) == ' ')
          rem++;
      }
      if (rem > 0) {
        gb_move(&g_buf, start);
        for (int i = 0; i < rem; i++)
          gb_delete(&g_buf);
      }
    } else {
      gb_move(&g_buf, start);
      for (int i = 0; i < tw; i++)
        gb_insert(&g_buf, ' ');
    }
    rebuild_lines(&g_buf);
  }

  /* Re-select the whole affected block. */
  sel_anchor = line_starts[r0];
  sel_active = line_starts[r1] + line_len(&g_buf, r1);
  cursor_pos = sel_active;
  cursor_sync_pos();
  g_modified = 1;
}

static void do_tab(void) {
  if (sel_active_flag()) {
    indent_selection(0);
    return;
  }
  undo_checkpoint(UK_OTHER);
  /* Align to the next tab stop rather than always inserting tab_width. */
  int tw = g_settings.tab_width;
  int n = tw - (cursor_col % tw);
  if (n <= 0)
    n = tw;
  int pos = cursor_pos;
  int before = gb_len(&g_buf);
  gb_move(&g_buf, cursor_pos);
  for (int i = 0; i < n; i++) {
    if (!gb_insert(&g_buf, ' '))
      break;
    cursor_pos++;
  }
  /* Spaces only: the table just slides. */
  lines_shift(pos, gb_len(&g_buf) - before);
  cursor_sync_pos();
  g_modified = 1;
}

static void do_untab(void) {
  if (sel_active_flag()) {
    indent_selection(1);
    return;
  }
  /* Outdent the current line. */
  undo_checkpoint(UK_OTHER);
  int tw = g_settings.tab_width;
  int start = line_starts[cursor_row];
  int ll = line_len(&g_buf, cursor_row);
  int rem = 0;
  if (ll > 0 && gb_get(&g_buf, start) == '\t') {
    rem = 1;
  } else {
    while (rem < tw && rem < ll && gb_get(&g_buf, start + rem) == ' ')
      rem++;
  }
  if (rem > 0) {
    gb_move(&g_buf, start);
    for (int i = 0; i < rem; i++)
      gb_delete(&g_buf);
    /* Leading whitespace only, so no line break is removed.  `start` is this
       line's own start, which stays put; later lines slide back. */
    lines_shift(start, -rem);
    if (cursor_col >= rem)
      cursor_col -= rem;
    else
      cursor_col = 0;
    cursor_sync_rowcol();
    g_modified = 1;
  }
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
  /* Collapse a selection to its top edge first, like Left does. */
  if (sel_active_flag()) {
    cursor_pos = sel_lo();
    sel_clear();
    cursor_sync_pos();
    cursor_goal_col = cursor_col;
    return;
  }
  if (cursor_row > 0) {
    cursor_row--;
    cursor_col = cursor_goal_col;
    cursor_sync_rowcol();
  }
}

static void do_down(void) {
  /* Collapse a selection to its bottom edge first, like Right does. */
  if (sel_active_flag()) {
    cursor_pos = sel_hi();
    sel_clear();
    cursor_sync_pos();
    cursor_goal_col = cursor_col;
    return;
  }
  if (cursor_row < num_lines - 1) {
    cursor_row++;
    cursor_col = cursor_goal_col;
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
  cursor_col = cursor_goal_col;
  cursor_sync_rowcol();
}

static void do_pgdn(void) {
  sel_clear();
  cursor_row += ROWS_VIS;
  if (cursor_row >= num_lines)
    cursor_row = num_lines - 1;
  cursor_col = cursor_goal_col;
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

static void do_bs_word(void) {
  if (sel_active_flag()) {
    undo_checkpoint(UK_OTHER);
    sel_delete_region();
    return;
  }
  if (cursor_pos == 0)
    return;
  undo_checkpoint(UK_OTHER);
  int old_pos = cursor_pos;

  /* Find the start of the previous word */
  while (cursor_pos > 0 && IS_HSPACE(gb_get(&g_buf, cursor_pos - 1)))
    cursor_pos--;
  if (cursor_pos > 0) {
    if (IS_WORD(gb_get(&g_buf, cursor_pos - 1))) {
      while (cursor_pos > 0 && IS_WORD(gb_get(&g_buf, cursor_pos - 1)))
        cursor_pos--;
    } else {
      cursor_pos--;
    }
  }

  int count = old_pos - cursor_pos;
  cursor_pos = old_pos; /* Restore cursor to delete backwards */
  gb_move(&g_buf, cursor_pos);
  for (int i = 0; i < count; i++) {
    gb_backspace(&g_buf);
    cursor_pos--;
  }
  rebuild_lines(&g_buf);
  cursor_sync_pos();
  g_modified = 1;
}

static void do_del_word(void) {
  if (sel_active_flag()) {
    undo_checkpoint(UK_OTHER);
    sel_delete_region();
    return;
  }
  int len = gb_len(&g_buf);
  if (cursor_pos >= len)
    return;
  undo_checkpoint(UK_OTHER);
  int target_pos = cursor_pos;

  /* Find the end of the next word */
  if (IS_WORD(gb_get(&g_buf, target_pos))) {
    while (target_pos < len && IS_WORD(gb_get(&g_buf, target_pos)))
      target_pos++;
  } else if (!IS_HSPACE(gb_get(&g_buf, target_pos))) {
    target_pos++;
  }
  while (target_pos < len && IS_HSPACE(gb_get(&g_buf, target_pos)))
    target_pos++;

  int count = target_pos - cursor_pos;
  gb_move(&g_buf, cursor_pos);
  for (int i = 0; i < count; i++) {
    gb_delete(&g_buf);
  }
  rebuild_lines(&g_buf);
  cursor_sync_pos();
  g_modified = 1;
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

/* Selection-extending movement variants.
   Each saves the old cursor_pos as anchor (first call), then moves and updates
   sel_active. */
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
    cursor_col = cursor_goal_col;
    cursor_sync_rowcol();
  }
  sel_extend(old);
}
static void do_sel_down(void) {
  int old = cursor_pos;
  if (cursor_row < num_lines - 1) {
    cursor_row++;
    cursor_col = cursor_goal_col;
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

static void editor_syscall_catalog(void) {
  const SyscallInfo *sc = syscall_pick();
  /* The picker is view-only; gate just the insertion so a read-only file can
     be browsed and only prompts right before text is inserted. */
  if (sc && editor_confirm_rw()) {
    char buf[32];
    /* Format Ndless extensions natively as hex for readability */
    if (sc->num >= 0x200000) {
      snprintf(buf, sizeof(buf), "swi #0x%X", sc->num);
    } else {
      snprintf(buf, sizeof(buf), "swi #%d", sc->num);
    }
    for (int i = 0; buf[i]; i++) {
      do_insert_char(buf[i]);
    }
  }
}

/* ================================================================
 * Cheat sheet
 *
 * Works out what the cursor is on - a SWI/SVC number, or a word to look
 * up in the MnemInfo database (case-insensitive) - and hands the result
 * to the matching popup in editor_ui.c.  Deciding what is under the
 * cursor needs the buffer, so it happens here; drawing the popup does
 * not, so it happens there.
 * ================================================================ */

static void editor_cheatsheet(void) {
  if (cursor_row >= num_lines)
    return;
  extract_line(cursor_row);
  const char *line = line_scratch;
  int len = (int)strlen(line);

  /* Check for SWI/SVC instructions on the current line */
  long sys_num = -1;
  int is_syscall = syntax_scan_swi(line, len, &sys_num);

  /* Show syscall description if found */
  if (is_syscall) {
    for (int s = 0; s < g_nsyscalls; s++) {
      if (db_syscalls[s].num == sys_num) {
        syscall_show_desc(&db_syscalls[s]);
        return;
      }
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "Syscall %ld (0x%lX) not in catalog.", sys_num,
             sys_num);
    const char *body[] = {msg};
    gfx_window_alert("Unknown Syscall", body, 1, "OK");
    return;
  }

  /* Standard ARM instruction lookup based on cursor position */
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

  mnem_show_desc(mi);
}


/* ================================================================
 * File operations: Open and Save As
 *
 * Forward declarations needed because these are defined later.
 * ================================================================ */
static int prompt_unsaved(void);
static int editor_do_save(void);

static void editor_open_file(void) {
  if (g_modified) {
    int choice = prompt_unsaved();
    if (choice == 1) {
      if (!editor_do_save())
        return; /* save failed or was cancelled - keep current buffer */
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
    /* Untitled buffer: reopen at the last-visited directory. */
    strncpy(start_dir, g_settings.last_dir, sizeof(start_dir) - 1);

  char newpath[1024] = "";
  if (!browser_pick_file(start_dir, 1, newpath, sizeof(newpath)))
    return;

  settings_remember_file_dir(newpath);

  strncpy(g_filepath, newpath, sizeof(g_filepath) - 1);
  g_filepath[sizeof(g_filepath) - 1] = '\0';
  gb_free(&g_buf);
  load_file(g_filepath);
  cursor_pos = 0;
  cursor_row = 0;
  cursor_col = 0;
  cursor_goal_col = 0;
  scroll_row = 0;
  scroll_col = 0;
  sel_anchor = SEL_NONE;
  sel_active = 0;
  undo_forget();
  /* A different buffer: whatever was built before says nothing about it. */
  g_build_stale = 0;
  jump_stack_clear(); /* the trail belonged to the previous file */
  g_modified = 0;
  g_readonly = !path_is_asm_source(g_filepath);
  /* The retained diagnostics describe the previous buffer, not this one. */
  diag_nav_reset();
}

/* Save As: pick a directory, then enter a filename; saves as dir/name.tns.
   Returns 1 if the file was written, 0 on cancel or failure. */
static int editor_save_as(void) {
  char start_dir[512];
  strncpy(start_dir, g_filepath, sizeof(start_dir) - 1);
  start_dir[sizeof(start_dir) - 1] = '\0';
  char *slash = strrchr(start_dir, '/');
  if (slash)
    *slash = '\0';
  else
    /* Untitled buffer: reopen at the last-visited directory. */
    strncpy(start_dir, g_settings.last_dir, sizeof(start_dir) - 1);

  char destdir[1024] = "";
  if (!browser_pick_dir(start_dir, destdir, sizeof(destdir)))
    return 0;

  /* destdir is already a directory (not a file path). */
  settings_set_last_dir(destdir);

  char fname[128] = "";
  {
    const char *base = strrchr(g_filepath, '/');
    base = base ? base + 1 : g_filepath;
    strncpy(fname, base, sizeof(fname) - 1);
    fname[sizeof(fname) - 1] = '\0';
    /* Strip a trailing ".tns" suffix only (not any embedded ".tns"). */
    int fl = (int)strlen(fname);
    if (fl >= 4 && strcmp(fname + fl - 4, ".tns") == 0)
      fname[fl - 4] = '\0';
  }
  if (!gfx_input_filename("Save As", "Filename (no extension):", fname,
                          sizeof(fname)))
    return 0;

  if (fname[0] == '\0') {
    const char *body[] = {"Filename cannot be empty."};
    gfx_window_alert("Save As", body, 1, "OK");
    return 0;
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
    g_saved = 1;
    return 1;
  }
  const char *body[] = {"Could not write to the specified path.",
                        "Check that the directory exists and is writable."};
  gfx_window_alert("Save As Failed", body, 2, "OK");
  return 0;
}

/* Save the buffer, routing Untitled buffers through Save As and
   reporting write failures.  Returns 1 if the file is on disk. */
static int editor_do_save(void) {
  if (g_filepath[0] == '\0')
    return editor_save_as();
  if (!save_file(g_filepath)) {
    const char *body[] = {"Could not write the file.",
                          "Check that the location is writable."};
    gfx_window_alert("Save Failed", body, 2, "OK");
    return 0;
  }
  g_modified = 0;
  g_saved = 1;
  return 1;
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
  jump_push();
  cursor_row = ln - 1;
  cursor_col = 0;
  cursor_sync_rowcol();
  scroll_to_cursor();
}

/* ================================================================
 * Label browser
 *
 * Scans the gap buffer for label definitions - under nasm's rule an
 * identifier in column 0 with NO trailing colon - into a table that the
 * picker in editor_ui.c displays.  Scanning needs the buffer and moving
 * the cursor to the chosen label needs the engine, so both stay here;
 * only the list UI lives over there.
 *
 * Also provides label-lookup by name (used by jump-to-label).
 * ================================================================ */

/* LabelEntry and MAX_LABEL_LEN live in editor_ui.h: the table is filled here
   from the buffer, and displayed by the picker there. */
#define MAX_LABELS 256

static LabelEntry g_labels[MAX_LABELS];
static int g_nlabels;

/* Rebuild label table from the gap buffer.  nasm rule: a label is a
   valid identifier in column 0 (letter first, then alnum/underscore),
   no trailing colon. */
static void labels_scan(void) {
  g_nlabels = 0;
  for (int row = 0; row < num_lines && g_nlabels < MAX_LABELS; row++) {
    int start = line_starts[row];
    int ll = line_len(&g_buf, row);
    if (ll == 0)
      continue;
    char first = gb_get(&g_buf, start);
    if (!isalpha((unsigned char)first))
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
    if (syn_is_reg(tmp, wlen) || syn_is_mnem(tmp, wlen) ||
        syn_is_shift(tmp, wlen) || syn_is_directive(tmp, wlen))
      continue;
    memcpy(g_labels[g_nlabels].name, tmp, wlen + 1);
    g_labels[g_nlabels].line = row;
    g_nlabels++;
  }
}

/* Return the line (0-based) of the label with the given name,
   case-insensitive.  Returns -1 if not found. */
static int label_find(const char *name) {
  int nlen = (int)strlen(name);
  for (int i = 0; i < g_nlabels; i++) {
    int llen = (int)strlen(g_labels[i].name);
    if (llen == nlen && strncaseeq(g_labels[i].name, name, nlen))
      return g_labels[i].line;
  }
  return -1;
}


/* Open the label browser.  Returns 1 if cursor was moved. */
static int editor_label_browser(void) {
  labels_scan();

  int sel = label_pick(g_labels, g_nlabels, cursor_row);
  if (sel < 0)
    return 0;

  jump_push();
  cursor_row = g_labels[sel].line;
  cursor_col = 0;
  cursor_sync_rowcol();
  scroll_to_cursor();
  return 1;
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


/* Copy the identifier the cursor sits on (or just after) into `out`.
   Returns its length, or 0 when the cursor is not on one. */
static int word_under_cursor(char *out, int outsz) {
  extract_line(cursor_row);
  const char *line = line_scratch;
  int len = (int)strlen(line);
  int col = cursor_col > len ? len : cursor_col;

  int ws = col;
  while (ws > 0 && (isalnum((unsigned char)line[ws - 1]) || line[ws - 1] == '_'))
    ws--;
  int we = col;
  while (we < len && (isalnum((unsigned char)line[we]) || line[we] == '_'))
    we++;

  int wlen = we - ws;
  if (wlen <= 0 || wlen >= outsz)
    return 0;
  memcpy(out, line + ws, (size_t)wlen);
  out[wlen] = '\0';
  return wlen;
}

/* Move to the definition of `name`, remembering where we came from.
   Returns 1 when the label exists. */
static int jump_to_label_named(const char *name) {
  labels_scan();
  int target = label_find(name);
  if (target < 0)
    return 0;

  jump_push();
  cursor_row = target;
  cursor_col = 0;
  cursor_sync_rowcol();
  scroll_to_cursor();
  return 1;
}

/*
 * Go to the definition of a label (Ctrl+Enter).
 *
 * On a branch instruction the target is its operand, so the cursor can sit
 * anywhere on the line - that is how this has always worked.  Anywhere else
 * the identifier under the cursor is used, which makes the shortcut useful on
 * an operand of any instruction, not only B/BL/BX.
 */
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

  char name[MAX_LABEL_LEN];
  int nlen = 0;

  if (mlen > 0 && is_branch_base(line + mstart, mlen)) {
    /* Branch line: follow its operand. */
    while (i < len && (line[i] == ' ' || line[i] == '\t'))
      i++;
    int ostart = i;
    while (i < len && line[i] != ' ' && line[i] != '\t' && line[i] != ',' &&
           line[i] != ';')
      i++;
    int olen = i - ostart;

    if (olen == 0) {
      static const char *body[] = {
          "No operand found on this branch instruction."};
      gfx_window_alert("No Operand", body, 1, "OK");
      return;
    }
    if (olen >= MAX_LABEL_LEN)
      olen = MAX_LABEL_LEN - 1;
    memcpy(name, line + ostart, (size_t)olen);
    name[olen] = '\0';
    nlen = olen;

    if (syn_is_reg(name, nlen)) {
      static const char *body[] = {"This branch uses a register operand,",
                                   "not a label, cannot jump to definition."};
      gfx_window_alert("Register Branch", body, 2, "OK");
      return;
    }
  } else {
    nlen = word_under_cursor(name, sizeof(name));
    if (nlen == 0) {
      static const char *body[] = {"Place the cursor on a label name,",
                                   "or on a B/BL/BX line, and try again."};
      gfx_window_alert("Go to Definition", body, 2, "OK");
      return;
    }
  }

  if (!jump_to_label_named(name)) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Label \"%s\" not found in this file.", name);
    const char *body[1] = {msg};
    gfx_window_alert("Label Not Found", body, 1, "OK");
  }
}

/* Step back to the position before the last jump (Ctrl+Shift+Enter). */
static void editor_jump_back(void) {
  if (g_jump_count == 0) {
    snprintf(g_diag_status, sizeof(g_diag_status), " Nowhere to jump back to");
    return;
  }
  int pos = g_jump_stack[--g_jump_count];
  int len = gb_len(&g_buf);
  cursor_pos = pos > len ? len : pos; /* edits may have shortened the buffer */
  cursor_sync_pos();
  scroll_to_cursor();
}

/* ================================================================
 * Find references
 *
 * Every mention of a label, wherever it appears as a whole identifier -
 * branch targets, operands, EQU right-hand sides.  Matching is
 * case-insensitive and word-bounded, the same rule label_find uses, so a
 * label named "loop" is not found inside "loopback".
 * ================================================================ */
#define MAX_REFS 128
#define REF_ROW_LEN 96

static int g_ref_lines[MAX_REFS];
static char g_ref_rows[MAX_REFS][REF_ROW_LEN];
static const char *g_ref_ptrs[MAX_REFS];
static int g_nrefs;

/* Collect the lines mentioning `name`, formatting a row for each. */
static void refs_scan(const char *name) {
  int nlen = (int)strlen(name);
  g_nrefs = 0;

  for (int row = 0; row < num_lines && g_nrefs < MAX_REFS; row++) {
    extract_line(row);
    const char *line = line_scratch;
    int len = (int)strlen(line);

    for (int i = 0; i < len;) {
      if (line[i] == ';')
        break; /* a comment mention is not a reference */
      if (!isalnum((unsigned char)line[i]) && line[i] != '_') {
        i++;
        continue;
      }
      int ws = i;
      while (i < len && (isalnum((unsigned char)line[i]) || line[i] == '_'))
        i++;
      if (i - ws != nlen || !strncaseeq(line + ws, name, nlen))
        continue;

      /* Trim leading blanks so the row shows the code, not the indent. */
      const char *text = line;
      while (*text == ' ' || *text == '\t')
        text++;
      snprintf(g_ref_rows[g_nrefs], REF_ROW_LEN, "Ln %-5d %s", row + 1, text);
      g_ref_lines[g_nrefs] = row;
      g_ref_ptrs[g_nrefs] = g_ref_rows[g_nrefs];
      g_nrefs++;
      break; /* one row per line, however often the name occurs on it */
    }
  }
}

/* List every mention of the label under the cursor (Ctrl+U). */
static void editor_find_refs(void) {
  char name[MAX_LABEL_LEN];
  if (word_under_cursor(name, sizeof(name)) == 0) {
    static const char *body[] = {"Place the cursor on a label name",
                                 "to list its references."};
    gfx_window_alert("Find References", body, 2, "OK");
    return;
  }

  refs_scan(name);
  if (g_nrefs == 0) {
    char msg[128];
    snprintf(msg, sizeof(msg), "No references to \"%s\" in this file.", name);
    const char *body[1] = {msg};
    gfx_window_alert("Find References", body, 1, "OK");
    return;
  }

  /* Pre-select the reference nearest the cursor, so the list opens where the
     user already is rather than at the top of the file. */
  int initial = 0;
  for (int i = 1; i < g_nrefs; i++) {
    if (abs(g_ref_lines[i] - cursor_row) < abs(g_ref_lines[initial] - cursor_row))
      initial = i;
  }

  /* Sized for the longest label plus the suffix, so it is never truncated. */
  char title[MAX_LABEL_LEN + 48];
  snprintf(title, sizeof(title), "%s  (%d reference%s)", name, g_nrefs,
           g_nrefs == 1 ? "" : "s");

  int sel = list_pick(title, g_ref_ptrs, NULL, g_nrefs, initial,
                      "Enter:go  Esc:close");
  if (sel < 0)
    return;

  jump_push();
  cursor_row = g_ref_lines[sel];
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

/* Updated to reflect the current line-ending mode before the menu opens. */
static char menu_le_label[28] = "Line Ending: LF";
static const char *menu_file_items[] = {
    "Save (Ctrl+S)", "Save As (Ctrl+Shift+S)", "Open (Ctrl+O)", "---",
    menu_le_label,   "---",                    "Close"};
static const char *menu_edit_items[] = {
    "Undo (Ctrl+Z)",       "Redo (Ctrl+Y)", "---",
    "Cut (Ctrl+X)",        "Copy (Ctrl+C)", "Paste (Ctrl+V)",
    "Select All (Ctrl+A)", "---",           "Search (Ctrl+F)",
    "Replace (Ctrl+H)"};
static const char *menu_nav_items[] = {
    "Go to Line (Ctrl+G)", "Go to Label (Ctrl+L)", "File Top", "File Bottom"};
static const char *menu_view_items[] = {
    "ARM Catalog", "Ndless Syscalls (Shift+Cat)", "Label Browser",
    "Instruction Help (Ctrl+Trig)"};
static const char *menu_settings_items[] = {"Preferences"};
static const char *menu_assemble_items[] = {"Assemble (Ctrl+B)"};

static const MenuDef g_menus[] = {
    {"File", menu_file_items, 7},         {"Edit", menu_edit_items, 10},
    {"Navigate", menu_nav_items, 4},      {"View", menu_view_items, 4},
    {"Assemble", menu_assemble_items, 1}, {"Settings", menu_settings_items, 1},
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
  gfx_panel(px, py, pw, ph, 0); /* dropdown panel: no title bar */

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

/* ================================================================
 * Assemble current file via nasm
 * ================================================================ */
/* Where nasm writes its JSON diagnostics for us to read back (see asmdiag.h
   for the contract).  Lives in the shared Ndless config directory, which
   already holds nstudio.cfg. */
#define ASM_DIAG_FILE "/documents/ndless/nstudio_diag.json"

/*
 * Where nasm will put the program it builds from `src`.
 *
 * This mirrors nasm's own make_outpath() exactly: every dot-extension is
 * stripped from the file name - it cuts at the FIRST dot, not the last - and
 * ".tns" is appended, so /documents/foo/bar.asm.tns becomes
 * /documents/foo/bar.tns.  nasm derives this itself and offers no way to
 * override it from the command line, so recomputing it here is the only way to
 * know what was built; keep the two in step.
 *
 * Returns 0 when no usable path exists: an unsaved buffer, a path too long to
 * extend, or a result identical to the source - the last being exactly the case
 * nasm itself refuses to build, so nothing was produced to run.  A name with no
 * dot simply gains ".tns", matching nasm rather than second-guessing it.
 */
static int editor_output_path(const char *src, char *out, size_t outsz) {
  if (!src || !src[0])
    return 0;
  if (strlen(src) + 5 > outsz)
    return 0; /* no room for the stripped name plus ".tns" and its NUL */

  strncpy(out, src, outsz - 1);
  out[outsz - 1] = '\0';

  char *name = strrchr(out, '/');
  name = name ? name + 1 : out;

  char *dot = strchr(name, '.');
  if (dot)
    *dot = '\0';

  strcat(out, ".tns");
  return strcmp(out, src) != 0;
}

/* Hand the calculator over to `path` and take the screen back afterwards.
   nl_exec runs the child in this process and returns once it exits. */
static void editor_run_program(const char *path) {
  nl_exec(path, 0, NULL);
  gfx_reinit();
}

/* Move the cursor to a 1-based line/column in the open buffer, clamping both
   to what the buffer actually holds, and scroll it into view. */
static void editor_goto_line_col(int line, int col) {
  cursor_row = (line > num_lines) ? num_lines - 1 : line - 1;
  if (cursor_row < 0)
    cursor_row = 0;
  int ll = line_len(&g_buf, cursor_row);
  cursor_col = (col >= 1) ? col - 1 : 0;
  if (cursor_col > ll)
    cursor_col = ll;
  cursor_sync_rowcol();
  scroll_to_cursor();
}

static const char *diag_severity_name(const AsmDiag *d) {
  return d->severity == ADIAG_WARNING
             ? "Warning"
             : (d->severity == ADIAG_NOTE ? "Note" : "Error");
}

/* Select the diagnostic at navigation position `nav_pos` (an index into
   g_diag_nav, in [0, g_diag_nav_count)) and describe it in the status bar.
   Diagnostics in the open file move the cursor to their location; those in
   another file - typically reached through INCLUDE, and previously skipped
   altogether - leave the cursor alone and report where they are, naming the
   file that included them so the origin is not a mystery. */
static void diag_jump_to_nav(int nav_pos) {
  g_diag_cur = nav_pos;
  const AsmDiag *d = &g_diags[g_diag_nav[nav_pos]];
  const char *sev = diag_severity_name(d);

  /* "  [+2 rel]" when secondary locations are available in the detail popup.
     Sized for any int so the formatting is provably never truncated. */
  char rel[24] = "";
  if (d->related_count > 0)
    snprintf(rel, sizeof(rel), "  [+%d rel]", d->related_count);

  if (asmdiag_in_file(d, g_filepath)) {
    editor_goto_line_col(d->line, d->col);
    snprintf(g_diag_status, sizeof(g_diag_status), " %s %d/%d, line %d: %s%s",
             sev, nav_pos + 1, g_diag_nav_count, d->line, d->message, rel);
    return;
  }

  /* Another file: name it, and the immediate includer when nasm gave us one
     (include frames run outermost first, so the parent is the last). */
  char from[80] = "";
  if (d->include_count > 0) {
    const AsmDiagInclude *parent = &d->include_stack[d->include_count - 1];
    snprintf(from, sizeof(from), " (from %s:%d)",
             asmdiag_base_name(parent->file), parent->line);
  }
  snprintf(g_diag_status, sizeof(g_diag_status), " %s %d/%d in %s:%d%s: %s%s",
           sev, nav_pos + 1, g_diag_nav_count, asmdiag_base_name(d->file),
           d->line, from, d->message, rel);
}

/* After an assemble, read the diagnostics nasm left behind and retain every one
   that has a real location, whichever file it is in: those in the open file are
   jumped to, the rest are reported in place by diag_jump_to_nav.  Diagnostics
   with no line at all stay out of the cycle, since there is nothing to show.
   The first entry is selected, preferring one the cursor can actually reach.

   Returns what the diagnostics file said: 0 when nasm wrote an empty batch
   (assembled cleanly), a positive count when it reported problems, and -1 when
   there was no file to read - a cancelled run, or an nasm too old to know the
   option.  Only 0 is proof of a successful build. */
static int editor_load_diagnostics(void) {
  diag_nav_reset();

  int n = asmdiag_parse_file(ASM_DIAG_FILE, g_diags, ASMDIAG_MAX);
  if (n <= 0)
    return n;
  g_ndiags = n;

  int first_here = -1;
  for (int i = 0; i < g_ndiags && g_diag_nav_count < ASMDIAG_MAX; i++) {
    if (g_diags[i].line < 1)
      continue;
    if (first_here < 0 && asmdiag_in_file(&g_diags[i], g_filepath))
      first_here = g_diag_nav_count;
    g_diag_nav[g_diag_nav_count++] = i;
  }

  if (g_diag_nav_count > 0)
    diag_jump_to_nav(first_here >= 0 ? first_here : 0);
  return n;
}

/* Jump to the next / previous navigable diagnostic, wrapping around at the
   ends.  With nothing retained (no assemble yet, or invalidated by an edit)
   they leave a short note instead. */
static void editor_diag_next(void) {
  if (g_diag_nav_count == 0) {
    snprintf(g_diag_status, sizeof(g_diag_status), " No errors to navigate");
    return;
  }
  diag_jump_to_nav((g_diag_cur + 1) % g_diag_nav_count);
}

static void editor_diag_prev(void) {
  if (g_diag_nav_count == 0) {
    snprintf(g_diag_status, sizeof(g_diag_status), " No errors to navigate");
    return;
  }
  diag_jump_to_nav((g_diag_cur - 1 + g_diag_nav_count) % g_diag_nav_count);
}

/* Can the cursor be placed on this secondary location, i.e. is it in the file
   the editor currently shows? */
static int diag_related_reachable(const AsmDiagRelated *r) {
  return r->line >= 1 && asmdiag_same_file(r->file, g_filepath);
}

/*
 * Detail popup for the currently selected diagnostic: its full message, the
 * INCLUDE chain that led to it, and nasm's secondary locations ("previously
 * defined here" and friends).  Related locations inside the open file are
 * selectable and Enter jumps to one; the rest are shown for context only.
 */
static void editor_diag_detail(void) {
  if (g_diag_nav_count == 0) {
    snprintf(g_diag_status, sizeof(g_diag_status), " No diagnostics to show");
    return;
  }

  const AsmDiag *d = &g_diags[g_diag_nav[g_diag_cur]];
  const int nrel = d->related_count;
  const int ninc = d->include_count;

  const int WIN_W = GFX_W - 16;
  const int WIN_X = 8;
  const int TITLE_H = 12;
  const int PAD = 5;
  const int HINT_H = 11;
  const int INNER_W = WIN_W - 2 * PAD - 2;
  const int LINE_H = GFX_FONT_H;
  const int SEC_GAP = 3;
  const int MSG_LINES = 3; /* message is wrapped into at most this many */

  /* Height the content wants: location, wrapped message, then the INCLUDE and
     Related sections when present (each a label plus one row per entry), and a
     final note when nasm gave us more than the caps keep. */
  int body_h = PAD + LINE_H + SEC_GAP + MSG_LINES * LINE_H;
  if (ninc)
    body_h += SEC_GAP + LINE_H + ninc * LINE_H;
  if (nrel)
    body_h += SEC_GAP + LINE_H + nrel * LINE_H;
  if (d->related_truncated || d->include_truncated)
    body_h += LINE_H;
  body_h += PAD;

  int WIN_H = TITLE_H + body_h + HINT_H + 2;
  if (WIN_H > GFX_H - 16)
    WIN_H = GFX_H - 16;
  const int WIN_Y = (GFX_H - WIN_H) / 2;

  int sel = 0; /* selected related location */

  while (any_key_pressed())
    msleep(20);

  for (;;) {
    gfx_panel(WIN_X, WIN_Y, WIN_W, WIN_H, TITLE_H);

    /* Title: severity, plus nasm's stable code when it tagged one. */
    char title[64];
    if (d->code[0])
      snprintf(title, sizeof(title), "%s  %s", diag_severity_name(d), d->code);
    else
      snprintf(title, sizeof(title), "%s", diag_severity_name(d));
    gfx_drawstr_clipped(WIN_X + PAD, WIN_Y + 1 + (TITLE_H - GFX_FONT_H) / 2,
                        title, g_default_theme.title_fg,
                        g_default_theme.title_bg, WIN_W - 2 * PAD);

    int body_top = WIN_Y + 1 + TITLE_H;
    int body_bot = WIN_Y + WIN_H - HINT_H - 1;
    gfx_fillrect(WIN_X + 1, body_top, WIN_W - 2, body_bot - body_top,
                 g_default_theme.bg);

    const uint16_t BG = g_default_theme.bg;
    const uint16_t FG = g_default_theme.fg;
    const uint16_t LABEL = g_default_theme.accent;
    const uint16_t DIM = g_default_theme.border_light;
    int tx = WIN_X + 1 + PAD;
    int ty = body_top + PAD;

    /* Where the diagnostic is, in file:line:col form (col only when known). */
    char loc[128];
    if (d->col >= 1)
      snprintf(loc, sizeof(loc), "%s:%d:%d", asmdiag_base_name(d->file), d->line,
               d->col);
    else
      snprintf(loc, sizeof(loc), "%s:%d", asmdiag_base_name(d->file), d->line);
    gfx_drawstr_clipped(tx, ty, loc, LABEL, BG, INNER_W);
    ty += LINE_H + SEC_GAP;

    ui_draw_wrapped(d->message, tx, ty, INNER_W, MSG_LINES, FG, BG);
    ty += MSG_LINES * LINE_H;

    /* INCLUDE chain, outermost includer first (nasm's order). */
    if (ninc && ty + LINE_H <= body_bot) {
      ty += SEC_GAP;
      gfx_drawstr_clipped(tx, ty, "Included from:", LABEL, BG, INNER_W);
      ty += LINE_H;
      for (int i = 0; i < ninc && ty + LINE_H <= body_bot; i++) {
        char fr[160];
        snprintf(fr, sizeof(fr), "  %s:%d",
                 asmdiag_base_name(d->include_stack[i].file),
                 d->include_stack[i].line);
        gfx_drawstr_clipped(tx, ty, fr, FG, BG, INNER_W);
        ty += LINE_H;
      }
    }

    /* Secondary locations; the reachable ones are selectable. */
    if (nrel && ty + LINE_H <= body_bot) {
      ty += SEC_GAP;
      gfx_drawstr_clipped(tx, ty, "Related:", LABEL, BG, INNER_W);
      ty += LINE_H;
      for (int i = 0; i < nrel && ty + LINE_H <= body_bot; i++) {
        const AsmDiagRelated *r = &d->related[i];
        int reachable = diag_related_reachable(r);
        char row[224];
        snprintf(row, sizeof(row), "%c %s:%d %s", i == sel ? '>' : ' ',
                 asmdiag_base_name(r->file), r->line, r->message);
        uint16_t rbg = (i == sel) ? g_default_theme.accent : BG;
        uint16_t rfg = (i == sel) ? g_default_theme.accent_text
                                  : (reachable ? FG : DIM);
        gfx_fillrect(WIN_X + 1, ty, WIN_W - 2, LINE_H, rbg);
        gfx_drawstr_clipped(tx, ty, row, rfg, rbg, INNER_W);
        ty += LINE_H;
      }
    }

    if ((d->related_truncated || d->include_truncated) &&
        ty + LINE_H <= body_bot)
      gfx_drawstr_clipped(tx, ty, "(more not shown)", DIM, BG, INNER_W);

    int hy = WIN_Y + WIN_H - HINT_H - 1;
    gfx_hline(WIN_X + 1, hy, WIN_W - 2, g_default_theme.border_light);
    gfx_fillrect(WIN_X + 1, hy + 1, WIN_W - 2, HINT_H - 1, BG);
    gfx_drawstr_clipped(WIN_X + PAD, hy + 2,
                        nrel ? "Up/Down: select   Enter: jump   Esc: close"
                             : "Esc: close",
                        FG, BG, WIN_W - 2 * PAD);
    gfx_flip();

    /* gfx_poll_nav() does not block, so idle until something is pressed
       instead of spinning on a full redraw (the shared modal convention). */
    NavAction a;
    for (;;) {
      a = gfx_poll_nav();
      if (a != NAV_NONE)
        break;
      msleep(16);
      idle();
    }

    if (a == NAV_ESC) {
      while (any_key_pressed())
        msleep(20);
      return;
    }
    if (a == NAV_UP && nrel)
      sel = (sel - 1 + nrel) % nrel;
    else if (a == NAV_DOWN && nrel)
      sel = (sel + 1) % nrel;
    else if (a == NAV_ENTER && nrel) {
      const AsmDiagRelated *r = &d->related[sel];
      while (any_key_pressed())
        msleep(20);
      if (diag_related_reachable(r)) {
        jump_push();
        editor_goto_line_col(r->line, r->col);
        snprintf(g_diag_status, sizeof(g_diag_status), " %s, line %d: %s",
                 asmdiag_base_name(r->file), r->line, r->message);
        return;
      }
      /* Not in this buffer: say so rather than moving the cursor somewhere
         misleading. */
      snprintf(g_diag_status, sizeof(g_diag_status), " %s:%d is in another file",
               asmdiag_base_name(r->file), r->line);
      return;
    }
  }
}

static void editor_assemble(void) {
  if (g_settings.nasm_path[0] == '\0') {
    static const char *body[] = {
        "No nasm executable path is set.",
        "Please configure it in Settings > Preferences."};
    gfx_window_alert("Assemble", body, 2, "OK");
    return;
  }

  {
    struct stat st;
    if (stat(g_settings.nasm_path, &st) != 0) {
      static const char *body[] = {
          "The configured nasm executable was not found.",
          "Check the path in Settings > Preferences."};
      gfx_window_alert("Assemble", body, 2, "OK");
      return;
    }
  }

  if (g_filepath[0] == '\0') {
    static const char *body[] = {"The file has not been saved yet.",
                                 "Save it now before assembling."};
    int choice = gfx_window_confirm2("Assemble", body, 2, "Save Now", "Cancel");
    if (choice != 0)
      return;
    if (!editor_save_as())
      return; /* User cancelled the Save As dialog */
  } else if (g_modified) {
    static const char *body[] = {"The file has unsaved changes.",
                                 "Save before assembling?"};
    int choice =
        gfx_window_confirm2("Assemble", body, 2, "Save & Assemble", "Cancel");
    if (choice != 0)
      return;
    if (!editor_do_save())
      return;
  }

  /*
   * argv layout seen by nasm:
   *   argv[0]  = nasm executable name  (set by nl_exec / Ndless)
   *   argv[1]  = asm source file path  (args[0])
   *   argv[2+] = tokens from g_settings.nasm_args
   *
   * nl_exec takes only the *extra* arguments after the program name,
   * and argsn / args[] map straight to argv[1..] of the child.
   *
   * We tokenise nasm_args in a local copy (strtok is destructive).
   * Maximum supported extra tokens: 16.
   */
#define NASM_MAX_ARGS                                                          \
  20 /* 1 (file) + up to 16 flag tokens + 3 (--diagnostics-file, --fb, addr) */
  char args_copy[128];
  strncpy(args_copy, g_settings.nasm_args, sizeof(args_copy) - 1);
  args_copy[sizeof(args_copy) - 1] = '\0';

  char *args[NASM_MAX_ARGS];
  int argsn = 0;
  args[argsn++] = g_filepath; /* always first: the source file */

  /* Keep three slots free for the injected diagnostics-file and "--fb addr". */
  char *tok = strtok(args_copy, " \t");
  while (tok && argsn < NASM_MAX_ARGS - 3) {
    args[argsn++] = tok;
    tok = strtok(NULL, " \t");
  }

  /* Ask nasm to write structured diagnostics to a file we can read back;
     passing this alone is enough to select JSON output.  An older nasm that
     predates the option ignores it, so we simply do not jump. */
  static char diag_arg[] = "--diagnostics-file=" ASM_DIAG_FILE;
  args[argsn++] = diag_arg;

  /* Inject the framebuffer argument so nasm can inherit our screen state */
  char fb_addr_str[32];
  snprintf(fb_addr_str, sizeof(fb_addr_str), "%lu",
           (unsigned long)gfx_framebuffer());
  args[argsn++] = "--fb";
  args[argsn++] = fb_addr_str;

  /* Clear any stale diagnostics from a previous run so a cancelled assemble
     (which never rewrites the file) is never mistaken for this run. */
  remove(ASM_DIAG_FILE);

  nl_exec(g_settings.nasm_path, argsn, args);
  /*
   * nl_exec returns after the child exits (or immediately if it could not
   * be launched).  The child may have re-initialised or restored the LCD
   * mode, so claim the screen back before drawing anything.
   */
  gfx_reinit();

  /* Read nasm's diagnostics and jump to the first error in this file. */
  int ndiag = editor_load_diagnostics();

  /* A clean build (nasm wrote an empty batch) is the only case worth offering
     to run.  Ask first: the program takes over the calculator, and a faulty
     one can take nStudio down with it. */
  char outpath[1024];
  struct stat ost;
  if (ndiag == 0)
    g_build_stale = 0; /* what is on disk now matches this buffer */

  if (ndiag == 0 && editor_output_path(g_filepath, outpath, sizeof(outpath)) &&
      stat(outpath, &ost) == 0) {
    char line[288];
    snprintf(line, sizeof(line), "Output: %s", asmdiag_base_name(outpath));
    const char *body[] = {"Assembly succeeded.", line, "Run it now?"};
    if (gfx_window_confirm2("Assemble", body, 3, "Run", "Close") == 0)
      editor_run_program(outpath);
  }
#undef NASM_MAX_ARGS
}

/*
 * Run the program already built from this source, without reassembling.
 * Runs straight away when the build is newer than the file on disk; when it is
 * older - or the buffer has unsaved edits - it says so first, since the program
 * about to run will not contain those changes.
 */
static void editor_run_build(void) {
  char outpath[1024];
  if (!editor_output_path(g_filepath, outpath, sizeof(outpath))) {
    static const char *body[] = {"This buffer has not been saved,",
                                 "so there is no build to run."};
    gfx_window_alert("Run", body, 2, "OK");
    return;
  }

  struct stat ost;
  if (stat(outpath, &ost) != 0) {
    char line[288];
    snprintf(line, sizeof(line), "No %s found.", asmdiag_base_name(outpath));
    const char *body[] = {line, "Assemble with Ctrl+B first."};
    gfx_window_alert("Run", body, 2, "OK");
    return;
  }

  /*
   * Warn when what we are about to run no longer matches the source.
   *
   * Two things prove that outright: unsaved edits in the buffer, and any change
   * made since the last successful assemble.  Both are tracked here rather than
   * asked of the filesystem, because Ndless does not fill in a modification
   * time - stat() reports zero - so a timestamp comparison silently never
   * fires on the calculator.  It is still attempted, guarded on both stamps
   * looking real, to catch a build left over from an earlier session on a
   * platform that does report times.
   */
  struct stat sst;
  int stale = g_modified || g_build_stale;
  if (!stale && stat(g_filepath, &sst) == 0 && sst.st_mtim.tv_sec > 0 &&
      ost.st_mtim.tv_sec > 0)
    stale = sst.st_mtim.tv_sec > ost.st_mtim.tv_sec;
  if (stale) {
    char line[288];
    snprintf(line, sizeof(line), "%s is older than the source.",
             asmdiag_base_name(outpath));
    const char *body[] = {line, "Run it anyway, without rebuilding?"};
    if (gfx_window_confirm2("Run", body, 2, "Run Anyway", "Cancel") != 0)
      return;
  }

  editor_run_program(outpath);
}

/* True for menu entries that modify the buffer, so read-only files can
   gate them behind a confirmation. */
/* Which menu actions unconditionally mutate the buffer and so require write
   permission up front.  Undo/Redo (no-ops when there is nothing to revert)
   and the View catalog/syscall inserts (gated at the point of insertion) are
   deliberately excluded, mirroring act_needs_write for the keyboard path. */
static int menu_action_edits(int top, int sub) {
  if (top == 1) /* Edit: Cut, Paste, Replace */
    return sub == 3 || sub == 5 || sub == 9;
  return 0;
}

/* Menu action dispatch.
   Returns 1 if handled (close menu + redraw), 2 if close editor requested. */
static int menu_dispatch(int top, int sub) {
  if (menu_action_edits(top, sub) && !editor_confirm_rw())
    return 1;

  if (top == 0) {
    if (sub == 0) {
      editor_do_save();
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
    if (sub == 4) { /* Toggle line endings */
      g_crlf = !g_crlf;
      g_modified = 1;
      return 1;
    }
    if (sub == 6)
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
      if (m && editor_confirm_rw())
        for (const char *p = m; *p; p++)
          do_insert_char(*p);
      return 1;
    }
    if (sub == 1) {
      editor_syscall_catalog();
      return 1;
    }
    if (sub == 2) {
      editor_label_browser();
      return 1;
    }
    if (sub == 3) {
      editor_cheatsheet();
      return 1;
    }
  }

  if (top == 4) {
    if (sub == 0) {
      editor_assemble();
      return 1;
    }
  }

  if (top == 5) {
    if (sub == 0) {
      settings_ui_open();
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

  snprintf(menu_le_label, sizeof(menu_le_label), "Line Ending: %s",
           g_crlf ? "CRLF" : "LF");

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

/* Only text entry and cursor movement auto-repeat when a key is held;
   commands (save, undo, search, catalog, ...) fire once per press. */
static int action_is_repeatable(int act) {
  if (act > 0)
    return 1; /* printable character */
  switch (act) {
  case ACT_ENTER:
  case ACT_BS:
  case ACT_DEL:
  case ACT_TAB:
  case ACT_UNTAB:
  case ACT_LEFT:
  case ACT_RIGHT:
  case ACT_UP:
  case ACT_DOWN:
  case ACT_HOME:
  case ACT_END:
  case ACT_PGUP:
  case ACT_PGDN:
  case ACT_WORD_LEFT:
  case ACT_WORD_RIGHT:
  case ACT_SEL_LEFT:
  case ACT_SEL_RIGHT:
  case ACT_SEL_UP:
  case ACT_SEL_DOWN:
  case ACT_SEL_HOME:
  case ACT_SEL_END:
  case ACT_SEL_WORD_LEFT:
  case ACT_SEL_WORD_RIGHT:
    return 1;
  default:
    return 0;
  }
}

static int key_repeat_poll(void) {
  int act = poll_key();
  int one_shot = !action_is_repeatable(act);
  return gfx_repeat_gate(&g_key_repeat, act, ACT_NONE, one_shot);
}

/* ================================================================
 * Public entry point
 * ================================================================ */
int editor_open(const char *path) {
  if (path && path[0] != '\0') {
    strncpy(g_filepath, path, sizeof(g_filepath) - 1);
    g_filepath[sizeof(g_filepath) - 1] = '\0';
    load_file(path);
    g_readonly = !path_is_asm_source(g_filepath);
  } else {
    g_filepath[0] = '\0';
    gb_init(&g_buf);
    rebuild_lines(&g_buf);
    g_readonly = 0;
  }

  g_modified = 0;
  g_saved = 0;
  g_gb_oom = 0;

  cursor_pos = 0;
  cursor_row = 0;
  cursor_col = 0;
  cursor_goal_col = 0;
  scroll_row = 0;
  scroll_col = 0;

  sel_anchor = SEL_NONE;
  sel_active = 0;
  undo_forget();
  /* A different buffer: whatever was built before says nothing about it. */
  g_build_stale = 0;
  jump_stack_clear(); /* the trail belonged to the previous file */

  render_all();

  if (g_lines_truncated) {
    static const char *body[] = {
        "Not enough memory to index every line.",
        "Later lines are shown but cannot be",
        "navigated separately.  Save your work."};
    gfx_window_alert("Large File", body, 3, "OK");
  }

  while (any_key_pressed())
    msleep(20);

  int running = 1;

  while (running) {
    /* Plain Menu opens the menu bar; Ctrl+Menu is the jump-to-bottom
       shortcut handled by poll_key. */
    if (isKeyPressed(KEY_NSPIRE_MENU) && !isKeyPressed(KEY_NSPIRE_CTRL)) {
      int close = menu_run();
      if (close) {
        if (g_modified) {
          int choice = prompt_unsaved();
          if (choice == 1) {
            if (editor_do_save())
              running = 0;
          } else if (choice == 2) {
            running = 0;
          }
        } else {
          running = 0;
        }
      }
      continue;
    }

    int act = key_repeat_poll();

    if (act == ACT_NONE) {
      msleep(16);
      idle();
      continue;
    }

    /* Any keypress dismisses a transient status message (e.g. an assemble
       error note), restoring the normal status line. */
    g_diag_status[0] = '\0';

    /* Gate a mutating action on a read-only file behind a confirmation.
       Viewers (pickers) and no-op undo/redo are not gated here; the pickers
       prompt at the point they actually insert. */
    if (act_needs_write(act) && !editor_confirm_rw()) {
      render_all();
      continue;
    }

    /* Any non-edit action (movement, save, search...) ends the current
       undo-coalescing group. */
    if (!act_is_edit(act))
      undo_break();

    switch (act) {
    case ACT_ESC:
      if (g_modified) {
        int choice = prompt_unsaved();
        if (choice == 1) { /* Save */
          if (editor_do_save())
            running = 0;
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
      editor_do_save();
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
    case ACT_BS_WORD:
      do_bs_word();
      break;
    case ACT_DEL_WORD:
      do_del_word();
      break;
    case ACT_TAB:
      do_tab();
      break;
    case ACT_UNTAB:
      do_untab();
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
      if (picked && editor_confirm_rw())
        do_insert_char(picked);
      break;
    }

    case ACT_CATALOG: {
      const char *m = catalog_pick();
      if (m && editor_confirm_rw()) {
        for (const char *p = m; *p; p++)
          do_insert_char(*p);
      }
      break;
    }

    case ACT_SYSCALL_CATALOG:
      editor_syscall_catalog();
      break;

    case ACT_JUMP_LABEL:
      editor_jump_to_label();
      break;
    case ACT_JUMP_BACK:
      editor_jump_back();
      break;
    case ACT_FIND_REFS:
      editor_find_refs();
      break;

    case ACT_CHEATSHEET:
      editor_cheatsheet();
      break;

    case ACT_ASSEMBLE:
      editor_assemble();
      break;
    case ACT_RUN:
      editor_run_build();
      break;
    case ACT_DIAG_NEXT:
      editor_diag_next();
      break;
    case ACT_DIAG_PREV:
      editor_diag_prev();
      break;
    case ACT_DIAG_DETAIL:
      editor_diag_detail();
      break;

    default:
      if (act > 0 && act < 128)
        editor_type_char((char)act);
      break;
    }

    if (g_gb_oom) {
      g_gb_oom = 0;
      static const char *body[] = {"Out of memory: the last edit was",
                                   "dropped.  Save your work now."};
      gfx_window_alert("Memory", body, 2, "OK");
    }

    /* Vertical moves keep the goal column; every other action (edits,
       horizontal moves, jumps) re-anchors it to the current column. */
    if (act != ACT_UP && act != ACT_DOWN && act != ACT_PGUP &&
        act != ACT_PGDN && act != ACT_SEL_UP && act != ACT_SEL_DOWN)
      cursor_goal_col = cursor_col;

    if (running) {
      scroll_to_cursor();
      render_all();
    }
  }

  gb_free(&g_buf);
  lines_free();
  /* Release the undo/redo snapshots (up to 32 buffer copies) rather than
     holding them while the user is back in the main menu. */
  undo_forget();
  return g_saved;
}
