// lcd_perf.h - display performance numbers, measured in the LVGL task (lcd_bsp.c), updated once a second.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t lcd_perf_fps(void);        // frames sent to the panel in the last second
uint32_t lcd_perf_busy_pct(void);   // share of the last second LVGL spent drawing (lv_timer_handler), 0-100
void lcd_perf_overlay(bool on);     // small "fps / busy" label in the top corner of the screen
bool lcd_perf_overlay_on(void);
void lcd_set_brightness_pct(int pct);  // 5-100 %; applied by the LVGL task between frames (it shares the SPI bus)
int lcd_get_brightness_pct(void);
#ifdef __cplusplus
}
#endif
