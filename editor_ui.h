/*
 * editor_ui.h
 * Modal reference pickers for the editor.
 *
 * These dialogs are pure reference UI: each takes over the screen, runs its own
 * key loop and returns what the user chose (or nothing, when cancelled).  They
 * read the static tables in asmdb.c and draw through gfx.c, and deliberately
 * know nothing about the text buffer, the cursor or the open file - acting on
 * the result, including any read-only confirmation, is the caller's job.  That
 * separation is what keeps them out of editor.c.
 */
#ifndef EDITOR_UI_H_INCLUDED
#define EDITOR_UI_H_INCLUDED

#include "asmdb.h"

/* Grid of special characters that are not on the keypad.
   Returns the selected character, or 0 when cancelled. */
char charmap_pick(void);

/* Categorised ARM instruction catalog: collapsible groups, with a detail view
   for the selected entry.  Returns the chosen mnemonic - a pointer into the
   static catalog tables, so it stays valid - or NULL when cancelled. */
const char *catalog_pick(void);

/* Scrollable list of Ndless syscalls with their numbers and signatures.
   Returns the chosen entry (static storage), or NULL when cancelled. */
const SyscallInfo *syscall_pick(void);

/* Detail popup for one syscall: name, number, arguments and description.
   Shared with the cheat sheet, which shows it for the SWI under the cursor. */
void syscall_show_desc(const SyscallInfo *si);

/* Full-width detail popup for one instruction: signature, description and the
   CPSR flags it affects.  Used by the cheat sheet for the mnemonic under the
   cursor; the catalog keeps its own narrower detail view. */
void mnem_show_desc(const MnemInfo *mi);

/* Draw `text` wrapped at word boundaries into a popup body, at most
   `max_lines` lines.  Returns the number of lines drawn.  Shared by the popups
   here and by the editor's diagnostic details. */
int ui_draw_wrapped(const char *text, int wx, int ty, int max_w, int max_lines,
                    uint16_t fg, uint16_t bg);

/* ------------------------------------------------------------------ */
/* Label browser                                                      */
/* ------------------------------------------------------------------ */

#define MAX_LABEL_LEN 64

/* One entry of the label table.  The editor scans these out of the buffer -
   under nasm's rule, an identifier in column 0 with no trailing colon - and
   the picker below only displays them. */
typedef struct {
  char name[MAX_LABEL_LEN];
  int line; /* 0-based line index */
} LabelEntry;

/* Modal list of the labels defined in the file.  `cur_line` is the line the
   cursor is on, used to pre-select the nearest label.  Returns the index of
   the chosen entry, or -1 when cancelled; moving the cursor is the caller's
   job, so this stays free of editor state like every dialog here. */
int label_pick(const LabelEntry *labels, int n, int cur_line);

/* ------------------------------------------------------------------ */
/* Generic list picker                                                */
/* ------------------------------------------------------------------ */

/*
 * Modal list of pre-formatted rows; returns the chosen index, or -1 when
 * cancelled or when the selected row is not actionable.  `actionable` may be
 * NULL, or an array of flags marking which rows can be chosen - the rest are
 * dimmed and shown for context.  `initial` pre-selects a row.
 *
 * Callers format their own row text, which is what lets one widget serve
 * lists whose columns differ (label references, diagnostics).
 */
int list_pick(const char *title, const char *const *rows,
              const char *actionable, int n, int initial, const char *hint);

#endif /* EDITOR_UI_H_INCLUDED */
