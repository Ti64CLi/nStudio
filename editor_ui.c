/*
 * editor_ui.c
 * Modal reference pickers for the editor: the special-character map, the ARM
 * instruction catalog and the Ndless syscall catalog.
 *
 * Extracted verbatim from editor.c.  Each picker takes over the screen, runs
 * its own key loop and returns what the user chose.  They read only the static
 * reference tables in asmdb.c and draw through gfx.c; none of them touches the
 * text buffer, the cursor or any other editor state, which is what lets them
 * live here.  Acting on the result - inserting it, and any read-only
 * confirmation that implies - remains the caller's job.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <keys.h>
#include <libndls.h>

#include "asmdb.h"
#include "editor_ui.h"
#include "gfx.h"

/* ================================================================
 * Character map picker
 * Ctrl+Cat opens a modal grid of special/ASCII characters.
 * Returns the selected char (>0), or 0 if cancelled.
 * ================================================================ */

static const char charmap_chars[] = "()[]{}<>"
                                    "+-*/\\%^&|~!"
                                    "=<>!?"
                                    ".,;:@#$"
                                    "\"'`"
                                    "_"
                                    "0123456789ABCDEF"
                                    "#@!^";

#define CM_CELL_W (GFX_CHAR_W + 4)
#define CM_CELL_H (GFX_FONT_H + 4)
#define CM_COLS 16
#define CM_WIN_PAD 6
#define CM_TITLE_H 12
#define CM_BTN_H 12

char charmap_pick(void) {
  static char chars[128];
  static int nchars = 0;
  if (nchars == 0) { /* build once */
    for (int i = 0; charmap_chars[i] && nchars < 127; i++) {
      char c = charmap_chars[i];
      int dup = 0;
      for (int j = 0; j < nchars; j++)
        if (chars[j] == c) {
          dup = 1;
          break;
        }
      if (!dup)
        chars[nchars++] = c;
    }
  }

  int nrows = (nchars + CM_COLS - 1) / CM_COLS;
  int grid_w = CM_COLS * CM_CELL_W + CM_WIN_PAD * 2;
  int grid_h = nrows * CM_CELL_H + CM_WIN_PAD * 2;
  int win_w = grid_w + 2;
  int win_h = CM_TITLE_H + grid_h + CM_BTN_H + 2;

  if (win_w > GFX_W - 4)
    win_w = GFX_W - 4;
  if (win_h > GFX_H - 4)
    win_h = GFX_H - 4;

  int wx = (GFX_W - win_w) / 2;
  int wy = (GFX_H - win_h) / 2;
  int gx = wx + 1 + CM_WIN_PAD;
  int gy = wy + 1 + CM_TITLE_H + CM_WIN_PAD;
  int sel = 0;

  while (any_key_pressed())
    msleep(20);

  int redraw = 1;
  for (;;) {
    if (redraw) {
      gfx_panel(wx, wy, win_w, win_h, CM_TITLE_H);
      gfx_drawstr_clipped(wx + 4, wy + 1 + (CM_TITLE_H - GFX_FONT_H) / 2,
                          "Special characters", g_default_theme.title_fg,
                          g_default_theme.title_bg, win_w - 8);
      gfx_fillrect(wx + 1, wy + 1 + CM_TITLE_H, win_w - 2,
                   win_h - CM_TITLE_H - CM_BTN_H - 1, g_default_theme.bg);
      int hy = wy + win_h - CM_BTN_H - 1;
      gfx_hline(wx + 1, hy, win_w - 2, g_default_theme.border_light);
      gfx_fillrect(wx + 1, hy + 1, win_w - 2, CM_BTN_H - 1, g_default_theme.bg);
      gfx_drawstr_clipped(wx + 4, hy + 2,
                          "Arrows:move  Enter:insert  Esc:cancel",
                          g_default_theme.fg, g_default_theme.bg, win_w - 8);

      for (int i = 0; i < nchars; i++) {
        int row = i / CM_COLS;
        int col = i % CM_COLS;
        int cx = gx + col * CM_CELL_W;
        int cy = gy + row * CM_CELL_H;
        int is_sel = (i == sel);
        uint16_t fg = is_sel ? g_default_theme.accent_text : g_default_theme.fg;
        uint16_t cbg =
            is_sel ? g_default_theme.accent : g_default_theme.item_bg;
        gfx_fillrect(cx, cy, CM_CELL_W - 1, CM_CELL_H - 1, cbg);
        int char_x = cx + (CM_CELL_W - 1 - GFX_CHAR_W) / 2;
        int char_y = cy + (CM_CELL_H - 1 - GFX_FONT_H) / 2;
        gfx_drawchar(char_x, char_y, chars[i], fg, cbg);
      }

      {
        char preview[4] = {'\'', chars[sel], '\'', '\0'};
        int px = wx + win_w - 4 - (int)strlen(preview) * GFX_CHAR_W;
        gfx_drawstr(px, wy + 1 + (CM_TITLE_H - GFX_FONT_H) / 2, preview,
                    g_default_theme.accent, g_default_theme.title_bg);
      }

      gfx_flip();
      redraw = 0;
    }

    NavAction nav = gfx_poll_nav();
    if (nav == NAV_NONE) {
      msleep(16);
      idle();
      continue;
    }

    if (nav == NAV_UP) {
      if (sel >= CM_COLS)
        sel -= CM_COLS;
      redraw = 1;
    } else if (nav == NAV_DOWN) {
      if (sel + CM_COLS < nchars)
        sel += CM_COLS;
      else
        sel = nchars - 1;
      redraw = 1;
    } else if (nav == NAV_LEFT) {
      if (sel > 0)
        sel--;
      redraw = 1;
    } else if (nav == NAV_RIGHT) {
      if (sel < nchars - 1)
        sel++;
      redraw = 1;
    } else if (nav == NAV_ENTER) {
      while (any_key_pressed())
        msleep(20);
      return chars[sel];
    } else if (nav == NAV_ESC) {
      while (any_key_pressed())
        msleep(20);
      return 0;
    }
  }
}

/* ================================================================
 * ARM Mnemonic Catalog
 * Opened by KEY_NSPIRE_CAT (alone) or from the editor menu.
 * Shows categorized mnemonics in a scrollable list with collapsible
 * category headers.  Selecting a mnemonic inserts it at cursor.
 * Returns the selected mnemonic string, or NULL if cancelled.
 * ================================================================ */


#define CAT_MAX_ROWS 256

typedef struct {
  int is_cat;
  int cat_idx;
  int mnem_idx;
} CatRow;

static CatRow cat_rows[CAT_MAX_ROWS];
static int cat_nrows;

static void catalog_build_rows(void) {
  cat_nrows = 0;
  for (int c = 0; c < NCATS && cat_nrows < CAT_MAX_ROWS; c++) {
    cat_rows[cat_nrows++] = (CatRow){1, c, 0};
    if (g_cats[c].expanded) {
      for (int m = 0; m < g_cats[c].count && cat_nrows < CAT_MAX_ROWS; m++)
        cat_rows[cat_nrows++] = (CatRow){0, c, m};
    }
  }
}

#define CAT_WIN_X 10
#define CAT_WIN_Y 6
#define CAT_WIN_W (GFX_W - 20)
#define CAT_WIN_H (GFX_H - 12)
#define CAT_TITLE_H 12
#define CAT_HINT_H 11
#define CAT_ROW_H 9
#define CAT_LIST_Y (CAT_WIN_Y + 1 + CAT_TITLE_H)
#define CAT_LIST_H (CAT_WIN_H - CAT_TITLE_H - CAT_HINT_H - 2)
#define CAT_ROWS_VIS (CAT_LIST_H / CAT_ROW_H)
#define CAT_INDENT 10
/* Column where the args string starts (in pixels from window left interior) */
#define CAT_ARGS_X (CAT_WIN_X + 1 + CAT_INDENT + 7 * GFX_CHAR_W)

/* Draw the mnemonic description popup over the catalog.
   Wraps the description text across multiple lines.           */
static void catalog_show_desc(const MnemInfo *mi) {
  const int PW = 220, TITLE_H = 12, PAD = 5, BTN_H = 12;
  int line_w = (PW - 2 * PAD) / GFX_CHAR_W;
  if (line_w < 1)
    line_w = 1;

  const char *desc = mi->desc;
  int dlen = (int)strlen(desc);
  char lines[6][64];
  int nlines = 0;
  int pos = 0;
  while (pos < dlen && nlines < 6) {
    int take = dlen - pos;
    if (take > line_w) {
      take = line_w;
      while (take > 1 && desc[pos + take - 1] != ' ')
        take--;
      if (take <= 1)
        take = line_w;
    }
    int copy = take < 63 ? take : 63;
    strncpy(lines[nlines], desc + pos, copy);
    lines[nlines][copy] = '\0';
    int tl = (int)strlen(lines[nlines]);
    while (tl > 0 && lines[nlines][tl - 1] == ' ')
      lines[nlines][--tl] = '\0';
    nlines++;
    pos += take;
    while (pos < dlen && desc[pos] == ' ')
      pos++;
  }

  int body_lines = nlines + (mi->args[0] ? 2 : 0); /* args + blank + desc */
  int body_h = body_lines * GFX_FONT_H + 2 * PAD;
  int win_h = TITLE_H + body_h + BTN_H + 2;
  int win_w = PW;
  int wx = (GFX_W - win_w) / 2;
  int wy = (GFX_H - win_h) / 2;

  gfx_panel(wx, wy, win_w, win_h, TITLE_H);

  char title[32];
  snprintf(title, sizeof(title), "%s", mi->name);
  gfx_drawstr_clipped(wx + PAD, wy + 1 + (TITLE_H - GFX_FONT_H) / 2, title,
                      g_default_theme.title_fg, g_default_theme.title_bg,
                      win_w - 2 * PAD);

  gfx_fillrect(wx + 1, wy + 1 + TITLE_H, win_w - 2, body_h, g_default_theme.bg);

  int ty = wy + 1 + TITLE_H + PAD;

  if (mi->args[0]) {
    char arg_line[80];
    snprintf(arg_line, sizeof(arg_line), "  %s", mi->args);
    gfx_drawstr_clipped(wx + PAD, ty, arg_line, g_default_theme.accent,
                        g_default_theme.bg, win_w - 2 * PAD);
    ty += GFX_FONT_H + 2; /* small gap */
  }

  for (int i = 0; i < nlines; i++) {
    gfx_drawstr_clipped(wx + PAD, ty, lines[i], g_default_theme.fg,
                        g_default_theme.bg, win_w - 2 * PAD);
    ty += GFX_FONT_H;
  }

  int hy = wy + win_h - BTN_H - 1;
  gfx_hline(wx + 1, hy, win_w - 2, g_default_theme.border_light);
  gfx_fillrect(wx + 1, hy + 1, win_w - 2, BTN_H - 1, g_default_theme.bg);
  gfx_drawstr_clipped(wx + PAD, hy + 2, "Any key: close", g_default_theme.fg,
                      g_default_theme.bg, win_w - 2 * PAD);

  gfx_flip();

  while (!any_key_pressed()) {
    msleep(16);
    idle();
  }
  while (any_key_pressed())
    msleep(20);
}

static void catalog_draw(int sel, int scroll) {
  uint16_t WIN_BG = g_default_theme.bg;
  uint16_t WIN_FG = g_default_theme.fg;
  uint16_t TITLE_BG = g_default_theme.title_bg;
  uint16_t TITLE_FG = g_default_theme.title_fg;
  uint16_t SEL_BG = g_default_theme.accent;
  uint16_t SEL_FG = g_default_theme.accent_text;
  uint16_t CAT_FG = g_default_theme.accent;
  uint16_t CAT_BG = g_default_theme.item_bg;
  uint16_t BORDER = g_default_theme.border_light;
  uint16_t ARGS_FG = g_default_theme.accent; /* args colour when not selected */

  gfx_panel(CAT_WIN_X, CAT_WIN_Y, CAT_WIN_W, CAT_WIN_H, CAT_TITLE_H);
  gfx_drawstr_clipped(
      CAT_WIN_X + 4, CAT_WIN_Y + 1 + (CAT_TITLE_H - GFX_FONT_H) / 2,
      "ARM Instruction Catalog", TITLE_FG, TITLE_BG, CAT_WIN_W - 8);

  for (int vi = 0; vi < CAT_ROWS_VIS; vi++) {
    int ri = scroll + vi;
    int row_y = CAT_LIST_Y + vi * CAT_ROW_H;

    if (ri >= cat_nrows) {
      gfx_fillrect(CAT_WIN_X + 1, row_y, CAT_WIN_W - 2, CAT_ROW_H, WIN_BG);
      continue;
    }

    CatRow *cr = &cat_rows[ri];
    int is_sel = (ri == sel);

    if (cr->is_cat) {
      uint16_t bg = is_sel ? SEL_BG : CAT_BG;
      uint16_t fg = is_sel ? SEL_FG : CAT_FG;
      gfx_fillrect(CAT_WIN_X + 1, row_y, CAT_WIN_W - 2, CAT_ROW_H, bg);
      char indicator = g_cats[cr->cat_idx].expanded ? '-' : '+';
      gfx_drawchar(CAT_WIN_X + 3, row_y + 1, indicator, fg, bg);
      gfx_drawstr_clipped(CAT_WIN_X + 3 + GFX_CHAR_W + 2, row_y + 1,
                          g_cats[cr->cat_idx].name, fg, bg, CAT_WIN_W - 14);
    } else {
      const MnemInfo *mi = &g_cats[cr->cat_idx].mnems[cr->mnem_idx];
      uint16_t bg = is_sel ? SEL_BG : WIN_BG;
      uint16_t fg = is_sel ? SEL_FG : WIN_FG;
      uint16_t afg = is_sel ? SEL_FG : ARGS_FG;
      gfx_fillrect(CAT_WIN_X + 1, row_y, CAT_WIN_W - 2, CAT_ROW_H, bg);
      gfx_drawstr_clipped(CAT_WIN_X + 1 + CAT_INDENT, row_y + 1, mi->name, fg,
                          bg, CAT_ARGS_X - (CAT_WIN_X + 1 + CAT_INDENT) - 2);
      if (mi->args[0])
        gfx_drawstr_clipped(CAT_ARGS_X, row_y + 1, mi->args, afg, bg,
                            CAT_WIN_X + CAT_WIN_W - 6 - CAT_ARGS_X);
    }
  }

  gfx_scrollbar_v(CAT_WIN_X, CAT_WIN_W, CAT_LIST_Y, CAT_LIST_H, cat_nrows,
                  CAT_ROWS_VIS, scroll);

  int hy = CAT_WIN_Y + CAT_WIN_H - CAT_HINT_H - 1;
  gfx_hline(CAT_WIN_X + 1, hy, CAT_WIN_W - 2, BORDER);
  gfx_fillrect(CAT_WIN_X + 1, hy + 1, CAT_WIN_W - 2, CAT_HINT_H - 1, WIN_BG);
  gfx_drawstr_clipped(CAT_WIN_X + 4, hy + 2,
                      "Enter:insert  Shift:desc  +/-:expand  Esc:close", WIN_FG,
                      WIN_BG, CAT_WIN_W - 8);

  gfx_flip();
}

const char *catalog_pick(void) {
  catalog_init_cats();
  catalog_build_rows();
  int sel = 0, scroll = 0;

  while (any_key_pressed())
    msleep(20);
  catalog_draw(sel, scroll);

  for (;;) {
    NavAction nav = gfx_poll_nav();
    if (nav == NAV_NONE) {
      msleep(16);
      idle();
      continue;
    }

    int shift = isKeyPressed(KEY_NSPIRE_SHIFT);

    if (nav == NAV_ESC || nav == NAV_CAT) {
      while (any_key_pressed())
        msleep(20);
      return NULL;
    } else if (nav == NAV_UP) {
      if (sel > 0) {
        sel--;
        if (sel < scroll)
          scroll = sel;
      }
    } else if (nav == NAV_DOWN) {
      if (sel < cat_nrows - 1) {
        sel++;
        if (sel >= scroll + CAT_ROWS_VIS)
          scroll = sel - CAT_ROWS_VIS + 1;
      }
    } else if (nav == NAV_LEFT) {
      CatRow *cr = &cat_rows[sel];
      if (g_cats[cr->cat_idx].expanded) {
        g_cats[cr->cat_idx].expanded = 0;
        catalog_build_rows();
        for (int i = 0; i < cat_nrows; i++)
          if (cat_rows[i].is_cat && cat_rows[i].cat_idx == cr->cat_idx) {
            sel = i;
            break;
          }
        if (sel < scroll)
          scroll = sel;
      }
    } else if (nav == NAV_RIGHT) {
      CatRow *cr = &cat_rows[sel];
      if (!g_cats[cr->cat_idx].expanded) {
        g_cats[cr->cat_idx].expanded = 1;
        catalog_build_rows();
      }
    } else if (nav == NAV_ENTER) {
      CatRow *cr = &cat_rows[sel];
      if (cr->is_cat) {
        g_cats[cr->cat_idx].expanded = !g_cats[cr->cat_idx].expanded;
        int save_cat = cr->cat_idx;
        catalog_build_rows();
        for (int i = 0; i < cat_nrows; i++)
          if (cat_rows[i].is_cat && cat_rows[i].cat_idx == save_cat) {
            sel = i;
            break;
          }
        if (sel >= cat_nrows)
          sel = cat_nrows - 1;
        if (sel < scroll)
          scroll = sel;
        if (sel >= scroll + CAT_ROWS_VIS)
          scroll = sel - CAT_ROWS_VIS + 1;
      } else {
        if (shift) {
          catalog_show_desc(&g_cats[cr->cat_idx].mnems[cr->mnem_idx]);
        } else {
          return g_cats[cr->cat_idx].mnems[cr->mnem_idx].name;
        }
      }
    }
    catalog_draw(sel, scroll);
  }
}


static void draw_scrolled_text(int x_min, int x_max, int y, const char *str,
                               int *col_idx, int hscroll, uint16_t fg,
                               uint16_t bg) {
  for (int i = 0; str[i]; i++, (*col_idx)++) {
    int px = x_min + (*col_idx - hscroll) * GFX_CHAR_W;
    if (px >= x_min && px + GFX_CHAR_W <= x_max) {
      gfx_drawchar(px, y, str[i], fg, bg);
    }
  }
}

void syscall_show_desc(const SyscallInfo *si) {
  const int WIN_W = GFX_W - 16;
  const int WIN_H = 120;
  const int WIN_X = 8;
  const int WIN_Y = (GFX_H - WIN_H) / 2;
  const int TITLE_H = 12;
  const int BTN_H = 12;
  const int PAD = 5;

  int hscroll = 0;

  char title[128];
  snprintf(title, sizeof(title), "Syscall %d: %s", si->num, si->name);

  while (any_key_pressed())
    msleep(20);

  int redraw = 1;
  for (;;) {
    if (redraw) {
      gfx_panel(WIN_X, WIN_Y, WIN_W, WIN_H, TITLE_H);

      int c = 0;
      draw_scrolled_text(WIN_X + PAD, WIN_X + WIN_W - PAD,
                         WIN_Y + 1 + (TITLE_H - GFX_FONT_H) / 2, title, &c,
                         hscroll, g_default_theme.title_fg,
                         g_default_theme.title_bg);

      gfx_fillrect(WIN_X + 1, WIN_Y + 1 + TITLE_H, WIN_W - 2,
                   WIN_H - TITLE_H - BTN_H - 1, g_default_theme.bg);

      int ty = WIN_Y + 1 + TITLE_H + PAD;

      c = 0;
      draw_scrolled_text(WIN_X + PAD, WIN_X + WIN_W - PAD, ty, "Arguments:", &c,
                         0, g_default_theme.accent, g_default_theme.bg);
      ty += GFX_FONT_H + 2;
      c = 0;
      draw_scrolled_text(WIN_X + PAD + 10, WIN_X + WIN_W - PAD, ty,
                         si->args[0] ? si->args : "(none)", &c, hscroll,
                         g_default_theme.fg, g_default_theme.bg);
      ty += GFX_FONT_H + 10;

      c = 0;
      draw_scrolled_text(WIN_X + PAD, WIN_X + WIN_W - PAD, ty,
                         "Description:", &c, 0, g_default_theme.accent,
                         g_default_theme.bg);
      ty += GFX_FONT_H + 2;
      c = 0;
      draw_scrolled_text(WIN_X + PAD + 10, WIN_X + WIN_W - PAD, ty, si->desc,
                         &c, hscroll, g_default_theme.fg, g_default_theme.bg);

      int hy = WIN_Y + WIN_H - BTN_H - 1;
      gfx_hline(WIN_X + 1, hy, WIN_W - 2, g_default_theme.border_light);
      gfx_fillrect(WIN_X + 1, hy + 1, WIN_W - 2, BTN_H - 1, g_default_theme.bg);

      c = 0;
      draw_scrolled_text(WIN_X + PAD, WIN_X + WIN_W - PAD, hy + 2,
                         "Left/Right: pan   Esc/Enter: close", &c, 0,
                         g_default_theme.fg, g_default_theme.bg);

      gfx_flip();
      redraw = 0;
    }

    NavAction nav = gfx_poll_nav();
    if (nav == NAV_NONE) {
      msleep(16);
      idle();
      continue;
    }

    int ctrl = isKeyPressed(KEY_NSPIRE_CTRL);
    int visible_cols = (WIN_W - 2 * PAD) / GFX_CHAR_W;

    if (nav == NAV_ESC || nav == NAV_ENTER || nav == NAV_CAT) {
      while (any_key_pressed())
        msleep(20);
      break;
    } else if (nav == NAV_LEFT) {
      if (hscroll > 0) {
        hscroll -= ctrl ? visible_cols : 8;
        if (hscroll < 0)
          hscroll = 0;
        redraw = 1;
      }
    } else if (nav == NAV_RIGHT) {
      hscroll += ctrl ? visible_cols : 8;
      redraw = 1;
    }
  }
}

static void syscall_draw(int sel, int scroll, int hscroll, int max_hscroll,
                         int total_cols, int visible_cols) {
  uint16_t WIN_BG = g_default_theme.bg;
  uint16_t WIN_FG = g_default_theme.fg;
  uint16_t TITLE_BG = g_default_theme.title_bg;
  uint16_t TITLE_FG = g_default_theme.title_fg;
  uint16_t SEL_BG = g_default_theme.accent;
  uint16_t SEL_FG = g_default_theme.accent_text;
  uint16_t BORDER = g_default_theme.border_light;
  uint16_t ARGS_FG = g_default_theme.accent;

  gfx_panel(CAT_WIN_X, CAT_WIN_Y, CAT_WIN_W, CAT_WIN_H, CAT_TITLE_H);

  int c = 0;
  draw_scrolled_text(CAT_WIN_X + 4, CAT_WIN_X + CAT_WIN_W - 4,
                     CAT_WIN_Y + 1 + (CAT_TITLE_H - GFX_FONT_H) / 2,
                     "Ndless Syscalls Catalog", &c, 0, TITLE_FG, TITLE_BG);

  /* Draw list background */
  gfx_fillrect(CAT_WIN_X + 1, CAT_LIST_Y, CAT_WIN_W - 2, CAT_LIST_H, WIN_BG);

  for (int vi = 0; vi < CAT_ROWS_VIS; vi++) {
    int ri = scroll + vi;
    int row_y = CAT_LIST_Y + vi * CAT_ROW_H;

    if (ri >= g_nsyscalls)
      continue;

    int is_sel = (ri == sel);
    const SyscallInfo *si = &db_syscalls[ri];

    uint16_t bg = is_sel ? SEL_BG : WIN_BG;
    uint16_t fg = is_sel ? SEL_FG : WIN_FG;
    uint16_t afg = is_sel ? SEL_FG : ARGS_FG;

    gfx_fillrect(CAT_WIN_X + 1, row_y, CAT_WIN_W - 2, CAT_ROW_H, bg);

    int c_col = 0;
    char numbuf[16];
    snprintf(numbuf, sizeof(numbuf), "%-8d", si->num);
    draw_scrolled_text(CAT_WIN_X + 1 + CAT_INDENT, CAT_WIN_X + CAT_WIN_W - 6,
                       row_y + 1, numbuf, &c_col, hscroll, fg, bg);

    char namebuf[48];
    snprintf(namebuf, sizeof(namebuf), "%-36.36s ", si->name);
    draw_scrolled_text(CAT_WIN_X + 1 + CAT_INDENT, CAT_WIN_X + CAT_WIN_W - 6,
                       row_y + 1, namebuf, &c_col, hscroll, fg, bg);

    if (si->args[0]) {
      draw_scrolled_text(CAT_WIN_X + 1 + CAT_INDENT, CAT_WIN_X + CAT_WIN_W - 6,
                         row_y + 1, si->args, &c_col, hscroll, afg, bg);
    }
  }

  gfx_scrollbar_v(CAT_WIN_X, CAT_WIN_W, CAT_LIST_Y, CAT_LIST_H, g_nsyscalls,
                  CAT_ROWS_VIS, scroll);

  if (max_hscroll > 0) {
    int bar_total_w = CAT_WIN_W - 6; /* Leave space for vertical scrollbar */
    int bar_w = bar_total_w * visible_cols / total_cols;
    if (bar_w < 8)
      bar_w = 8;
    int bar_x = CAT_WIN_X + 1 + (bar_total_w - bar_w) * hscroll / max_hscroll;
    int bar_y = CAT_LIST_Y + CAT_LIST_H -
                4; /* Position at the very bottom of the list */

    gfx_fillrect(CAT_WIN_X + 1, bar_y, bar_total_w, 4, g_default_theme.item_bg);
    gfx_fillrect(bar_x, bar_y, bar_w, 4, BORDER);
  }

  int hy = CAT_WIN_Y + CAT_WIN_H - CAT_HINT_H - 1;
  gfx_hline(CAT_WIN_X + 1, hy, CAT_WIN_W - 2, BORDER);
  gfx_fillrect(CAT_WIN_X + 1, hy + 1, CAT_WIN_W - 2, CAT_HINT_H - 1, WIN_BG);

  c = 0;
  draw_scrolled_text(CAT_WIN_X + 4, CAT_WIN_X + CAT_WIN_W - 4, hy + 2,
                     "Enter:ins  Shift:desc  < >:pan  Esc:close", &c, 0, WIN_FG,
                     WIN_BG);

  gfx_flip();
}

const SyscallInfo *syscall_pick(void) {
  int sel = 0, scroll = 0, hscroll = 0;

  int max_arg_len = 0;
  for (int i = 0; i < g_nsyscalls; i++) {
    int len = (int)strlen(db_syscalls[i].args);
    if (len > max_arg_len)
      max_arg_len = len;
  }

  int total_cols = 8 + 37 + max_arg_len;
  int visible_cols = (CAT_WIN_W - 6 - CAT_INDENT - 1) / GFX_CHAR_W;

  int max_hscroll = total_cols - visible_cols;
  if (max_hscroll < 0)
    max_hscroll = 0;

  while (any_key_pressed())
    msleep(20);
  syscall_draw(sel, scroll, hscroll, max_hscroll, total_cols, visible_cols);

  for (;;) {
    NavAction nav = gfx_poll_nav();
    if (nav == NAV_NONE) {
      msleep(16);
      idle();
      continue;
    }

    int shift = isKeyPressed(KEY_NSPIRE_SHIFT);
    int ctrl = isKeyPressed(KEY_NSPIRE_CTRL);

    if (nav == NAV_ESC || (nav == NAV_CAT && shift)) {
      while (any_key_pressed())
        msleep(20);
      return NULL;
    } else if (nav == NAV_UP) {
      if (ctrl) {
        /* Page up */
        sel -= CAT_ROWS_VIS;
        if (sel < 0)
          sel = 0;
        scroll -= CAT_ROWS_VIS;
        if (scroll < 0)
          scroll = 0;
        if (sel < scroll)
          scroll = sel; /* ensure selection remains visible */
      } else {
        if (sel > 0) {
          sel--;
          if (sel < scroll)
            scroll = sel;
        }
      }
    } else if (nav == NAV_DOWN) {
      if (ctrl) {
        /* Page down */
        sel += CAT_ROWS_VIS;
        if (sel > g_nsyscalls - 1)
          sel = g_nsyscalls - 1;
        scroll += CAT_ROWS_VIS;
        if (scroll > g_nsyscalls - CAT_ROWS_VIS)
          scroll = g_nsyscalls - CAT_ROWS_VIS;
        if (scroll < 0)
          scroll = 0;
        if (sel >= scroll + CAT_ROWS_VIS)
          scroll = sel - CAT_ROWS_VIS + 1;
      } else {
        if (sel < g_nsyscalls - 1) {
          sel++;
          if (sel >= scroll + CAT_ROWS_VIS)
            scroll = sel - CAT_ROWS_VIS + 1;
        }
      }
    } else if (nav == NAV_LEFT) {
      if (hscroll > 0) {
        hscroll -= ctrl ? visible_cols : 8;
        if (hscroll < 0)
          hscroll = 0;
      }
    } else if (nav == NAV_RIGHT) {
      if (hscroll < max_hscroll) {
        hscroll += ctrl ? visible_cols : 8;
        if (hscroll > max_hscroll)
          hscroll = max_hscroll;
      }
    } else if (nav == NAV_ENTER) {
      if (shift) {
        syscall_show_desc(&db_syscalls[sel]);
        syscall_draw(sel, scroll, hscroll, max_hscroll, total_cols,
                     visible_cols);
        continue;
      } else {
        while (any_key_pressed())
          msleep(20);
        return &db_syscalls[sel];
      }
    }
    syscall_draw(sel, scroll, hscroll, max_hscroll, total_cols, visible_cols);
  }
}

/* ================================================================
 * Instruction detail popup (cheat sheet)
 * ================================================================ */

int ui_draw_wrapped(const char *text, int wx, int ty, int max_w,
                                   int max_lines, uint16_t fg, uint16_t bg) {
  int line_chars = max_w / GFX_CHAR_W;
  if (line_chars < 1)
    line_chars = 1;
  int pos = 0, nlines = 0;
  int len = (int)strlen(text);
  while (pos < len && nlines < max_lines) {
    int take = len - pos;
    if (take > line_chars) {
      take = line_chars;
      while (take > 1 && text[pos + take - 1] != ' ')
        take--;
      if (take <= 1)
        take = line_chars;
    }
    while (take > 0 && text[pos] == ' ') {
      pos++;
      take--;
    }
    char linebuf[128];
    int copy = take < 127 ? take : 127;
    strncpy(linebuf, text + pos, copy);
    linebuf[copy] = '\0';
    gfx_drawstr_clipped(wx, ty + nlines * GFX_FONT_H, linebuf, fg, bg, max_w);
    pos += take;
    nlines++;
  }
  return nlines;
}

/*
 * Full-width detail popup for one instruction: signature, description and the
 * CPSR flags it touches.  Shown by the cheat sheet for the mnemonic under the
 * cursor; the catalog has its own, narrower detail view (catalog_show_desc).
 */
void mnem_show_desc(const MnemInfo *mi) {
  const int WIN_W = GFX_W - 16;
  const int WIN_X = 8;
  const int WIN_Y = 8;
  const int TITLE_H = 12;
  const int PAD = 5;
  const int HINT_H = 11;
  const int INNER_W = WIN_W - 2 * PAD - 2;
  const int LINE_H = GFX_FONT_H;
  const int SEC_GAP = 3; /* pixels between sections */

  int body_h = PAD + LINE_H + SEC_GAP /* "Syntax:" label */
               + LINE_H + SEC_GAP     /* args value */
               + LINE_H + SEC_GAP     /* "Description:" label */
               + 3 * LINE_H + SEC_GAP /* desc (up to 3 wrapped lines) */
               + LINE_H + SEC_GAP     /* "Flags:" label */
               + 3 * LINE_H           /* flags (up to 3 wrapped lines) */
               + PAD;
  int WIN_H = TITLE_H + body_h + HINT_H + 2;
  if (WIN_H > GFX_H - 8)
    WIN_H = GFX_H - 8;

  gfx_panel(WIN_X, WIN_Y, WIN_W, WIN_H, TITLE_H);
  char title[32];
  snprintf(title, sizeof(title), "%s", mi->name);
  for (int i = 0; title[i]; i++)
    title[i] = toupper((unsigned char)title[i]);
  gfx_drawstr_clipped(WIN_X + PAD, WIN_Y + 1 + (TITLE_H - GFX_FONT_H) / 2,
                      title, g_default_theme.title_fg, g_default_theme.title_bg,
                      WIN_W - 2 * PAD);

  int body_top = WIN_Y + 1 + TITLE_H;
  int body_bot = WIN_Y + WIN_H - HINT_H - 1;
  gfx_fillrect(WIN_X + 1, body_top, WIN_W - 2, body_bot - body_top,
               g_default_theme.bg);

  int tx = WIN_X + 1 + PAD;
  int ty = body_top + PAD;
  uint16_t BG = g_default_theme.bg;
  uint16_t FG = g_default_theme.fg;
  uint16_t LABEL = g_default_theme.accent;

  gfx_drawstr_clipped(tx, ty, "Syntax:", LABEL, BG, INNER_W);
  ty += LINE_H;
  if (mi->args[0]) {
    char argbuf[80];
    snprintf(argbuf, sizeof(argbuf), "%s  %s", mi->name, mi->args);
    int ni = 0;
    while (argbuf[ni] && argbuf[ni] != ' ') {
      argbuf[ni] = toupper((unsigned char)argbuf[ni]);
      ni++;
    }
    gfx_drawstr_clipped(tx, ty, argbuf, FG, BG, INNER_W);
  } else {
    gfx_drawstr_clipped(tx, ty, mi->name, FG, BG, INNER_W);
  }
  ty += LINE_H + SEC_GAP;

  gfx_drawstr_clipped(tx, ty, "Description:", LABEL, BG, INNER_W);
  ty += LINE_H;
  int used = ui_draw_wrapped(mi->desc, tx, ty, INNER_W, 4, FG, BG);
  ty += used * LINE_H + SEC_GAP;

  if (ty + LINE_H < body_bot) {
    gfx_drawstr_clipped(tx, ty, "CPSR Flags:", LABEL, BG, INNER_W);
    ty += LINE_H;
    ui_draw_wrapped(mi->flags, tx, ty, INNER_W, 3, FG, BG);
  }

  int hy = WIN_Y + WIN_H - HINT_H - 1;
  gfx_hline(WIN_X + 1, hy, WIN_W - 2, g_default_theme.border_light);
  gfx_fillrect(WIN_X + 1, hy + 1, WIN_W - 2, HINT_H - 1, BG);
  gfx_drawstr_clipped(WIN_X + PAD, hy + 2, "Any key: close", FG, BG,
                      WIN_W - 2 * PAD);

  gfx_flip();
  while (!any_key_pressed()) {
    msleep(16);
    idle();
  }
  while (any_key_pressed())
    msleep(20);
}

/* ================================================================
 * Label browser
 * ================================================================ */

#define LBL_WIN_X 20
#define LBL_WIN_Y 10
#define LBL_WIN_W (GFX_W - 40)
#define LBL_WIN_H (GFX_H - 20)
#define LBL_TITLE_H 12
#define LBL_HINT_H 11
#define LBL_ROW_H 10
#define LBL_LIST_Y (LBL_WIN_Y + 1 + LBL_TITLE_H)
#define LBL_LIST_H (LBL_WIN_H - LBL_TITLE_H - LBL_HINT_H - 2)
#define LBL_ROWS_VIS (LBL_LIST_H / LBL_ROW_H)

static void labels_draw(const LabelEntry *labels, int n, int sel,
                        int scroll) {
  uint16_t WIN_BG = g_default_theme.bg;
  uint16_t WIN_FG = g_default_theme.fg;
  uint16_t TIT_BG = g_default_theme.title_bg;
  uint16_t TIT_FG = g_default_theme.title_fg;
  uint16_t SEL_BG = g_default_theme.accent;
  uint16_t SEL_FG = g_default_theme.accent_text;
  uint16_t DIM_FG = g_default_theme.border_light;
  uint16_t BORDER = g_default_theme.border_light;

  gfx_panel(LBL_WIN_X, LBL_WIN_Y, LBL_WIN_W, LBL_WIN_H, LBL_TITLE_H);

  char title[48];
  snprintf(title, sizeof(title), "Labels  (%d defined)", n);
  gfx_drawstr_clipped(LBL_WIN_X + 4,
                      LBL_WIN_Y + 1 + (LBL_TITLE_H - GFX_FONT_H) / 2, title,
                      TIT_FG, TIT_BG, LBL_WIN_W - 8);

  if (n == 0) {
    gfx_fillrect(LBL_WIN_X + 1, LBL_LIST_Y, LBL_WIN_W - 2,
                 LBL_WIN_H - LBL_TITLE_H - LBL_HINT_H - 2, WIN_BG);
    gfx_drawstr_clipped(LBL_WIN_X + 8, LBL_LIST_Y + 10,
                        "No labels defined in this file.", DIM_FG, WIN_BG,
                        LBL_WIN_W - 16);
  }

  for (int vi = 0; vi < LBL_ROWS_VIS; vi++) {
    int ri = scroll + vi;
    int row_y = LBL_LIST_Y + vi * LBL_ROW_H;

    if (ri >= n) {
      gfx_fillrect(LBL_WIN_X + 1, row_y, LBL_WIN_W - 2, LBL_ROW_H, WIN_BG);
      continue;
    }

    int is_sel = (ri == sel);
    uint16_t bg = is_sel ? SEL_BG : WIN_BG;
    uint16_t fg = is_sel ? SEL_FG : WIN_FG;
    uint16_t lfg = is_sel ? SEL_FG : DIM_FG;

    gfx_fillrect(LBL_WIN_X + 1, row_y, LBL_WIN_W - 2, LBL_ROW_H, bg);

    gfx_drawstr_clipped(LBL_WIN_X + 4, row_y + 1, labels[ri].name, fg, bg,
                        LBL_WIN_W - 50);

    char lnbuf[16];
    snprintf(lnbuf, sizeof(lnbuf), "Ln %d", labels[ri].line + 1);
    int lnw = (int)strlen(lnbuf) * GFX_CHAR_W;
    gfx_drawstr(LBL_WIN_X + LBL_WIN_W - 6 - lnw, row_y + 1, lnbuf, lfg, bg);
  }

  gfx_scrollbar_v(LBL_WIN_X, LBL_WIN_W, LBL_LIST_Y, LBL_LIST_H, n,
                  LBL_ROWS_VIS, scroll);

  int hy = LBL_WIN_Y + LBL_WIN_H - LBL_HINT_H - 1;
  gfx_hline(LBL_WIN_X + 1, hy, LBL_WIN_W - 2, BORDER);
  gfx_fillrect(LBL_WIN_X + 1, hy + 1, LBL_WIN_W - 2, LBL_HINT_H - 1, WIN_BG);
  gfx_drawstr_clipped(LBL_WIN_X + 4, hy + 2, "Enter:jump  Esc:close", WIN_FG,
                      WIN_BG, LBL_WIN_W - 8);

  gfx_flip();
}

/*
 * Modal label list.  `labels` is the table the editor scanned from the buffer;
 * `cur_line` is the line the cursor sits on, used to pre-select the nearest
 * label.  Returns the index of the chosen label, or -1 when cancelled - moving
 * the cursor is the caller's job.
 */
int label_pick(const LabelEntry *labels, int n, int cur_line) {
  int sel = 0;
  int scroll = 0;

  if (n > 0) {
    int best = 0, bestd = abs(labels[0].line - cur_line);
    for (int i = 1; i < n; i++) {
      int d = abs(labels[i].line - cur_line);
      if (d < bestd) {
        bestd = d;
        best = i;
      }
    }
    sel = best;
    scroll = sel - LBL_ROWS_VIS / 2;
    if (scroll < 0)
      scroll = 0;
    if (scroll > n - LBL_ROWS_VIS && n > LBL_ROWS_VIS)
      scroll = n - LBL_ROWS_VIS;
  }

  while (any_key_pressed())
    msleep(20);
  labels_draw(labels, n, sel, scroll);

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
      return -1;
    } else if (nav == NAV_UP) {
      if (sel > 0) {
        sel--;
        if (sel < scroll)
          scroll = sel;
      }
    } else if (nav == NAV_DOWN) {
      if (sel < n - 1) {
        sel++;
        if (sel >= scroll + LBL_ROWS_VIS)
          scroll = sel - LBL_ROWS_VIS + 1;
      }
    } else if (nav == NAV_ENTER) {
      while (any_key_pressed())
        msleep(20);
      return n > 0 ? sel : -1;
    }
    labels_draw(labels, n, sel, scroll);
  }
}

/* ================================================================
 * Generic list picker
 *
 * A scrollable list of pre-formatted rows that returns the chosen index.
 * Used for anything whose result is "pick one of these places": label
 * references now, the diagnostics panel next.  The callers format their own
 * rows, which is what lets one widget serve lists whose columns differ.
 *
 * Deliberately plain - up, down, Enter, Esc.  The catalog and syscall pickers
 * are not built on this and should not be: their left/right keys mean
 * different things (collapsing a tree, scrolling text sideways), and folding
 * those into one widget would cost more than the duplication saves.
 * ================================================================ */

#define LP_WIN_X 14
#define LP_WIN_Y 8
#define LP_WIN_W (GFX_W - 28)
#define LP_WIN_H (GFX_H - 16)
#define LP_TITLE_H 12
#define LP_HINT_H 11
#define LP_ROW_H 10
#define LP_LIST_Y (LP_WIN_Y + 1 + LP_TITLE_H)
#define LP_LIST_H (LP_WIN_H - LP_TITLE_H - LP_HINT_H - 2)
#define LP_ROWS_VIS (LP_LIST_H / LP_ROW_H)

/* Columns of row text the window can show at once. */
#define LP_VIS_COLS ((LP_WIN_W - 10) / GFX_CHAR_W)

static void list_draw(const char *title, const char *const *rows,
                      const char *actionable, int n, int sel, int scroll,
                      int hscroll, const char *hint) {
  const uint16_t BG = g_default_theme.bg;
  const uint16_t FG = g_default_theme.fg;
  const uint16_t DIM = g_default_theme.border_light;

  gfx_panel(LP_WIN_X, LP_WIN_Y, LP_WIN_W, LP_WIN_H, LP_TITLE_H);
  gfx_drawstr_clipped(LP_WIN_X + 4,
                      LP_WIN_Y + 1 + (LP_TITLE_H - GFX_FONT_H) / 2, title,
                      g_default_theme.title_fg, g_default_theme.title_bg,
                      LP_WIN_W - 8);

  if (n == 0)
    gfx_drawstr_clipped(LP_WIN_X + 8, LP_LIST_Y + 10, "Nothing to show.", DIM,
                        BG, LP_WIN_W - 16);

  for (int vi = 0; vi < LP_ROWS_VIS; vi++) {
    int ri = scroll + vi;
    int row_y = LP_LIST_Y + vi * LP_ROW_H;
    if (ri >= n) {
      gfx_fillrect(LP_WIN_X + 1, row_y, LP_WIN_W - 2, LP_ROW_H, BG);
      continue;
    }
    int is_sel = (ri == sel);
    /* Rows the caller marked unactionable are dimmed but still shown: they
       are context, not choices. */
    int on = !actionable || actionable[ri];
    uint16_t bg = is_sel ? g_default_theme.accent : BG;
    uint16_t fg = is_sel ? g_default_theme.accent_text : (on ? FG : DIM);
    gfx_fillrect(LP_WIN_X + 1, row_y, LP_WIN_W - 2, LP_ROW_H, bg);
    /* Scrolled sideways by advancing into the row; past its end there is
       simply nothing left to show. */
    int rlen = (int)strlen(rows[ri]);
    const char *text = hscroll < rlen ? rows[ri] + hscroll : "";
    gfx_drawstr_clipped(LP_WIN_X + 4, row_y + 1, text, fg, bg, LP_WIN_W - 10);
  }

  gfx_scrollbar_v(LP_WIN_X, LP_WIN_W, LP_LIST_Y, LP_LIST_H, n, LP_ROWS_VIS,
                  scroll);

  int hy = LP_WIN_Y + LP_WIN_H - LP_HINT_H - 1;
  gfx_hline(LP_WIN_X + 1, hy, LP_WIN_W - 2, g_default_theme.border_light);
  gfx_fillrect(LP_WIN_X + 1, hy + 1, LP_WIN_W - 2, LP_HINT_H - 1, BG);
  gfx_drawstr_clipped(LP_WIN_X + 4, hy + 2, hint, FG, BG, LP_WIN_W - 8);
  gfx_flip();
}

int list_pick(const char *title, const char *const *rows,
              const char *actionable, int n, int initial, const char *hint) {
  int sel = (initial > 0 && initial < n) ? initial : 0;
  int scroll = sel - LP_ROWS_VIS / 2;
  if (scroll < 0)
    scroll = 0;
  if (scroll > n - LP_ROWS_VIS && n > LP_ROWS_VIS)
    scroll = n - LP_ROWS_VIS;

  /* Rows are often wider than the window - a diagnostic message, or a line of
     source - so allow scrolling sideways as far as the longest one needs. */
  int longest = 0;
  for (int i = 0; i < n; i++) {
    int l = (int)strlen(rows[i]);
    if (l > longest)
      longest = l;
  }
  int max_hscroll = longest - LP_VIS_COLS;
  if (max_hscroll < 0)
    max_hscroll = 0;
  int hscroll = 0;

  while (any_key_pressed())
    msleep(20);
  list_draw(title, rows, actionable, n, sel, scroll, hscroll, hint);

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
      return -1;
    } else if (nav == NAV_UP) {
      if (sel > 0) {
        sel--;
        if (sel < scroll)
          scroll = sel;
      }
    } else if (nav == NAV_DOWN) {
      if (sel < n - 1) {
        sel++;
        if (sel >= scroll + LP_ROWS_VIS)
          scroll = sel - LP_ROWS_VIS + 1;
      }
    } else if (nav == NAV_LEFT) {
      /* Ctrl jumps a windowful, matching the syscall catalog. */
      hscroll -= isKeyPressed(KEY_NSPIRE_CTRL) ? LP_VIS_COLS : 8;
      if (hscroll < 0)
        hscroll = 0;
    } else if (nav == NAV_RIGHT) {
      hscroll += isKeyPressed(KEY_NSPIRE_CTRL) ? LP_VIS_COLS : 8;
      if (hscroll > max_hscroll)
        hscroll = max_hscroll;
    } else if (nav == NAV_ENTER) {
      while (any_key_pressed())
        msleep(20);
      if (n == 0 || (actionable && !actionable[sel]))
        return -1; /* nothing to act on */
      return sel;
    }
    list_draw(title, rows, actionable, n, sel, scroll, hscroll, hint);
  }
}
