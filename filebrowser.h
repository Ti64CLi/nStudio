#ifndef FILEBROWSER_H
#define FILEBROWSER_H

/*
 * filebrowser.h
 * Filesystem browser for TI-Nspire CX / CX II (Ndless)
 *
 * Displays a scrollable list of files rooted at /documents.
 * Default filter: *.asm.tns  |  Tab toggles to show all files.
 * Up/Down to navigate, Enter to select, Escape to cancel.
 *
 * Returns the full path of the selected file, or NULL if cancelled.
 * The returned pointer is a static buffer : copy it before calling again.
 */
const char *filebrowser_select(void);

#endif /* FILEBROWSER_H */
