/*
 * asmdiag.h
 * Consumer for the structured assembler diagnostics produced by the companion
 * nasm assembler.  This is the nStudio side of the contract; nasm's
 * diag.c is the producer.
 *
 * ------------------------------------------------------------------
 * Contract between nStudio and nasm
 * ------------------------------------------------------------------
 * nStudio launches nasm through nl_exec(), whose child has no reliably
 * captured stdout on the calculator, so the two communicate through a file.
 *
 *   1. nStudio removes the diagnostics file, then invokes:
 *        nasm <source> [user args] --no-ui --report=2 --format=<zehn|prg>
 *             --diagnostics-file=<path> --fb <addr>
 *      Passing --diagnostics-file alone makes nasm emit JSON to that file
 *      (it does not also need --diagnostics=json); the file takes precedence
 *      over stdout.  --no-ui (nasm 1.3) makes nasm show nothing: no format
 *      question, since --format answers it; no extension warning, since
 *      nStudio asks that itself; no outcome window, since nStudio shows the
 *      outcome.  Every option is ignored by an nasm that predates it, which
 *      then asks and shows as it always has - the integration degrades
 *      cleanly, down to an nasm that writes no file at all.
 *
 *   2. nasm writes <path> once, at the end.  With --report=2 (nasm 1.3) it is
 *      a results report: a JSON object saying how the run ended (assembled,
 *      errors, refused, cancelled), the program built, its format and size,
 *      the error and warning counts - every one, including those nasm drops
 *      past its own cap - and the diagnostics.  nasm 1.2 writes only the
 *      diagnostics, as a bare JSON array: "[]" on success, one object per
 *      diagnostic on failure, and nothing at all for a run that stopped
 *      before assembling.  That is why nStudio removes the file up front: a
 *      stale file from a previous run is never mistaken for this one.
 *
 *   3. After nl_exec() returns, nStudio parses <path> as either shape
 *      (asmdiag_parse_report).  Absent/unreadable is "no report".
 *
 *   4. nl_exec() also returns nasm's exit status (ASMDIAG_EXIT_* below), or
 *      Ndless's own code when nasm could not be run at all.
 *      asmdiag_report_outcome() combines the two channels.  From a report, a
 *      build succeeded when it says "assembled" and the status is 0.  From a
 *      bare array, only when the status is 0 AND the array is empty.  An nasm
 *      that predates exit statuses always returns 0, which still works.
 *
 * Results report (from nasm's diag_write_report), keys in any order:
 *   version       2
 *   status        "assembled" | "errors" | "refused" | "cancelled"
 *   output        the program written (or that a failed build would have
 *                 written), "" when there is none
 *   format        "zehn" | "prg", "" when none was chosen
 *   code_size     bytes of machine code, 0 unless assembled
 *   errors, warnings   every diagnostic of that severity, kept or not
 *   diagnostics   the array below
 * Unknown keys are skipped, so later versions can add fields.
 *
 * Diagnostics (from nasm's diag_write_json; reused here verbatim, never
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

/*
 * Diagnostics retained from one assemble.
 *
 * nasm caps its own batch at MAX_ERR_LINES (512), so a very broken file can
 * produce more than this and the extras are dropped - see asmdiag_parse_file.
 * The limit is a memory decision rather than a protocol one: the batch is a
 * static array, so every byte per entry costs this many times over, and no one
 * works through sixty-four errors before reassembling.
 */
#define ASMDIAG_MAX 64

typedef enum { ADIAG_ERROR, ADIAG_WARNING, ADIAG_NOTE } AsmDiagSeverity;

/*
 * Bounded per-diagnostic capacities.  nasm may emit more entries than these;
 * the parser keeps the first N, sets the matching *_truncated flag and never
 * fails - the primary diagnostic is always preserved.  The caps are a
 * deliberate trade-off: the retained batch is a static array of ASMDIAG_MAX
 * diagnostics, so every byte here costs 100x in RAM on the calculator.
 */
/* nasm attaches at most one related location today (the "previously defined
   here" of a redefined label), so two is already headroom; include nesting
   deeper than four is not plausible on this device.  These two arrays are the
   bulk of an entry, so their sizes dominate the static footprint. */
#define ASMDIAG_MAX_RELATED 2
#define ASMDIAG_MAX_INCLUDE 4

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
 *
 * A batch longer than `max` is truncated to the first `max` entries.  Since the
 * count returned is then exactly `max`, a caller that cares can tell it may
 * have lost some and say so rather than presenting the total as complete.
 */
int asmdiag_parse_file(const char *path, AsmDiag *out, int max);

/* How the run ended, as a results report states it. */
typedef enum {
  ASMRUN_UNKNOWN,   /* no report, or a status this nStudio does not know */
  ASMRUN_ASSEMBLED, /* the program on disk is this build */
  ASMRUN_ERRORS,    /* not assembled; the diagnostics say why */
  ASMRUN_REFUSED,   /* not assembled: the program would overwrite the source */
  ASMRUN_CANCELLED  /* not assembled: the user cancelled in nasm */
} AsmRunStatus;

/* What the diagnostics file says beyond the diagnostics themselves. */
typedef struct {
  int version; /* 0: no file; 1: nasm 1.2's bare array; 2+: a results report.
                  The fields below are filled only from a report. */
  AsmRunStatus status;
  char output[512]; /* the program built, or "" */
  char format[8];   /* "zehn", "prg", or "" */
  int code_size;    /* bytes of machine code */
  int errors;       /* every error nasm found, including any dropped */
  int warnings;     /* likewise for warnings */
} AsmReport;

/*
 * Parse the diagnostics file at `path`, in either shape: fill *rep, and the
 * diagnostics into out[0..max) exactly as asmdiag_parse_file() does, with the
 * same return value.  *rep is always set; an absent file leaves version 0.
 */
int asmdiag_parse_report(const char *path, AsmReport *rep, AsmDiag *out,
                         int max);

/*
 * Index of the first diagnostic that belongs to `path` (see
 * asmdiag_same_file) and carries a real line number (line >= 1), or -1 if
 * none - e.g. when every error is in an INCLUDEd file.  Used to jump only when
 * the location exists in the buffer the editor currently shows.
 */
int asmdiag_first_for_file(const AsmDiag *diags, int n, const char *path);

/*
 * Whether diagnostic `d` belongs to the file `path` (see asmdiag_same_file)
 * and carries a real line number (line >= 1) - i.e. it has a location the
 * editor can jump to in the buffer it currently shows.  Shared by
 * asmdiag_first_for_file and by the editor's error-navigation list.
 */
int asmdiag_in_file(const AsmDiag *d, const char *path);

/*
 * Whether a file path recorded in a diagnostic (or in one of its related
 * locations / include frames) refers to `path`.  Both are normalised first -
 * "//", "." and ".." segments resolved - and then compared whole: two files
 * that merely share a name in different directories are different files.
 * Exposed so callers can test a related location, which carries its own file,
 * with exactly the rule asmdiag_in_file applies to the primary.
 */
int asmdiag_same_file(const char *diag_file, const char *path);

/* Where a diagnostic can be shown in the open file - see asmdiag_locate. */
typedef enum {
  ASMDIAG_NOWHERE,   /* no line, or in a file the open one does not include */
  ASMDIAG_AT_LINE,   /* on its own line, in the open file */
  ASMDIAG_AT_INCLUDE /* in an included file: at the INCLUDE that brought it in */
} AsmDiagPlace;

/*
 * Where diagnostic `d` can be shown in the file `path`: its own line (and
 * column) when it is in that file; otherwise the line of the innermost INCLUDE
 * in that file through which its own file was reached, with *col 0.  *line and
 * *col are set unless the result is ASMDIAG_NOWHERE.
 */
AsmDiagPlace asmdiag_locate(const AsmDiag *d, const char *path, int *line,
                            int *col);

/* Base name (component after the last '/') of a path; "" for NULL.  Handy for
   showing a compact origin in a status line. */
const char *asmdiag_base_name(const char *path);

/*
 * What nl_exec() returns when the program never ran: Ndless's loader reports
 * 0xDEAD when it could not load it at all (missing, not a program, out of
 * memory), and 0xBEEF when it declined and has already told the user why.
 */
#define ASMDIAG_NL_CANNOT_RUN 0xDEAD
#define ASMDIAG_NL_DECLINED 0xBEEF

/* nasm's exit status for a command-line assemble (its NASM_EXIT_* values). */
#define ASMDIAG_EXIT_ASSEMBLED 0 /* the output file holds this build */
#define ASMDIAG_EXIT_ERRORS 1    /* not assembled; the diagnostics say why */
#define ASMDIAG_EXIT_NOT_RUN 2   /* cancelled or refused before assembling */
#define ASMDIAG_EXIT_NO_REPORT 3 /* the diagnostics file could not be written */

/* What one assemble came to, judged from both channels together. */
typedef enum {
  ASMBUILD_OK,          /* assembled: the program on disk is this build */
  ASMBUILD_ERRORS,      /* failed, with diagnostics to show */
  ASMBUILD_FAILED,      /* failed, without any diagnostic to show */
  ASMBUILD_NOT_RUN,     /* nasm assembled nothing: cancelled or refused */
  ASMBUILD_NO_REPORT,   /* nasm could not write the diagnostics file */
  ASMBUILD_NOT_STARTED, /* Ndless could not run nasm */
  ASMBUILD_DECLINED,    /* Ndless declined to run nasm, and said why */
  ASMBUILD_UNKNOWN,     /* status 0 but no diagnostics file: an nasm older
                           than both channels, which cannot say how it went */
  ASMBUILD_REFUSED      /* nasm would not build: the program would overwrite
                           the source.  Only a report says so; nasm 1.2 shows
                           it itself and returns NOT_RUN */
} AsmBuildOutcome;

/*
 * Combine nl_exec()'s return value with asmdiag_parse_file()'s result.  Only
 * status 0 with an empty batch is success.  A non-empty batch means errors
 * whatever the status, since an older nasm always returns 0, and a status
 * this nStudio does not know is never taken for success.
 */
AsmBuildOutcome asmdiag_build_outcome(int exit_status, int ndiag);

/*
 * The same judgement from asmdiag_parse_report()'s results.  A results report
 * is believed over the batch: "assembled" with status 0 is success even with
 * warnings in the batch.  Without one (version < 2), this is exactly
 * asmdiag_build_outcome().
 */
AsmBuildOutcome asmdiag_report_outcome(int exit_status, const AsmReport *rep,
                                       int ndiag);

#endif /* ASMDIAG_H_INCLUDED */
