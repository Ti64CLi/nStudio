/*
 * undo.c
 * Delta-based undo/redo history.  See undo.h for the model.
 */

#include <stdlib.h>
#include <string.h>

#include "textdiff.h"
#include "undo.h"

static UndoDelta g_undo[UNDO_MAX];
static int g_undo_head; /* next write slot */
static int g_undo_count;

static UndoDelta g_redo[UNDO_MAX];
static int g_redo_head;
static int g_redo_count;

static void delta_free(UndoDelta *d) {
  free(d->old_text);
  free(d->new_text);
  memset(d, 0, sizeof(*d));
}

static void ring_clear(UndoDelta *ring, int *head, int *count) {
  for (int i = 0; i < UNDO_MAX; i++)
    delta_free(&ring[i]);
  *head = 0;
  *count = 0;
}

/* Store `d` in the ring, taking ownership of its buffers.  The slot being
   overwritten is the oldest entry, which is dropped to make room. */
static UndoDelta *ring_push(UndoDelta *ring, int *head, int *count,
                            const UndoDelta *d) {
  UndoDelta *slot = &ring[*head];
  delta_free(slot);
  *slot = *d;
  *head = (*head + 1) % UNDO_MAX;
  if (*count < UNDO_MAX)
    (*count)++;
  return slot;
}

/* Detach the newest entry.  The slot is zeroed, so ownership passes to the
   caller and the entry can be handed to the other ring without a double free.*/
static int ring_pop(UndoDelta *ring, int *head, int *count, UndoDelta *out) {
  if (*count == 0)
    return 0;
  *head = (*head - 1 + UNDO_MAX) % UNDO_MAX;
  (*count)--;
  *out = ring[*head];
  memset(&ring[*head], 0, sizeof(ring[*head]));
  return 1;
}

void undo_reset(void) {
  ring_clear(g_undo, &g_undo_head, &g_undo_count);
  ring_clear(g_redo, &g_redo_head, &g_redo_count);
}

int undo_can_undo(void) { return g_undo_count > 0; }
int undo_can_redo(void) { return g_redo_count > 0; }

int undo_record(const char *before, int blen, const char *after, int alen,
                int cursor_before, int anchor_before, int active_before,
                int cursor_after, int anchor_after, int active_after) {
  int pos, old_len, new_len;
  buf_diff_span(before, blen, after, alen, &pos, &old_len, &new_len);

  if (old_len == 0 && new_len == 0)
    return 0; /* nothing actually changed */

  UndoDelta d;
  memset(&d, 0, sizeof(d));
  d.pos = pos;
  d.old_len = old_len;
  d.new_len = new_len;

  /* Allocate both sides before touching the history, so a failure leaves it
     exactly as it was. */
  if (old_len > 0) {
    d.old_text = (char *)malloc((size_t)old_len);
    if (!d.old_text)
      return 0;
    memcpy(d.old_text, before + pos, (size_t)old_len);
  }
  if (new_len > 0) {
    d.new_text = (char *)malloc((size_t)new_len);
    if (!d.new_text) {
      free(d.old_text);
      return 0;
    }
    memcpy(d.new_text, after + pos, (size_t)new_len);
  }

  d.cursor_before = cursor_before;
  d.anchor_before = anchor_before;
  d.active_before = active_before;
  d.cursor_after = cursor_after;
  d.anchor_after = anchor_after;
  d.active_after = active_after;

  ring_push(g_undo, &g_undo_head, &g_undo_count, &d);

  /* Editing after an undo abandons the redo path, as it always has. */
  ring_clear(g_redo, &g_redo_head, &g_redo_count);
  return 1;
}

const UndoDelta *undo_take_undo(void) {
  UndoDelta d;
  if (!ring_pop(g_undo, &g_undo_head, &g_undo_count, &d))
    return NULL;
  return ring_push(g_redo, &g_redo_head, &g_redo_count, &d);
}

const UndoDelta *undo_take_redo(void) {
  UndoDelta d;
  if (!ring_pop(g_redo, &g_redo_head, &g_redo_count, &d))
    return NULL;
  return ring_push(g_undo, &g_undo_head, &g_undo_count, &d);
}
