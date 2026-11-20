// cpu_load.h - about how busy each CPU core was over the last second (Board status page).
// A FreeRTOS idle hook counts, per core, how many times the core went idle (once per tick or interrupt); a core that
// was idle every tick of the second is 0 % busy. Interrupts other than the tick also wake the idle task, so on a busy
// radio this reads a little low: it is an estimate. Use it from ONE source file per board (the counters are static).
// The same file is in esp32-input/ (test_board_status.cpp checks the two match).
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Busy %, from the idle wake-ups counted over `ms` with a `tickHz` FreeRTOS tick. Pure: host-tested.
static inline uint8_t cpu_load_from(uint32_t idleWakes, uint32_t ms, uint32_t tickHz) {
  if (!ms || !tickHz) return 0;
  uint64_t idlePct = (uint64_t)idleWakes * 1000u * 100u / ((uint64_t)ms * tickHz);
  return idlePct >= 100 ? 0 : (uint8_t)(100 - idlePct);
}

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "esp_freertos_hooks.h"
#include "esp_timer.h"
static volatile uint32_t s_cpuIdle[2] = { 0, 0 };
static bool cpu_load_idle0(void) { s_cpuIdle[0]++; return true; }   // true: once per interrupt, then the core sleeps
static bool cpu_load_idle1(void) { s_cpuIdle[1]++; return true; }
static inline void cpu_load_begin(void) {
  esp_register_freertos_idle_hook_for_cpu(cpu_load_idle0, 0);
#if portNUM_PROCESSORS > 1
  esp_register_freertos_idle_hook_for_cpu(cpu_load_idle1, 1);
#endif
}
// Busy % of core 0 and core 1, refreshed at most once a second.
static inline void cpu_load_pct(uint8_t out[2]) {
  static uint32_t lastMs = 0, last[2] = { 0, 0 };
  static uint8_t pct[2] = { 0, 0 };
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
  if (now - lastMs >= 1000) {
    for (int c = 0; c < 2; c++) { uint32_t n = s_cpuIdle[c]; if (lastMs) pct[c] = cpu_load_from(n - last[c], now - lastMs, configTICK_RATE_HZ); last[c] = n; }
    lastMs = now;
  }
  out[0] = pct[0]; out[1] = pct[1];
}
#else   // PC builds (tests): nothing to count
static inline void cpu_load_begin(void) {}
static inline void cpu_load_pct(uint8_t out[2]) { out[0] = out[1] = 0; }
#endif
