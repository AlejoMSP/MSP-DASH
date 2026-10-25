// color_presets.h - the 10 gauge colour sets, shared by the screen firmware (the hub's "Color scheme" button
// action sends a preset number) and the gauge editor (web_monitor.cpp keeps the same list in JavaScript; a test
// checks they match). Each: needle, ticks/dial, background, 0xRRGGBB.
#pragma once
#include <stdint.h>
#define COLOR_PRESET_COUNT 10
#define COLOR_PRESET_CYCLE 255   // button action value: step to the next preset instead of a fixed one
typedef struct { const char *name; uint32_t needle, tick, bg; } ColorPreset;
static const ColorPreset COLOR_PRESETS_C[COLOR_PRESET_COUNT] = {
  {"Classic: white on black, red needle", 0xFF3B30, 0xFFFFFF, 0x000000},
  {"Racing orange",                       0xFF8C00, 0xE8E8E8, 0x000000},
  {"Night amber (easy on the eyes)",      0xFF5A00, 0xFFB000, 0x000000},
  {"Arctic blue",                         0x00B4FF, 0xCFE8FF, 0x06121F},
  {"Neon: cyan ticks, magenta needle",    0xFF2E88, 0x7DF9FF, 0x000000},
  {"Teal dial, coral needle",             0xFF6B3D, 0x66FFD9, 0x001414},
  {"Motorsport yellow needle",            0xFFE600, 0xFFFFFF, 0x000000},
  {"Vintage: cream dial, red needle",     0xD7262E, 0xF2E6C9, 0x1C1A14},
  {"Carbon grey",                         0xFF1744, 0xF5F5F5, 0x1E1E1E},
  {"Green phosphor",                      0xFFFFFF, 0x39FF14, 0x000000},
};
