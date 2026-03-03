/*
 * nstudio.c
 * nStudio - ARM assembly IDE for TI-Nspire CX / CX II (Ndless)
 */

#include <libndls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "filebrowser.h"
#include "gfx.h"
#include "settings.h"

/* ================================================================
 * Helpers
 * ================================================================ */

static void action_new_file(void) { editor_open(""); }

static void action_open_file(void) {
  /* filebrowser_select() owns the screen; on cancel it calls gfx_deinit().
     Re-initialise before returning to the main menu. */
  const char *path = filebrowser_select();
  if (!path) {
    gfx_init();
    return;
  }
  editor_open(path);
}

/* ================================================================
 * Settings UI (Using Modular Object-Oriented Form)
 * ================================================================ */

typedef struct {
  NStudioSettings original;
  NStudioSettings current_frame;
} SettingsContext;

static void settings_tick(GfxWindow *win) {
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

static void action_settings(void) {
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
      w_beh_hdr,   w_tab_lbl, w_tab_val, w_ind_lbl, w_ind_val, w_syn_lbl,
      w_syn_val,   w_ext_lbl, w_ext_val, w_app_hdr, w_thm_lbl, w_thm_val,
      w_bg_lbl,    w_bg_val,  w_fg_lbl,  w_fg_val,  w_acc_lbl, w_acc_val,
      w_atx_lbl,   w_atx_val, w_ttb_lbl, w_ttb_val, w_ttf_lbl, w_ttf_val,
      w_itm_lbl,   w_itm_val, w_brl_lbl, w_brl_val, w_brd_lbl, w_brd_val,
      w_syx_hdr,   w_nrm_lbl, w_nrm_val, w_mnm_lbl, w_mnm_val, w_reg_lbl,
      w_reg_val,   w_imm_lbl, w_imm_val, w_lbl_lbl, w_lbl_val, w_cmt_lbl,
      w_cmt_val,   w_dir_lbl, w_dir_val, w_str_lbl, w_str_val, btn_apply,
      btn_restore,
  };
  int num_widgets = (int)(sizeof(children) / sizeof(GfxWidget *));

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
  win.focused_idx = 2; /* start on Tab Width */
  win.scroll_y = 0;
  win.user_data = &ctx;
  win.on_tick = settings_tick;

  for (;;) {
    int result = gfx_window_exec(&win);

    if (result == num_widgets - 2) {
      ctx.original = g_settings;
      settings_save();
      win.focused_idx = 2;

    } else if (result == num_widgets - 1) {
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

/* ================================================================
 * Main menu
 * ================================================================ */
#define MENU_NEW 0
#define MENU_OPEN 1
#define MENU_SETTINGS 2
#define MENU_QUIT 3

static const char *menu_items[] = {
    "  New File",
    "  Open File",
    "  Settings",
    "  Quit",
};

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  settings_load();
  gfx_init();

  int last_sel = MENU_NEW;

  for (;;) {
    int choice = gfx_menu("nStudio", "ARM Assembly IDE for TI-Nspire",
                          menu_items, 4, last_sel);
    if (choice < 0 || choice == MENU_QUIT)
      break;

    last_sel = choice;

    switch (choice) {
    case MENU_NEW:
      action_new_file();
      break;
    case MENU_OPEN:
      action_open_file();
      break;
    case MENU_SETTINGS:
      action_settings();
      break;
    }

    gfx_init();
  }

  gfx_deinit();
  return 0;
}
