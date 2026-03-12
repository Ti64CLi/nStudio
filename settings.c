#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "filebrowser.h"
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
      if (strcasecmp(name, "nasm.tns") == 0) {
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

/* ================================================================
 * Unified Settings UI
 * ================================================================ */

typedef struct {
  NStudioSettings original;
  NStudioSettings current_frame;
} SettingsContext;

static void settings_ui_tick(GfxWindow *win) {
  SettingsContext *ctx = (SettingsContext *)win->user_data;

  if (g_settings.theme != ctx->current_frame.theme) {
    if (g_settings.theme == 0)
      settings_theme_dark(&g_settings);
    else if (g_settings.theme == 1)
      settings_theme_light(&g_settings);
  } else if (g_settings.theme != 2) {
    NStudioSettings *c = &g_settings;
    NStudioSettings *p = &ctx->current_frame;
    if (c->ui_bg != p->ui_bg || c->ui_fg != p->ui_fg ||
        c->ui_border_light != p->ui_border_light ||
        c->ui_border_dark != p->ui_border_dark ||
        c->ui_title_bg != p->ui_title_bg || c->ui_title_fg != p->ui_title_fg ||
        c->ui_accent != p->ui_accent ||
        c->ui_accent_text != p->ui_accent_text ||
        c->ui_item_bg != p->ui_item_bg || c->syn.mnem != p->syn.mnem ||
        c->syn.reg != p->syn.reg || c->syn.imm != p->syn.imm ||
        c->syn.label != p->syn.label || c->syn.comment != p->syn.comment ||
        c->syn.directive != p->syn.directive ||
        c->syn.string != p->syn.string || c->syn.normal != p->syn.normal) {
      g_settings.theme = 2;
    }
  }

  settings_apply_theme();

  int changed =
      (memcmp(&g_settings, &ctx->original, sizeof(NStudioSettings)) != 0);
  win->children[win->num_children - 2]->disabled = !changed;
  win->children[win->num_children - 1]->disabled = !changed;

  ctx->current_frame = g_settings;
}

void settings_ui_open(void) {
  static const char *theme_opts[] = {"Dark", "Light", "Custom"};
  const char *color_opts[32];
  for (int i = 0; i < g_palette_size && i < 32; i++)
    color_opts[i] = g_palette[i].name;

  const int row_h = 24, col1 = 10, col2 = 140, ww = 110;
  int yy = 10;

  GfxWidget *w_beh_hdr =
      widget_create_heading(col1, yy, 280 - col1 * 2, row_h, "Behaviour");
  yy += row_h;
  GfxWidget *w_tab_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Tab Width:");
  GfxWidget *w_tab_val =
      widget_create_number(col2, yy, ww, row_h, &g_settings.tab_width, 1, 8);
  yy += row_h;
  GfxWidget *w_ind_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Auto Indent:");
  GfxWidget *w_ind_val =
      widget_create_toggle(col2, yy, ww, row_h, &g_settings.auto_indent);
  yy += row_h;
  GfxWidget *w_syn_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Syntax Colors:");
  GfxWidget *w_syn_val =
      widget_create_toggle(col2, yy, ww, row_h, &g_settings.syntax_highlight);
  yy += row_h;
  GfxWidget *w_ext_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "ASM Extension:");
  GfxWidget *w_ext_val = widget_create_text(
      col2, yy, ww, row_h, g_settings.asm_extension, 16, "Edit ASM Extension:");
  yy += row_h;
  GfxWidget *w_nasm_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "nasm Path:");
  GfxWidget *w_nasm_val = widget_create_text(
      col2, yy, ww, row_h, g_settings.nasm_path, 255, "nasm Executable Path:");
  yy += row_h;
  GfxWidget *w_nasm_detect =
      widget_create_button(col1, yy, 260, row_h, "Auto-detect nasm");
  yy += row_h;
  GfxWidget *w_nasm_args_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "nasm Args:");
  GfxWidget *w_nasm_args_val = widget_create_text(
      col2, yy, ww, row_h, g_settings.nasm_args, 127, "nasm Arguments:");
  yy += row_h;

  yy += 10;

  GfxWidget *w_app_hdr =
      widget_create_heading(col1, yy, 280 - col1 * 2, row_h, "Appearance");
  yy += row_h;
  GfxWidget *w_thm_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Base Theme:");
  GfxWidget *w_thm_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.theme, theme_opts, 3);
  yy += row_h;
  GfxWidget *w_bg_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Background:");
  GfxWidget *w_bg_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.ui_bg, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_fg_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Text:");
  GfxWidget *w_fg_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.ui_fg, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_acc_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Accent:");
  GfxWidget *w_acc_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.ui_accent, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_atx_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Accent Text:");
  GfxWidget *w_atx_val =
      widget_create_dropdown(col2, yy, ww, row_h, &g_settings.ui_accent_text,
                             color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_ttb_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Title Bar:");
  GfxWidget *w_ttb_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.ui_title_bg, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_ttf_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Title Text:");
  GfxWidget *w_ttf_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.ui_title_fg, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_itm_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Gutter/Shade:");
  GfxWidget *w_itm_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.ui_item_bg, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_brl_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Border Light:");
  GfxWidget *w_brl_val =
      widget_create_dropdown(col2, yy, ww, row_h, &g_settings.ui_border_light,
                             color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_brd_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Border Dark:");
  GfxWidget *w_brd_val =
      widget_create_dropdown(col2, yy, ww, row_h, &g_settings.ui_border_dark,
                             color_opts, g_palette_size);
  yy += row_h;

  yy += 10;

  GfxWidget *w_syx_hdr =
      widget_create_heading(col1, yy, 280 - col1 * 2, row_h, "Syntax");
  yy += row_h;
  GfxWidget *w_nrm_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Normal Text:");
  GfxWidget *w_nrm_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.normal, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_mnm_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Mnemonics:");
  GfxWidget *w_mnm_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.mnem, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_reg_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Registers:");
  GfxWidget *w_reg_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.reg, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_imm_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Immediates:");
  GfxWidget *w_imm_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.imm, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_lbl_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Labels:");
  GfxWidget *w_lbl_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.label, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_cmt_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Comments:");
  GfxWidget *w_cmt_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.comment, color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_dir_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Directives:");
  GfxWidget *w_dir_val =
      widget_create_dropdown(col2, yy, ww, row_h, &g_settings.syn.directive,
                             color_opts, g_palette_size);
  yy += row_h;
  GfxWidget *w_str_lbl =
      widget_create_label(col1, yy, col2 - col1, row_h, "Strings:");
  GfxWidget *w_str_val = widget_create_dropdown(
      col2, yy, ww, row_h, &g_settings.syn.string, color_opts, g_palette_size);
  yy += row_h + 14;

  GfxWidget *btn_apply = widget_create_button(10, yy, 125, row_h, "Apply");
  GfxWidget *btn_restore = widget_create_button(145, yy, 125, row_h, "Restore");

  GfxWidget *children[] = {
      w_beh_hdr,  w_tab_lbl,     w_tab_val,       w_ind_lbl,       w_ind_val,
      w_syn_lbl,  w_syn_val,     w_ext_lbl,       w_ext_val,       w_nasm_lbl,
      w_nasm_val, w_nasm_detect, w_nasm_args_lbl, w_nasm_args_val, w_app_hdr,
      w_thm_lbl,  w_thm_val,     w_bg_lbl,        w_bg_val,        w_fg_lbl,
      w_fg_val,   w_acc_lbl,     w_acc_val,       w_atx_lbl,       w_atx_val,
      w_ttb_lbl,  w_ttb_val,     w_ttf_lbl,       w_ttf_val,       w_itm_lbl,
      w_itm_val,  w_brl_lbl,     w_brl_val,       w_brd_lbl,       w_brd_val,
      w_syx_hdr,  w_nrm_lbl,     w_nrm_val,       w_mnm_lbl,       w_mnm_val,
      w_reg_lbl,  w_reg_val,     w_imm_lbl,       w_imm_val,       w_lbl_lbl,
      w_lbl_val,  w_cmt_lbl,     w_cmt_val,       w_dir_lbl,       w_dir_val,
      w_str_lbl,  w_str_val,     btn_apply,       btn_restore,
  };

  int num_widgets = (int)(sizeof(children) / sizeof(GfxWidget *));
  int idx_nasm_detect = 11;
  int idx_apply = num_widgets - 2;
  int idx_restore = num_widgets - 1;

  SettingsContext ctx;
  ctx.original = g_settings;
  ctx.current_frame = g_settings;

  GfxWindow win;
  win.x = (GFX_W - 280) / 2;
  win.y = (GFX_H - 220) / 2;
  win.w = 280;
  win.h = 220;
  win.title = "Preferences";
  win.theme = NULL;
  win.children = children;
  win.num_children = num_widgets;
  win.focused_idx = 2;
  win.scroll_y = 0;
  win.user_data = &ctx;
  win.on_tick = settings_ui_tick;

  for (;;) {
    int result = gfx_window_exec(&win);

    if (result == idx_nasm_detect) {
      char found[256];
      if (settings_find_nasm(found, sizeof(found))) {
        strncpy(g_settings.nasm_path, found, 255);
        g_settings.nasm_path[255] = '\0';
        const char *ok2[2] = {"nasm found:", found};
        gfx_window_alert("Auto-detect", ok2, 2, "OK");
      } else {
        const char *body[] = {"nasm.tns not found in /documents.",
                              "Please select it manually."};
        gfx_window_alert("Auto-detect", body, 2, "OK");

        const char *picked = filebrowser_select();
        if (picked && picked[0]) {
          strncpy(g_settings.nasm_path, picked, 255);
          g_settings.nasm_path[255] = '\0';
        }
      }
    } else if (result == idx_apply) {
      ctx.original = g_settings;
      settings_save();
      win.focused_idx = 2;
    } else if (result == idx_restore) {
      g_settings = ctx.original;
      ctx.current_frame = g_settings;
      settings_apply_theme();
      win.focused_idx = 2;
    } else {
      int changed =
          (memcmp(&g_settings, &ctx.original, sizeof(NStudioSettings)) != 0);
      if (changed) {
        static const char *body[] = {"You have unapplied changes.",
                                     "What would you like to do?"};
        int choice = gfx_window_confirm3("Unsaved Changes", body, 2, "Apply",
                                         "Discard", "Cancel");
        if (choice == 0) {
          ctx.original = g_settings;
          settings_save();
          break;
        } else if (choice == 1) {
          g_settings = ctx.original;
          settings_apply_theme();
          break;
        }
      } else {
        break;
      }
    }
  }

  for (int i = 0; i < num_widgets; i++)
    free(children[i]);
}
