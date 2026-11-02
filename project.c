/*
 * project.c
 * The program being edited, and the symbols of each of its files.  See
 * project.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asmdiag.h"
#include "project.h"

#define PATH_LEN 512

void project_init(Project *p, const ProjectLang *lang) {
  memset(p, 0, sizeof(*p));
  p->lang = lang;
}

void project_close(Project *p) {
  const ProjectLang *lang = p->lang;
  free(p->files);
  free(p->syms);
  free(p->pool);
  project_init(p, lang);
}

const char *project_root(const Project *p) {
  return p->nfiles > 0 ? p->pool + p->files[0].path : NULL;
}

const char *project_path(const Project *p, int f) {
  return p->pool + p->files[f].path;
}

const char *project_symbol_name(const Project *p, int i) {
  return p->pool + p->syms[i].name;
}

/* Append `s` to the pool; returns its offset, or -1 when memory runs out. */
static int pool_add(Project *p, const char *s) {
  int len = (int)strlen(s) + 1;
  if (p->pool_len + len > p->pool_cap) {
    int cap = p->pool_cap ? p->pool_cap : 1024;
    while (cap < p->pool_len + len)
      cap *= 2;
    char *grown = realloc(p->pool, (size_t)cap);
    if (!grown) {
      p->incomplete = 1;
      return -1;
    }
    p->pool = grown;
    p->pool_cap = cap;
  }
  memcpy(p->pool + p->pool_len, s, (size_t)len);
  int at = p->pool_len;
  p->pool_len += len;
  return at;
}

static void add_symbol(Project *p, const char *name, int file, int line) {
  if (p->nsyms == PROJECT_MAX_SYMBOLS) {
    p->incomplete = 1;
    return;
  }
  if (p->nsyms == p->syms_cap) {
    int cap = p->syms_cap ? p->syms_cap * 2 : 64;
    ProjectSymbol *grown = realloc(p->syms, (size_t)cap * sizeof(*grown));
    if (!grown) {
      p->incomplete = 1;
      return;
    }
    p->syms = grown;
    p->syms_cap = cap;
  }
  int at = pool_add(p, name);
  if (at < 0)
    return;
  ProjectSymbol *s = &p->syms[p->nsyms++];
  s->name = at;
  s->file = file;
  s->line = line;
}

int project_find_file(const Project *p, const char *path) {
  char norm[PATH_LEN];
  if (!path || !asmdiag_normalize_path(path, norm, sizeof(norm)))
    return -1;
  for (int f = 0; f < p->nfiles; f++)
    if (strcmp(project_path(p, f), norm) == 0)
      return f;
  return -1;
}

/* The next line of `fp` in *buf, grown as needed, without its line ending.
   Returns 1, or 0 at the end of the file - or when memory runs out, which also
   sets *oom.  Lines of any length are read whole, so line numbers always agree
   with the assembler's. */
static int read_line(FILE *fp, char **buf, int *cap, int *oom) {
  int len = 0, c;
  while ((c = getc(fp)) != EOF && c != '\n') {
    if (len + 2 > *cap) {
      int ncap = *cap ? *cap * 2 : 256;
      char *grown = realloc(*buf, (size_t)ncap);
      if (!grown) {
        *oom = 1;
        return 0;
      }
      *buf = grown;
      *cap = ncap;
    }
    (*buf)[len++] = (char)c;
  }
  if (c == EOF && len == 0)
    return 0;
  if (*cap == 0) { /* an empty first line: nothing allocated yet */
    *buf = malloc(256);
    if (!*buf) {
      *oom = 1;
      return 0;
    }
    *cap = 256;
  }
  if (len > 0 && (*buf)[len - 1] == '\r')
    len--;
  (*buf)[len] = '\0';
  return 1;
}

/* FNV-1a, over the included files' paths in order. */
#define HASH_SEED 2166136261u

static unsigned hash_path(unsigned h, const char *s) {
  for (; *s; s++)
    h = (h ^ (unsigned char)*s) * 16777619u;
  return (h ^ '\n') * 16777619u;
}

/* Files waiting to be read, with what brought them in. */
typedef struct {
  char *path;
  int includer, line;
} Pending;

typedef struct {
  Pending *items;
  int n, cap;
} PendingList;

/* Add `path` (copied) to `l`.  Returns 0 when memory runs out. */
static int pending_push(PendingList *l, const char *path, int includer,
                        int line) {
  if (l->n == l->cap) {
    int cap = l->cap ? l->cap * 2 : 16;
    Pending *grown = realloc(l->items, (size_t)cap * sizeof(*grown));
    if (!grown)
      return 0;
    l->items = grown;
    l->cap = cap;
  }
  char *copy = malloc(strlen(path) + 1);
  if (!copy)
    return 0;
  strcpy(copy, path);
  l->items[l->n].path = copy;
  l->items[l->n].includer = includer;
  l->items[l->n].line = line;
  l->n++;
  return 1;
}

static void pending_free(PendingList *l) {
  for (int i = 0; i < l->n; i++)
    free(l->items[i].path);
  free(l->items);
  l->items = NULL;
  l->n = l->cap = 0;
}

/* Read file `f`: index the symbols it defines and, when `children` is given,
   list the files it includes, in order.  Returns the hash of those files (see
   ProjectFile.includes). */
static unsigned scan_file(Project *p, int f, PendingList *children) {
  char path[PATH_LEN]; /* the pool may move while symbols are added */
  snprintf(path, sizeof(path), "%s", project_path(p, f));

  unsigned h = HASH_SEED;
  FILE *fp = fopen(path, "rb");
  if (!fp)
    return h;

  char *line = NULL;
  int cap = 0, oom = 0, n = 0;
  char name[PROJECT_NAME_MAX], operand[PATH_LEN], resolved[PATH_LEN],
      norm[PATH_LEN];
  while (read_line(fp, &line, &cap, &oom)) {
    n++;
    if (p->lang->symbol_def(line, name, sizeof(name)))
      add_symbol(p, name, f, n);
    if (p->lang->include_operand(line, operand, sizeof(operand)) &&
        p->lang->resolve_include(path, operand, resolved, sizeof(resolved)) &&
        asmdiag_normalize_path(resolved, norm, sizeof(norm))) {
      h = hash_path(h, norm);
      if (children && !pending_push(children, norm, f, n))
        p->incomplete = 1;
    }
  }
  if (oom || ferror(fp))
    p->incomplete = 1;
  free(line);
  fclose(fp);
  return h;
}

static int readable(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp)
    return 0;
  fclose(fp);
  return 1;
}

int project_open(Project *p, const char *root) {
  project_close(p);

  p->files = malloc(PROJECT_MAX_FILES * sizeof(*p->files));
  PendingList pending = {NULL, 0, 0}, children = {NULL, 0, 0};
  if (!p->files || !pending_push(&pending, root, -1, 0)) {
    p->incomplete = 1;
    pending_free(&pending);
    return 0;
  }

  /* Depth first, each file's includes in the order they appear: the order the
     assembler reads them in. */
  while (pending.n > 0) {
    Pending it = pending.items[--pending.n];
    char norm[PATH_LEN];
    int ok = asmdiag_normalize_path(it.path, norm, sizeof(norm));
    free(it.path);
    if (!ok) {
      p->incomplete = 1;
      continue;
    }
    if (project_find_file(p, norm) >= 0)
      continue; /* read once: a second include of it, or a cycle */
    if (it.includer >= 0 && !readable(norm))
      continue; /* missing: the assembler says so, and there is nothing here */
    if (p->nfiles == PROJECT_MAX_FILES) {
      p->incomplete = 1;
      continue;
    }
    int at = pool_add(p, norm);
    if (at < 0)
      continue;

    int f = p->nfiles++;
    p->files[f].path = at;
    p->files[f].includer = it.includer;
    p->files[f].line = it.line;
    p->files[f].includes = scan_file(p, f, &children);

    /* Pushed last first, so the first include is read next. */
    for (int i = children.n - 1; i >= 0; i--) {
      Pending *c = &children.items[i];
      if (!pending_push(&pending, c->path, c->includer, c->line))
        p->incomplete = 1;
    }
    for (int i = 0; i < children.n; i++)
      free(children.items[i].path);
    children.n = 0;
  }

  pending_free(&pending);
  pending_free(&children);
  return !p->incomplete;
}

/* Reverse syms[lo, hi). */
static void reverse_syms(ProjectSymbol *s, int lo, int hi) {
  for (hi--; lo < hi; lo++, hi--) {
    ProjectSymbol t = s[lo];
    s[lo] = s[hi];
    s[hi] = t;
  }
}

/* Copy every live string into a fresh pool, dropping the names of symbols
   that are gone.  Keeps the old pool if memory runs out: wasteful, not
   wrong. */
static void compact_pool(Project *p) {
  char *fresh = malloc((size_t)p->pool_cap);
  if (!fresh)
    return;
  int len = 0;
  for (int f = 0; f < p->nfiles; f++) {
    const char *s = project_path(p, f);
    int n = (int)strlen(s) + 1;
    memcpy(fresh + len, s, (size_t)n);
    p->files[f].path = len;
    len += n;
  }
  for (int i = 0; i < p->nsyms; i++) {
    const char *s = project_symbol_name(p, i);
    int n = (int)strlen(s) + 1;
    memcpy(fresh + len, s, (size_t)n);
    p->syms[i].name = len;
    len += n;
  }
  free(p->pool);
  p->pool = fresh;
  p->pool_len = len;
}

int project_file_saved(Project *p, const char *path) {
  int f = project_find_file(p, path);
  if (f < 0)
    return 0;

  /* Drop its symbols, then read it again: the new ones land at the end. */
  int k = 0;
  for (int i = 0; i < p->nsyms; i++)
    if (p->syms[i].file != f)
      p->syms[k++] = p->syms[i];
  p->nsyms = k;
  int first_new = p->nsyms;

  if (scan_file(p, f, NULL) != p->files[f].includes) {
    /* It includes other files now: the program itself may have changed. */
    char root[PATH_LEN];
    snprintf(root, sizeof(root), "%s", project_root(p));
    project_open(p, root);
    return 1;
  }

  /* Move them back among their file's neighbours, keeping symbols ordered by
     file: a rotation, so nothing needs allocating. */
  int pos = 0;
  while (pos < first_new && p->syms[pos].file < f)
    pos++;
  reverse_syms(p->syms, pos, first_new);
  reverse_syms(p->syms, first_new, p->nsyms);
  reverse_syms(p->syms, pos, p->nsyms);

  compact_pool(p);
  return 1;
}

int project_find_symbol(const Project *p, const char *name, int skip,
                        int *file, int *line) {
  for (int i = 0; i < p->nsyms; i++) {
    const ProjectSymbol *s = &p->syms[i];
    if (s->file != skip && strcmp(project_symbol_name(p, i), name) == 0) {
      *file = s->file;
      *line = s->line;
      return 1;
    }
  }
  return 0;
}

int project_find_refs(const Project *p, const char *name, int skip,
                      ProjectRefFn fn, void *ctx) {
  int calls = 0, more = 1;
  for (int f = 0; f < p->nfiles && more; f++) {
    if (f == skip)
      continue;
    FILE *fp = fopen(project_path(p, f), "rb");
    if (!fp)
      continue;
    char *line = NULL;
    int cap = 0, oom = 0, n = 0;
    while (more && read_line(fp, &line, &cap, &oom)) {
      n++;
      if (p->lang->mentions(line, name)) {
        calls++;
        more = fn(ctx, f, n, line);
      }
    }
    free(line);
    fclose(fp);
  }
  return calls;
}
