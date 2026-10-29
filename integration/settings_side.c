/*
 * nStudio's real settings code (settings.c) on the build machine, for the
 * settings round trip through nasm in run.sh:
 *
 *   settings_side write   save a configuration that differs from the defaults
 *                         in every field nStudio has, bracket colours included
 *   settings_side dump    load the configuration and print every field
 *
 * The configuration file is ./nstudio.cfg (SETTINGS_FILE, set by run.sh).
 */
#include <stdio.h>
#include <string.h>

#include "settings.h"

static void write_settings(void) {
  NStudioSettings *s = &g_settings;

  settings_defaults(s);
  s->tab_width = 6;
  s->auto_indent = 0;
  s->syntax_highlight = 0;
  strcpy(s->asm_extension, "s");
  strcpy(s->nasm_path, "/documents/tools/nasm.tns");
  strcpy(s->nasm_args, "--first --second");
  strcpy(s->last_dir, "/documents/start");
  s->build_format = 1;
  s->error_list = 0;

  s->theme = 2; /* custom, so the colours below are kept as they are */
  s->ui_bg = 1;
  s->ui_fg = 2;
  s->ui_border_light = 3;
  s->ui_border_dark = 4;
  s->ui_title_bg = 5;
  s->ui_title_fg = 6;
  s->ui_accent = 7;
  s->ui_accent_text = 8;
  s->ui_item_bg = 9;
  s->syn.mnem = 10;
  s->syn.reg = 11;
  s->syn.imm = 12;
  s->syn.label = 13;
  s->syn.comment = 14;
  s->syn.directive = 15;
  s->syn.string = 16;
  s->syn.normal = 17;
  s->syn.bracket1 = 18;
  s->syn.bracket2 = 19;
  s->syn.bracket3 = 20;

  settings_save();
}

static void dump_settings(void) {
  const NStudioSettings *s = &g_settings;

  settings_load();
  printf("tab_width=%d\nauto_indent=%d\nsyntax_highlight=%d\n", s->tab_width,
         s->auto_indent, s->syntax_highlight);
  printf("asm_extension=%s\nnasm_path=%s\nnasm_args=%s\nlast_dir=%s\n",
         s->asm_extension, s->nasm_path, s->nasm_args, s->last_dir);
  printf("build_format=%d\nerror_list=%d\n", s->build_format, s->error_list);
  printf("theme=%d\n", s->theme);
  printf("ui=%d %d %d %d %d %d %d %d %d\n", s->ui_bg, s->ui_fg,
         s->ui_border_light, s->ui_border_dark, s->ui_title_bg, s->ui_title_fg,
         s->ui_accent, s->ui_accent_text, s->ui_item_bg);
  printf("syn=%d %d %d %d %d %d %d %d\n", s->syn.mnem, s->syn.reg, s->syn.imm,
         s->syn.label, s->syn.comment, s->syn.directive, s->syn.string,
         s->syn.normal);
  printf("brackets=%d %d %d\n", s->syn.bracket1, s->syn.bracket2,
         s->syn.bracket3);
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "write") == 0) {
    write_settings();
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "dump") == 0) {
    dump_settings();
    return 0;
  }
  fprintf(stderr, "usage: %s write|dump\n", argv[0]);
  return 2;
}
