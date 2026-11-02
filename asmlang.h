/*
 * asmlang.h
 * ARM assembly as nasm reads it: which words are keywords, the label a line
 * defines, the names it mentions - and, for the project index, the language
 * description that brings these together with nasm's INCLUDE rules.
 *
 * One reading shared by the highlighter, the editor's label commands and the
 * project index, so they cannot disagree.  Needs only optab and nasmlink,
 * which makes it buildable on the host for the unit tests.
 */
#ifndef ASMLANG_H_INCLUDED
#define ASMLANG_H_INCLUDED

#include "project.h"

/* Word-slice classifiers (word need not be NUL-terminated); all are
   backed by the vendored optab.c tables shared with nasm. */
int syn_is_mnem(const char *word, int wlen);
int syn_is_reg(const char *word, int wlen);
int syn_is_shift(const char *word, int wlen);
int syn_is_directive(const char *word, int wlen);
int syn_is_label_word(const char *word, int wlen);
int is_branch_base(const char *w, int wlen);

/*
 * The label `line` defines, under nasm's rule: a word in column 0, letter
 * first then letters, digits or '_', that is not a register, mnemonic, shift
 * or directive.  Copies it into `out` and returns its length; returns 0 when
 * the line defines none, or the name does not fit in `outsz`.
 */
int asmlang_label_def(const char *line, char *out, int outsz);

/*
 * Whether `line` mentions `name` as a whole word before its comment (the
 * first ';').  Case-sensitive, like nasm's labels: "LOOP" is not "loop", and
 * neither is "loopback".
 */
int asmlang_mentions(const char *line, const char *name);

/* ARM assembly for the project index (project.h): INCLUDE and GET as
   nasmlink reads and resolves them, and labels as above. */
extern const ProjectLang asmlang_project;

#endif /* ASMLANG_H_INCLUDED */
