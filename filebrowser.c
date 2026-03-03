/*
 * filebrowser.c
 * Filesystem browser for TI-Nspire CX / CX II (Ndless)
 */

#include <ctype.h>
#include <dirent.h>
#include <keys.h>
#include <libndls.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "filebrowser.h"
#include "gfx.h"
#include "settings.h"

/* ------------------------------------------------------------------ */
/* Colours used by the browser (Themed dynamically via unified UI)    */
/* ------------------------------------------------------------------ */
#define COL_BG g_default_theme.bg
#define COL_FG g_default_theme.fg
#define COL_HDR g_default_theme.title_bg
#define COL_HDR_FG g_default_theme.title_fg
#define COL_SEL_BG g_default_theme.accent
#define COL_SEL_FG g_default_theme.accent_text
#define COL_DIR_FG g_default_theme.accent
#define COL_HINT_FG g_default_theme.border_light
#define COL_BORDER g_default_theme.border_light

/* ------------------------------------------------------------------ */
/*  Layout                                                            */
/* ------------------------------------------------------------------ */
#define HEADER_H 14
#define HINT_H 10
#define ROW_H 11
#define LIST_Y (HEADER_H + 2)
#define LIST_H (GFX_H - HEADER_H - HINT_H - 4)
#define ROWS_VISIBLE (LIST_H / ROW_H)
#define LIST_X 2
#define LIST_W (GFX_W - 4)

/* ------------------------------------------------------------------ */
/*  Directory entry list                                              */
/* ------------------------------------------------------------------ */
#define MAX_ENTRIES 512
#define MAX_PATH 512
#define MAX_NAME 256

typedef struct {
  char name[MAX_NAME];
  char fullpath[MAX_PATH];
  int is_dir;
} Entry;

static Entry entries[MAX_ENTRIES];
static int num_entries = 0;

static int str_endswith_ci(const char *s, const char *suffix) {
  size_t sl = strlen(s), pl = strlen(suffix);
  if (pl > sl)
    return 0;
  const char *p = s + sl - pl;
  while (*p && *suffix) {
    if (tolower((unsigned char)*p) != tolower((unsigned char)*suffix))
      return 0;
    p++;
    suffix++;
  }
  return 1;
}

static int entry_cmp(const void *a, const void *b) {
  const Entry *ea = (const Entry *)a;
  const Entry *eb = (const Entry *)b;
  if (ea->is_dir != eb->is_dir)
    return eb->is_dir - ea->is_dir;
  const char *na = ea->name, *nb = eb->name;
  while (*na && *nb) {
    int d = tolower((unsigned char)*na) - tolower((unsigned char)*nb);
    if (d)
      return d;
    na++;
    nb++;
  }
  return tolower((unsigned char)*na) - tolower((unsigned char)*nb);
}

/* Build the suffix we filter on: ".<ext>.tns", e.g. ".asm.tns" */
static void make_filter_suffix(char *buf, int bufsz) {
  snprintf(buf, bufsz, ".%s.tns", g_settings.asm_extension);
}

static void load_dir(const char *path, int filter_asm) {
  DIR *d = opendir(path);
  num_entries = 0;
  if (!d)
    return;

  if (strcmp(path, "/") != 0) {
    strncpy(entries[0].name, "../  (parent)", MAX_NAME - 1);
    entries[0].name[MAX_NAME - 1] = '\0';
    strncpy(entries[0].fullpath, path, MAX_PATH - 1);
    entries[0].fullpath[MAX_PATH - 1] = '\0';

    char *slash = strrchr(entries[0].fullpath, '/');
    if (slash && slash != entries[0].fullpath)
      *slash = '\0';
    else
      strncpy(entries[0].fullpath, "/", MAX_PATH - 1);

    entries[0].is_dir = 1;
    num_entries = 1;
  }

  char filter_suffix[48];
  make_filter_suffix(filter_suffix, sizeof(filter_suffix));

  struct dirent *de;
  while ((de = readdir(d)) != NULL && num_entries < MAX_ENTRIES) {
    if (de->d_name[0] == '.')
      continue;

    char fullpath[MAX_PATH];
    snprintf(fullpath, MAX_PATH, "%s/%s", path, de->d_name);

    struct stat st;
    int is_dir = 0;
    if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode))
      is_dir = 1;

    if (!is_dir && filter_asm && !str_endswith_ci(de->d_name, filter_suffix))
      continue;

    strncpy(entries[num_entries].name, de->d_name, MAX_NAME - 1);
    entries[num_entries].name[MAX_NAME - 1] = '\0';
    strncpy(entries[num_entries].fullpath, fullpath, MAX_PATH - 1);
    entries[num_entries].fullpath[MAX_PATH - 1] = '\0';
    entries[num_entries].is_dir = is_dir;
    num_entries++;
  }
  closedir(d);

  if (num_entries > 0)
    qsort(entries, num_entries, sizeof(Entry), entry_cmp);
}

/* ------------------------------------------------------------------ */
/*  Rendering                                                         */
/* ------------------------------------------------------------------ */
static void render(const char *path, int cursor, int scroll, int filter_asm) {
  gfx_fillrect(0, 0, GFX_W, GFX_H, COL_BG);
  gfx_fillrect(0, 0, GFX_W, HEADER_H, COL_HDR);

  char disp_path[MAX_PATH + 2];
  const char *clean_path = path;
  while (clean_path[0] == '/' && clean_path[1] == '/')
    clean_path++;
  snprintf(disp_path, sizeof(disp_path), "/%s", clean_path);

  gfx_drawstr_clipped(4, (HEADER_H - GFX_FONT_H) / 2, disp_path, COL_HDR_FG,
                      COL_HDR, GFX_W - 8);

  for (int i = 0; i < ROWS_VISIBLE; i++) {
    int idx = scroll + i;
    if (idx >= num_entries)
      break;

    int y = LIST_Y + i * ROW_H;
    int is_sel = (idx == cursor);

    uint16_t bg = is_sel ? COL_SEL_BG : COL_BG;
    uint16_t fg =
        is_sel ? COL_SEL_FG : (entries[idx].is_dir ? COL_DIR_FG : COL_FG);

    gfx_fillrect(0, y, GFX_W, ROW_H, bg);
    gfx_drawstr_clipped(8, y + 1, entries[idx].name, fg, bg, GFX_W - 12);
  }

  if (num_entries > ROWS_VISIBLE) {
    int bt = LIST_H;
    int vis = ROWS_VISIBLE;
    int bh = bt * vis / num_entries;
    if (bh < 4)
      bh = 4;
    int ms = num_entries - vis;
    int by = LIST_Y + (bt - bh) * scroll / (ms > 0 ? ms : 1);
    gfx_fillrect(GFX_W - 4, LIST_Y, 3, bt, g_default_theme.item_bg);
    gfx_fillrect(GFX_W - 4, by, 3, bh, COL_BORDER);
  }

  gfx_fillrect(0, GFX_H - HINT_H, GFX_W, HINT_H, COL_BG);
  gfx_hline(0, GFX_H - HINT_H - 1, GFX_W, COL_BORDER);

  char hint_buf[128];
  const char *hint;
  if (filter_asm) {
    hint = "Tab: show all files";
  } else {
    snprintf(hint_buf, sizeof(hint_buf), "Tab: show *.%s.tns",
             g_settings.asm_extension);
    hint = hint_buf;
  }

  gfx_drawstr_clipped(4, GFX_H - HINT_H + 1, hint, COL_HINT_FG, COL_BG,
                      GFX_W - 8);

  gfx_flip();
}

/* ------------------------------------------------------------------ */
/*  Key helpers                                                       */
/* ------------------------------------------------------------------ */
static void wait_key_release_all(void) {
  while (any_key_pressed()) {
    msleep(20);
  }
}

static void wait_debounce(t_key key) {
  while (isKeyPressed(key)) {
    msleep(15);
  }
  msleep(30);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */
static char result_path[MAX_PATH];

const char *filebrowser_select(void) {
  char cur_path[MAX_PATH];
  strncpy(cur_path, "/documents", MAX_PATH - 1);
  cur_path[MAX_PATH - 1] = '\0';
  int cursor = 0;
  int scroll = 0;
  int filter_asm = 1;

  load_dir(cur_path, filter_asm);
  render(cur_path, cursor, scroll, filter_asm);

  while (any_key_pressed())
    msleep(20);

  for (;;) {
    NavAction nav = gfx_poll_nav();
    if (nav == NAV_NONE) {
      msleep(16);
      idle();
      continue;
    }

    if (nav == NAV_ESC) {
      while (any_key_pressed())
        msleep(20);
      return NULL;
    } else if (nav == NAV_UP) {
      if (cursor > 0) {
        cursor--;
        if (cursor < scroll)
          scroll = cursor;
      }
    } else if (nav == NAV_DOWN) {
      if (cursor < num_entries - 1) {
        cursor++;
        if (cursor >= scroll + ROWS_VISIBLE)
          scroll = cursor - ROWS_VISIBLE + 1;
      }
    } else if (nav == NAV_TAB) {
      filter_asm = !filter_asm;
      cursor = 0;
      scroll = 0;
      load_dir(cur_path, filter_asm);
    } else if (nav == NAV_ENTER) {
      if (num_entries > 0) {
        if (entries[cursor].is_dir) {
          if (strcmp(entries[cursor].name, "../  (parent)") == 0) {
            char *slash = strrchr(cur_path, '/');
            if (slash && slash != cur_path)
              *slash = '\0';
            else
              strncpy(cur_path, "/", MAX_PATH - 1);
          } else {
            if (strcmp(cur_path, "/") == 0) {
              snprintf(cur_path, MAX_PATH, "/%s", entries[cursor].name);
            } else {
              snprintf(cur_path + strlen(cur_path), MAX_PATH - strlen(cur_path),
                       "/%s", entries[cursor].name);
            }
          }
          cursor = 0;
          scroll = 0;
          load_dir(cur_path, filter_asm);
        } else {
          strncpy(result_path, entries[cursor].fullpath, MAX_PATH - 1);
          result_path[MAX_PATH - 1] = '\0';
          while (any_key_pressed())
            msleep(20);
          return result_path;
        }
      }
    }
    render(cur_path, cursor, scroll, filter_asm);
  }
}
