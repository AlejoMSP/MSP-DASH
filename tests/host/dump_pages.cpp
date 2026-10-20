// Includes web_monitor.cpp itself so the pages are assembled by the real C++ compiler, then prints each one.
#include "web_monitor.cpp"
#include <cstdio>
int main(int argc, char **argv) {
  struct { const char *name; const char *html; } pages[] = {
    {"INDEX_HTML", INDEX_HTML}, {"GAUGES_HTML", GAUGES_HTML},
    {"SD_HTML", SD_HTML}, {"OTA_HTML", OTA_HTML}, {"LOGCHANS_HTML", LOGCHANS_HTML}, {"SIM_HTML", SIM_HTML}, {"SETTINGS_HTML", SETTINGS_HTML} };
  for (auto &p : pages) { char fn[64]; snprintf(fn, sizeof(fn), "compiler_%s.html", p.name); FILE *f = fopen(fn, "wb"); fputs(p.html, f); fclose(f); }
  return 0;
}
