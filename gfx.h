#ifndef GFX_H_INCLUDED
#define GFX_H_INCLUDED

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Screen geometry                                                    */
/* ------------------------------------------------------------------ */
#define GFX_W 320
#define GFX_H 240

/* ------------------------------------------------------------------ */
/* Font geometry                                                      */
/* ------------------------------------------------------------------ */
#define GFX_FONT_W 5
#define GFX_FONT_H 8
#define GFX_FONT_GAP 1
#define GFX_CHAR_W (GFX_FONT_W + GFX_FONT_GAP) /* advance per glyph */

/* ------------------------------------------------------------------ */
/* Colour palette (RGB565)                                            */
/* ------------------------------------------------------------------ */
#define GFX_COL_BLACK 0x0000u
#define GFX_COL_WHITE 0xFFFFu
#define GFX_COL_DARK_BLUE 0x3186u
#define GFX_COL_GREEN 0x03E0u
#define GFX_COL_RED 0xF800u
#define GFX_COL_GREY 0x8C71u
#define GFX_COL_LIGHT_GREY 0xC618u
#define GFX_COL_ORANGE 0xFD20u

/* ------------------------------------------------------------------ */
/* Lifecycle & Primitives                                             */
/* ------------------------------------------------------------------ */
uint16_t *gfx_framebuffer(void);
void gfx_init(void);
void gfx_deinit(void);
void gfx_flip(void);

void gfx_set_clip(int x, int y, int w, int h);
void gfx_clear_clip(void);

void gfx_setpixel(int x, int y, uint16_t col);
void gfx_fillrect(int x, int y, int w, int h, uint16_t col);
void gfx_hline(int x, int y, int len, uint16_t col);
void gfx_vline(int x, int y, int len, uint16_t col);
void gfx_borderrect(int x, int y, int w, int h, uint16_t fill, uint16_t border);

/* ------------------------------------------------------------------ */
/* Text                                                               */
/* ------------------------------------------------------------------ */
int gfx_drawchar(int x, int y, char ch, uint16_t fg, uint16_t bg);
int gfx_drawstr(int x, int y, const char *s, uint16_t fg, uint16_t bg);
int gfx_drawstr_n(int x, int y, const char *s, int n, uint16_t fg, uint16_t bg);
void gfx_drawstr_clipped(int x, int y, const char *s, uint16_t fg, uint16_t bg,
                         int maxw);

/* ------------------------------------------------------------------ */
/* Legacy Utility Windows                                             */
/* ------------------------------------------------------------------ */
void gfx_window_alert(const char *title, const char **lines, int nlines,
                      const char *ok_label);
void gfx_window_scrolltext(const char *title, const char **lines, int nlines,
                           const char *ok_label);
int gfx_window_confirm2(const char *title, const char **body, int nbody,
                        const char *btn0, const char *btn1);
int gfx_window_confirm3(const char *title, const char **body, int nbody,
                        const char *btn0, const char *btn1, const char *btn2);
int gfx_menu(const char *title, const char *subtitle, const char **items,
             int nitems, int initial_sel);
int gfx_input_filename(const char *title, const char *prompt, char *out,
                       int outmax);

/* ------------------------------------------------------------------ */
/* Modular GUI Framework (Object-Oriented Window Manager)             */
/* ------------------------------------------------------------------ */
typedef enum {
  NAV_NONE = 0,
  NAV_UP,
  NAV_DOWN,
  NAV_LEFT,
  NAV_RIGHT,
  NAV_ENTER,
  NAV_ESC,
  NAV_TAB,
  NAV_CAT
} NavAction;

NavAction gfx_poll_nav(void);

typedef struct {
  uint16_t bg;
  uint16_t fg;
  uint16_t border_light;
  uint16_t border_dark;
  uint16_t title_bg;
  uint16_t title_fg;
  uint16_t accent;
  uint16_t accent_text;
  uint16_t item_bg;
} GfxTheme;

extern GfxTheme g_default_theme;

struct GfxWidget;
struct GfxWindow;

typedef void (*GfxDrawCallback)(struct GfxWindow *win, struct GfxWidget *w,
                                int abs_x, int abs_y, int is_focused);
typedef int (*GfxInputCallback)(struct GfxWindow *win, struct GfxWidget *w,
                                NavAction nav);

typedef struct GfxWidget {
  int x, y;
  int w, h;
  GfxTheme *theme;
  void *data;
  void *extra;
  int min_val, max_val;
  int max_len;

  GfxDrawCallback draw;
  GfxInputCallback input;

  int focusable;
  int disabled; /* If 1, renders greyed out and skips input */
} GfxWidget;

typedef struct GfxWindow {
  int x, y, w, h;
  const char *title;
  GfxTheme *theme;
  GfxWidget **children;
  int num_children;
  int focused_idx;
  int scroll_y;

  void (*on_tick)(struct GfxWindow *win); /* Runs once per frame */
  void *user_data;                        /* External context data */
} GfxWindow;

/* Returns -1 if ESC is pressed, or the index of the widget that triggered an
 * action (e.g. Button) */
int gfx_window_exec(GfxWindow *win);

/* Constructors for standard widgets */
GfxWidget *widget_create_label(int x, int y, int w, int h, const char *text);
GfxWidget *widget_create_heading(int x, int y, int w, int h, const char *text);
GfxWidget *widget_create_toggle(int x, int y, int w, int h, int *val_ptr);
GfxWidget *widget_create_number(int x, int y, int w, int h, int *val_ptr,
                                int min_v, int max_v);
GfxWidget *widget_create_dropdown(int x, int y, int w, int h, int *val_ptr,
                                  const char **options, int num_options);
GfxWidget *widget_create_text(int x, int y, int w, int h, char *buf_ptr,
                              int max_len, const char *prompt);
GfxWidget *widget_create_button(int x, int y, int w, int h, const char *label);

#endif /* GFX_H_INCLUDED */
