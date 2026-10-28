/*
 * Host stand-in for Ndless's <keys.h>, so that gfx.h - and with it settings.c -
 * compiles on the build machine for the integration test.  gfx.h only names
 * the type; no key is ever read.
 */
#ifndef HOST_KEYS_H_INCLUDED
#define HOST_KEYS_H_INCLUDED

typedef struct {
  int row, col, tpad_row, tpad_col, tpad_arrow;
} t_key;

#endif /* HOST_KEYS_H_INCLUDED */
