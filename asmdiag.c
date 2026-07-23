/*
 * asmdiag.c
 * Minimal JSON reader for the assembler diagnostics file written by nasm.
 * See asmdiag.h for the full nStudio <-> nasm contract and JSON schema.
 *
 * The parser is a small, self-contained JSON subset scanner: it reads the
 * documented keys by name, skips nested arrays/objects and keys it does not
 * consume, and unescapes JSON strings.  It has no editor or Ndless
 * dependencies, so it builds and unit-tests on the host unchanged.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asmdiag.h"

/* Largest diagnostics file we will read; nasm's output is far smaller. */
#define ASMDIAG_MAX_FILE (256 * 1024)

static int hexval(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static void skip_ws(const char **p) {
  while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r')
    (*p)++;
}

/* Advance *p from an opening quote past the matching closing quote. */
static void skip_string(const char **p) {
  (*p)++; /* opening quote */
  while (**p) {
    if (**p == '\\' && (*p)[1]) {
      *p += 2;
      continue;
    }
    if (**p == '"') {
      (*p)++;
      return;
    }
    (*p)++;
  }
}

/* Skip one JSON value (string, number, literal, or a nested array/object),
   leaving *p just past it.  Strings inside containers are skipped so their
   brackets do not disturb the depth count. */
static void skip_value(const char **p) {
  skip_ws(p);
  if (**p == '"') {
    skip_string(p);
    return;
  }
  if (**p == '[' || **p == '{') {
    int depth = 0;
    while (**p) {
      char c = **p;
      if (c == '"') {
        skip_string(p);
        continue;
      }
      if (c == '[' || c == '{')
        depth++;
      else if (c == ']' || c == '}') {
        depth--;
        (*p)++;
        if (depth == 0)
          return;
        continue;
      }
      (*p)++;
    }
    return;
  }
  while (**p && **p != ',' && **p != '}' && **p != ']' && **p != ' ' &&
         **p != '\t' && **p != '\n' && **p != '\r')
    (*p)++;
}

/* Parse a JSON string at *p into out (NUL-terminated, bounded), unescaping
   the standard sequences.  Returns 1 on success. */
static int parse_string(const char **p, char *out, int outsz) {
  skip_ws(p);
  if (**p != '"')
    return 0;
  (*p)++;

  int n = 0;
  while (**p && **p != '"') {
    char c = **p;
    (*p)++;
    if (c == '\\' && **p) {
      char e = **p;
      (*p)++;
      switch (e) {
      case 'n':
        c = '\n';
        break;
      case 'r':
        c = '\r';
        break;
      case 't':
        c = '\t';
        break;
      case 'b':
        c = '\b';
        break;
      case 'f':
        c = '\f';
        break;
      case 'u': {
        int cp = 0;
        for (int k = 0; k < 4 && **p; k++) {
          int d = hexval(**p);
          if (d < 0)
            break;
          cp = cp * 16 + d;
          (*p)++;
        }
        /* The on-calc font is ASCII; keep printable ASCII, else '?'. */
        c = (cp >= 0x20 && cp < 0x7f) ? (char)cp : '?';
        break;
      }
      default:
        c = e; /* '"', '\\', '/', and any other char pass through */
        break;
      }
    }
    if (n < outsz - 1)
      out[n++] = c;
  }
  if (**p == '"')
    (*p)++;
  out[n] = '\0';
  return 1;
}

static int parse_int(const char **p) {
  skip_ws(p);
  int sign = 1;
  if (**p == '-') {
    sign = -1;
    (*p)++;
  }
  int v = 0;
  while (**p >= '0' && **p <= '9') {
    v = v * 10 + (**p - '0');
    (*p)++;
  }
  return sign * v;
}

static AsmDiagSeverity severity_from(const char *s) {
  if (!strcmp(s, "warning"))
    return ADIAG_WARNING;
  if (!strcmp(s, "note"))
    return ADIAG_NOTE;
  return ADIAG_ERROR;
}

/* Parse one diagnostic object at *p (which points at '{') into d.
   Returns 1 on a well-formed object. */
static int parse_object(const char **p, AsmDiag *d) {
  skip_ws(p);
  if (**p != '{')
    return 0;
  (*p)++;

  d->severity = ADIAG_ERROR;
  d->line = -1;
  d->col = 0;
  d->end_col = 0;
  d->code[0] = '\0';
  d->file[0] = '\0';
  d->message[0] = '\0';

  for (;;) {
    skip_ws(p);
    if (**p == '}') {
      (*p)++;
      return 1;
    }
    if (**p == '\0')
      return 0;

    char key[24];
    if (!parse_string(p, key, sizeof(key)))
      return 0;
    skip_ws(p);
    if (**p != ':')
      return 0;
    (*p)++;

    if (!strcmp(key, "severity")) {
      char s[16];
      parse_string(p, s, sizeof(s));
      d->severity = severity_from(s);
    } else if (!strcmp(key, "code")) {
      parse_string(p, d->code, sizeof(d->code));
    } else if (!strcmp(key, "file")) {
      parse_string(p, d->file, sizeof(d->file));
    } else if (!strcmp(key, "message")) {
      parse_string(p, d->message, sizeof(d->message));
    } else if (!strcmp(key, "line")) {
      d->line = parse_int(p);
    } else if (!strcmp(key, "col")) {
      d->col = parse_int(p);
    } else if (!strcmp(key, "end_col")) {
      d->end_col = parse_int(p);
    } else {
      skip_value(p); /* source_line, expanded_from, related, include_stack... */
    }

    skip_ws(p);
    if (**p == ',') {
      (*p)++;
      continue;
    }
    if (**p == '}') {
      (*p)++;
      return 1;
    }
    return 1; /* malformed tail: keep what we parsed */
  }
}

static int parse_document(const char *buf, AsmDiag *out, int max) {
  const char *p = buf;
  skip_ws(&p);
  if (*p != '[')
    return 0; /* not an array: treat as no diagnostics */
  p++;

  int n = 0;
  for (;;) {
    skip_ws(&p);
    if (*p == ']' || *p == '\0')
      break;

    if (*p == '{') {
      AsmDiag d;
      if (parse_object(&p, &d)) {
        if (n < max)
          out[n] = d;
        if (n < max)
          n++;
      } else {
        skip_value(&p); /* resync past a malformed object */
      }
    } else {
      skip_value(&p); /* unexpected token: resync */
    }

    skip_ws(&p);
    if (*p == ',') {
      p++;
      continue;
    }
    if (*p == ']' || *p == '\0')
      break;
    p++; /* stray separator: advance to make progress */
  }
  return n;
}

int asmdiag_parse_file(const char *path, AsmDiag *out, int max) {
  if (!path || !out || max <= 0)
    return -1;

  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;

  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  if (sz < 0) {
    fclose(f);
    return -1;
  }
  if (sz > ASMDIAG_MAX_FILE)
    sz = ASMDIAG_MAX_FILE;

  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -1;
  }
  size_t rd = fread(buf, 1, (size_t)sz, f);
  buf[rd] = '\0';
  fclose(f);

  int n = parse_document(buf, out, max);
  free(buf);
  return n;
}

/* Base name (component after the last '/') of a path. */
static const char *base_name(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

int asmdiag_first_for_file(const AsmDiag *diags, int n, const char *path) {
  if (!diags || !path)
    return -1;
  const char *want_base = base_name(path);
  for (int i = 0; i < n; i++) {
    if (diags[i].line < 1)
      continue;
    if (strcmp(diags[i].file, path) == 0 ||
        strcmp(base_name(diags[i].file), want_base) == 0)
      return i;
  }
  return -1;
}
