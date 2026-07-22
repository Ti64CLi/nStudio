/*
 * syntax.c
 * ARM assembly tokeniser / syntax highlighting for the nStudio editor.
 *
 * All keyword classification is driven by the vendored optab.c tables,
 * which are shared verbatim with the nasm assembler: whatever nasm
 * assembles is what the editor highlights.
 *
 * nasm syntax (see ../nasm/README.md):
 *   - labels sit in column 0 (letter first, then alnum/underscore),
 *     with NO trailing colon; instructions must be indented
 *   - comments run from ';' to end of line (the renderer consumes
 *     quoted strings first, so ';' inside quotes is safe)
 *   - directives are bare words (ALIGN, DCD, EQU, ...), never dot-
 *     or percent-prefixed
 *   - both pre-UAL (LDMEQFD, LDREQB) and UAL (LDMFDEQ, SUBSEQ)
 *     suffix orders are accepted
 *   - numeric literals: decimal, 0x hex, 0 octal, 0b binary, 'c' char
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "asmdb.h"
#include "gfx.h"
#include "optab.h"
#include "settings.h"
#include "syntax.h"

#define C_FG settings_col(g_settings.ui_fg)
#define C_MNEM settings_col(g_settings.syn.mnem)
#define C_REG settings_col(g_settings.syn.reg)
#define C_IMM settings_col(g_settings.syn.imm)
#define C_LABEL settings_col(g_settings.syn.label)
#define C_CMT settings_col(g_settings.syn.comment)
#define C_DIR settings_col(g_settings.syn.directive)
#define C_STR settings_col(g_settings.syn.string)

/* Case-insensitive compare of at most n chars */
int strncaseeq(const char *a, const char *b, int n) {
  int i;
  for (i = 0; i < n; i++) {
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
      return 0;
    if (!a[i])
      return 1;
  }
  return 1;
}

/* Copy a word slice into a NUL-terminated buffer for the optab
   classifiers.  Returns 0 if the word cannot be a keyword (too long). */
static int slice(const char *w, int wlen, char *buf, int bufsz) {
  if (wlen <= 0 || wlen >= bufsz)
    return 0;
  memcpy(buf, w, wlen);
  buf[wlen] = '\0';
  return 1;
}

int syn_is_reg(const char *word, int wlen) {
  char b[16];
  if (!slice(word, wlen, b, sizeof(b)))
    return 0;
  return is_reg(b) || is_psr(b) || is_coprocreg(b) || is_coproc(b);
}

int syn_is_shift(const char *word, int wlen) {
  char b[8];
  if (!slice(word, wlen, b, sizeof(b)))
    return 0;
  return is_shiftname(b) || strcasecmp(b, "RRX") == 0;
}

int syn_is_directive(const char *word, int wlen) {
  char b[16];
  if (!slice(word, wlen, b, sizeof(b)))
    return 0;
  return is_directive(b) || is_preasm_directive(b);
}

/* Full mnemonic match: the exact opname set nasm assembles, with a
   condition code accepted at any valid position (covers both the
   pre-UAL and UAL suffix orders). */
int syn_is_mnem(const char *word, int wlen) {
  char b[16], cand[16];
  if (!slice(word, wlen, b, sizeof(b)))
    return 0;
  if (is_opname(b))
    return 1;

  for (int p = 1; p + 2 <= wlen; p++) {
    char cc[3] = {b[p], b[p + 1], '\0'};
    if (!is_condcode(cc))
      continue;
    memcpy(cand, b, p);
    memcpy(cand + p, b + p + 2, wlen - p - 2);
    cand[wlen - 2] = '\0';
    if (is_conditionable(cand))
      return 1;
  }
  return 0;
}

/* Is this word a branch instruction (B/BL/BX + optional condition)?
   Used by ctrl+Enter jump-to-label.  BLX is intentionally absent:
   nasm does not assemble it. */
int is_branch_base(const char *w, int wlen) {
  char b[16], cand[16];
  if (!slice(w, wlen, b, sizeof(b)))
    return 0;
  if (is_branchop(b))
    return 1;

  for (int p = 1; p + 2 <= wlen; p++) {
    char cc[3] = {b[p], b[p + 1], '\0'};
    if (!is_condcode(cc))
      continue;
    memcpy(cand, b, p);
    memcpy(cand + p, b + p + 2, wlen - p - 2);
    cand[wlen - 2] = '\0';
    if (is_branchop(cand))
      return 1;
  }
  return 0;
}

/* A valid label word: letter first, then alnum/underscore (mirrors
   optab's is_valid_label, which needs NUL-terminated input). */
int syn_is_label_word(const char *word, int wlen) {
  if (wlen <= 0 || !isalpha((unsigned char)word[0]))
    return 0;
  for (int i = 1; i < wlen; i++) {
    if (!isalnum((unsigned char)word[i]) && word[i] != '_')
      return 0;
  }
  return 1;
}

typedef enum {
  TOK_OTHER,
  TOK_MNEM,
  TOK_REG,
  TOK_IMM,
  TOK_LABEL,
  TOK_CMT,
  TOK_DIR,
  TOK_STR
} TokType;

static uint16_t tok_colour(TokType t) {
  if (!g_settings.syntax_highlight)
    return C_FG;
  switch (t) {
  case TOK_MNEM:
    return C_MNEM;
  case TOK_REG:
    return C_REG;
  case TOK_IMM:
    return C_IMM;
  case TOK_LABEL:
    return C_LABEL;
  case TOK_CMT:
    return C_CMT;
  case TOK_DIR:
    return C_DIR;
  case TOK_STR:
    return C_STR;
  default:
    return C_FG;
  }
}

void render_line_highlighted(const char *line_buf, int px, int py, uint16_t bg,
                             int col_off, int max_w) {
  int len = (int)strlen(line_buf);
  int i = 0;
  int draw_x = px - col_off * GFX_CHAR_W;

  /* Pre-scan for SWI/SVC to render an inline hint */
  long sys_num = -1;
  int is_syscall = syntax_scan_swi(line_buf, len, &sys_num);

#define DRAWC(ch, fg)                                                          \
  do {                                                                         \
    if (draw_x >= px && draw_x + GFX_CHAR_W <= px + max_w)                     \
      gfx_drawchar(draw_x, py, (ch), (fg), bg);                                \
    else if (draw_x >= px + max_w)                                             \
      goto done_line;                                                          \
    draw_x += GFX_CHAR_W;                                                      \
  } while (0)

  while (i < len) {
    char c = line_buf[i];

    if (c == ';') {
      while (i < len) {
        DRAWC(line_buf[i], C_CMT);
        i++;
      }
      break;
    }

    if (c == '"' || c == '\'') {
      char delim = c;
      DRAWC(c, C_STR);
      i++;
      while (i < len && line_buf[i] != delim) {
        DRAWC(line_buf[i], C_STR);
        i++;
      }
      if (i < len) {
        DRAWC(line_buf[i], C_STR);
        i++;
      }
      continue;
    }

    /* Immediates: #... (named constants too) and any digit-led literal
       (decimal, 0x hex, 0 octal, 0b binary). */
    if (c == '#' || isdigit((unsigned char)c)) {
      while (i < len && !isspace((unsigned char)line_buf[i]) &&
             line_buf[i] != ',' && line_buf[i] != ']' && line_buf[i] != ')' &&
             line_buf[i] != ';') {
        DRAWC(line_buf[i], C_IMM);
        i++;
      }
      continue;
    }

    if (isalpha((unsigned char)c) || c == '_') {
      int start = i;
      while (i < len &&
             (isalnum((unsigned char)line_buf[i]) || line_buf[i] == '_'))
        i++;
      int wlen = i - start;

      TokType t = TOK_OTHER;
      if (syn_is_reg(line_buf + start, wlen))
        t = TOK_REG;
      else if (syn_is_mnem(line_buf + start, wlen))
        t = TOK_MNEM;
      else if (syn_is_shift(line_buf + start, wlen))
        t = TOK_MNEM;
      else if (syn_is_directive(line_buf + start, wlen))
        t = TOK_DIR;
      else if (start == 0 && syn_is_label_word(line_buf + start, wlen))
        t = TOK_LABEL;

      for (int j = start; j < i; j++) {
        DRAWC(line_buf[j], tok_colour(t));
      }
      continue;
    }

    DRAWC(c, C_FG);
    i++;
  }

done_line:
  /* Render the inline syscall hint */
  if (is_syscall) {
    const char *sname = get_syscall_name(sys_num);
    if (sname) {
      char hint[64];
      snprintf(hint, sizeof(hint), "  [%s]", sname);
      int h_i = 0;
      while (hint[h_i]) {
        if (draw_x >= px && draw_x + GFX_CHAR_W <= px + max_w) {
          gfx_drawchar(draw_x, py, hint[h_i], C_CMT, bg);
        } else if (draw_x >= px + max_w) {
          break;
        }
        draw_x += GFX_CHAR_W;
        h_i++;
      }
    }
  }

  /* Clear remaining visual line */
  if (draw_x < px + max_w) {
    int fill_x = draw_x < px ? px : draw_x;
    if (fill_x < px + max_w)
      gfx_fillrect(fill_x, py, px + max_w - fill_x, GFX_FONT_H, bg);
  }
#undef DRAWC
}

/* Scan a source line for a SWI/SVC instruction and parse its operand.
   Handles a leading label token and an optional '#' prefix; the number
   may be decimal or 0x hex (strtol base 0).  Returns 1 and stores the
   syscall number on success. */
int syntax_scan_swi(const char *line, int len, long *out_num) {
  int i = 0;
  while (i < len) {
    while (i < len && (line[i] == ' ' || line[i] == '\t'))
      i++;
    if (i >= len || line[i] == ';')
      break;

    int start = i;
    while (i < len && (isalnum((unsigned char)line[i]) || line[i] == '_'))
      i++;
    int tok_len = i - start;

    if (tok_len == 3 && (strncaseeq(line + start, "swi", 3) ||
                         strncaseeq(line + start, "svc", 3))) {
      while (i < len && (line[i] == ' ' || line[i] == '\t'))
        i++;
      if (i < len && line[i] == '#')
        i++;
      if (i < len) {
        char *end;
        long v = strtol(line + i, &end, 0);
        if (end != line + i) {
          *out_num = v;
          return 1;
        }
      }
      return 0;
    }
    while (i < len && line[i] != ' ' && line[i] != '\t' && line[i] != ';')
      i++;
  }
  return 0;
}
