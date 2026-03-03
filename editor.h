#ifndef EDITOR_H
#define EDITOR_H

/*
 * editor.h
 * ARM assembly text editor for TI-Nspire CX / CX II (Ndless)
 *
 * Features:
 *   - Gap-buffer backed text storage (efficient insert/delete)
 *   - ARM assembly syntax highlighting
 *   - Line numbers gutter
 *   - Horizontal + vertical scrolling
 *   - Save (ctrl+S via [ctrl] key), Quit (Esc with unsaved-changes prompt)
 *
 * Call editor_open(path) to launch the editor on a file.
 * Returns 1 if the file was saved, 0 if quit without saving.
 *
 * gfx_init() must have been called before editor_open().
 * The editor does NOT call gfx_deinit() on exit.
 */

/*
 * Open the editor on an existing file, or on an empty buffer if path
 * does not yet exist.  Blocks until the user quits.
 * Returns 1 if the file was written, 0 otherwise.
 */
int editor_open(const char *path);

#endif /* EDITOR_H */
