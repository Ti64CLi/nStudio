/*
 * nStudio's side of an assemble, on the build machine: the code the editor
 * runs (nasmlink.c, asmdiag.c) around a real nasm - nasm_cli, nasm's own main().
 *
 *   nstudio_side <nasm> <source> <user args> <diagnostics file> <format>
 *
 * It builds nasm's command line with nasmlink_invocation(), clears the
 * diagnostics file, runs nasm - fork and exec standing in for nl_exec - and
 * then judges the run as the editor does: the outcome, from the exit status
 * and the report together; the program it would offer to run; and where each
 * diagnostic lands in the source.  For a diagnostic reached through an INCLUDE
 * it also reads that INCLUDE line and resolves it as the editor would, which
 * must name the very file nasm reported.
 *
 * <format> is the Build Format preference, "zehn" or "prg" - or "legacy", for
 * the command line nStudio sent before nasm 1.3 (no --no-ui, --report or
 * --format), which nasm must still answer exactly as it used to.
 *
 * The report goes to stdout, with paths relative to the source's directory and
 * any byte outside printable ASCII written as \xNN, so the expected reports
 * are plain text that does not depend on where the test runs.  nasm's own
 * output goes to stderr.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "asmdiag.h"
#include "nasmlink.h"

static char g_base[1024]; /* the source's directory, with its trailing '/' */

/* Print `s` relative to the source's directory, escaping unprintable bytes. */
static void put_path(const char *s) {
  size_t blen = strlen(g_base);
  if (strncmp(s, g_base, blen) == 0)
    s += blen;
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c >= 0x20 && c < 0x7f)
      putchar(c);
    else
      printf("\\x%02x", c);
  }
}

/* What nl_exec does for nStudio: run `prog` with `argv` after its name, and
   return its exit status - or Ndless's 0xDEAD when it cannot be run at all. */
static int host_nl_exec(const char *prog, int argc, char **argv) {
  if (access(prog, X_OK) != 0)
    return ASMDIAG_NL_CANNOT_RUN;

  char *full[NASMLINK_MAX_ARGS + 2];
  full[0] = (char *)prog;
  for (int i = 0; i < argc; i++)
    full[i + 1] = argv[i];
  full[argc + 1] = NULL;

  fflush(stdout);
  pid_t pid = fork();
  if (pid < 0)
    return ASMDIAG_NL_CANNOT_RUN;
  if (pid == 0) {
    dup2(2, 1); /* nasm's own output must not mix into the report */
    execv(prog, full);
    _exit(127);
  }

  int st;
  if (waitpid(pid, &st, 0) < 0)
    return ASMDIAG_NL_CANNOT_RUN;
  if (WIFSIGNALED(st)) {
    printf("nasm crashed (signal %d)\n", WTERMSIG(st));
    exit(1);
  }
  return WEXITSTATUS(st);
}

/* Line `n` (1-based) of `path`, without its line ending, into `out`. */
static int read_line(const char *path, int n, char *out, size_t outsz) {
  FILE *f = fopen(path, "r");
  if (!f)
    return 0;
  char *line = NULL;
  size_t cap = 0;
  int found = 0;
  for (int i = 1; getline(&line, &cap, f) >= 0; i++) {
    if (i == n) {
      line[strcspn(line, "\r\n")] = '\0';
      snprintf(out, outsz, "%s", line);
      found = 1;
      break;
    }
  }
  free(line);
  fclose(f);
  return found;
}

static const char *outcome_name(AsmBuildOutcome o) {
  static const char *const names[] = {
      "OK",          "ERRORS",   "FAILED",  "NOT_RUN", "NO_REPORT",
      "NOT_STARTED", "DECLINED", "UNKNOWN", "REFUSED"};
  return names[o];
}

static const char *severity_name(AsmDiagSeverity s) {
  return s == ADIAG_WARNING ? "warning" : s == ADIAG_NOTE ? "note" : "error";
}

/* The container nasm wrote, from the program's first bytes. */
static const char *container(const char *path) {
  char head[4] = {0};
  FILE *f = fopen(path, "rb");
  if (!f)
    return "?";
  size_t got = fread(head, 1, sizeof(head), f);
  fclose(f);
  if (got == 4 && memcmp(head, "Zehn", 4) == 0)
    return "Zehn";
  if (got >= 3 && memcmp(head, "PRG", 3) == 0)
    return "PRG";
  return "unknown";
}

static const char *run_status_name(AsmRunStatus s) {
  static const char *const names[] = {"unknown", "assembled", "errors",
                                      "refused", "cancelled"};
  return names[s];
}

/* What the diagnostics file said beyond the diagnostics. */
static void print_report(const AsmReport *rep) {
  if (rep->version == 0) {
    printf("report: none\n");
  } else if (rep->version == 1) {
    printf("report: bare list\n");
  } else {
    printf("report: %s", run_status_name(rep->status));
    if (rep->status == ASMRUN_ASSEMBLED)
      printf(" (%s, %d bytes)", rep->format, rep->code_size);
    else if (rep->format[0])
      printf(" (%s)", rep->format);
    printf(", errors %d, warnings %d\n", rep->errors, rep->warnings);
  }
}

/* Drop the options nasm 1.3 introduced, leaving the command line nStudio sent
   before it. */
static void make_legacy(NasmInvocation *inv) {
  int k = 0;
  for (int i = 0; i < inv->argc; i++)
    if (strcmp(inv->argv[i], "--no-ui") != 0 &&
        strcmp(inv->argv[i], "--report=2") != 0 &&
        strncmp(inv->argv[i], "--format=", 9) != 0)
      inv->argv[k++] = inv->argv[i];
  inv->argc = k;
}

/* For a diagnostic reached through an INCLUDE: does the editor's reading of
   the innermost INCLUDE line lead to the file nasm reported? */
static void check_include(const AsmDiag *d) {
  const AsmDiagInclude *fr = &d->include_stack[d->include_count - 1];
  char line[1024], operand[256], resolved[1024];

  printf("    via ");
  put_path(fr->file);
  printf(":%d ", fr->line);

  if (!read_line(fr->file, fr->line, line, sizeof(line)) ||
      !nasmlink_include_operand(line, operand, sizeof(operand))) {
    printf("NOT AN INCLUDE LINE\n");
    return;
  }
  printf("\"");
  put_path(operand);
  printf("\": ");
  if (!nasmlink_resolve_include(fr->file, operand, resolved,
                                sizeof(resolved))) {
    printf("NOT RESOLVED\n");
    return;
  }
  if (asmdiag_same_file(resolved, d->file)) {
    printf("same file\n");
  } else {
    printf("DIFFERENT FILE (");
    put_path(resolved);
    printf(")\n");
  }
}

int main(int argc, char **argv) {
  if (argc != 6) {
    fprintf(stderr,
            "usage: %s <nasm> <source> <user args> <diag file> <format>\n",
            argv[0]);
    return 2;
  }
  const char *nasm = argv[1], *src = argv[2], *user_args = argv[3],
             *diag = argv[4], *format = argv[5];
  int legacy = strcmp(format, "legacy") == 0;

  snprintf(g_base, sizeof(g_base), "%s", src);
  char *slash = strrchr(g_base, '/');
  if (slash)
    slash[1] = '\0';
  else
    g_base[0] = '\0';

  /* What the editor does, in its order: the command line, then the stale
     diagnostics cleared, then nasm. */
  static NasmInvocation inv;
  if (!nasmlink_invocation(&inv, src, user_args, diag,
                           legacy ? NULL : format, NULL)) {
    printf("command line could not be built\n");
    return 1;
  }
  if (legacy)
    make_legacy(&inv);
  remove(diag);
  int status = host_nl_exec(nasm, inv.argc, inv.argv);

  static AsmDiag diags[ASMDIAG_MAX];
  AsmReport rep;
  int ndiag = asmdiag_parse_report(diag, &rep, diags, ASMDIAG_MAX);
  AsmBuildOutcome outcome = asmdiag_report_outcome(status, &rep, ndiag);
  printf("status %d: %s\n", status, outcome_name(outcome));
  print_report(&rep);

  /* The program it would run: offered only after a successful build.  A
     report names it; otherwise the editor works it out, as Ctrl+R always
     does - and the two must agree. */
  char program[1024], rule[1024];
  struct stat st;
  int has_rule = nasmlink_output_path(src, rule, sizeof(rule));
  int has_program;
  if (rep.version >= 2) {
    has_program = rep.output[0] != '\0';
    snprintf(program, sizeof(program), "%s", rep.output);
    if (has_program && (!has_rule || strcmp(rule, program) != 0))
      printf("Ctrl+R LOOKS ELSEWHERE\n");
  } else {
    has_program = has_rule;
    snprintf(program, sizeof(program), "%s", rule);
  }
  if (!has_program) {
    printf("program: none\n");
  } else {
    int written = stat(program, &st) == 0;
    printf("program: ");
    put_path(program);
    if (written)
      printf(" (written, %s)", container(program));
    else
      printf(" (not written)");
    printf(", Run %s\n",
           outcome == ASMBUILD_OK && written ? "offered" : "not offered");
  }

  /* Each diagnostic, and where the editor would put the cursor for it.  The
     first few are enough to see where they go. */
  for (int i = 0; i < ndiag && i < 5; i++) {
    const AsmDiag *d = &diags[i];
    printf("%s %s ", severity_name(d->severity), d->code);
    put_path(d->file);
    if (d->line >= 1)
      printf(":%d", d->line);
    if (d->line >= 1 && d->col >= 1)
      printf(":%d", d->col);

    int line, col;
    switch (asmdiag_locate(d, src, &line, &col)) {
    case ASMDIAG_AT_LINE:
      printf(" -> line %d\n", line);
      break;
    case ASMDIAG_AT_INCLUDE:
      printf(" -> include %d\n", line);
      break;
    default:
      printf(" -> nowhere\n");
      break;
    }

    if (d->include_count > 0)
      check_include(d);
  }
  if (ndiag > 5)
    printf("(%d more)\n", ndiag - 5);

  return 0;
}
