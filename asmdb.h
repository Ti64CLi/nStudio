/*
 * asmdb.h
 * Static reference databases: ARM mnemonic catalog + Ndless syscalls.
 */
#ifndef ASMDB_H_INCLUDED
#define ASMDB_H_INCLUDED

typedef struct {
  const char *name;  /* lowercase, inserted on Enter   */
  const char *args;  /* short signature shown on row   */
  const char *desc;  /* full description for popup     */
  const char *flags; /* CPSR flag effects (N,Z,C,V)    */
} MnemInfo;

typedef struct {
  const char *name;
  const MnemInfo *mnems;
  int count;
  int expanded;
} CatalogCat;

#define NCATS 14
extern CatalogCat g_cats[NCATS];
void catalog_init_cats(void);

typedef struct {
  const char *name;
  int num;
  const char *args;
  const char *desc;
} SyscallInfo;

extern const SyscallInfo db_syscalls[];
extern const int g_nsyscalls;

const char *get_syscall_name(long num);

/* Look up a mnemonic (any case, optional condition-code / 'S' suffix)
   in the catalog.  Returns NULL if not found. */
const MnemInfo *cheatsheet_lookup(const char *word, int wlen);

#endif /* ASMDB_H_INCLUDED */
