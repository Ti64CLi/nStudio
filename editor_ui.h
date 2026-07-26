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

#endif /* EDITOR_UI_H_INCLUDED */
