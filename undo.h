/*
 * undo.h
 * Undo/redo history stored as deltas rather than whole-buffer copies.
 *
 * A snapshot-per-edit ring costs the file size times the ring depth, which on
 * a large source is more memory than the calculator has - so undo would simply
 * stop working there.  Each entry here holds only the region an edit changed,
 * in both directions, so one entry serves undo and redo alike.
 *
 * The history is deliberately kept independent of the gap buffer and the
 * editor's globals: it takes and returns plain byte arrays, and the caller is
 * responsible for applying a span to whatever holds the text.  That keeps the
 * part that would corrupt the user's work if it were wrong testable on the
 * host, which is the only real defence against that happening.
 */
#ifndef UNDO_H_INCLUDED
#define UNDO_H_INCLUDED

/* Edits retained in each direction. */
#define UNDO_MAX 32

/*
 * One recorded edit: the text at [pos, pos+old_len) became the text at
 * [pos, pos+new_len).  Undo replaces the new bytes with `old_text`; redo
 * replaces the old bytes with `new_text`.  The cursor/selection fields are the
 * editor state to restore in each direction.
 */
typedef struct {
  int pos;
  char *old_text; /* bytes present before the edit, or NULL when none */
  int old_len;
  char *new_text; /* bytes present after the edit, or NULL when none */
  int new_len;

  int cursor_before, anchor_before, active_before;
  int cursor_after, anchor_after, active_after;
} UndoDelta;

/* Drop the whole history and release its memory. */
void undo_reset(void);

/*
 * Record the transition from `before` to `after`, storing only what differs.
 * Also clears the redo history, since a new edit invalidates it.
 *
 * Returns 1 when an entry was stored, 0 when the two texts are identical (no
 * entry needed) or memory ran out (history is left intact and consistent).
 */
int undo_record(const char *before, int blen, const char *after, int alen,
                int cursor_before, int anchor_before, int active_before,
                int cursor_after, int anchor_after, int active_after);

int undo_can_undo(void);
int undo_can_redo(void);

/*
 * Move the newest edit from one history to the other and return it, so the
 * caller can apply it; NULL when that direction is empty.  The returned
 * pointer is owned by the history and stays valid until the next call.
 */
const UndoDelta *undo_take_undo(void);
const UndoDelta *undo_take_redo(void);

#endif /* UNDO_H_INCLUDED */
