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
 *   related       [ {file,line,col,end_col,message} ] (stored, capped)
 *   include_stack [ {file,line} ]                      (stored, capped)
 * Both arrays are always present, empty when there is nothing to report;
 * include frames run outermost includer first.
 * Strings are JSON-escaped.  This parser reads by key name (not position),
 * skips nested arrays/objects it does not consume, and tolerates keys it does
 * not know, so it stays compatible if nasm adds fields.
 *
 * ------------------------------------------------------------------
 * Extending this later (kept deliberately cheap)
 * ------------------------------------------------------------------
 * asmdiag_parse_file() returns the whole batch, and AsmDiag keeps the span,
 * code, secondary locations and INCLUDE chain, so a diagnostics panel,
 * jump-to-related or quick-fixes only need to add UI - no change to the
 * protocol or this parser.
 */
#ifndef ASMDIAG_H_INCLUDED
#define ASMDIAG_H_INCLUDED

/* Upper bound on diagnostics retained from one assemble.  nasm's own engine
   caps its output near this, so overflow is not expected in practice. */
#define ASMDIAG_MAX 100

typedef enum { ADIAG_ERROR, ADIAG_WARNING, ADIAG_NOTE } AsmDiagSeverity;

/*
 * Bounded per-diagnostic capacities.  nasm may emit more entries than these;
 * the parser keeps the first N, sets the matching *_truncated flag and never
 * fails - the primary diagnostic is always preserved.  The caps are a
 * deliberate trade-off: the retained batch is a static array of ASMDIAG_MAX
 * diagnostics, so every byte here costs 100x in RAM on the calculator.
 */
#define ASMDIAG_MAX_RELATED 3
#define ASMDIAG_MAX_INCLUDE 6

/*
 * A secondary source location explaining the diagnostic, each with its own
 * message (e.g. "previously defined here").  Mirrors nasm's DiagRelated.
 */
typedef struct {
  int line;    /* 1-based, or -1 when unknown */
  int col;     /* 1-based start column, or 0 when unknown */
  int end_col; /* 1-based exclusive end column, or 0 when unknown */
  char file[256];
  char message[192];
} AsmDiagRelated;

/*
 * One frame of the INCLUDE chain: a file that included the next one, and the
 * line of the INCLUDE directive within it.  nasm orders these outermost
 * includer first, immediate parent last; that order is preserved verbatim.
 */
typedef struct {
  int line; /* 1-based line of the INCLUDE directive in `file` */
  char file[256];
} AsmDiagInclude;

typedef struct {
  AsmDiagSeverity severity;
  int line;    /* 1-based, or -1 when not tied to a line */
  int col;     /* 1-based start column, or 0 when unknown */
  int end_col; /* 1-based exclusive end column, or 0 when unknown */
  char code[32];
  char file[256];
  char message[192];

  /* Secondary locations, in the order nasm emitted them. */
  AsmDiagRelated related[ASMDIAG_MAX_RELATED];
  int related_count;     /* how many are stored (0..ASMDIAG_MAX_RELATED) */
  int related_truncated; /* nasm emitted more than we kept */

  /* INCLUDE chain, empty when the diagnostic is in the top-level file. */
  AsmDiagInclude include_stack[ASMDIAG_MAX_INCLUDE];
  int include_count;     /* how many are stored (0..ASMDIAG_MAX_INCLUDE) */
  int include_truncated; /* nasm emitted a deeper chain than we kept */
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

/*
 * Whether diagnostic `d` belongs to the file `path` (matched by full path,
 * else by base name) and carries a real line number (line >= 1) - i.e. it has
 * a location the editor can jump to in the buffer it currently shows.  Shared
 * by asmdiag_first_for_file and by the editor's error-navigation list.
 */
int asmdiag_in_file(const AsmDiag *d, const char *path);

#endif /* ASMDIAG_H_INCLUDED */
