/*
 * fileio.c
 * Atomic whole-file replacement shared by the editor's document save and the
 * settings writer.
 */

#include <stdio.h>

#include "fileio.h"

/*
 * Write a file at `path` without ever risking the file already there.
 *
 * The content is produced by `writer`, which is called with an open stream on
 * a sibling temporary file (<path>.tmp) plus the caller's `ctx`.  Every step
 * is verified: the writer's own return value, ferror() on the stream (so a
 * failed fprintf/fwrite inside the writer is caught), and the final fclose()
 * (which flushes buffered data and can fail on a full disk).  Only once a
 * complete, verified temporary exists is the destination touched.  If any step
 * fails the temporary is removed and the original file is left exactly as it
 * was, so a failed save can never truncate or corrupt existing work.
 *
 * Nucleus rename() does not overwrite an existing destination, so the target
 * is removed immediately before the rename (harmless when it does not yet
 * exist).  The freshly written content lives in the temporary throughout, and
 * is deliberately left there if the final rename fails, so nothing is lost.
 *
 * Returns 1 only when `path` holds the fully written new content.
 */
int write_file_atomic(const char *path, int (*writer)(FILE *, void *),
                      void *ctx) {
  /* Large enough for the longest path nStudio builds (Save As uses a
     2048-byte buffer) plus the ".tmp" suffix. */
  char tmp[2100];
  if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
    return 0; /* path too long to form a temporary name */

  FILE *f = fopen(tmp, "wb");
  if (!f)
    return 0;

  int ok = writer(f, ctx);

  if (ferror(f))
    ok = 0;
  if (fclose(f) != 0)
    ok = 0;

  if (!ok) {
    remove(tmp); /* discard the partial file; the original is untouched */
    return 0;
  }

  /* Move the verified temporary into place. */
  remove(path); /* Nucleus rename() will not overwrite; clear first */
  if (rename(tmp, path) != 0)
    return 0; /* content is preserved in `tmp`; leave it for recovery */

  return 1;
}
