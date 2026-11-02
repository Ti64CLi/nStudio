/*
 * asmlang.c
 * ARM assembly as nasm reads it.  See asmlang.h.
 *
 * All keyword classification is driven by the vendored optab.c tables,
 * which are shared verbatim with the nasm assembler: whatever nasm
 * assembles is what the editor recognises.
 */

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "asmlang.h"
#include "nasmlink.h"
#include "optab.h"

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

int asmlang_label_def(const char *line, char *out, int outsz) {
  if (!isalpha((unsigned char)line[0]))
    return 0;
  int wlen = 0;
  while (isalnum((unsigned char)line[wlen]) || line[wlen] == '_')
    wlen++;
  if (wlen >= outsz)
    return 0;
  if (syn_is_reg(line, wlen) || syn_is_mnem(line, wlen) ||
      syn_is_shift(line, wlen) || syn_is_directive(line, wlen))
    return 0;
  memcpy(out, line, (size_t)wlen);
  out[wlen] = '\0';
  return wlen;
}

int asmlang_mentions(const char *line, const char *name) {
  int nlen = (int)strlen(name);
  for (int i = 0; line[i] && line[i] != ';';) {
    if (!isalnum((unsigned char)line[i]) && line[i] != '_') {
      i++;
      continue;
    }
    int ws = i;
    while (isalnum((unsigned char)line[i]) || line[i] == '_')
      i++;
    if (i - ws == nlen && strncmp(line + ws, name, (size_t)nlen) == 0)
      return 1;
  }
  return 0;
}

const ProjectLang asmlang_project = {nasmlink_include_operand,
                                     nasmlink_resolve_include,
                                     asmlang_label_def, asmlang_mentions};
