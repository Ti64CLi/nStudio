/*
 * asmdiag.h
 * Consumer for the structured assembler diagnostics produced by the companion
 * nasm assembler (../nasm).  This is the nStudio side of the contract; nasm's
 * diag.c is the producer.
 *
 * ------------------------------------------------------------------
 * Contract between nStudio and nasm
 * ------------------------------------------------------------------
 * nStudio launches nasm through nl_exec(), whose child has no reliably
 * captured stdout on the calculator, so the two communicate through a file.
 *
 *   1. nStudio removes the diagnostics file, then invokes:
 *        nasm <source> [user args] --diagnostics-file=<path> --fb <addr>
 *      Passing --diagnostics-file alone makes nasm emit JSON to that file
 *      (it does not also need --diagnostics=json); the file takes precedence
 *      over stdout.  An older nasm that predates the option ignores it as an
 *      unknown flag, so nothing is written and nStudio simply does not jump -
 *      the integration degrades cleanly.
 *
 *   2. nasm writes the whole batch to <path> once, after assembling, as a
 *      JSON array - "[]" on success, one object per diagnostic on failure.
 *      Because a cancelled run (e.g. the extension warning) returns before
 *      that write, nStudio removes the file up front so a stale file from a
 *      previous run is never mistaken for this run's result.
 *
 *   3. After nl_exec() returns, nStudio parses <path>.  Absent/unreadable is
 *      "no diagnostics available"; an empty array is "assembled cleanly".
 *
 * JSON schema (from nasm's diag_write_json; reused here verbatim, never
 * reinterpreted).  Each object has, in order:
 *   severity      "error" | "warning" | "note"
 *   code          stable short identifier, or ""
 *   file          originating file path
 *   line          1-based line number, or -1 when not line-specific
 *   col, end_col  1-based half-open column span, 0 when unknown
 *   message       human-readable text
 *   source_line   text of the offending line          (parsed-and-ignored)
 *   expanded_from pseudo-op the line came from, or ""  (parsed-and-ignored)
 *   related       [ {file,line,col,end_col,message} ] (parsed-and-ignored)
 *   include_stack [ {file,line} ]                      (parsed-and-ignored)
 * Strings are JSON-escaped.  This parser reads by key name (not position),
 * skips nested arrays/objects it does not consume, and tolerates keys it does
 * not know, so it stays compatible if nasm adds fields.
 *
 * ------------------------------------------------------------------
 * Extending this later (kept deliberately cheap)
 * ------------------------------------------------------------------
 * asmdiag_parse_file() already returns the whole batch, and AsmDiag keeps the
 * span and code, so a diagnostics panel, next/previous-error navigation or
 * quick-fixes only need to retain the array and add UI - no change to the
 * protocol or this parser.  M5 itself consumes only the first in-file error.
 */
#ifndef ASMDIAG_H_INCLUDED
#define ASMDIAG_H_INCLUDED

/* Upper bound on diagnostics retained from one assemble.  nasm's own engine
   caps its output near this, so overflow is not expected in practice. */
#define ASMDIAG_MAX 100

typedef enum { ADIAG_ERROR, ADIAG_WARNING, ADIAG_NOTE } AsmDiagSeverity;

typedef struct {
  AsmDiagSeverity severity;
  int line;    /* 1-based, or -1 when not tied to a line */
  int col;     /* 1-based start column, or 0 when unknown */
  int end_col; /* 1-based exclusive end column, or 0 when unknown */
  char code[32];
  char file[256];
  char message[192];
} AsmDiag;

/*
 * Parse the JSON diagnostics file at `path` into out[0..max).  Returns the
 * number of diagnostics stored (0..max), or -1 if the file is absent or
 * cannot be read.  A malformed document is parsed best-effort.
 */
int asmdiag_parse_file(const char *path, AsmDiag *out, int max);

/*
 * Index of the first diagnostic that belongs to `path` (matched by full path,
 * else by base name) and carries a real line number (line >= 1), or -1 if
 * none - e.g. when every error is in an INCLUDEd file.  Used to jump only when
 * the location exists in the buffer the editor currently shows.
 */
int asmdiag_first_for_file(const AsmDiag *diags, int n, const char *path);

#endif /* ASMDIAG_H_INCLUDED */
