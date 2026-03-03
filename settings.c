#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "settings.h"

#define SETTINGS_FILE "/documents/ndless/nstudio.cfg"

/* ================================================================
 * Colour palette
 * All configurable colours are chosen from this list.
 * ================================================================ */
const NamedColour g_palette[] = {{"Black", GFX_COL_BLACK},
                                 {"Dark Grey", 0x2104u},
                                 {"Mid Grey", 0x4208u},
                                 {"Grey", GFX_COL_GREY},
                                 {"Light Grey", GFX_COL_LIGHT_GREY},
                                 {"White", GFX_COL_WHITE},
                                 {"Dark Blue", GFX_COL_DARK_BLUE},
                                 {"Blue", 0x001Fu},
                                 {"Light Blue", 0x567Fu},
                                 {"Cyan", 0x07FFu},
                                 {"Teal", 0x03EFu},
                                 {"Dark Green", 0x0320u},
                                 {"Green", GFX_COL_GREEN},
                                 {"Light Green", 0x47E0u},
                                 {"Yellow", 0xFFE0u},
                                 {"Amber", 0xFD00u},
                                 {"Orange", GFX_COL_ORANGE},
                                 {"Red", GFX_COL_RED},
                                 {"Pink", 0xFC1Fu},
                                 {"Magenta", 0xF81Fu},
                                 {"Purple", 0x801Fu},
                                 {"Dim BG", 0x0861u}};

const int g_palette_size = sizeof(g_palette) / sizeof(NamedColour);
NStudioSettings g_settings;

uint16_t settings_col(int idx) {
  if (idx < 0 || idx >= g_palette_size)
    return 0;
  return g_palette[idx].value;
}

void settings_apply_theme(void) {
  g_default_theme.bg = settings_col(g_settings.ui_bg);
  g_default_theme.fg = settings_col(g_settings.ui_fg);
  g_default_theme.border_light = settings_col(g_settings.ui_border_light);
  g_default_theme.border_dark = settings_col(g_settings.ui_border_dark);
  g_default_theme.title_bg = settings_col(g_settings.ui_title_bg);
  g_default_theme.title_fg = settings_col(g_settings.ui_title_fg);
  g_default_theme.accent = settings_col(g_settings.ui_accent);
  g_default_theme.accent_text = settings_col(g_settings.ui_accent_text);
  g_default_theme.item_bg = settings_col(g_settings.ui_item_bg);
}

void settings_defaults(NStudioSettings *s) {
  s->tab_width = 4;
  s->auto_indent = 1;
  s->syntax_highlight = 1;
  strncpy(s->asm_extension, "asm", 31);
  settings_theme_dark(s);
}

void settings_theme_dark(NStudioSettings *s) {
  s->theme = 0;
  s->ui_bg = 0;
  s->ui_fg = 5;
  s->ui_border_light = 4;
  s->ui_border_dark = 6;
  s->ui_title_bg = 6;
  s->ui_title_fg = 5;
  s->ui_accent = 12;
  s->ui_accent_text = 0;
  s->ui_item_bg = 1;

  s->syn.mnem = 9;
  s->syn.reg = 12;
  s->syn.imm = 16;
  s->syn.label = 14;
  s->syn.comment = 3;
  s->syn.directive = 19;
  s->syn.string = 16;
  s->syn.normal = 5;
}

void settings_theme_light(NStudioSettings *s) {
  s->theme = 1;
  s->ui_bg = 5;
  s->ui_fg = 0;
  s->ui_border_light = 3;
  s->ui_border_dark = 1;
  s->ui_title_bg = 4;
  s->ui_title_fg = 0;
  s->ui_accent = 12;
  s->ui_accent_text = 5;
  s->ui_item_bg = 4;

  s->syn.mnem = 6;
  s->syn.reg = 11;
  s->syn.imm = 15;
  s->syn.label = 17;
  s->syn.comment = 3;
  s->syn.directive = 20;
  s->syn.string = 15;
  s->syn.normal = 0;
}

void settings_load(void) {
  settings_defaults(&g_settings);
  FILE *f = fopen(SETTINGS_FILE, "r");
  if (f) {
    char line[128], k[64], v[64];
    while (fgets(line, sizeof(line), f)) {
      if (sscanf(line, "%63[^=]=%63s", k, v) == 2) {
        int vi = atoi(v);
        if (!strcmp(k, "tab_width"))
          g_settings.tab_width = vi;
        else if (!strcmp(k, "auto_indent"))
          g_settings.auto_indent = vi;
        else if (!strcmp(k, "syntax_highlight"))
          g_settings.syntax_highlight = vi;
        else if (!strcmp(k, "asm_extension"))
          strncpy(g_settings.asm_extension, v, 31);
        else if (!strcmp(k, "theme"))
          g_settings.theme = vi;
        else if (!strcmp(k, "ui_bg"))
          g_settings.ui_bg = vi;
        else if (!strcmp(k, "ui_fg"))
          g_settings.ui_fg = vi;
        else if (!strcmp(k, "ui_border_light"))
          g_settings.ui_border_light = vi;
        else if (!strcmp(k, "ui_border_dark"))
          g_settings.ui_border_dark = vi;
        else if (!strcmp(k, "ui_title_bg"))
          g_settings.ui_title_bg = vi;
        else if (!strcmp(k, "ui_title_fg"))
          g_settings.ui_title_fg = vi;
        else if (!strcmp(k, "ui_accent"))
          g_settings.ui_accent = vi;
        else if (!strcmp(k, "ui_accent_text"))
          g_settings.ui_accent_text = vi;
        else if (!strcmp(k, "ui_item_bg"))
          g_settings.ui_item_bg = vi;
        else if (!strcmp(k, "syn_mnem"))
          g_settings.syn.mnem = vi;
        else if (!strcmp(k, "syn_reg"))
          g_settings.syn.reg = vi;
        else if (!strcmp(k, "syn_imm"))
          g_settings.syn.imm = vi;
        else if (!strcmp(k, "syn_label"))
          g_settings.syn.label = vi;
        else if (!strcmp(k, "syn_comment"))
          g_settings.syn.comment = vi;
        else if (!strcmp(k, "syn_directive"))
          g_settings.syn.directive = vi;
        else if (!strcmp(k, "syn_string"))
          g_settings.syn.string = vi;
        else if (!strcmp(k, "syn_normal"))
          g_settings.syn.normal = vi;
      }
    }
    fclose(f);
  }

  /* Lock in preset palettes to prevent visual tearing if user changes theme
   * setting */
  if (g_settings.theme == 0)
    settings_theme_dark(&g_settings);
  else if (g_settings.theme == 1)
    settings_theme_light(&g_settings);

  settings_apply_theme();
}

void settings_save(void) {
  FILE *f = fopen(SETTINGS_FILE, "w");
  if (!f)
    return;
  fprintf(f, "tab_width=%d\n", g_settings.tab_width);
  fprintf(f, "auto_indent=%d\n", g_settings.auto_indent);
  fprintf(f, "syntax_highlight=%d\n", g_settings.syntax_highlight);
  fprintf(f, "asm_extension=%s\n", g_settings.asm_extension);
  fprintf(f, "theme=%d\n", g_settings.theme);

  fprintf(f, "ui_bg=%d\n", g_settings.ui_bg);
  fprintf(f, "ui_fg=%d\n", g_settings.ui_fg);
  fprintf(f, "ui_border_light=%d\n", g_settings.ui_border_light);
  fprintf(f, "ui_border_dark=%d\n", g_settings.ui_border_dark);
  fprintf(f, "ui_title_bg=%d\n", g_settings.ui_title_bg);
  fprintf(f, "ui_title_fg=%d\n", g_settings.ui_title_fg);
  fprintf(f, "ui_accent=%d\n", g_settings.ui_accent);
  fprintf(f, "ui_accent_text=%d\n", g_settings.ui_accent_text);
  fprintf(f, "ui_item_bg=%d\n", g_settings.ui_item_bg);

  fprintf(f, "syn_mnem=%d\n", g_settings.syn.mnem);
  fprintf(f, "syn_reg=%d\n", g_settings.syn.reg);
  fprintf(f, "syn_imm=%d\n", g_settings.syn.imm);
  fprintf(f, "syn_label=%d\n", g_settings.syn.label);
  fprintf(f, "syn_comment=%d\n", g_settings.syn.comment);
  fprintf(f, "syn_directive=%d\n", g_settings.syn.directive);
  fprintf(f, "syn_string=%d\n", g_settings.syn.string);
  fprintf(f, "syn_normal=%d\n", g_settings.syn.normal);
  fclose(f);
}
