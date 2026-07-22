/*
 * util.c
 * Minimal string helpers required by the vendored optab.c
 * (subset of nasm's util.c, same author/license).
 */

#include <ctype.h>

#include "util.h"

void strupper(const char *src, char *dst, size_t maxlen) {
  size_t i;
  for (i = 0; i < maxlen - 1 && src[i]; i++)
    dst[i] = (char)toupper((unsigned char)src[i]);

  dst[i] = '\0';
}

int my_isalnum(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}
