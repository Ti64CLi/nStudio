#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Named colour table (used by the colour picker)                     */
/* ------------------------------------------------------------------ */
typedef struct {
  const char *name;
  uint16_t value; /* RGB565 */
} NamedColour;

extern const NamedColour g_palette[];
extern const int g_palette_size;

/* ------------------------------------------------------------------ */
/* Syntax highlight colour indices                                    */
/* ------------------------------------------------------------------ */
typedef struct {
  int mnem;      /* ARM mnemonic            */
  int reg;       /* register name           */
  int imm;       /* immediate / 0x literal  */
  int label;     /* label definition        */
  int comment;   /* comments                */
  int directive; /* assembler directives    */
  int string;    /* string literals         */
  int normal;    /* generic text            */
} SyntaxColours;

/* ------------------------------------------------------------------ */
/* Master Configuration Struct                                        */
/* ------------------------------------------------------------------ */
typedef struct {
  int tab_width;
  int auto_indent;
  int syntax_highlight;
  char asm_extension[32];
  char nasm_path[256]; /* Full path to the nasm executable */
  char nasm_args[128]; /* Extra arguments passed after the source file */

  /* 0 = Dark, 1 = Light, 2 = Custom */
  int theme;

  int ui_bg;
  int ui_fg;
  int ui_border_light;
  int ui_border_dark;
  int ui_title_bg;
  int ui_title_fg;
  int ui_accent;
  int ui_accent_text;
  int ui_item_bg; /* Window shade / Gutters / Current line highlight */

  SyntaxColours syn;
} NStudioSettings;

extern NStudioSettings g_settings;

/* ------------------------------------------------------------------ */
/* API                                                                */
/* ------------------------------------------------------------------ */
void settings_defaults(NStudioSettings *s);
void settings_theme_light(NStudioSettings *s);
void settings_theme_dark(NStudioSettings *s);

void settings_load(void);
void settings_save(void);

uint16_t settings_col(int idx);
void settings_apply_theme(void); /* Pushes g_settings out to g_default_theme */

/*
 * Walk /documents recursively looking for a file named "nasm.tns".
 * On success, writes the full path into out (up to outsz bytes) and
 * returns 1.  Returns 0 if nothing was found.
 */
int settings_find_nasm(char *out, int outsz);

/*
 * Opens the settings UI window.
 * Blocks until the user closes or applies the settings.
 */
void settings_ui_open(void);

#endif /* SETTINGS_H */
