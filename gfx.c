/*
 * gfx.c
 * Shared graphics implementation for TI-Nspire CX / CX II (Ndless)
 */

#include <keys.h>
#include <libndls.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "settings.h"

/* --------------------------------------------------------------*/
/* Framebuffer & Lifecycle                                      */
/* --------------------------------------------------------------*/

static uint16_t fb[GFX_W * GFX_H];
uint16_t *gfx_framebuffer(void) { return fb; }

static int gfx_initialised = 0;
static int clip_x0 = 0, clip_y0 = 0, clip_x1 = GFX_W, clip_y1 = GFX_H;

void gfx_set_clip(int x, int y, int w, int h) {
  clip_x0 = x < 0 ? 0 : x;
  clip_y0 = y < 0 ? 0 : y;
  clip_x1 = x + w > GFX_W ? GFX_W : x + w;
  clip_y1 = y + h > GFX_H ? GFX_H : y + h;
}

void gfx_clear_clip(void) {
  clip_x0 = 0;
  clip_y0 = 0;
  clip_x1 = GFX_W;
  clip_y1 = GFX_H;
}

void gfx_init(void) {
  if (!gfx_initialised) {
    lcd_init(SCR_320x240_565);
    gfx_clear_clip();
    gfx_initialised = 1;
  }
}
void gfx_deinit(void) {
  if (gfx_initialised) {
    lcd_init(SCR_TYPE_INVALID);
    gfx_initialised = 0;
  }
}
void gfx_flip(void) { lcd_blit(fb, SCR_320x240_565); }

/* --------------------------------------------------------------*/
/* Primitives (Clip-Aware)                                      */
/* --------------------------------------------------------------*/

void gfx_setpixel(int x, int y, uint16_t col) {
  if (x >= clip_x0 && x < clip_x1 && y >= clip_y0 && y < clip_y1)
    fb[y * GFX_W + x] = col;
}

void gfx_fillrect(int x, int y, int w, int h, uint16_t col) {
  int cx0 = x < clip_x0 ? clip_x0 : x;
  int cy0 = y < clip_y0 ? clip_y0 : y;
  int cx1 = x + w > clip_x1 ? clip_x1 : x + w;
  int cy1 = y + h > clip_y1 ? clip_y1 : y + h;
  for (int r = cy0; r < cy1; r++)
    for (int c = cx0; c < cx1; c++)
      fb[r * GFX_W + c] = col;
}

void gfx_hline(int x, int y, int len, uint16_t col) {
  if (y < clip_y0 || y >= clip_y1)
    return;
  int cx0 = x < clip_x0 ? clip_x0 : x;
  int cx1 = x + len > clip_x1 ? clip_x1 : x + len;
  for (int c = cx0; c < cx1; c++)
    fb[y * GFX_W + c] = col;
}

void gfx_vline(int x, int y, int len, uint16_t col) {
  if (x < clip_x0 || x >= clip_x1)
    return;
  int cy0 = y < clip_y0 ? clip_y0 : y;
  int cy1 = y + len > clip_y1 ? clip_y1 : y + len;
  for (int r = cy0; r < cy1; r++)
    fb[r * GFX_W + x] = col;
}

void gfx_borderrect(int x, int y, int w, int h, uint16_t fill,
                    uint16_t border) {
  gfx_fillrect(x, y, w, h, fill);
  gfx_hline(x, y, w, border);
  gfx_hline(x, y + h - 1, w, border);
  gfx_vline(x, y, h, border);
  gfx_vline(x + w - 1, y, h, border);
}

/* --------------------------------------------------------------*/
/* 5x8 bitmap font (ASCII 32-126)                               */
/* --------------------------------------------------------------*/

static const uint8_t font5x8[][GFX_FONT_H] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ' ' 32 */
    {0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00}, /* '!' 33 */
    {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '"' 34 */
    {0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00, 0x00}, /* '#' 35 */
    {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04, 0x00}, /* '$' 36 */
    {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03, 0x00}, /* '%' 37 */
    {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D, 0x00}, /* '&' 38 */
    {0x0C, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ''' 39 */
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, 0x00}, /* '(' 40 */
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00}, /* ')' 41 */
    {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00, 0x00}, /* '*' 42 */
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00, 0x00}, /* '+' 43 */
    {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08, 0x00}, /* ',' 44 */
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00}, /* '-' 45 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, /* '.' 46 */
    {0x01, 0x02, 0x02, 0x04, 0x08, 0x10, 0x10, 0x00}, /* '/' 47 */
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E, 0x00}, /* '0' 48 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00}, /* '1' 49 */
    {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F, 0x00}, /* '2' 50 */
    {0x1F, 0x02, 0x04, 0x06, 0x01, 0x11, 0x0E, 0x00}, /* '3' 51 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x00}, /* '4' 52 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E, 0x00}, /* '5' 53 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E, 0x00}, /* '6' 54 */
    {0x1F, 0x01, 0x02, 0x04, 0x04, 0x04, 0x04, 0x00}, /* '7' 55 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00}, /* '8' 56 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00}, /* '9' 57 */
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00, 0x00}, /* ':' 58 */
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08, 0x00}, /* ';' 59 */
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00}, /* '<' 60 */
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00}, /* '=' 61 */
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00}, /* '>' 62 */
    {0x0E, 0x11, 0x02, 0x04, 0x04, 0x00, 0x04, 0x00}, /* '?' 63 */
    {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E, 0x00}, /* '@' 64 */
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00}, /* 'A' 65 */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00}, /* 'B' 66 */
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E, 0x00}, /* 'C' 67 */
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C, 0x00}, /* 'D' 68 */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00}, /* 'E' 69 */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00}, /* 'F' 70 */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F, 0x00}, /* 'G' 71 */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00}, /* 'H' 72 */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00}, /* 'I' 73 */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C, 0x00}, /* 'J' 74 */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00}, /* 'K' 75 */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00}, /* 'L' 76 */
    {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11, 0x00}, /* 'M' 77 */
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11, 0x00}, /* 'N' 78 */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00}, /* 'O' 79 */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00}, /* 'P' 80 */
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00}, /* 'Q' 81 */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11, 0x00}, /* 'R' 82 */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E, 0x00}, /* 'S' 83 */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, /* 'T' 84 */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00}, /* 'U' 85 */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00}, /* 'V' 86 */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11, 0x00}, /* 'W' 87 */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x00}, /* 'X' 88 */
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00}, /* 'Y' 89 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00}, /* 'Z' 90 */
    {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E, 0x00}, /* '[' 91 */
    {0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01, 0x00}, /* '\' 92 */
    {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E, 0x00}, /* ']' 93 */
    {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '^' 94 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00}, /* '_' 95 */
    {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '`' 96 */
    {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F, 0x00}, /* 'a' 97 */
    {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E, 0x00}, /* 'b' 98 */
    {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E, 0x00}, /* 'c' 99 */
    {0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F, 0x00}, /* 'd' 100 */
    {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x00}, /* 'e' 101 */
    {0x06, 0x08, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x00}, /* 'f' 102 */
    {0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E}, /* 'g' 103 */
    {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x00}, /* 'h' 104 */
    {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x00}, /* 'i' 105 */
    {0x02, 0x00, 0x06, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* 'j' 106 */
    {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00}, /* 'k' 107 */
    {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00}, /* 'l' 108 */
    {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11, 0x00}, /* 'm' 109 */
    {0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x00}, /* 'n' 110 */
    {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00}, /* 'o' 111 */
    {0x00, 0x00, 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10}, /* 'p' 112 */
    {0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01}, /* 'q' 113 */
    {0x00, 0x00, 0x16, 0x18, 0x10, 0x10, 0x10, 0x00}, /* 'r' 114 */
    {0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E, 0x00}, /* 's' 115 */
    {0x08, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x06, 0x00}, /* 't' 116 */
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x11, 0x0F, 0x00}, /* 'u' 117 */
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00}, /* 'v' 118 */
    {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00}, /* 'w' 119 */
    {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00}, /* 'x' 120 */
    {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E, 0x00}, /* 'y' 121 */
    {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00}, /* 'z' 122 */
    {0x06, 0x04, 0x04, 0x08, 0x04, 0x04, 0x06, 0x00}, /* '{' 123 */
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, /* '|' 124 */
    {0x0C, 0x04, 0x04, 0x02, 0x04, 0x04, 0x0C, 0x00}, /* '}' 125 */
    {0x00, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00, 0x00}, /* '~' 126 */
};

int gfx_drawchar(int x, int y, char ch, uint16_t fg, uint16_t bg) {
  uint8_t idx = (uint8_t)ch;
  if (idx < 32 || idx > 126)
    idx = 63;
  const uint8_t *glyph = font5x8[idx - 32];

  for (int row = 0; row < GFX_FONT_H; row++) {
    uint8_t bits = glyph[row];
    for (int col = 0; col < GFX_FONT_W; col++)
      gfx_setpixel(x + col, y + row, (bits & (0x10 >> col)) ? fg : bg);
    gfx_setpixel(x + GFX_FONT_W, y + row, bg);
  }
  return GFX_CHAR_W;
}

int gfx_drawstr(int x, int y, const char *s, uint16_t fg, uint16_t bg) {
  while (*s)
    x += gfx_drawchar(x, y, *s++, fg, bg);
  return x;
}

int gfx_drawstr_n(int x, int y, const char *s, int n, uint16_t fg,
                  uint16_t bg) {
  for (int i = 0; i < n && *s; i++)
    x += gfx_drawchar(x, y, *s++, fg, bg);
  return x;
}

void gfx_drawstr_clipped(int x, int y, const char *s, uint16_t fg, uint16_t bg,
                         int maxw) {
  int cx = x;
  while (*s && cx + GFX_CHAR_W <= x + maxw) {
    cx += gfx_drawchar(cx, y, *s++, fg, bg);
  }
  if (cx < x + maxw)
    gfx_fillrect(cx, y, x + maxw - cx, GFX_FONT_H, bg);
}

/* --------------------------------------------------------------*/
/*  Modal alert window                                           */
/* --------------------------------------------------------------*/

#define WIN_MIN_W 160
#define WIN_MAX_W 300
#define WIN_MIN_H 60
#define WIN_MAX_H 200
#define WIN_TITLE_H 13
#define WIN_BODY_PAD 6
#define WIN_BTN_H 14
#define WIN_BORDER 1
#define WIN_H_PAD 8 /* horizontal padding inside body */

void gfx_window_alert(const char *title, const char **lines, int nlines,
                      const char *ok_label) {
  if (!ok_label)
    ok_label = "OK";

  int title_chars = title ? (int)strlen(title) : 0;
  int max_body_chars = 0;
  int i;

  for (i = 0; i < nlines; i++) {
    int l = lines[i] ? (int)strlen(lines[i]) : 0;
    if (l > max_body_chars)
      max_body_chars = l;
  }
  int ok_chars = (int)strlen(ok_label);

  int content_w = max_body_chars * GFX_CHAR_W;
  if (title_chars * GFX_CHAR_W > content_w)
    content_w = title_chars * GFX_CHAR_W;
  if (ok_chars * GFX_CHAR_W + 8 > content_w)
    content_w = ok_chars * GFX_CHAR_W + 8;

  int win_w = content_w + WIN_H_PAD * 2 + WIN_BORDER * 2;
  if (win_w < WIN_MIN_W)
    win_w = WIN_MIN_W;
  if (win_w > WIN_MAX_W)
    win_w = WIN_MAX_W;

  int body_h = nlines * (GFX_FONT_H + 2);
  int win_h = WIN_TITLE_H + WIN_BODY_PAD + body_h + WIN_BODY_PAD + WIN_BTN_H +
              WIN_BORDER;

  int wx = (GFX_W - win_w) / 2;
  int wy = (GFX_H - win_h) / 2;

  gfx_fillrect(wx + 3, wy + 3, win_w, win_h, GFX_COL_GREY);

  gfx_borderrect(wx, wy, win_w, win_h, GFX_COL_LIGHT_GREY, GFX_COL_DARK_BLUE);

  gfx_fillrect(wx + WIN_BORDER, wy + WIN_BORDER, win_w - WIN_BORDER * 2,
               WIN_TITLE_H, GFX_COL_DARK_BLUE);
  if (title) {
    gfx_drawstr_clipped(
        wx + WIN_H_PAD, wy + WIN_BORDER + (WIN_TITLE_H - GFX_FONT_H) / 2, title,
        GFX_COL_WHITE, GFX_COL_DARK_BLUE, win_w - WIN_H_PAD * 2);
  }

  int body_top = wy + WIN_BORDER + WIN_TITLE_H;
  int body_area_h = win_h - WIN_BORDER - WIN_TITLE_H - WIN_BTN_H - WIN_BORDER;
  gfx_fillrect(wx + WIN_BORDER, body_top, win_w - WIN_BORDER * 2, body_area_h,
               GFX_COL_BLACK);

  int inner_w = win_w - WIN_BORDER * 2 - WIN_H_PAD * 2;
  for (i = 0; i < nlines; i++) {
    if (!lines[i])
      continue;
    int ty = body_top + WIN_BODY_PAD + i * (GFX_FONT_H + 2);
    gfx_drawstr_clipped(wx + WIN_BORDER + WIN_H_PAD, ty, lines[i],
                        GFX_COL_WHITE, GFX_COL_BLACK, inner_w);
  }

  int sep_y = wy + win_h - WIN_BTN_H - WIN_BORDER;
  gfx_hline(wx + WIN_BORDER, sep_y, win_w - WIN_BORDER * 2, GFX_COL_DARK_BLUE);

  gfx_fillrect(wx + WIN_BORDER, sep_y + 1, win_w - WIN_BORDER * 2,
               WIN_BTN_H - 1, GFX_COL_BLACK);

  int btn_label_w = (int)strlen(ok_label) * GFX_CHAR_W;
  int btn_w = btn_label_w + 8;
  int btn_h = GFX_FONT_H + 4;
  int btn_x = wx + (win_w - btn_w) / 2;
  int btn_y = sep_y + (WIN_BTN_H - btn_h) / 2;

  gfx_borderrect(btn_x, btn_y, btn_w, btn_h, GFX_COL_GREEN, GFX_COL_WHITE);
  gfx_drawstr(btn_x + 4, btn_y + 2, ok_label, GFX_COL_BLACK, GFX_COL_GREEN);

  gfx_flip();

  while (any_key_pressed()) {
    msleep(20);
  }

  for (;;) {
    if (isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_SPACE) ||
        isKeyPressed(KEY_NSPIRE_ESC) || isKeyPressed(KEY_NSPIRE_CLICK)) {
      while (any_key_pressed()) {
        msleep(20);
      }
      return;
    }

    msleep(20);
    idle();
  }
}

void gfx_window_scrolltext(const char *title, const char **lines, int nlines,
                           const char *ok_label) {
  if (!ok_label)
    ok_label = "OK";

  int title_chars = title ? (int)strlen(title) : 0;
  int max_len = 0;
  for (int i = 0; i < nlines; i++) {
    if (lines[i]) {
      int len = (int)strlen(lines[i]);
      if (len > max_len)
        max_len = len;
    }
  }
  int ok_chars = (int)strlen(ok_label);

  int content_w = max_len * GFX_CHAR_W;
  if (title_chars * GFX_CHAR_W > content_w)
    content_w = title_chars * GFX_CHAR_W;
  if (ok_chars * GFX_CHAR_W + 8 > content_w)
    content_w = ok_chars * GFX_CHAR_W + 8;

  int line_spacing = GFX_FONT_H + 2;

  int win_w = content_w + WIN_H_PAD * 2 + WIN_BORDER * 2;
  if (win_w < WIN_MIN_W)
    win_w = WIN_MIN_W;

  int need_h_scroll = 0;
  if (win_w > WIN_MAX_W) {
    win_w = WIN_MAX_W;
    need_h_scroll = 1;
  }

  int body_h = nlines * line_spacing;
  if (need_h_scroll)
    body_h += 6;

  int win_h = WIN_TITLE_H + WIN_BODY_PAD * 2 + body_h + WIN_BTN_H + WIN_BORDER;
  int need_v_scroll = 0;

  if (win_h > WIN_MAX_H) {
    win_h = WIN_MAX_H;
    need_v_scroll = 1;

    if (!need_h_scroll &&
        (content_w > win_w - WIN_BORDER * 2 - WIN_H_PAD * 2 - 6)) {
      need_h_scroll = 1;
    }
  }

  int wx = (GFX_W - win_w) / 2;
  int wy = (GFX_H - win_h) / 2;
  int body_top = wy + WIN_BORDER + WIN_TITLE_H;
  int body_area_h = win_h - WIN_BORDER - WIN_TITLE_H - WIN_BTN_H - WIN_BORDER;
  int sep_y = wy + win_h - WIN_BTN_H - WIN_BORDER;

  int inner_w = win_w - WIN_BORDER * 2 - WIN_H_PAD * 2;
  if (need_v_scroll)
    inner_w -= 6;

  int visible_body_h = body_area_h - WIN_BODY_PAD * 2;
  if (need_h_scroll)
    visible_body_h -= 6;

  int max_visible_y = visible_body_h / line_spacing;
  if (max_visible_y < 1)
    max_visible_y = 1;
  int max_visible_x = inner_w / GFX_CHAR_W;

  int btn_label_w = ok_chars * GFX_CHAR_W;
  int btn_w = btn_label_w + 8;
  int btn_h = GFX_FONT_H + 4;
  int btn_x = wx + (win_w - btn_w) / 2;
  int btn_y = sep_y + (WIN_BTN_H - btn_h) / 2;

  int scroll_y = 0;
  int scroll_x = 0;

  while (any_key_pressed()) {
    msleep(20);
  }

  for (;;) {
    gfx_fillrect(wx + 3, wy + 3, win_w, win_h, GFX_COL_GREY);
    gfx_borderrect(wx, wy, win_w, win_h, GFX_COL_LIGHT_GREY, GFX_COL_DARK_BLUE);

    gfx_fillrect(wx + WIN_BORDER, wy + WIN_BORDER, win_w - WIN_BORDER * 2,
                 WIN_TITLE_H, GFX_COL_DARK_BLUE);
    if (title) {
      gfx_drawstr_clipped(
          wx + WIN_H_PAD, wy + WIN_BORDER + (WIN_TITLE_H - GFX_FONT_H) / 2,
          title, GFX_COL_WHITE, GFX_COL_DARK_BLUE, win_w - WIN_H_PAD * 2);
    }

    gfx_fillrect(wx + WIN_BORDER, body_top, win_w - WIN_BORDER * 2, body_area_h,
                 GFX_COL_BLACK);

    for (int i = 0; i < max_visible_y; i++) {
      int idx = scroll_y + i;
      if (idx >= nlines)
        break;
      if (!lines[idx])
        continue;

      int ty = body_top + WIN_BODY_PAD + i * line_spacing;
      int len = (int)strlen(lines[idx]);

      const char *disp = "";
      if (len > scroll_x) {
        disp = lines[idx] + scroll_x;
      }

      gfx_drawstr_clipped(wx + WIN_BORDER + WIN_H_PAD, ty, disp, GFX_COL_WHITE,
                          GFX_COL_BLACK, inner_w);
    }

    if (need_v_scroll && nlines > max_visible_y) {
      int sb_x = wx + win_w - WIN_BORDER - 4;
      int sb_y = body_top + WIN_BODY_PAD;
      int sb_total_h = max_visible_y * line_spacing;

      int bar_h = (sb_total_h * max_visible_y) / nlines;
      if (bar_h < 4)
        bar_h = 4;

      int bar_y =
          sb_y + ((sb_total_h - bar_h) * scroll_y) / (nlines - max_visible_y);

      gfx_fillrect(sb_x, sb_y, 2, sb_total_h, GFX_COL_GREY);
      gfx_fillrect(sb_x, bar_y, 2, bar_h, GFX_COL_WHITE);
    }

    if (need_h_scroll && max_len > max_visible_x) {
      int sb_x = wx + WIN_BORDER + WIN_H_PAD;
      int sb_y = body_top + body_area_h - 4;
      int sb_total_w = inner_w;

      int bar_w = (sb_total_w * max_visible_x) / max_len;
      if (bar_w < 4)
        bar_w = 4;

      int bar_x =
          sb_x + ((sb_total_w - bar_w) * scroll_x) / (max_len - max_visible_x);

      gfx_fillrect(sb_x, sb_y, sb_total_w, 2, GFX_COL_GREY);
      gfx_fillrect(bar_x, sb_y, bar_w, 2, GFX_COL_WHITE);
    }

    gfx_hline(wx + WIN_BORDER, sep_y, win_w - WIN_BORDER * 2,
              GFX_COL_DARK_BLUE);
    gfx_fillrect(wx + WIN_BORDER, sep_y + 1, win_w - WIN_BORDER * 2,
                 WIN_BTN_H - 1, GFX_COL_BLACK);
    gfx_borderrect(btn_x, btn_y, btn_w, btn_h, GFX_COL_GREEN, GFX_COL_WHITE);
    gfx_drawstr(btn_x + 4, btn_y + 2, ok_label, GFX_COL_BLACK, GFX_COL_GREEN);

    gfx_flip();

    if (isKeyPressed(KEY_NSPIRE_UP) || isKeyPressed(KEY_NSPIRE_8)) {
      if (scroll_y > 0)
        scroll_y--;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_UPRIGHT) || isKeyPressed(KEY_NSPIRE_9)) {
      if (scroll_y > 0)
        scroll_y--;
      if (need_h_scroll && scroll_x < max_len - max_visible_x)
        scroll_x++;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_RIGHT) || isKeyPressed(KEY_NSPIRE_6)) {
      if (need_h_scroll && scroll_x < max_len - max_visible_x)
        scroll_x++;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_RIGHTDOWN) ||
               isKeyPressed(KEY_NSPIRE_3)) {
      if (need_v_scroll && scroll_y < nlines - max_visible_y)
        scroll_y++;
      if (need_h_scroll && scroll_x < max_len - max_visible_x)
        scroll_x++;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_DOWN) || isKeyPressed(KEY_NSPIRE_2)) {
      if (need_v_scroll && scroll_y < nlines - max_visible_y)
        scroll_y++;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_DOWNLEFT) ||
               isKeyPressed(KEY_NSPIRE_1)) {
      if (need_v_scroll && scroll_y < nlines - max_visible_y)
        scroll_y++;
      if (scroll_x > 0)
        scroll_x--;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_LEFT) || isKeyPressed(KEY_NSPIRE_4)) {
      if (scroll_x > 0)
        scroll_x--;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_LEFTUP) || isKeyPressed(KEY_NSPIRE_7)) {
      if (scroll_y > 0)
        scroll_y--;
      if (scroll_x > 0)
        scroll_x--;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_ENTER) ||
               isKeyPressed(KEY_NSPIRE_SPACE) || isKeyPressed(KEY_NSPIRE_ESC) ||
               isKeyPressed(KEY_NSPIRE_CLICK)) {
      while (any_key_pressed())
        msleep(20);

      break;
    } else {
      msleep(20);
      idle();
    }
  }
}

/* ================================================================
 * gfx_menu  -  full-screen vertical selection menu
 * ================================================================ */

#define MENU_HEADER_H 52   /* px: logo/title area at top             */
#define MENU_ITEM_H 22     /* px: height of each menu row            */
#define MENU_ITEM_PAD_X 16 /* px: left indent of item text           */
#define MENU_FOOTER_H 12   /* px: hint bar at bottom                 */

static void menu_draw_item(int idx, int sel, const char *label) {
  int y = MENU_HEADER_H + idx * MENU_ITEM_H;
  int is_sel = (idx == sel);

  uint16_t bg = is_sel ? g_default_theme.accent : g_default_theme.bg;
  uint16_t fg = is_sel ? g_default_theme.accent_text : g_default_theme.fg;

  gfx_fillrect(0, y, GFX_W, MENU_ITEM_H, bg);

  if (is_sel)
    gfx_fillrect(0, y, 3, MENU_ITEM_H, g_default_theme.border_light);

  int ty = y + (MENU_ITEM_H - GFX_FONT_H) / 2;
  gfx_drawstr(MENU_ITEM_PAD_X, ty, label, fg, bg);

  gfx_hline(MENU_ITEM_PAD_X, y + MENU_ITEM_H - 1, GFX_W - MENU_ITEM_PAD_X,
            g_default_theme.item_bg);
}

static void menu_render(const char *title, const char *subtitle,
                        const char **items, int nitems, int sel) {
  int i;

  gfx_fillrect(0, 0, GFX_W, GFX_H, g_default_theme.bg);

  gfx_fillrect(0, 0, GFX_W, MENU_HEADER_H, g_default_theme.title_bg);

  gfx_fillrect(0, 0, 4, MENU_HEADER_H, g_default_theme.accent);

  int title_y = (subtitle ? 10 : 20);
  gfx_drawstr(12, title_y, title, g_default_theme.title_fg,
              g_default_theme.title_bg);
  gfx_drawstr(12, title_y + 1, title, g_default_theme.title_fg,
              g_default_theme.title_bg);

  if (subtitle) {
    gfx_drawstr(13, title_y + GFX_FONT_H + 6, subtitle,
                g_default_theme.border_light, g_default_theme.title_bg);
  }

  for (i = 0; i < nitems; i++)
    menu_draw_item(i, sel, items[i]);

  int list_bottom = MENU_HEADER_H + nitems * MENU_ITEM_H;
  if (list_bottom < GFX_H - MENU_FOOTER_H)
    gfx_fillrect(0, list_bottom, GFX_W, GFX_H - MENU_FOOTER_H - list_bottom,
                 g_default_theme.bg);

  int fy = GFX_H - MENU_FOOTER_H;
  gfx_fillrect(0, fy, GFX_W, MENU_FOOTER_H, g_default_theme.title_bg);
  gfx_drawstr_clipped(
      4, fy + 2, "Up/Down: navigate    Enter: select    Esc: quit",
      g_default_theme.border_light, g_default_theme.title_bg, GFX_W - 8);

  gfx_flip();
}

int gfx_menu(const char *title, const char *subtitle, const char **items,
             int nitems, int initial_sel) {
  int sel = initial_sel;
  if (sel < 0)
    sel = 0;
  if (sel >= nitems)
    sel = nitems - 1;

  while (any_key_pressed())
    msleep(20);
  menu_render(title, subtitle, items, nitems, sel);

  for (;;) {
    NavAction nav = gfx_poll_nav();
    if (nav == NAV_NONE) {
      msleep(16);
      idle();
      continue;
    }

    if (nav == NAV_UP) {
      if (sel > 0) {
        sel--;
        menu_render(title, subtitle, items, nitems, sel);
      }
    } else if (nav == NAV_DOWN) {
      if (sel < nitems - 1) {
        sel++;
        menu_render(title, subtitle, items, nitems, sel);
      }
    } else if (nav == NAV_ENTER) {
      while (any_key_pressed())
        msleep(20);
      return sel;
    } else if (nav == NAV_ESC) {
      while (any_key_pressed())
        msleep(20);
      return -1;
    }
  }
}

/* ================================================================
 * gfx_input_filename  -  single-line filename input dialog
 * ================================================================ */

#define INPUT_KEYMAP_SIZE 40

typedef struct {
  t_key key;
  char normal;
  char shifted;
} InputKey;

static const InputKey input_keymap[INPUT_KEYMAP_SIZE] = {
    {KEY_NSPIRE_A, 'a', 'A'},      {KEY_NSPIRE_B, 'b', 'B'},
    {KEY_NSPIRE_C, 'c', 'C'},      {KEY_NSPIRE_D, 'd', 'D'},
    {KEY_NSPIRE_E, 'e', 'E'},      {KEY_NSPIRE_F, 'f', 'F'},
    {KEY_NSPIRE_G, 'g', 'G'},      {KEY_NSPIRE_H, 'h', 'H'},
    {KEY_NSPIRE_I, 'i', 'I'},      {KEY_NSPIRE_J, 'j', 'J'},
    {KEY_NSPIRE_K, 'k', 'K'},      {KEY_NSPIRE_L, 'l', 'L'},
    {KEY_NSPIRE_M, 'm', 'M'},      {KEY_NSPIRE_N, 'n', 'N'},
    {KEY_NSPIRE_O, 'o', 'O'},      {KEY_NSPIRE_P, 'p', 'P'},
    {KEY_NSPIRE_Q, 'q', 'Q'},      {KEY_NSPIRE_R, 'r', 'R'},
    {KEY_NSPIRE_S, 's', 'S'},      {KEY_NSPIRE_T, 't', 'T'},
    {KEY_NSPIRE_U, 'u', 'U'},      {KEY_NSPIRE_V, 'v', 'V'},
    {KEY_NSPIRE_W, 'w', 'W'},      {KEY_NSPIRE_X, 'x', 'X'},
    {KEY_NSPIRE_Y, 'y', 'Y'},      {KEY_NSPIRE_Z, 'z', 'Z'},
    {KEY_NSPIRE_0, '0', '}'},      {KEY_NSPIRE_1, '1', '!'},
    {KEY_NSPIRE_2, '2', '@'},      {KEY_NSPIRE_3, '3', '#'},
    {KEY_NSPIRE_4, '4', '$'},      {KEY_NSPIRE_5, '5', '%'},
    {KEY_NSPIRE_6, '6', '^'},      {KEY_NSPIRE_7, '7', '&'},
    {KEY_NSPIRE_8, '8', '*'},      {KEY_NSPIRE_9, '9', '('},
    {KEY_NSPIRE_MINUS, '-', '_'},  {KEY_NSPIRE_PERIOD, '.', '.'},
    {KEY_NSPIRE_DIVIDE, '/', '/'}, {KEY_NSPIRE_SPACE, ' ', '_'},
};

#define INP_WIN_W 260
#define INP_WIN_H 70
#define INP_TITLE_H 13
#define INP_FIELD_H 14 /* text field box height */
#define INP_PAD 8

#define INP_REPEAT_DELAY 18
#define INP_REPEAT_RATE 4

static void input_render(const char *title, const char *prompt, const char *buf,
                         int cursor, int wx, int wy) {
  gfx_fillrect(wx + 3, wy + 3, INP_WIN_W, INP_WIN_H, GFX_COL_GREY);
  gfx_borderrect(wx, wy, INP_WIN_W, INP_WIN_H, GFX_COL_BLACK,
                 GFX_COL_DARK_BLUE);
  gfx_fillrect(wx + 1, wy + 1, INP_WIN_W - 2, INP_TITLE_H, GFX_COL_DARK_BLUE);
  gfx_drawstr_clipped(wx + INP_PAD, wy + 1 + (INP_TITLE_H - GFX_FONT_H) / 2,
                      title, GFX_COL_WHITE, GFX_COL_DARK_BLUE,
                      INP_WIN_W - INP_PAD * 2);
  int body_y = wy + 1 + INP_TITLE_H;
  int body_h = INP_WIN_H - INP_TITLE_H - 2;
  gfx_fillrect(wx + 1, body_y, INP_WIN_W - 2, body_h, GFX_COL_BLACK);

  int prompt_y = body_y + INP_PAD / 2;
  if (prompt) {
    gfx_drawstr(wx + INP_PAD, prompt_y, prompt, GFX_COL_GREY, GFX_COL_BLACK);
    prompt_y += GFX_FONT_H + 3;
  }

  int field_x = wx + INP_PAD;
  int field_w = INP_WIN_W - INP_PAD * 2;
  int field_y = prompt_y;
  gfx_borderrect(field_x, field_y, field_w, INP_FIELD_H, 0x1082u, GFX_COL_GREY);

  int text_x = field_x + 2;
  int text_y = field_y + (INP_FIELD_H - GFX_FONT_H) / 2;
  int max_vis = (field_w - 4) / GFX_CHAR_W;
  int len = (int)strlen(buf);

  int start = 0;
  if (cursor >= max_vis)
    start = cursor - max_vis + 1;

  int i;
  for (i = 0; i < max_vis; i++) {
    int idx = start + i;
    int cx = text_x + i * GFX_CHAR_W;
    int is_cur = (idx == cursor);
    if (idx < len) {
      gfx_drawchar(cx, text_y, buf[idx], is_cur ? GFX_COL_BLACK : GFX_COL_WHITE,
                   is_cur ? GFX_COL_WHITE : 0x1082u);
    } else if (is_cur) {
      gfx_drawchar(cx, text_y, ' ', GFX_COL_BLACK, GFX_COL_WHITE);
    } else {
      gfx_drawchar(cx, text_y, ' ', GFX_COL_WHITE, 0x1082u);
    }
  }

  int hint_y = field_y + INP_FIELD_H + 3;
  gfx_drawstr_clipped(field_x, hint_y, "Enter:confirm  Esc:cancel",
                      GFX_COL_GREY, GFX_COL_BLACK, field_w);

  gfx_flip();
}

int gfx_input_filename(const char *title, const char *prompt, char *out,
                       int outmax) {

  int wx = (GFX_W - INP_WIN_W) / 2;
  int wy = (GFX_H - INP_WIN_H) / 2;

  char buf[256];
  int len = 0;
  int cursor = 0;
  buf[0] = '\0';

  int last_ch = 0;
  int rep_timer = 0;

  while (any_key_pressed()) {
    msleep(20);
    idle();
  }

  input_render(title, prompt, buf, cursor, wx, wy);

  for (;;) {
    int ch = 0;
    int is_bs = 0;
    int shift = (int)isKeyPressed(KEY_NSPIRE_SHIFT);

    if (isKeyPressed(KEY_NSPIRE_ENTER)) {
      while (any_key_pressed()) {
        msleep(20);
        idle();
      }
      if (len == 0)
        continue;
      int copy = len < outmax - 1 ? len : outmax - 1;
      memcpy(out, buf, copy);
      out[copy] = '\0';
      return 1;
    }
    if (isKeyPressed(KEY_NSPIRE_ESC)) {
      while (any_key_pressed()) {
        msleep(20);
        idle();
      }
      return 0;
    }
    if (isKeyPressed(KEY_NSPIRE_DEL)) {
      is_bs = 1;
    }
    if (!is_bs) {
      int i;
      for (i = 0; i < INPUT_KEYMAP_SIZE; i++) {
        if (isKeyPressed(input_keymap[i].key)) {
          ch = shift ? (unsigned char)input_keymap[i].shifted
                     : (unsigned char)input_keymap[i].normal;
          break;
        }
      }
    }

    int action = is_bs ? -1 : ch;

    if (action == 0) {
      last_ch = 0;
      rep_timer = 0;
      msleep(16);
      idle();
      continue;
    }

    if (action != last_ch) {
      last_ch = action;
      rep_timer = 0;
    } else {
      rep_timer++;
      if (rep_timer < INP_REPEAT_DELAY ||
          (rep_timer - INP_REPEAT_DELAY) % INP_REPEAT_RATE != 0) {
        msleep(16);
        idle();
        continue;
      }
    }

    if (is_bs) {
      if (cursor > 0) {
        memmove(buf + cursor - 1, buf + cursor, len - cursor + 1);
        cursor--;
        len--;
      }
    } else if (ch >= 32 && ch < 128 && len < outmax - 1 && len < 254) {
      memmove(buf + cursor + 1, buf + cursor, len - cursor + 1);
      buf[cursor] = (char)ch;
      cursor++;
      len++;
    }

    input_render(title, prompt, buf, cursor, wx, wy);

    msleep(16);
    idle();
  }
}

/* ================================================================
 * Confirm dialogs (buttons)
 *
 * Both confirm2 and confirm3 delegate to a shared backend that draws
 * the chrome and runs the input loop for an arbitrary button row.
 * ================================================================ */

static int measure_buttons_w(const char **labels, const int *widths, int n,
                             int gap) {
  int total = 0;
  for (int i = 0; i < n; i++)
    total += widths[i] + (i < n - 1 ? gap : 0);
  (void)labels;
  return total;
}

static int confirm_draw_frame(int wx, int wy, int win_w, int win_h,
                              const char *title, const char **body, int nbody) {
  gfx_fillrect(wx + 3, wy + 3, win_w, win_h, GFX_COL_GREY);
  gfx_borderrect(wx, wy, win_w, win_h, GFX_COL_LIGHT_GREY, GFX_COL_DARK_BLUE);

  gfx_fillrect(wx + WIN_BORDER, wy + WIN_BORDER, win_w - WIN_BORDER * 2,
               WIN_TITLE_H, GFX_COL_DARK_BLUE);
  if (title) {
    gfx_drawstr_clipped(
        wx + WIN_H_PAD, wy + WIN_BORDER + (WIN_TITLE_H - GFX_FONT_H) / 2, title,
        GFX_COL_WHITE, GFX_COL_DARK_BLUE, win_w - WIN_H_PAD * 2);
  }

  int body_top = wy + WIN_BORDER + WIN_TITLE_H;
  int body_area_h = win_h - WIN_BORDER - WIN_TITLE_H - WIN_BTN_H - WIN_BORDER;
  gfx_fillrect(wx + WIN_BORDER, body_top, win_w - WIN_BORDER * 2, body_area_h,
               GFX_COL_BLACK);

  int inner_w = win_w - WIN_BORDER * 2 - WIN_H_PAD * 2;
  for (int i = 0; i < nbody; i++) {
    if (!body[i])
      continue;
    int ty = body_top + WIN_BODY_PAD + i * (GFX_FONT_H + 2);
    gfx_drawstr_clipped(wx + WIN_BORDER + WIN_H_PAD, ty, body[i], GFX_COL_WHITE,
                        GFX_COL_BLACK, inner_w);
  }

  int sep_y = wy + win_h - WIN_BTN_H - WIN_BORDER;
  gfx_hline(wx + WIN_BORDER, sep_y, win_w - WIN_BORDER * 2, GFX_COL_DARK_BLUE);
  gfx_fillrect(wx + WIN_BORDER, sep_y + 1, win_w - WIN_BORDER * 2,
               WIN_BTN_H - 1, GFX_COL_BLACK);
  return sep_y;
}

static void confirm_draw_buttons(int wx, int win_w, int sep_y,
                                 const char **btns, const int *bw, int n,
                                 int gap, int focus) {
  int btns_w = measure_buttons_w(btns, bw, n, gap);
  int start_x = wx + (win_w - btns_w) / 2;
  int btn_y = sep_y + (WIN_BTN_H - (GFX_FONT_H + 4)) / 2;
  int btn_h = GFX_FONT_H + 4;
  int cur_x = start_x;

  for (int i = 0; i < n; i++) {
    int sel = (i == focus);
    gfx_borderrect(cur_x, btn_y, bw[i], btn_h,
                   sel ? GFX_COL_GREEN : GFX_COL_BLACK,
                   sel ? GFX_COL_WHITE : GFX_COL_GREY);
    gfx_drawstr(cur_x + 4, btn_y + 2, btns[i],
                sel ? GFX_COL_BLACK : GFX_COL_WHITE,
                sel ? GFX_COL_GREEN : GFX_COL_BLACK);
    cur_x += bw[i] + gap;
  }
}

static int confirm_run(int wx, int wy, int win_w, int win_h, const char *title,
                       const char **body, int nbody, const char **btns,
                       const int *bw, int n, int gap) {
  int focus = 0;

  while (any_key_pressed()) {
    msleep(20);
    idle();
  }

  for (;;) {
    int sep_y = confirm_draw_frame(wx, wy, win_w, win_h, title, body, nbody);
    confirm_draw_buttons(wx, win_w, sep_y, btns, bw, n, gap, focus);
    gfx_flip();

    if (isKeyPressed(KEY_NSPIRE_LEFT) || isKeyPressed(KEY_NSPIRE_4)) {
      if (focus > 0)
        focus--;
      while (isKeyPressed(KEY_NSPIRE_LEFT) || isKeyPressed(KEY_NSPIRE_4)) {
        msleep(20);
        idle();
      }
    } else if (isKeyPressed(KEY_NSPIRE_RIGHT) || isKeyPressed(KEY_NSPIRE_6)) {
      if (focus < n - 1)
        focus++;
      while (isKeyPressed(KEY_NSPIRE_RIGHT) || isKeyPressed(KEY_NSPIRE_6)) {
        msleep(20);
        idle();
      }
    } else if (isKeyPressed(KEY_NSPIRE_ENTER) ||
               isKeyPressed(KEY_NSPIRE_SPACE) ||
               isKeyPressed(KEY_NSPIRE_CLICK)) {
      while (any_key_pressed()) {
        msleep(20);
        idle();
      }
      return focus;
    } else if (isKeyPressed(KEY_NSPIRE_ESC)) {
      while (any_key_pressed()) {
        msleep(20);
        idle();
      }
      return -1;
    } else {
      msleep(20);
      idle();
    }
  }
}

static void confirm_measure(const char *title, const char **body, int nbody,
                            int btns_w, int *out_win_w, int *out_win_h) {
  int title_chars = title ? (int)strlen(title) : 0;
  int max_body_chars = 0;
  for (int i = 0; i < nbody; i++) {
    int l = body[i] ? (int)strlen(body[i]) : 0;
    if (l > max_body_chars)
      max_body_chars = l;
  }

  int content_w = max_body_chars * GFX_CHAR_W;
  if (title_chars * GFX_CHAR_W > content_w)
    content_w = title_chars * GFX_CHAR_W;
  if (btns_w > content_w)
    content_w = btns_w;

  int win_w = content_w + WIN_H_PAD * 2 + WIN_BORDER * 2;
  if (win_w < WIN_MIN_W)
    win_w = WIN_MIN_W;
  if (win_w > WIN_MAX_W)
    win_w = WIN_MAX_W;

  int body_h = nbody * (GFX_FONT_H + 2);
  int win_h = WIN_TITLE_H + WIN_BODY_PAD + body_h + WIN_BODY_PAD + WIN_BTN_H +
              WIN_BORDER;

  *out_win_w = win_w;
  *out_win_h = win_h;
}

int gfx_window_confirm2(const char *title, const char **body, int nbody,
                        const char *btn0, const char *btn1) {
  const char *btns[2] = {btn0, btn1};
  int bw[2] = {(int)strlen(btn0) * GFX_CHAR_W + 8,
               (int)strlen(btn1) * GFX_CHAR_W + 8};
  int gap = 16;
  int btns_w = bw[0] + gap + bw[1];

  int win_w, win_h;
  confirm_measure(title, body, nbody, btns_w, &win_w, &win_h);

  int wx = (GFX_W - win_w) / 2;
  int wy = (GFX_H - win_h) / 2;
  return confirm_run(wx, wy, win_w, win_h, title, body, nbody, btns, bw, 2,
                     gap);
}

int gfx_window_confirm3(const char *title, const char **body, int nbody,
                        const char *btn0, const char *btn1, const char *btn2) {
  const char *btns[3] = {btn0, btn1, btn2};
  int bw[3] = {(int)strlen(btn0) * GFX_CHAR_W + 8,
               (int)strlen(btn1) * GFX_CHAR_W + 8,
               (int)strlen(btn2) * GFX_CHAR_W + 8};
  int gap = 8;
  int btns_w = bw[0] + gap + bw[1] + gap + bw[2];

  int win_w, win_h;
  confirm_measure(title, body, nbody, btns_w, &win_w, &win_h);

  int wx = (GFX_W - win_w) / 2;
  int wy = (GFX_H - win_h) / 2;
  return confirm_run(wx, wy, win_w, win_h, title, body, nbody, btns, bw, 3,
                     gap);
}

/* --------------------------------------------------------------*/
/* Modular GUI Framework Implementation                         */
/* --------------------------------------------------------------*/

GfxTheme g_default_theme = {
    GFX_COL_BLACK,     GFX_COL_WHITE,     GFX_COL_LIGHT_GREY,
    GFX_COL_DARK_BLUE, GFX_COL_DARK_BLUE, GFX_COL_WHITE,
    GFX_COL_GREEN,     GFX_COL_BLACK,     0x2104u};

static GfxTheme *resolve_theme(GfxWindow *win, GfxWidget *w) {
  if (w && w->theme)
    return w->theme;
  if (win && win->theme)
    return win->theme;
  return &g_default_theme;
}

/* --------------------------------------------------------------*/
/* Input Polling (Unified Repeating Nav)                         */
/* --------------------------------------------------------------*/

NavAction gfx_poll_nav(void) {
  int active = NAV_NONE;

  if (isKeyPressed(KEY_NSPIRE_UP) || isKeyPressed(KEY_NSPIRE_8))
    active = NAV_UP;
  else if (isKeyPressed(KEY_NSPIRE_DOWN) || isKeyPressed(KEY_NSPIRE_2))
    active = NAV_DOWN;
  else if (isKeyPressed(KEY_NSPIRE_LEFT) || isKeyPressed(KEY_NSPIRE_4))
    active = NAV_LEFT;
  else if (isKeyPressed(KEY_NSPIRE_RIGHT) || isKeyPressed(KEY_NSPIRE_6))
    active = NAV_RIGHT;
  else if (isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_CLICK) ||
           isKeyPressed(KEY_NSPIRE_SPACE))
    active = NAV_ENTER;
  else if (isKeyPressed(KEY_NSPIRE_ESC))
    active = NAV_ESC;
  else if (isKeyPressed(KEY_NSPIRE_TAB))
    active = NAV_TAB;
  else if (isKeyPressed(KEY_NSPIRE_CAT))
    active = NAV_CAT;

  static int last_active = NAV_NONE;
  static int repeat_timer = 0;

  if (active == NAV_NONE) {
    last_active = NAV_NONE;
    repeat_timer = 0;
    return NAV_NONE;
  }

  if (active != last_active) {
    last_active = active;
    repeat_timer = 0;
    return active;
  }

  if (active == NAV_ENTER || active == NAV_ESC || active == NAV_CAT ||
      active == NAV_TAB) {
    return NAV_NONE;
  }

  repeat_timer++;
  if (repeat_timer < 18)
    return NAV_NONE;
  if ((repeat_timer - 18) % 4 != 0)
    return NAV_NONE;

  return active;
}

static int gfx_popup_dropdown(int dx, int dy, int dw, const char **opts,
                              int nopts, int current_sel, GfxTheme *th) {
  int item_h = GFX_FONT_H + 4;
  int max_vis = 6;
  if (nopts < max_vis)
    max_vis = nopts;
  int dh = max_vis * item_h + 2;
  if (dy + dh > GFX_H - 2) {
    int possible_vis = (GFX_H - dy - 4) / item_h;
    if (possible_vis >= 3) {
      max_vis = possible_vis;
      dh = max_vis * item_h + 2;
    } else {
      dy = GFX_H - dh - 4;
    }
  }
  if (dy < 2) {
    dy = 2;
    max_vis = (GFX_H - 6) / item_h;
    if (nopts < max_vis)
      max_vis = nopts;
    dh = max_vis * item_h + 2;
  }

  int scroll = 0;
  if (current_sel >= max_vis)
    scroll = current_sel - max_vis + 1;
  int sel = current_sel;
  int has_scrollbar = (nopts > max_vis);
  int inner_w = has_scrollbar ? (dw - 8) : (dw - 2);

  while (any_key_pressed())
    msleep(20);

  int redraw = 1;
  for (;;) {
    if (redraw) {
      gfx_fillrect(dx + 3, dy + 3, dw, dh, g_default_theme.border_dark);
      gfx_borderrect(dx, dy, dw, dh, th->bg, th->border_light);

      for (int i = 0; i < max_vis; i++) {
        int idx = scroll + i;
        if (idx >= nopts)
          break;
        int iy = dy + 1 + i * item_h;
        uint16_t bg = (idx == sel) ? th->accent : th->bg;
        uint16_t fg = (idx == sel) ? th->accent_text : th->fg;

        gfx_fillrect(dx + 1, iy, inner_w, item_h, bg);
        gfx_drawstr_clipped(dx + 4, iy + 2, opts[idx], fg, bg, inner_w - 6);
        if (has_scrollbar)
          gfx_fillrect(dx + 1 + inner_w, iy, 2, item_h, th->bg);
      }

      if (has_scrollbar) {
        int sb_x = dx + dw - 5;
        int sb_y_start = dy + 1;
        int sb_total_h = dh - 2;
        int bar_h = (sb_total_h * max_vis) / nopts;
        if (bar_h < 4)
          bar_h = 4;
        int bar_y =
            sb_y_start + ((sb_total_h - bar_h) * scroll) / (nopts - max_vis);
        gfx_fillrect(sb_x, sb_y_start, 4, sb_total_h, th->item_bg);
        gfx_fillrect(sb_x, bar_y, 4, bar_h, th->border_light);
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
      if (sel > 0)
        sel--;
      if (sel < scroll)
        scroll = sel;
      redraw = 1;
    } else if (nav == NAV_DOWN) {
      if (sel < nopts - 1)
        sel++;
      if (sel >= scroll + max_vis)
        scroll = sel - max_vis + 1;
      redraw = 1;
    } else if (nav == NAV_ENTER) {
      while (any_key_pressed())
        msleep(20);
      return sel;
    } else if (nav == NAV_ESC) {
      while (any_key_pressed())
        msleep(20);
      return current_sel;
    }
  }
}

/* --------------------------------------------------------------*/
/* Widget Implementations                                       */
/* --------------------------------------------------------------*/

static void lbl_draw(GfxWindow *win, GfxWidget *w, int x, int y, int foc) {
  (void)foc;
  GfxTheme *th = resolve_theme(win, w);
  gfx_drawstr_clipped(x, y + (w->h - GFX_FONT_H) / 2, (const char *)w->data,
                      th->fg, th->bg, w->w);
}
GfxWidget *widget_create_label(int x, int y, int w, int h, const char *text) {
  GfxWidget *wg = calloc(1, sizeof(GfxWidget));
  wg->x = x;
  wg->y = y;
  wg->w = w;
  wg->h = h;
  wg->data = (void *)text;
  wg->draw = lbl_draw;
  wg->focusable = 0;
  wg->disabled = 0;
  return wg;
}

static void heading_draw(GfxWindow *win, GfxWidget *w, int x, int y, int foc) {
  (void)foc;
  GfxTheme *th = resolve_theme(win, w);

  int ty = y + (w->h - GFX_FONT_H) / 2;
  gfx_drawstr(x, ty, (const char *)w->data, th->accent, th->bg);

  int tw = strlen((const char *)w->data) * GFX_CHAR_W;
  int line_x = x + tw + 6;
  int line_w = w->w - tw - 6;

  if (line_w > 0) {
    int line_y = y + w->h / 2;
    gfx_hline(line_x, line_y, line_w, th->item_bg);
  }
}

GfxWidget *widget_create_heading(int x, int y, int w, int h, const char *text) {
  GfxWidget *wg = calloc(1, sizeof(GfxWidget));
  wg->x = x;
  wg->y = y;
  wg->w = w;
  wg->h = h;
  wg->data = (void *)text;
  wg->draw = heading_draw;
  wg->focusable = 0;
  wg->disabled = 0;
  return wg;
}

static void btn_draw(GfxWindow *win, GfxWidget *w, int x, int y, int foc) {
  GfxTheme *th = resolve_theme(win, w);
  uint16_t bg = foc ? th->accent : th->item_bg;
  uint16_t fg = foc ? th->accent_text : th->fg;
  uint16_t border = foc ? th->accent : th->border_light;

  if (w->disabled) {
    bg = th->bg;
    fg = th->border_dark;
    border = th->border_dark;
  }

  gfx_borderrect(x, y + (w->h - GFX_FONT_H - 8) / 2, w->w, GFX_FONT_H + 8, bg,
                 border);

  int tw = strlen((const char *)w->data) * GFX_CHAR_W;
  int max_tw = w->w - 4;
  if (tw > max_tw) {
    tw = max_tw;
  }

  int tx = x + (w->w - tw) / 2;
  int ty = y + (w->h - GFX_FONT_H) / 2;

  gfx_drawstr_clipped(tx, ty, (const char *)w->data, fg, bg, tw);
}
static int btn_input(GfxWindow *win, GfxWidget *w, NavAction nav) {
  (void)win;
  if (w->disabled)
    return 0;
  if (nav == NAV_ENTER) {
    while (any_key_pressed())
      msleep(20);
    return 2;
  }
  return 0;
}
GfxWidget *widget_create_button(int x, int y, int w, int h, const char *label) {
  GfxWidget *wg = calloc(1, sizeof(GfxWidget));
  wg->x = x;
  wg->y = y;
  wg->w = w;
  wg->h = h;
  wg->data = (void *)label;
  wg->draw = btn_draw;
  wg->input = btn_input;
  wg->focusable = 1;
  return wg;
}

static void tog_draw(GfxWindow *win, GfxWidget *w, int x, int y, int foc) {
  GfxTheme *th = resolve_theme(win, w);
  int val = *(int *)w->data;
  if (foc)
    gfx_fillrect(x - 2, y - 2, w->w + 4, w->h + 4, th->item_bg);
  int sw_w = 30, sw_h = 14;
  int sw_y = y + (w->h - sw_h) / 2;
  gfx_borderrect(x, sw_y, sw_w, sw_h, val ? th->accent : th->item_bg,
                 th->border_light);
  int thumb_x = val ? (x + sw_w - 12) : (x + 2);
  gfx_borderrect(thumb_x, sw_y + 2, 10, 10, th->border_light, th->border_dark);
  gfx_drawstr(x + sw_w + 6, y + (w->h - GFX_FONT_H) / 2, val ? "ON" : "OFF",
              val ? th->fg : th->border_light, foc ? th->item_bg : th->bg);
}
static int tog_input(GfxWindow *win, GfxWidget *w, NavAction nav) {
  (void)win;
  if (nav == NAV_LEFT) {
    *(int *)w->data = 0;
    return 1;
  }
  if (nav == NAV_RIGHT) {
    *(int *)w->data = 1;
    return 1;
  }
  if (nav == NAV_ENTER) {
    *(int *)w->data = !(*(int *)w->data);
    return 1;
  }
  return 0;
}
GfxWidget *widget_create_toggle(int x, int y, int w, int h, int *val_ptr) {
  GfxWidget *wg = calloc(1, sizeof(GfxWidget));
  wg->x = x;
  wg->y = y;
  wg->w = w;
  wg->h = h;
  wg->data = val_ptr;
  wg->draw = tog_draw;
  wg->input = tog_input;
  wg->focusable = 1;
  return wg;
}

static void num_draw(GfxWindow *win, GfxWidget *w, int x, int y, int foc) {
  GfxTheme *th = resolve_theme(win, w);
  int val = *(int *)w->data;
  char buf[32];
  snprintf(buf, sizeof(buf), "< %d >", val);
  if (foc)
    gfx_fillrect(x - 2, y - 2, w->w + 4, w->h + 4, th->item_bg);
  gfx_drawstr_clipped(x, y + (w->h - GFX_FONT_H) / 2, buf,
                      foc ? th->accent : th->fg, foc ? th->item_bg : th->bg,
                      w->w);
}
static int num_input(GfxWindow *win, GfxWidget *w, NavAction nav) {
  (void)win;
  int *v = (int *)w->data;
  if (nav == NAV_LEFT) {
    if (*v > w->min_val)
      (*v)--;
    return 1;
  }
  if (nav == NAV_RIGHT) {
    if (*v < w->max_val)
      (*v)++;
    return 1;
  }
  return 0;
}
GfxWidget *widget_create_number(int x, int y, int w, int h, int *val_ptr,
                                int min_v, int max_v) {
  GfxWidget *wg = calloc(1, sizeof(GfxWidget));
  wg->x = x;
  wg->y = y;
  wg->w = w;
  wg->h = h;
  wg->data = val_ptr;
  wg->min_val = min_v;
  wg->max_val = max_v;
  wg->draw = num_draw;
  wg->input = num_input;
  wg->focusable = 1;
  return wg;
}

static void drop_draw(GfxWindow *win, GfxWidget *w, int x, int y, int foc) {
  GfxTheme *th = resolve_theme(win, w);
  int val = *(int *)w->data;
  const char **opts = (const char **)w->extra;
  gfx_borderrect(x, y + (w->h - GFX_FONT_H - 4) / 2, w->w, GFX_FONT_H + 4,
                 foc ? th->item_bg : th->bg, th->border_light);
  gfx_drawstr_clipped(x + 4, y + (w->h - GFX_FONT_H) / 2, opts[val],
                      foc ? th->accent : th->fg, foc ? th->item_bg : th->bg,
                      w->w - 16);
  gfx_drawstr(x + w->w - 10, y + (w->h - GFX_FONT_H) / 2, "v", th->border_light,
              foc ? th->item_bg : th->bg);
}
static int drop_input(GfxWindow *win, GfxWidget *w, NavAction nav) {
  if (nav == NAV_ENTER) {
    GfxTheme *th = resolve_theme(win, w);
    int abs_x = win->x + 1 + w->x;
    int abs_y =
        win->y + 1 + 13 + w->y - win->scroll_y + (w->h - GFX_FONT_H - 4) / 2;
    *(int *)w->data =
        gfx_popup_dropdown(abs_x, abs_y, w->w, (const char **)w->extra,
                           w->max_len, *(int *)w->data, th);
    return 1;
  }
  return 0;
}
GfxWidget *widget_create_dropdown(int x, int y, int w, int h, int *val_ptr,
                                  const char **opts, int nopts) {
  GfxWidget *wg = calloc(1, sizeof(GfxWidget));
  wg->x = x;
  wg->y = y;
  wg->w = w;
  wg->h = h;
  wg->data = val_ptr;
  wg->extra = (void *)opts;
  wg->max_len = nopts;
  wg->draw = drop_draw;
  wg->input = drop_input;
  wg->focusable = 1;
  return wg;
}

static void text_draw(GfxWindow *win, GfxWidget *w, int x, int y, int foc) {
  GfxTheme *th = resolve_theme(win, w);
  char *str = (char *)w->data;
  gfx_borderrect(x, y + (w->h - GFX_FONT_H - 4) / 2, w->w, GFX_FONT_H + 4,
                 foc ? th->item_bg : th->bg, th->border_light);
  gfx_drawstr_clipped(x + 4, y + (w->h - GFX_FONT_H) / 2, str,
                      foc ? th->accent : th->fg, foc ? th->item_bg : th->bg,
                      w->w - 8);
}
static int text_input(GfxWindow *win, GfxWidget *w, NavAction nav) {
  (void)win;
  if (nav == NAV_ENTER) {
    gfx_input_filename("Edit Value", (const char *)w->extra, (char *)w->data,
                       w->max_len);
    return 1;
  }
  return 0;
}
GfxWidget *widget_create_text(int x, int y, int w, int h, char *buf_ptr,
                              int max_len, const char *prompt) {
  GfxWidget *wg = calloc(1, sizeof(GfxWidget));
  wg->x = x;
  wg->y = y;
  wg->w = w;
  wg->h = h;
  wg->data = buf_ptr;
  wg->max_len = max_len;
  wg->extra = (void *)prompt;
  wg->draw = text_draw;
  wg->input = text_input;
  wg->focusable = 1;
  return wg;
}

/* --------------------------------------------------------------*/
/* Window Manager Execution Loop                                */
/* --------------------------------------------------------------*/
int gfx_window_exec(GfxWindow *win) {
  GfxTheme *wTheme = resolve_theme(win, NULL);
  int win_border = 1, win_title_h = 13, win_h_pad = 8;
  int body_top = win->y + win_border + win_title_h;
  int inner_w = win->w - win_border * 2;
  int body_h = win->h - win_border - win_title_h - win_border;
  int last_focused_idx = -1;

  while (any_key_pressed())
    msleep(20);

  int redraw = 1;
  for (;;) {
    if (redraw) {
      if (win->on_tick)
        win->on_tick(win);

      gfx_fillrect(win->x + 3, win->y + 3, win->w, win->h,
                   g_default_theme.border_dark);
      gfx_borderrect(win->x, win->y, win->w, win->h, wTheme->border_light,
                     wTheme->border_dark);
      gfx_fillrect(win->x + win_border, win->y + win_border, inner_w,
                   win_title_h, wTheme->title_bg);

      if (win->title) {
        gfx_drawstr_clipped(
            win->x + win_h_pad,
            win->y + win_border + (win_title_h - GFX_FONT_H) / 2, win->title,
            wTheme->title_fg, wTheme->title_bg, inner_w - win_h_pad * 2);
      }
      gfx_fillrect(win->x + win_border, body_top, inner_w, body_h, wTheme->bg);

      int max_content_h = 0;
      for (int i = 0; i < win->num_children; i++) {
        if (win->children[i]->y + win->children[i]->h > max_content_h)
          max_content_h = win->children[i]->y + win->children[i]->h;
      }
      int max_scroll = max_content_h - body_h;
      if (max_scroll < 0)
        max_scroll = 0;

      if (win->focused_idx >= 0 && win->focused_idx < win->num_children &&
          win->focused_idx != last_focused_idx) {
        GfxWidget *focused = win->children[win->focused_idx];
        int f_abs_y = body_top + focused->y - win->scroll_y;
        if (f_abs_y < body_top)
          win->scroll_y -= (body_top - f_abs_y);
        else if (f_abs_y + focused->h > body_top + body_h)
          win->scroll_y += (f_abs_y + focused->h - (body_top + body_h));
        last_focused_idx = win->focused_idx;
      }
      if (win->scroll_y > max_scroll)
        win->scroll_y = max_scroll;
      if (win->scroll_y < 0)
        win->scroll_y = 0;

      gfx_set_clip(win->x + win_border, body_top, inner_w, body_h);
      for (int i = 0; i < win->num_children; i++) {
        GfxWidget *child = win->children[i];
        int abs_x = win->x + win_border + child->x;
        int abs_y = body_top + child->y - win->scroll_y;
        if (abs_y + child->h < body_top || abs_y > body_top + body_h)
          continue;
        int is_focused = (i == win->focused_idx);
        if (child->draw)
          child->draw(win, child, abs_x, abs_y, is_focused);
      }
      gfx_clear_clip();

      if (max_content_h > body_h) {
        int sb_h = (body_h * body_h) / max_content_h;
        if (sb_h < 10)
          sb_h = 10;
        int sb_y = body_top + ((body_h - sb_h) * win->scroll_y) / max_scroll;
        gfx_fillrect(win->x + win->w - win_border - 4, body_top, 2, body_h,
                     wTheme->item_bg);
        gfx_fillrect(win->x + win->w - win_border - 4, sb_y, 2, sb_h,
                     wTheme->border_light);
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

    redraw = 1;
    int key_handled = 0;
    GfxWidget *focused = win->children[win->focused_idx];

    if (focused && focused->input) {
      int res = focused->input(win, focused, nav);
      if (res == 2)
        return win->focused_idx;
      if (res == 1)
        key_handled = 1;
    }

    if (!key_handled) {
      if (nav == NAV_DOWN || nav == NAV_TAB) {
        int n_idx = win->focused_idx, moved = 0;
        while (n_idx < win->num_children - 1) {
          n_idx++;
          if (win->children[n_idx]->focusable &&
              !win->children[n_idx]->disabled) {
            win->focused_idx = n_idx;
            moved = 1;
            break;
          }
        }
        if (!moved)
          win->scroll_y += 15;
      } else if (nav == NAV_UP) {
        int n_idx = win->focused_idx, moved = 0;
        while (n_idx > 0) {
          n_idx--;
          if (win->children[n_idx]->focusable &&
              !win->children[n_idx]->disabled) {
            win->focused_idx = n_idx;
            moved = 1;
            break;
          }
        }
        if (!moved)
          win->scroll_y -= 15;
      } else if (nav == NAV_LEFT) {
        int n_idx = win->focused_idx;
        while (n_idx > 0) {
          n_idx--;
          if (win->children[n_idx]->focusable &&
              !win->children[n_idx]->disabled) {
            if (abs(win->children[n_idx]->y - focused->y) < 10)
              win->focused_idx = n_idx;
            break;
          }
        }
      } else if (nav == NAV_RIGHT) {
        int n_idx = win->focused_idx;
        while (n_idx < win->num_children - 1) {
          n_idx++;
          if (win->children[n_idx]->focusable &&
              !win->children[n_idx]->disabled) {
            if (abs(win->children[n_idx]->y - focused->y) < 10)
              win->focused_idx = n_idx;
            break;
          }
        }
      } else if (nav == NAV_ESC) {
        while (any_key_pressed())
          msleep(20);
        return -1;
      }
    }
  }
}
