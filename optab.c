/*
 * optab.c
 * ARM opcode tables and mnemonic/operand-keyword classifiers.
 */

#include <ctype.h>
#include <string.h>

#include "optab.h"
#include "util.h"

int is_shiftname(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "ASL") == 0 || strcmp(u, "LSL") == 0 ||
         strcmp(u, "LSR") == 0 || strcmp(u, "ASR") == 0 ||
         strcmp(u, "ROR") == 0;
}

int is_coprocreg(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"C0",  "C1",  "C2",  "C3",  "C4",  "C5",
                        "C6",  "C7",  "C8",  "C9",  "C10", "C11",
                        "C12", "C13", "C14", "C15", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

int is_coproc(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"P0",  "P1",  "P2",  "P3",  "P4",  "P5",
                        "P6",  "P7",  "P8",  "P9",  "P10", "P11",
                        "P12", "P13", "P14", "P15", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

int is_psr(const char *s) {
  char u[16];

  strupper(s, u, sizeof(u));

  const char *list[] = {"CPSR",     "SPSR",     "CPSR_ALL", "SPSR_ALL",
                        "SPSR_FLG", "CPSR_FLG", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

int get_reg_num(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *names[] = {"R0", "R1",  "R2", "R3",  "R4",  "R5",  "R6",
                         "R7", "R8",  "R9", "R10", "R11", "R12", "R13",
                         "SP", "R14", "LR", "R15", "PC",  NULL};
  const int nums[] = {0,  1,  2,  3,  4,  5,  6,  7,  8, 9,
                      10, 11, 12, 13, 13, 14, 14, 15, 15};

  int i;
  for (i = 0; names[i]; i++)
    if (strcmp(u, names[i]) == 0)
      return nums[i];

  return -1;
}

int is_reg(const char *s) { return get_reg_num(s) != -1; }


int get_condcode_value(const char *s) {
  char u[4];

  strupper(s, u, sizeof(u));

  const char *names[] = {"EQ", "NE", "HS", "CS", "LO", "CC", "MI", "PL", "VS",
                         "VC", "HI", "LS", "GE", "LT", "GT", "LE", "AL", NULL};
  const int vals[] = {0, 1, 2, 2, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

  int i;
  for (i = 0; names[i]; i++)
    if (strcmp(u, names[i]) == 0)
      return vals[i];

  return -1;
}

int is_condcode(const char *s) { return get_condcode_value(s) != -1; }


int is_preasm_directive(const char *s) {
  char u[16];

  strupper(s, u, sizeof(u));

  return strcmp(u, "GET") == 0 || strcmp(u, "INCLUDE") == 0;
}

int is_directive(const char *s) {
  char u[16];

  strupper(s, u, sizeof(u));

  const char *list[] = {"DCD", "DCDU",   "DCW",   "DCWU", "ALIGN",
                        "DCB", "INCBIN", "LTORG", "EQU",  NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

int is_dataproc_fullop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"ADC", "ADD", "RSB", "RSC", "SBC", "SUB",
                        "AND", "BIC", "EOR", "ORR", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

int is_dataproc_testop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "CMP") == 0 || strcmp(u, "CMN") == 0 ||
         strcmp(u, "TEQ") == 0 || strcmp(u, "TST") == 0;
}

int get_dataprocop_num(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *names[] = {"ADC", "ADD", "RSB", "RSC", "SBC", "SUB",
                         "AND", "BIC", "EOR", "ORR", "CMP", "CMN",
                         "TEQ", "TST", "MOV", "MVN", NULL};
  const int vals[] = {5, 4, 3, 7, 6, 2, 0, 14, 1, 12, 10, 11, 9, 8, 13, 15};

  int i;
  for (i = 0; names[i]; i++)
    if (strcmp(u, names[i]) == 0)
      return vals[i];

  return -1;
}

int is_dataprocop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"ADC",  "ADD",  "RSB",  "RSC",  "SBC",  "SUB",
                        "AND",  "BIC",  "EOR",  "ORR",  "CMP",  "CMN",
                        "TEQ",  "TST",  "MOV",  "MVN",  "ADCS", "ADDS",
                        "RSBS", "RSCS", "SBCS", "SUBS", "ANDS", "BICS",
                        "EORS", "ORRS", "MOVS", "MVNS", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

int is_branchop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "BX") == 0 || strcmp(u, "B") == 0 || strcmp(u, "BL") == 0;
}

int is_psrtransop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "MSR") == 0 || strcmp(u, "MRS") == 0;
}

int is_mulop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "MUL") == 0 || strcmp(u, "MLA") == 0 ||
         strcmp(u, "MULS") == 0 || strcmp(u, "MLAS") == 0;
}

int is_longmulop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"UMULL",  "SMULL",  "UMLAL",  "SMLAL", "UMULLS",
                        "SMULLS", "UMLALS", "SMLALS", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

int is_swiop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "SWI") == 0 || strcmp(u, "SVC") == 0;
}

int is_singledatatransop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"LDR",  "STR",   "LDRB",  "STRB", "LDRT",
                        "STRT", "LDRBT", "STRBT", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

int is_halfsigneddatatransop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "LDRH") == 0 || strcmp(u, "LDRSH") == 0 ||
         strcmp(u, "LDRSB") == 0 || strcmp(u, "STRH") == 0;
}

int is_swapop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "SWP") == 0 || strcmp(u, "SWPB") == 0;
}

int is_blockdatatransop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"LDMFD", "LDMED", "LDMFA", "LDMEA", "LDMIA", "LDMIB",
                        "LDMDA", "LDMDB", "STMFD", "STMED", "STMFA", "STMEA",
                        "STMIA", "STMIB", "STMDA", "STMDB", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

int is_coprocregtransop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "MRC") == 0 || strcmp(u, "MCR") == 0;
}

int is_pseudoinstructionop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "ADR") == 0 || strcmp(u, "PUSH") == 0 ||
         strcmp(u, "POP") == 0 || strcmp(u, "NOP") == 0;
}

int is_miscarithmeticop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "CLZ") == 0;
}

int is_otherkeyword(const char *s) {
  char u[16];

  strupper(s, u, sizeof(u));

  const char *list[] = {
      "LSL",      "LSR",      "ASL", "ASR",  "ROR",  "RRX",      "P0",
      "P1",       "P2",       "P3",  "P4",   "P5",   "P6",       "P7",
      "P8",       "P9",       "P10", "P11",  "P12",  "P13",      "P14",
      "P15",      "C0",       "C1",  "C2",   "C3",   "C4",       "C5",
      "C6",       "C7",       "C8",  "C9",   "C10",  "C11",      "C12",
      "C13",      "C14",      "C15", "CPSR", "SPSR", "CPSR_ALL", "SPSR_ALL",
      "SPSR_FLG", "CPSR_FLG", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

int is_opname(const char *s) {
  return is_preasm_directive(s) || is_directive(s) || is_dataprocop(s) ||
         is_branchop(s) || is_psrtransop(s) || is_mulop(s) || is_longmulop(s) ||
         is_swiop(s) || is_singledatatransop(s) ||
         is_halfsigneddatatransop(s) || is_swapop(s) ||
         is_blockdatatransop(s) || is_coprocregtransop(s) ||
         is_pseudoinstructionop(s) || is_miscarithmeticop(s);
}

int is_conditionable(const char *s) {
  return !(is_preasm_directive(s) || is_directive(s)) && is_opname(s);
}

int is_pseudoinstruction(const char *opname) {
  return is_pseudoinstructionop(opname);
}

int is_valid_label(const char *s) {
  if (!s || !*s)
    return 0;

  if (!isalpha((unsigned char)s[0]))
    return 0;

  while (*s) {
    if (!my_isalnum(*s) && *s != '_')
      return 0;

    s++;
  }

  return 1;
}

int is_private_label(const char *s) {
  return is_directive(s) || is_opname(s) || is_reg(s) || is_otherkeyword(s);
}

/* Valid size/type suffixes for LDR/STR/SWP (pre-UAL {B|BT|T|H|SB|SH}) */
int is_tbhs_suffix(const char *s) {
  const char *list[] = {"T", "B", "BT", "H", "SB", "SH", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(s, list[i]) == 0)
      return 1;

  return 0;
}
