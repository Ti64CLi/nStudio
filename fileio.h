/*
 * fileio.h
 * Atomic whole-file replacement via a verified temporary + rename.
 */
#ifndef FILEIO_H_INCLUDED
#define FILEIO_H_INCLUDED

#include <stdio.h>

/*
 * Replace the file at `path` atomically.  `writer` is invoked with a stream
 * open on a sibling temporary file and the caller's `ctx`, and must return 1
 * on success or 0 on failure.  The destination is replaced only once a
 * complete, verified temporary exists, so a failed or partial write never
 * truncates or corrupts the file already there.
 *
 * Returns 1 only when `path` holds the fully written new content.
 */
int write_file_atomic(const char *path, int (*writer)(FILE *, void *),
                      void *ctx);

#endif /* FILEIO_H_INCLUDED */
