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
  const char *path = filebrowser_select();
  if (!path) {
    return;
  }
  editor_open(path);
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
      settings_ui_open();
      break;
    }

    gfx_init();
  }

  gfx_deinit();
  return 0;
}
