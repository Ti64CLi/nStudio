/*
 * util.h
 * Minimal string helpers required by the vendored optab.c
 * (subset of nasm's util.h, same author/license).
 */
#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include <stddef.h>

void strupper(const char *src, char *dst, size_t maxlen);
int my_isalnum(char c);

/* Case-insensitive compare of at most n chars (stops at NUL in either).
   A generic string primitive shared by syntax.c, asmdb.c and editor.c;
   kept here so no module has to depend on syntax.h just for it. */
int strncaseeq(const char *a, const char *b, int n);

#endif /* UTIL_H_INCLUDED */
