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
  /* Brackets are coloured by nesting depth so a matching pair shares a colour;
     the cycle repeats past the third level.  The defaults reproduce what the
     depths borrowed before they were configurable: register, immediate and
     directive colours. */
  int bracket1; /* outermost depth         */
  int bracket2;
  int bracket3;
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
  char last_dir[256];  /* Directory the file browser last visited; persisted
                          as "nasm_last_dir" and shared with the nasm app */

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

/*
 * Remember the directory the file browser should reopen at next time, and
 * persist it (shared with the nasm app via the "nasm_last_dir" cfg key).
 * settings_set_last_dir stores a directory path; settings_remember_file_dir
 * takes a file path and stores its parent directory. Both persist only when
 * the value actually changes, so repeated opens in the same folder do not
 * rewrite the config.
 */
void settings_set_last_dir(const char *dir);
void settings_remember_file_dir(const char *filepath);

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
