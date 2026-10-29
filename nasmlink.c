/*
 * nasmlink.c
 * How nStudio calls nasm, and the rules it shares with it.  See nasmlink.h.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nasmlink.h"
#include "optab.h"

int nasmlink_output_path(const char *src, char *out, size_t outsz) {
  if (!src || !src[0])
    return 0;
  if (strlen(src) + 5 > outsz)
    return 0; /* no room for the stripped name plus ".tns" and its NUL */

  strncpy(out, src, outsz - 1);
  out[outsz - 1] = '\0';

  char *name = strrchr(out, '/');
  name = name ? name + 1 : out;

  char *dot = strchr(name, '.');
  if (dot)
    *dot = '\0';

  strcat(out, ".tns");
  return strcmp(out, src) != 0;
}

int nasmlink_invocation(NasmInvocation *inv, const char *src,
                        const char *user_args, const char *diag_path,
                        const char *format, const void *fb) {
  static char fb_flag[] = "--fb";
  static char no_ui_flag[] = "--no-ui";
  static char report_flag[] = "--report=2";

  inv->argc = 0;
  if (strlen(src) >= sizeof(inv->source))
    return 0;
  if (snprintf(inv->diag_arg, sizeof(inv->diag_arg), "--diagnostics-file=%s",
               diag_path) >= (int)sizeof(inv->diag_arg))
    return 0;
  if (format && strcmp(format, "zehn") != 0 && strcmp(format, "prg") != 0)
    return 0;

  strcpy(inv->source, src);
  inv->argv[inv->argc++] = inv->source; /* always first: the source file */

  /* strtok is destructive, hence the copy.  The last slots stay free for
     nStudio's own options. */
  strncpy(inv->user_args, user_args ? user_args : "",
          sizeof(inv->user_args) - 1);
  inv->user_args[sizeof(inv->user_args) - 1] = '\0';
  char *tok = strtok(inv->user_args, " \t");
  while (tok && inv->argc < NASMLINK_MAX_ARGS - NASMLINK_INJECTED) {
    inv->argv[inv->argc++] = tok;
    tok = strtok(NULL, " \t");
  }

  inv->argv[inv->argc++] = no_ui_flag;
  inv->argv[inv->argc++] = report_flag;
  if (format) {
    snprintf(inv->format_arg, sizeof(inv->format_arg), "--format=%s", format);
    inv->argv[inv->argc++] = inv->format_arg;
  }

  /* Passing the diagnostics file alone is enough to select JSON output; an
     older nasm that predates the option ignores it as an unknown flag. */
  inv->argv[inv->argc++] = inv->diag_arg;

  snprintf(inv->fb_arg, sizeof(inv->fb_arg), "%lu",
           (unsigned long)(uintptr_t)fb);
  inv->argv[inv->argc++] = fb_flag;
  inv->argv[inv->argc++] = inv->fb_arg;
  return 1;
}

int nasmlink_include_operand(const char *line, char *out, int outsz) {
  /* The comment starts at the first ';' outside "..." or a 'c' literal, as in
     nasm's sl_parse_comments; trailing blanks before it do not count. */
  int end = 0, inquote = 0;
  for (; line[end]; end++) {
    if (line[end] == '"')
      inquote = !inquote;
    if (!inquote && line[end] == '\'' && line[end + 1] &&
        line[end + 2] == '\'') {
      end += 2;
      continue;
    }
    if (line[end] == ';' && !inquote)
      break;
  }
  while (end > 0 && isspace((unsigned char)line[end - 1]))
    end--;

  /* A word in column 0 is a label; the directive comes after it. */
  int i = 0;
  if (end > 0 && !isspace((unsigned char)line[0]))
    while (i < end && !isspace((unsigned char)line[i]))
      i++;
  while (i < end && isspace((unsigned char)line[i]))
    i++;

  int ds = i;
  while (i < end && !isspace((unsigned char)line[i]))
    i++;
  int dlen = i - ds;
  if (dlen == 0 || dlen >= 16)
    return 0;

  char directive[16];
  memcpy(directive, line + ds, (size_t)dlen);
  directive[dlen] = '\0';
  if (!is_preasm_directive(directive))
    return 0; /* INCLUDE and GET, in any case, as optab defines them */

  while (i < end && isspace((unsigned char)line[i]))
    i++;

  int olen = end - i;
  if (olen <= 0 || olen >= outsz)
    return 0;
  memcpy(out, line + i, (size_t)olen);
  out[olen] = '\0';
  return olen;
}

int nasmlink_resolve_include(const char *includer, const char *operand,
                             char *out, int outsz) {
  const char *rel = operand;

  if (rel[0] == '/') { /* already absolute */
    if ((int)strlen(rel) >= outsz)
      return 0;
    strncpy(out, rel, (size_t)outsz - 1);
    out[outsz - 1] = '\0';
    return 1;
  }

  char dir[1024];
  strncpy(dir, includer, sizeof(dir) - 1);
  dir[sizeof(dir) - 1] = '\0';
  char *slash = strrchr(dir, '/');
  if (slash)
    slash[1] = '\0'; /* keep the trailing slash, as nasm does */
  else
    dir[0] = '\0';

  while (*rel) {
    if (rel[0] == '.' && rel[1] == '/') {
      rel += 2;
      continue;
    }
    if (rel[0] == '.' && rel[1] == '.' && rel[2] == '/') {
      /* Climb one directory: drop the trailing slash, then the last name. */
      int dl = (int)strlen(dir);
      if (dl > 0 && dir[dl - 1] == '/')
        dir[--dl] = '\0';
      char *up = strrchr(dir, '/');
      if (up)
        up[1] = '\0';
      else
        dir[0] = '\0';
      rel += 3;
      continue;
    }
    break;
  }

  int dl = (int)strlen(dir), rl = (int)strlen(rel);
  if (dl + rl >= outsz)
    return 0;
  memcpy(out, dir, (size_t)dl);
  memcpy(out + dl, rel, (size_t)rl + 1); /* copies the terminator too */
  return 1;
}
