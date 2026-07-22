/*
 * browser.c
 * File / directory browser window for TI-Nspire CX / CX II.
 *
 * The parent ("..") row is synthetic: it is drawn pinned above the
 * entry list and works even when the directory itself could not be
 * read, so the user can always navigate back out.
 */

#include <ctype.h>
#include <dirent.h>
#include <libndls.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "browser.h"
#include "gfx.h"
#include "settings.h"

#define BR_MAX_ENTRIES 512
#define BR_NAME_MAX 256
#define BR_ROW_H 10
#define BR_ROWS_VIS 18
#define BR_WIN_W (GFX_W - 20)
#define BR_WIN_X 10
#define BR_WIN_Y 4
#define BR_TITLE_H 12
#define BR_HINT_H 11
#define BR_LIST_H (BR_ROWS_VIS * BR_ROW_H)
#define BR_WIN_H (BR_TITLE_H + BR_LIST_H + BR_HINT_H + 4)
#define BR_LIST_Y (BR_WIN_Y + 1 + BR_TITLE_H)

typedef struct {
  char name[BR_NAME_MAX];
  int is_dir;
} BrEntry;

static BrEntry br_entries[BR_MAX_ENTRIES];
static int br_nentries;

static int endswith_ci(const char *s, const char *suffix) {
  size_t sl = strlen(s), pl = strlen(suffix);
  if (pl > sl)
    return 0;
  s += sl - pl;
  while (*suffix) {
    if (tolower((unsigned char)*s) != tolower((unsigned char)*suffix))
      return 0;
    s++;
    suffix++;
  }
  return 1;
}

/* An asm source name matches ".<ext>.tns" or bare ".<ext>"
   (the nasm assembler accepts both). */
static int br_is_asm_name(const char *name) {
  char suf[48];
  snprintf(suf, sizeof(suf), ".%s.tns", g_settings.asm_extension);
  if (endswith_ci(name, suf))
    return 1;
  snprintf(suf, sizeof(suf), ".%s", g_settings.asm_extension);
  return endswith_ci(name, suf);
}

/* Directories first, then case-insensitive name order. */
static int br_cmp(const void *a, const void *b) {
  const BrEntry *ea = (const BrEntry *)a;
  const BrEntry *eb = (const BrEntry *)b;
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

/* Returns 1 if the directory could be read (list may still be empty). */
static int br_load_dir(const char *path, int pick_dir, int filter_asm) {
  br_nentries = 0;
  DIR *d = opendir(path);
  if (!d)
    return 0;

  struct dirent *de;
  while ((de = readdir(d)) != NULL && br_nentries < BR_MAX_ENTRIES) {
    if (de->d_name[0] == '.')
      continue;

    char full[768];
    snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
    struct stat st;
    int is_dir = 0;
    if (stat(full, &st) == 0)
      is_dir = S_ISDIR(st.st_mode);

    if (pick_dir && !is_dir)
      continue;
    if (!is_dir && !pick_dir && filter_asm && !br_is_asm_name(de->d_name))
      continue;

    BrEntry *e = &br_entries[br_nentries];
    strncpy(e->name, de->d_name, BR_NAME_MAX - 1);
    e->name[BR_NAME_MAX - 1] = '\0';
    e->is_dir = is_dir;
    br_nentries++;
  }
  closedir(d);

  qsort(br_entries, br_nentries, sizeof(BrEntry), br_cmp);
  return 1;
}

static void br_draw(const char *cwd, int sel, int scroll, int pick_dir,
                    int has_parent, int filter_asm) {
  uint16_t BG = g_default_theme.bg;
  uint16_t FG = g_default_theme.fg;
  uint16_t SEL_BG = g_default_theme.accent;
  uint16_t SEL_FG = g_default_theme.accent_text;
  uint16_t DIR_FG = g_default_theme.accent;
  uint16_t DIM_FG = g_default_theme.border_light;
  uint16_t BD = g_default_theme.border_light;

  gfx_fillrect(BR_WIN_X + 3, BR_WIN_Y + 3, BR_WIN_W, BR_WIN_H,
               g_default_theme.border_dark);
  gfx_borderrect(BR_WIN_X, BR_WIN_Y, BR_WIN_W, BR_WIN_H, BG, BD);

  gfx_fillrect(BR_WIN_X + 1, BR_WIN_Y + 1, BR_WIN_W - 2, BR_TITLE_H,
               g_default_theme.title_bg);

  /* Leading extra '/' marks a root folder (calculator convention). */
  char disp_path[512];
  const char *clean_cwd = cwd;
  while (clean_cwd[0] == '/' && clean_cwd[1] == '/')
    clean_cwd++;
  snprintf(disp_path, sizeof(disp_path), "/%s", clean_cwd);

  gfx_drawstr_clipped(
      BR_WIN_X + 4, BR_WIN_Y + 1 + (BR_TITLE_H - GFX_FONT_H) / 2, disp_path,
      g_default_theme.title_fg, g_default_theme.title_bg, BR_WIN_W - 8);

  gfx_fillrect(BR_WIN_X + 1, BR_LIST_Y, BR_WIN_W - 2, BR_LIST_H, BG);

  int base = 0;
  if (has_parent) {
    int ry = BR_LIST_Y;
    int is_sel = (sel == -1);
    uint16_t rbg = is_sel ? SEL_BG : BG;
    uint16_t rfg = is_sel ? SEL_FG : DIR_FG;
    gfx_fillrect(BR_WIN_X + 1, ry, BR_WIN_W - 2, BR_ROW_H, rbg);
    gfx_drawstr_clipped(BR_WIN_X + 4, ry + 1, "../  (parent)", rfg, rbg,
                        BR_WIN_W - 8);
    base = 1;
  }

  for (int vi = 0; vi < BR_ROWS_VIS - base; vi++) {
    int ri = scroll + vi;
    int ry = BR_LIST_Y + (vi + base) * BR_ROW_H;
    if (ri >= br_nentries) {
      gfx_fillrect(BR_WIN_X + 1, ry, BR_WIN_W - 2, BR_ROW_H, BG);
      continue;
    }
    int is_sel = (sel == ri);
    uint16_t rbg = is_sel ? SEL_BG : BG;
    uint16_t rfg = is_sel ? SEL_FG : (br_entries[ri].is_dir ? DIR_FG : FG);

    gfx_fillrect(BR_WIN_X + 1, ry, BR_WIN_W - 2, BR_ROW_H, rbg);
    gfx_drawstr_clipped(BR_WIN_X + 4, ry + 1, br_entries[ri].name, rfg, rbg,
                        BR_WIN_W - 8);
  }

  if (br_nentries > BR_ROWS_VIS - base) {
    int bt = BR_LIST_H - base * BR_ROW_H;
    int vis = BR_ROWS_VIS - base;
    int bh = bt * vis / br_nentries;
    if (bh < 4)
      bh = 4;
    int ms = br_nentries - vis;
    int by =
        BR_LIST_Y + base * BR_ROW_H + (bt - bh) * scroll / (ms > 0 ? ms : 1);
    gfx_fillrect(BR_WIN_X + BR_WIN_W - 5, BR_LIST_Y + base * BR_ROW_H, 3, bt,
                 g_default_theme.item_bg);
    gfx_fillrect(BR_WIN_X + BR_WIN_W - 5, by, 3, bh, BD);
  }

  int hy = BR_WIN_Y + BR_WIN_H - BR_HINT_H - 1;
  gfx_hline(BR_WIN_X + 1, hy, BR_WIN_W - 2, BD);
  gfx_fillrect(BR_WIN_X + 1, hy + 1, BR_WIN_W - 2, BR_HINT_H - 1, BG);

  char hint_buf[128];
  const char *hint;
  if (pick_dir) {
    hint = "Tab:save here  Enter:open  Esc:cancel";
  } else if (filter_asm) {
    hint = "Tab:all files  Enter:open  Esc:cancel";
  } else {
    snprintf(hint_buf, sizeof(hint_buf), "Tab:*.%s.tns  Enter:open  Esc:cancel",
             g_settings.asm_extension);
    hint = hint_buf;
  }
  gfx_drawstr_clipped(BR_WIN_X + 4, hy + 2, hint, DIM_FG, BG, BR_WIN_W - 8);

  gfx_flip();
}

static int br_run(const char *start, int pick_dir, int filter_asm, char *out,
                  int outsz) {
  char cwd[512];
  strncpy(cwd, start && start[0] ? start : "/documents", sizeof(cwd) - 1);
  cwd[sizeof(cwd) - 1] = '\0';

  if (!br_load_dir(cwd, pick_dir, filter_asm) &&
      strcmp(cwd, "/documents") != 0) {
    /* Unreadable start directory: fall back to /documents. */
    strncpy(cwd, "/documents", sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';
    br_load_dir(cwd, pick_dir, filter_asm);
  }

  int has_parent = (strcmp(cwd, "/") != 0);
  int vis_rows = BR_ROWS_VIS - (has_parent ? 1 : 0);

  int sel = has_parent ? -1 : 0;
  int scroll = 0;

  while (any_key_pressed())
    msleep(20);
  br_draw(cwd, sel, scroll, pick_dir, has_parent, filter_asm);

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
      return 0;

    } else if (nav == NAV_TAB) {
      if (pick_dir) {
        while (any_key_pressed())
          msleep(20);
        strncpy(out, cwd, outsz - 1);
        out[outsz - 1] = '\0';
        return 1;
      }
      filter_asm = !filter_asm;
      br_load_dir(cwd, pick_dir, filter_asm);
      sel = has_parent ? -1 : 0;
      scroll = 0;

    } else if (nav == NAV_UP) {
      if (sel == 0 && has_parent)
        sel = -1;
      else if (sel > 0)
        sel--;
      if (sel >= 0 && sel < scroll)
        scroll = sel;

    } else if (nav == NAV_DOWN) {
      if (sel == -1) {
        if (br_nentries > 0)
          sel = 0;
      } else if (sel < br_nentries - 1) {
        sel++;
      }
      if (sel >= scroll + vis_rows)
        scroll = sel - vis_rows + 1;

    } else if (nav == NAV_ENTER) {
      while (any_key_pressed())
        msleep(20);

      if (sel == -1) {
        char *slash = strrchr(cwd, '/');
        if (slash && slash != cwd)
          *slash = '\0';
        else
          strcpy(cwd, "/");
        br_load_dir(cwd, pick_dir, filter_asm);
        has_parent = (strcmp(cwd, "/") != 0);
        vis_rows = BR_ROWS_VIS - (has_parent ? 1 : 0);
        sel = has_parent ? -1 : 0;
        scroll = 0;
        br_draw(cwd, sel, scroll, pick_dir, has_parent, filter_asm);
        continue;
      }

      if (sel < 0 || sel >= br_nentries)
        continue;

      if (br_entries[sel].is_dir) {
        if (strcmp(cwd, "/") == 0)
          snprintf(cwd, sizeof(cwd), "/%s", br_entries[sel].name);
        else
          snprintf(cwd + strlen(cwd), sizeof(cwd) - strlen(cwd), "/%s",
                   br_entries[sel].name);
        br_load_dir(cwd, pick_dir, filter_asm);
        has_parent = (strcmp(cwd, "/") != 0);
        vis_rows = BR_ROWS_VIS - (has_parent ? 1 : 0);
        sel = has_parent ? -1 : 0;
        scroll = 0;

      } else if (!pick_dir) {
        if (strcmp(cwd, "/") == 0)
          snprintf(out, outsz, "/%s", br_entries[sel].name);
        else
          snprintf(out, outsz, "%s/%s", cwd, br_entries[sel].name);
        return 1;
      }
    }
    br_draw(cwd, sel, scroll, pick_dir, has_parent, filter_asm);
  }
}

int browser_pick_file(const char *start_dir, int filter_asm, char *out,
                      int outsz) {
  return br_run(start_dir, 0, filter_asm, out, outsz);
}

int browser_pick_dir(const char *start_dir, char *out, int outsz) {
  return br_run(start_dir, 1, 0, out, outsz);
}
