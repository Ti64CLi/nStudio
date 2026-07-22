/*
 * optab.h
 * ARM opcode tables and mnemonic/operand-keyword classifiers.
 */
#ifndef OPTAB_H_INCLUDED
#define OPTAB_H_INCLUDED

/* registers / condition codes / other operand keywords */
int get_reg_num(const char *s);
int is_reg(const char *s);
int get_condcode_value(const char *s);
int is_condcode(const char *s);
int is_shiftname(const char *s);
int is_coprocreg(const char *s);
int is_coproc(const char *s);
int is_psr(const char *s);
int is_otherkeyword(const char *s);

/* instruction-family classifiers */
int is_preasm_directive(const char *s);
int is_directive(const char *s);
int is_dataproc_fullop(const char *s);
int is_dataproc_testop(const char *s);
int get_dataprocop_num(const char *s);
int is_dataprocop(const char *s);
int is_branchop(const char *s);
int is_psrtransop(const char *s);
int is_mulop(const char *s);
int is_longmulop(const char *s);
int is_swiop(const char *s);
int is_singledatatransop(const char *s);
int is_halfsigneddatatransop(const char *s);
int is_swapop(const char *s);
int is_blockdatatransop(const char *s);
int is_coprocregtransop(const char *s);
int is_pseudoinstructionop(const char *s);
int is_miscarithmeticop(const char *s);
int is_opname(const char *s);
int is_conditionable(const char *s);
int is_pseudoinstruction(const char *opname);
int is_tbhs_suffix(const char *s);

/* labels */
int is_valid_label(const char *s);
int is_private_label(const char *s);

#endif /* OPTAB_H_INCLUDED */
