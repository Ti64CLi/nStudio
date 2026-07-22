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

#endif /* UTIL_H_INCLUDED */
