/*
 * browser.h
 * File / directory browser window for TI-Nspire CX / CX II.
 *
 * One implementation serves every picker in nStudio:
 *   - main menu "Open File"
 *   - editor Ctrl+O
 *   - Save As destination-directory picker
 *   - settings "select nasm manually"
 *
 * Controls: Up/Down navigate, Enter opens a folder / selects a file,
 * Esc cancels.  Tab toggles the *.<ext>.tns filter in file mode, or
 * selects the current directory in pick-dir mode.
 *
 * The displayed path is prefixed with an extra '/' ("//documents") to
 * mark root folders, matching the calculator's own convention.
 */
#ifndef BROWSER_H_INCLUDED
#define BROWSER_H_INCLUDED

/* Pick a file.  filter_asm=1 starts with the asm-source filter enabled
   (*.<ext>.tns and *.<ext>; Tab shows all files).  Returns 1 with the
   full path in out, 0 on cancel. */
int browser_pick_file(const char *start_dir, int filter_asm, char *out,
                      int outsz);

/* Pick a directory (Tab selects the currently shown directory).
   Returns 1 with the directory path in out, 0 on cancel. */
int browser_pick_dir(const char *start_dir, char *out, int outsz);

#endif /* BROWSER_H_INCLUDED */
