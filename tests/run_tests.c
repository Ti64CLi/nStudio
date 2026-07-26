/*
 * tests/run_tests.c
 * Host-side regression tests for nStudio's pure modules.
 *
 * These are compiled and run on the development host with the system
 * compiler (see `make test`), NOT with the Ndless toolchain.  They cover
 * only the modules that have no editor / graphics / Ndless dependency:
 *   util, optab, gapbuf, asmdiag, asmdb
 * The UI, rendering and syntax renderer (which pulls in gfx.h -> <keys.h>)
 * are deliberately out of scope; syntax classification is exercised here
 * through optab, which is what it delegates to.
 *
 * POSIX host-only: uses mkstemp()/unlink() for the diagnostics fixture.
 * Every assertion is grounded in the current source, never a guess.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asmdb.h"
#include "asmdiag.h"
#include "fileio.h"
#include "gapbuf.h"
#include "optab.h"
#include "util.h"

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
      g_fails++;                                                               \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                 \
    }                                                                          \
  } while (0)

/* ================================================================ */
/* util                                                             */
/* ================================================================ */
static void test_util(void) {
  printf("util\n");

  /* strncaseeq: case-insensitive, stops at NUL in either operand */
  CHECK(strncaseeq("MOV", "mov", 3) == 1);
  CHECK(strncaseeq("mov", "mvn", 3) == 0);
  CHECK(strncaseeq("move", "moved", 3) == 1); /* first 3 match */
  CHECK(strncaseeq("abc", "abc", 5) == 1);    /* NUL reached before n */
  CHECK(strncaseeq("abc", "abd", 5) == 0);
  CHECK(strncaseeq("ab", "abc", 5) == 0); /* differing length at NUL */

  /* strupper */
  char up[16];
  strupper("mov", up, sizeof(up));
  CHECK(strcmp(up, "MOV") == 0);
  strupper("Ldr12", up, sizeof(up));
  CHECK(strcmp(up, "LDR12") == 0);

  /* my_isalnum: letters and digits only, not '_' or punctuation */
  CHECK(my_isalnum('a') == 1);
  CHECK(my_isalnum('Z') == 1);
  CHECK(my_isalnum('7') == 1);
  CHECK(my_isalnum('_') == 0);
  CHECK(my_isalnum(' ') == 0);
}

/* ================================================================ */
/* optab  (the shared classification source of truth)              */
/* ================================================================ */
static void test_optab(void) {
  printf("optab\n");

  /* registers, including the sp/lr/pc aliases */
  CHECK(get_reg_num("r0") == 0);
  CHECK(get_reg_num("R15") == 15);
  CHECK(get_reg_num("sp") == 13);
  CHECK(get_reg_num("lr") == 14);
  CHECK(get_reg_num("pc") == 15);
  CHECK(get_reg_num("r16") == -1);
  CHECK(is_reg("r7") == 1);
  CHECK(is_reg("mov") == 0);

  /* condition codes */
  CHECK(get_condcode_value("eq") == 0);
  CHECK(get_condcode_value("AL") == 14);
  CHECK(is_condcode("ne") == 1);
  CHECK(is_condcode("xy") == 0);

  /* opnames: mnemonics + directives, case-insensitive */
  CHECK(is_opname("mov") == 1);
  CHECK(is_opname("MOV") == 1);
  CHECK(is_opname("ldr") == 1);
  CHECK(is_opname("b") == 1);
  CHECK(is_opname("bl") == 1);
  CHECK(is_opname("bx") == 1);
  CHECK(is_opname("align") == 1);   /* directive is an opname */
  CHECK(is_opname("include") == 1); /* pre-assembly directive */
  CHECK(is_opname("bogusop") == 0);
  CHECK(is_opname("frobnicate") == 0);

  /* conditionable excludes directives, includes real instructions */
  CHECK(is_conditionable("mov") == 1);
  CHECK(is_conditionable("b") == 1);
  CHECK(is_conditionable("align") == 0);
  CHECK(is_conditionable("include") == 0);

  /* directives */
  CHECK(is_directive("EQU") == 1);
  CHECK(is_directive("align") == 1);
  CHECK(is_directive("mov") == 0);
  CHECK(is_preasm_directive("include") == 1);
  CHECK(is_preasm_directive("get") == 1);
  CHECK(is_preasm_directive("align") == 0);

  /* branch ops (BLX intentionally absent - nasm does not assemble it) */
  CHECK(is_branchop("b") == 1);
  CHECK(is_branchop("bl") == 1);
  CHECK(is_branchop("bx") == 1);
  CHECK(is_branchop("blx") == 0);

  /* shift names (RRX is handled separately by syntax.c, not here) */
  CHECK(is_shiftname("lsl") == 1);
  CHECK(is_shiftname("ROR") == 1);
  CHECK(is_shiftname("rrx") == 0);

  /* other operand keywords */
  CHECK(is_psr("cpsr") == 1);
  CHECK(is_coprocreg("c0") == 1);
  CHECK(is_coproc("p15") == 1);

  /* labels: letter first, then alnum/underscore */
  CHECK(is_valid_label("start") == 1);
  CHECK(is_valid_label("lab_1") == 1);
  CHECK(is_valid_label("_x") == 0); /* must start with a letter */
  CHECK(is_valid_label("1x") == 0);
  CHECK(is_valid_label("") == 0);
}

/* ================================================================ */
/* gapbuf                                                           */
/* ================================================================ */
static void test_gapbuf(void) {
  printf("gapbuf\n");

  GapBuf g;
  gb_init(&g);

  /* empty buffer is one (empty) line */
  rebuild_lines(&g);
  CHECK(gb_len(&g) == 0);
  CHECK(num_lines == 1);
  CHECK(line_starts[0] == 0);
  CHECK(line_len(&g, 0) == 0);
  CHECK(g_lines_truncated == 0);

  /* three lines of two chars each */
  gb_inserts(&g, "ab\ncd\nef");
  CHECK(gb_len(&g) == 8);
  rebuild_lines(&g);
  CHECK(num_lines == 3);
  CHECK(line_starts[0] == 0);
  CHECK(line_starts[1] == 3);
  CHECK(line_starts[2] == 6);
  CHECK(line_len(&g, 0) == 2);
  CHECK(line_len(&g, 1) == 2);
  CHECK(line_len(&g, 2) == 2); /* last line runs to end of buffer */

  /* gb_get reads logical indices across the gap */
  CHECK(gb_get(&g, 0) == 'a');
  CHECK(gb_get(&g, 2) == '\n');
  CHECK(gb_get(&g, 7) == 'f');

  /* insert in the middle: "ab\nXcd\nef" */
  gb_move(&g, 3);
  CHECK(gb_insert(&g, 'X') == 1);
  CHECK(gb_len(&g) == 9);
  CHECK(gb_get(&g, 3) == 'X');
  CHECK(gb_get(&g, 4) == 'c');
  rebuild_lines(&g);
  CHECK(num_lines == 3);
  CHECK(line_len(&g, 1) == 3); /* "Xcd" */

  /* backspace/delete around the cursor (currently at 4) */
  gb_backspace(&g); /* removes 'X' at logical 3 */
  CHECK(gb_len(&g) == 8);
  CHECK(gb_get(&g, 3) == 'c');

  gb_free(&g);

  /* gb_insert_n is NUL-safe (binary round trip) */
  GapBuf b;
  gb_init(&b);
  char raw[4] = {'x', 'y', '\0', 'z'};
  CHECK(gb_insert_n(&b, raw, 4) == 4);
  CHECK(gb_len(&b) == 4);
  CHECK(gb_get(&b, 2) == '\0');
  CHECK(gb_get(&b, 3) == 'z');
  gb_free(&b);
}

/* Differential test for lines_shift(): drive a buffer through many random
   single-character edits, maintaining the line table the way editor.c does -
   lines_shift() when no line break is involved, rebuild_lines() otherwise -
   and after every edit assert the table is byte-for-byte what a full rescan
   would have produced.  This is what guarantees the cheap path cannot drift
   from the authoritative one. */
static void test_lines_shift(void) {
  printf("lines_shift\n");

  GapBuf g;
  gb_init(&g);
  gb_inserts(&g, "start\n mov r0, #1\n add r1, r2\nloop\n bx lr\n");
  rebuild_lines(&g);

  unsigned seed = 12345;
  int mismatches = 0;
  static int saved_starts[8192];
  int saved_n;

  for (int step = 0; step < 4000; step++) {
    seed = seed * 1103515245u + 12345u;
    int len = gb_len(&g);
    int pos = len ? (int)((seed >> 8) % (unsigned)(len + 1)) : 0;
    int insert = ((seed >> 3) & 1) || len < 8;

    if (insert) {
      /* mostly ordinary characters, occasionally a newline */
      char c = (((seed >> 16) % 11u) == 0) ? '\n' : (char)('a' + (seed >> 20) % 26u);
      int before = gb_len(&g);
      gb_move(&g, pos);
      gb_insert(&g, c);
      if (c == '\n')
        rebuild_lines(&g);
      else
        lines_shift(pos, gb_len(&g) - before);
    } else {
      char c = gb_get(&g, pos);
      gb_move(&g, pos);
      gb_delete(&g);
      if (c == '\n')
        rebuild_lines(&g);
      else
        lines_shift(pos, -1);
    }

    /* compare the maintained table against a fresh full rescan */
    saved_n = num_lines;
    memcpy(saved_starts, line_starts, sizeof(int) * (size_t)num_lines);
    rebuild_lines(&g);
    if (saved_n != num_lines ||
        memcmp(saved_starts, line_starts, sizeof(int) * (size_t)num_lines) != 0)
      mismatches++;
  }
  CHECK(mismatches == 0);
  gb_free(&g);

  /* A line start exactly at the edit position must not move: inserting at the
     head of a line leaves that line starting where it was. */
  GapBuf h;
  gb_init(&h);
  gb_inserts(&h, "ab\ncd\n");
  rebuild_lines(&h);
  int second = line_starts[1]; /* start of "cd" */
  gb_move(&h, second);
  gb_insert(&h, 'X'); /* insert at the head of line 1 */
  lines_shift(second, 1);
  CHECK(line_starts[1] == second);
  CHECK(num_lines == 3);
  rebuild_lines(&h);
  CHECK(line_starts[1] == second); /* and the rescan agrees */
  gb_free(&h);

  /* The table grows past the old fixed 4096-line cap. */
  GapBuf big;
  gb_init(&big);
  const int BIG_LINES = 20000;
  for (int i = 0; i < BIG_LINES; i++)
    gb_inserts(&big, "x\n");
  rebuild_lines(&big);
  CHECK(num_lines == BIG_LINES + 1); /* trailing newline opens a last line */
  CHECK(g_lines_truncated == 0);
  CHECK(line_starts[BIG_LINES] == BIG_LINES * 2);
  CHECK(line_len(&big, 0) == 1);
  gb_free(&big);
  lines_free();
  CHECK(num_lines == 0);
}

/* ================================================================ */
/* asmdiag  (nasm JSON diagnostics consumer)                        */
/* ================================================================ */

/* Write `content` to a fresh temp file; returns 1 and fills path_out. */
static int write_tmp(const char *content, char *path_out) {
  strcpy(path_out, "/tmp/nstudio_diagXXXXXX");
  int fd = mkstemp(path_out);
  if (fd < 0)
    return 0;
  FILE *f = fdopen(fd, "w");
  if (!f) {
    close(fd);
    return 0;
  }
  fputs(content, f);
  fclose(f);
  return 1;
}

static void test_asmdiag(void) {
  printf("asmdiag\n");

  /* Two diagnostics.  The first carries nested "related" and
     "include_stack" arrays whose own line/col MUST NOT leak into the
     top-level fields - this exercises the skip-nested-array logic.  The
     second's message contains a ']' that must not be mistaken for the end
     of an array. */
  static const char *json =
      "[\n"
      "  {\"severity\":\"error\",\"code\":\"E100\","
      "\"file\":\"/documents/test.asm.tns\","
      "\"line\":24,\"col\":2,\"end_col\":9,"
      "\"message\":\"This instruction (name:BOGUSOP, flags:) is unknown\","
      "\"source_line\":\" bogusop r3\",\"expanded_from\":\"\","
      "\"related\":[{\"file\":\"a.asm\",\"line\":1,\"col\":1,"
      "\"end_col\":2,\"message\":\"first defined here\"}],"
      "\"include_stack\":[{\"file\":\"inc.asm\",\"line\":5}]},\n"
      "  {\"severity\":\"warning\",\"code\":\"\","
      "\"file\":\"/documents/test.asm.tns\","
      "\"line\":-1,\"col\":0,\"end_col\":0,"
      "\"message\":\"brackets ] inside a string must not end an array\","
      "\"source_line\":\"\",\"expanded_from\":\"\","
      "\"related\":[],\"include_stack\":[]}\n"
      "]\n";

  AsmDiag out[ASMDIAG_MAX];
  char path[64];

  CHECK(write_tmp(json, path));
  int n = asmdiag_parse_file(path, out, ASMDIAG_MAX);
  unlink(path);

  CHECK(n == 2);
  if (n == 2) {
    /* top-level fields survive the nested arrays intact */
    CHECK(out[0].severity == ADIAG_ERROR);
    CHECK(out[0].line == 24);
    CHECK(out[0].col == 2);
    CHECK(out[0].end_col == 9);
    CHECK(strcmp(out[0].code, "E100") == 0);
    CHECK(strcmp(out[0].file, "/documents/test.asm.tns") == 0);
    CHECK(strstr(out[0].message, "BOGUSOP") != NULL);

    CHECK(out[1].severity == ADIAG_WARNING);
    CHECK(out[1].line == -1);
    CHECK(strstr(out[1].message, "brackets") != NULL);

    /* nested related / include_stack are now stored, not skipped */
    CHECK(out[0].related_count == 1);
    CHECK(out[0].related_truncated == 0);
    CHECK(strcmp(out[0].related[0].file, "a.asm") == 0);
    CHECK(out[0].related[0].line == 1);
    CHECK(out[0].related[0].col == 1);
    CHECK(out[0].related[0].end_col == 2);
    CHECK(strcmp(out[0].related[0].message, "first defined here") == 0);

    CHECK(out[0].include_count == 1);
    CHECK(out[0].include_truncated == 0);
    CHECK(strcmp(out[0].include_stack[0].file, "inc.asm") == 0);
    CHECK(out[0].include_stack[0].line == 5);

    /* empty arrays yield empty, untruncated collections */
    CHECK(out[1].related_count == 0);
    CHECK(out[1].include_count == 0);
    CHECK(out[1].related_truncated == 0);
    CHECK(out[1].include_truncated == 0);

    /* first-for-file: full path, then base name, then no match */
    CHECK(asmdiag_first_for_file(out, n, "/documents/test.asm.tns") == 0);
    CHECK(asmdiag_first_for_file(out, n, "/other/dir/test.asm.tns") == 0);
    CHECK(asmdiag_first_for_file(out, n, "nope.asm.tns") == -1);

    /* the path matcher shared with related locations / include frames */
    CHECK(asmdiag_same_file("/documents/test.asm.tns",
                            "/documents/test.asm.tns") == 1);
    CHECK(asmdiag_same_file("test.asm.tns", "/other/test.asm.tns") == 1);
    CHECK(asmdiag_same_file("/a/x.asm.tns", "/a/y.asm.tns") == 0);
    CHECK(asmdiag_same_file(NULL, "/a/x.asm.tns") == 0);
    CHECK(strcmp(asmdiag_base_name("/a/b/c.asm.tns"), "c.asm.tns") == 0);
    CHECK(strcmp(asmdiag_base_name("bare.tns"), "bare.tns") == 0);
    CHECK(strcmp(asmdiag_base_name(NULL), "") == 0);

    /* the underlying in-file predicate (full path / base name / line>=1) */
    CHECK(asmdiag_in_file(&out[0], "/documents/test.asm.tns") == 1);
    CHECK(asmdiag_in_file(&out[0], "/other/dir/test.asm.tns") == 1);
    CHECK(asmdiag_in_file(&out[1], "/documents/test.asm.tns") == 0); /* line -1 */
    CHECK(asmdiag_in_file(&out[0], "nope.asm.tns") == 0);
  }

  /* Over-long related / include_stack truncate gracefully: the caps are
     honoured, the flags are set, order is preserved and the primary
     diagnostic (and the keys after the arrays) survive intact. */
  static const char *json_trunc =
      "[{\"severity\":\"error\",\"code\":\"E1\",\"file\":\"deep.asm\","
      "\"line\":3,\"col\":1,\"end_col\":2,"
      "\"related\":["
      "{\"file\":\"r0\",\"line\":10,\"col\":1,\"end_col\":2,\"message\":\"m0\"},"
      "{\"file\":\"r1\",\"line\":11,\"col\":1,\"end_col\":2,\"message\":\"m1\"},"
      "{\"file\":\"r2\",\"line\":12,\"col\":1,\"end_col\":2,\"message\":\"m2\"},"
      "{\"file\":\"r3\",\"line\":13,\"col\":1,\"end_col\":2,\"message\":\"m3\"},"
      "{\"file\":\"r4\",\"line\":14,\"col\":1,\"end_col\":2,\"message\":\"m4\"}],"
      "\"include_stack\":["
      "{\"file\":\"i0\",\"line\":1},{\"file\":\"i1\",\"line\":2},"
      "{\"file\":\"i2\",\"line\":3},{\"file\":\"i3\",\"line\":4},"
      "{\"file\":\"i4\",\"line\":5},{\"file\":\"i5\",\"line\":6},"
      "{\"file\":\"i6\",\"line\":7},{\"file\":\"i7\",\"line\":8}],"
      "\"message\":\"after the arrays\"}]\n";

  CHECK(write_tmp(json_trunc, path));
  int tn = asmdiag_parse_file(path, out, ASMDIAG_MAX);
  unlink(path);

  CHECK(tn == 1);
  if (tn == 1) {
    /* the primary diagnostic is preserved, including a key that follows the
       nested arrays (so the arrays were consumed to their exact end) */
    CHECK(out[0].line == 3);
    CHECK(strcmp(out[0].file, "deep.asm") == 0);
    CHECK(strcmp(out[0].message, "after the arrays") == 0);

    CHECK(out[0].related_count == ASMDIAG_MAX_RELATED);
    CHECK(out[0].related_truncated == 1);
    CHECK(strcmp(out[0].related[0].file, "r0") == 0); /* order preserved */
    CHECK(out[0].related[ASMDIAG_MAX_RELATED - 1].line == 10 +
                                                          ASMDIAG_MAX_RELATED -
                                                          1);

    CHECK(out[0].include_count == ASMDIAG_MAX_INCLUDE);
    CHECK(out[0].include_truncated == 1);
    CHECK(strcmp(out[0].include_stack[0].file, "i0") == 0); /* outermost 1st */
    CHECK(out[0].include_stack[ASMDIAG_MAX_INCLUDE - 1].line ==
          ASMDIAG_MAX_INCLUDE);
  }

  /* clean assemble: "[]" -> zero diagnostics */
  CHECK(write_tmp("[]\n", path));
  CHECK(asmdiag_parse_file(path, out, ASMDIAG_MAX) == 0);
  unlink(path);

  /* absent file -> -1 (distinct from a clean, empty batch) */
  CHECK(asmdiag_parse_file("/tmp/nstudio_no_such_diag_file_zzz", out,
                           ASMDIAG_MAX) == -1);

  /* first-for-file honours the line >= 1 guard even on a file match */
  AsmDiag one;
  memset(&one, 0, sizeof(one));
  strcpy(one.file, "only.asm.tns");
  one.severity = ADIAG_ERROR;
  one.line = -1;
  CHECK(asmdiag_first_for_file(&one, 1, "only.asm.tns") == -1);
  one.line = 7;
  CHECK(asmdiag_first_for_file(&one, 1, "only.asm.tns") == 0);
}

/* ================================================================ */
/* asmdb  (catalog + syscall lookups)                               */
/* ================================================================ */
static void test_asmdb(void) {
  printf("asmdb\n");

  /* syscall lookup (0x400000 is the emulator debug-alloc entry) */
  const char *name = get_syscall_name(0x400000);
  CHECK(name != NULL && strcmp(name, "NDLSEMU_DEBUG_ALLOC") == 0);
  CHECK(get_syscall_name(-12345) == NULL);

  /* cheatsheet_lookup: exact, case-insensitive, and suffix stripping */
  const MnemInfo *mi = cheatsheet_lookup("mov", 3);
  CHECK(mi != NULL && strcmp(mi->name, "mov") == 0);

  mi = cheatsheet_lookup("MOV", 3);
  CHECK(mi != NULL && strcmp(mi->name, "mov") == 0);

  mi = cheatsheet_lookup("moveq", 5); /* strip condition code */
  CHECK(mi != NULL && strcmp(mi->name, "mov") == 0);

  mi = cheatsheet_lookup("adds", 4); /* strip trailing S */
  CHECK(mi != NULL && strcmp(mi->name, "add") == 0);

  mi = cheatsheet_lookup("addeqs", 6); /* condition code before S */
  CHECK(mi != NULL && strcmp(mi->name, "add") == 0);

  CHECK(cheatsheet_lookup("bogusop", 7) == NULL);
  CHECK(cheatsheet_lookup("frobnicate", 10) == NULL);
  CHECK(cheatsheet_lookup("x", 0) == NULL);  /* wlen <= 0 */
  CHECK(cheatsheet_lookup("aaaaaaaaaaaaaaaa", 16) == NULL); /* wlen >= 16 */
}

/* ================================================================ */
/* fileio  (atomic whole-file replacement)                          */
/* ================================================================ */
static int writer_hello(FILE *f, void *ctx) {
  (void)ctx;
  fputs("hello", f);
  return 1;
}

/* Writes something, then reports failure - the partial content must be
   discarded and the pre-existing file left untouched. */
static int writer_fail(FILE *f, void *ctx) {
  (void)ctx;
  fputs("partial garbage", f);
  return 0;
}

/* Read a whole file into buf (NUL-terminated); returns bytes read, -1 if
   the file could not be opened. */
static int read_all(const char *path, char *buf, int cap) {
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;
  int n = (int)fread(buf, 1, (size_t)cap - 1, f);
  fclose(f);
  buf[n] = '\0';
  return n;
}

static void test_fileio(void) {
  printf("fileio\n");

  char path[64];
  strcpy(path, "/tmp/nstudio_fioXXXXXX");
  int fd = mkstemp(path);
  CHECK(fd >= 0);
  if (fd < 0)
    return;

  /* seed an existing file with known content */
  FILE *seed = fdopen(fd, "w");
  fputs("original", seed);
  fclose(seed);

  char buf[64];

  /* success: the file is replaced with the writer's output */
  CHECK(write_file_atomic(path, writer_hello, NULL) == 1);
  CHECK(read_all(path, buf, sizeof(buf)) == 5);
  CHECK(strcmp(buf, "hello") == 0);

  /* failure: the previous content survives, unchanged */
  CHECK(write_file_atomic(path, writer_fail, NULL) == 0);
  CHECK(read_all(path, buf, sizeof(buf)) == 5);
  CHECK(strcmp(buf, "hello") == 0);

  /* and no temporary is left behind */
  char tmp[80];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  FILE *leftover = fopen(tmp, "r");
  CHECK(leftover == NULL);
  if (leftover)
    fclose(leftover);

  unlink(path);
}

int main(void) {
  test_util();
  test_optab();
  test_gapbuf();
  test_lines_shift();
  test_asmdiag();
  test_asmdb();
  test_fileio();

  printf("\n%d checks, %d failed\n", g_checks, g_fails);
  return g_fails ? 1 : 0;
}
