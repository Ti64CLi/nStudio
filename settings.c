#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
  s->nasm_path[0] = '\0';
  strncpy(s->nasm_args, "--ask-name --no-auto-tns", 127);
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
    char line[256], k[64], v[192];
    while (fgets(line, sizeof(line), f)) {
      /* Strip trailing newline/CR */
      int ll = (int)strlen(line);
      while (ll > 0 && (line[ll - 1] == '\n' || line[ll - 1] == '\r'))
        line[--ll] = '\0';

      /* Split on first '=' only, preserving spaces in value */
      char *eq = strchr(line, '=');
      if (!eq)
        continue;
      int klen = (int)(eq - line);
      if (klen <= 0 || klen >= 64)
        continue;
      strncpy(k, line, klen);
      k[klen] = '\0';
      strncpy(v, eq + 1, 191);
      v[191] = '\0';

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
      else if (!strcmp(k, "nasm_path"))
        strncpy(g_settings.nasm_path, v, 255);
      else if (!strcmp(k, "nasm_args"))
        strncpy(g_settings.nasm_args, v, 127);
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
  fprintf(f, "nasm_path=%s\n", g_settings.nasm_path);
  fprintf(f, "nasm_args=%s\n", g_settings.nasm_args);
  fclose(f);
}

/* ================================================================
 * Auto-detect nasm executable
 * Recursively scans /documents for a file named "nasm.tns".
 * ================================================================ */
static int find_nasm_recursive(const char *dir, char *out, int outsz) {
  DIR *d = opendir(dir);
  if (!d)
    return 0;

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.')
      continue;

    char full[512];
    snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);

    struct stat st;
    if (stat(full, &st) != 0)
      continue;

    if (S_ISDIR(st.st_mode)) {
      if (find_nasm_recursive(full, out, outsz)) {
        closedir(d);
        return 1;
      }
    } else {
      /* Match "nasm.tns" case-insensitively */
      const char *name = de->d_name;
      if ((name[0] == 'n' || name[0] == 'N') &&
          (name[1] == 'a' || name[1] == 'A') &&
          (name[2] == 's' || name[2] == 'S') &&
          (name[3] == 'm' || name[3] == 'M') && name[4] == '.' &&
          (name[5] == 't' || name[5] == 'T') &&
          (name[6] == 'n' || name[6] == 'N') &&
          (name[7] == 's' || name[7] == 'S') && name[8] == '\0') {
        strncpy(out, full, outsz - 1);
        out[outsz - 1] = '\0';
        closedir(d);
        return 1;
      }
    }
  }

  closedir(d);
  return 0;
}

int settings_find_nasm(char *out, int outsz) {
  return find_nasm_recursive("/documents", out, outsz);
}
