/* Renders the real gauge_app.c on the host and saves what LVGL draws as raw RGB, for viewing. */
#include "lvgl.h"
#include "gauge_config.h"
#include "gauge_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static PageConfig s_cfg[MAX_PAGES];
static float s_val[SIG_COUNT];
static bool s_has[SIG_COUNT];
PageConfig *gauge_config_get_pages(void) { return s_cfg; }
int gauge_config_cells_for_layout(LayoutType l) { switch (l) { case LAYOUT_1CELL: return 1; case LAYOUT_2UP: return 2; case LAYOUT_3UP: return 3; default: return 4; } }
float gauge_config_signal_value(SignalId s) { return s_val[s]; }
bool gauge_config_signal_has_data(SignalId s) { return s_has[s]; }
const char *gauge_config_signal_name(SignalId s) { static const char *n[] = {"None","RPM","Coolant Temp","MAP/Boost","AFR","Oil Pressure","Oil Temp","Fuel Pressure","Ethanol %","Throttle Position","Battery Voltage"}; return n[s]; }
bool sd_logging_is_active(void) { return false; }

static uint8_t fb[466 * 466 * 3];
static lv_color_t dbuf[466 * 466];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static void flush_cb(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p) {
  for (int y = a->y1; y <= a->y2; y++) for (int x = a->x1; x <= a->x2; x++) {
    uint32_t c = lv_color_to32(*p++);
    uint8_t *o = &fb[(y * 466 + x) * 3]; o[0] = (c >> 16) & 0xFF; o[1] = (c >> 8) & 0xFF; o[2] = c & 0xFF;
  }
  lv_disp_flush_ready(d);
}
static void run_ms(int ms) { for (int t = 0; t < ms; t += 5) { lv_tick_inc(5); lv_timer_handler(); } }
static void save(const char *name) { FILE *f = fopen(name, "wb"); fwrite(fb, 1, sizeof(fb), f); fclose(f); }

static void cell(CellConfig *c, SignalId sig, GaugeType t, const char *cap, int min, int max, bool warn, float thr, bool above, bool alert) {
  memset(c, 0, sizeof(*c));
  c->enabled = true; c->signal = sig; c->gauge_type = t; c->min_val = min; c->max_val = max; c->rest_val = min;
  strncpy(c->caption, cap, GAUGE_CAPTION_MAXLEN - 1);
  c->needle_color = 0x4DD0E1; c->tick_color = 0xAAAAAA; c->bg_color = 0x000000;
  c->value_divisor = 1.0f; c->tick_count = 26; c->major_nth = 5; c->refresh_hz = 50; c->value_font_size = 28;
  c->has_warning = warn; c->warning_threshold = thr; c->warning_above = above; c->alert_fullscreen = alert;
#ifdef HAVE_HISTORY
  c->history_sec = 30;
#endif
}

int main(int argc, char **argv) {
  const char *prefix = argc > 1 ? argv[1] : "shot";
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, dbuf, NULL, 466 * 466);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 466; disp_drv.ver_res = 466; disp_drv.flush_cb = flush_cb; disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  memset(s_cfg, 0, sizeof(s_cfg));
  s_cfg[0].enabled = true; s_cfg[0].layout = LAYOUT_1CELL;
  cell(&s_cfg[0].cells[0], SIG_MAP, GAUGE_TYPE_HISTOGRAM, "MAP", 0, 250, false, 0, false, false);
  s_cfg[1].enabled = true; s_cfg[1].layout = LAYOUT_2UP;
  cell(&s_cfg[1].cells[0], SIG_AFR, GAUGE_TYPE_HISTOGRAM, "AFR", 10, 20, true, 15, true, false);
  cell(&s_cfg[1].cells[1], SIG_OILP, GAUGE_TYPE_NEEDLE, "OIL PRESS", 0, 100, true, 40, false, true);
#ifdef HAVE_HISTORY
  s_cfg[1].cells[0].ref_enabled = true; s_cfg[1].cells[0].ref_value = 14.7f;
#endif

  build_gauge_app();
  s_has[SIG_RPM] = true; s_val[SIG_RPM] = 3000; s_has[SIG_OILP] = true; s_val[SIG_OILP] = 60;
  s_has[SIG_MAP] = true; s_has[SIG_AFR] = true;
  for (int t = 0; t < 600; t++) {                       /* 60 simulated seconds, 10 samples/sec */
    double x = t / 10.0;
    s_val[SIG_MAP] = 110 + 90 * sin(x / 6.0) * sin(x / 6.0) + 6 * sin(x * 3.0);
    s_val[SIG_AFR] = 14.7 + 0.9 * sin(x / 2.5) + 0.25 * sin(x * 4.0);
    run_ms(100);
  }
  run_ms(200);
  char name[200];
  snprintf(name, sizeof(name), "%s_map_page.rgb", prefix); save(name);

  g_page_nav_delta = 1; run_ms(200);                   /* page 1: AFR trend + oil needle */
  run_ms(2000);
  snprintf(name, sizeof(name), "%s_afr_page.rgb", prefix); save(name);

  s_has[SIG_OILP] = true; s_val[SIG_OILP] = 22; run_ms(400);   /* oil pressure collapses: alert */
  snprintf(name, sizeof(name), "%s_alert.rgb", prefix); save(name);
  printf("saved\n");
  return 0;
}
