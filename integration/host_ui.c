/*
 * Host stand-ins for the screen and browser that settings.c's preferences
 * window uses, so the real settings code links on the build machine.  The
 * integration test only loads and saves settings; none of these is called.
 */
#include <stdlib.h>

#include "browser.h"
#include "gfx.h"

GfxTheme g_default_theme;

void gfx_window_alert(const char *title, const char **lines, int nlines,
                      const char *ok_label) {
  (void)title; (void)lines; (void)nlines; (void)ok_label;
}

int gfx_window_confirm3(const char *title, const char **body, int nbody,
                        const char *btn0, const char *btn1, const char *btn2) {
  (void)title; (void)body; (void)nbody; (void)btn0; (void)btn1; (void)btn2;
  return 2;
}

int gfx_window_exec(GfxWindow *win) {
  (void)win;
  return -1;
}

int browser_pick_file(const char *start_dir, int filter_asm, char *out,
                      int outsz) {
  (void)start_dir; (void)filter_asm; (void)out; (void)outsz;
  return 0;
}

static GfxWidget *stub_widget(void) { return calloc(1, sizeof(GfxWidget)); }
GfxWidget *widget_create_label(int x, int y, int w, int h, const char *t) {
  (void)x; (void)y; (void)w; (void)h; (void)t;
  return stub_widget();
}
GfxWidget *widget_create_heading(int x, int y, int w, int h, const char *t) {
  (void)x; (void)y; (void)w; (void)h; (void)t;
  return stub_widget();
}
GfxWidget *widget_create_toggle(int x, int y, int w, int h, int *v) {
  (void)x; (void)y; (void)w; (void)h; (void)v;
  return stub_widget();
}
GfxWidget *widget_create_number(int x, int y, int w, int h, int *v, int lo,
                                int hi) {
  (void)x; (void)y; (void)w; (void)h; (void)v; (void)lo; (void)hi;
  return stub_widget();
}
GfxWidget *widget_create_dropdown(int x, int y, int w, int h, int *v,
                                  const char **o, int n) {
  (void)x; (void)y; (void)w; (void)h; (void)v; (void)o; (void)n;
  return stub_widget();
}
GfxWidget *widget_create_text(int x, int y, int w, int h, char *b, int m,
                              const char *p) {
  (void)x; (void)y; (void)w; (void)h; (void)b; (void)m; (void)p;
  return stub_widget();
}
GfxWidget *widget_create_button(int x, int y, int w, int h, const char *l) {
  (void)x; (void)y; (void)w; (void)h; (void)l;
  return stub_widget();
}
