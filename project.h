/*
 * project.h
 * The program being edited: the file that was opened, every file it includes -
 * directly or not - and the symbols each one defines.
 *
 * The editor keeps one program, rooted at the file the user opened.  Moving to
 * a file that belongs to it keeps it; any other file roots a new one.  Files
 * beside the root that it does not include are never read.
 *
 * Nothing here knows a language.  What an include looks like, how it is
 * resolved, what a line defines and what it mentions all come from a
 * ProjectLang (asmlang.h has nasm's), so another language only needs another
 * description.  Every file is read from disk; the editor overlays the one it
 * has open, whose unsaved text the index cannot see.
 *
 * Paths are kept normalised (asmdiag_normalize_path), so the same file is
 * always the same string.  No editor or Ndless dependency: the unit tests
 * cover it on the build machine.
 */
#ifndef PROJECT_H_INCLUDED
#define PROJECT_H_INCLUDED

#define PROJECT_MAX_FILES 64
#define PROJECT_MAX_SYMBOLS 4096
#define PROJECT_NAME_MAX 64 /* longest symbol name kept, its NUL included */

/* What a language looks like to the index. */
typedef struct {
  /* If `line` includes a file, copy the file as written into `out` and
     return its length; otherwise return 0. */
  int (*include_operand)(const char *line, char *out, int outsz);
  /* Resolve what include_operand found against the including file.  Returns
     0 if the result would not fit in `outsz`. */
  int (*resolve_include)(const char *includer, const char *operand, char *out,
                         int outsz);
  /* If `line` defines a symbol, copy its name into `out` and return its
     length; otherwise (or if it does not fit) return 0. */
  int (*symbol_def)(const char *line, char *out, int outsz);
  /* Whether `line` mentions the symbol `name`. */
  int (*mentions)(const char *line, const char *name);
} ProjectLang;

typedef struct {
  int path;          /* offset of its path in the pool */
  int includer;      /* index of the file that brought it in; -1: the root */
  int line;          /* 1-based line of that include in the includer */
  unsigned includes; /* hash of the files it includes, in order: tells a save
                        that changed only symbols from one that changed the
                        program */
} ProjectFile;

typedef struct {
  int name; /* offset of its name in the pool */
  int file; /* index in files[] */
  int line; /* 1-based */
} ProjectSymbol;

typedef struct {
  const ProjectLang *lang;
  ProjectFile *files; /* the root first, then each include in order */
  int nfiles;
  ProjectSymbol *syms; /* ordered by file, then by line */
  int nsyms, syms_cap;
  char *pool; /* every path and name, NUL-terminated */
  int pool_len, pool_cap;
  int incomplete; /* a limit was hit or memory ran out: some files or
                     symbols are missing */
} Project;

/* An empty program, for `lang`. */
void project_init(Project *p, const ProjectLang *lang);

/*
 * Make `root` the program: read it and every file it includes, and index
 * their symbols.  An include naming a file that does not exist is skipped, and
 * each file is read once however often it is included - which also ends a
 * cycle.  The root is kept even if it cannot be read (a new file).  Returns 1,
 * or 0 when the program is incomplete (see `incomplete`).
 */
int project_open(Project *p, const char *root);

/* Forget the program and free everything it holds. */
void project_close(Project *p);

/* The root's path, or NULL when there is no program. */
const char *project_root(const Project *p);

/* Path of file `f`. */
const char *project_path(const Project *p, int f);

/* Name of symbol `i`. */
const char *project_symbol_name(const Project *p, int i);

/* Index of `path` among the program's files, or -1 when it is not one. */
int project_find_file(const Project *p, const char *path);

/*
 * `path` was just written: read it again.  When the files it includes are the
 * same, only its symbols are replaced; otherwise the whole program is read
 * again from its root, since files may have joined or left it.  Returns 1 if
 * `path` belongs to the program, 0 otherwise (nothing to do).
 */
int project_file_saved(Project *p, const char *path);

/*
 * The first definition of `name` in program order, outside file `skip` (-1
 * to skip none): fills *file and *line and returns 1, or returns 0.
 */
int project_find_symbol(const Project *p, const char *name, int skip,
                        int *file, int *line);

/* Called for each line that mentions a symbol; returns 0 to stop. */
typedef int (*ProjectRefFn)(void *ctx, int file, int line, const char *text);

/*
 * Read every file of the program but `skip`, in order, calling `fn` for each
 * line that mentions `name`.  Returns the number of calls made.
 */
int project_find_refs(const Project *p, const char *name, int skip,
                      ProjectRefFn fn, void *ctx);

#endif /* PROJECT_H_INCLUDED */
