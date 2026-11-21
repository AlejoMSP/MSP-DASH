// settings_backup.h - settings export / restore and factory reset, for the screens and the hub (factory setup).
// The same file is in esp32-input/ (tests/host/test_settings_backup.cpp checks the two match).
//
// A board's settings are its NVS entries in its own namespaces (Preferences), plus, on a screen, its gauge file.
// Export: one line per entry, everything hex so nothing needs escaping:
//   n <namespace> <key> <type> <value>     type u8 i8 u16 i16 u32 i32 u64 i64 (value in decimal), str / blob (hex)
//   f <file name> <hex>                    a settings file (the screen's cfg_display.json)
// Restore checks every line first and writes nothing if one is wrong; then each namespace in the file is cleared and
// written again (namespaces not in the file are left alone), and the board restarts.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BACKUP_MAX_VALUE 8192     // bytes in one string or blob
enum { BK_U8, BK_I8, BK_U16, BK_I16, BK_U32, BK_I32, BK_U64, BK_I64, BK_STR, BK_BLOB, BK_TYPES };
static const char *const BK_TYPE_NAMES[BK_TYPES] = { "u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64", "str", "blob" };

static inline int bk_type(const char *t) { for (int i = 0; i < BK_TYPES; i++) if (!strcmp(t, BK_TYPE_NAMES[i])) return i; return -1; }
static inline int bk_hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
// Hex text into bytes. Returns the byte count, or -1 if it is not even-length hex or does not fit.
static inline int bk_unhex(const char *h, uint8_t *out, int cap) {
  int n = (int)strlen(h);
  if (n % 2 || n / 2 > cap) return -1;
  for (int i = 0; i < n / 2; i++) { int a = bk_hexval(h[2 * i]), b = bk_hexval(h[2 * i + 1]); if (a < 0 || b < 0) return -1; out[i] = (uint8_t)(a << 4 | b); }
  return n / 2;
}
static inline void bk_hex(const uint8_t *in, int n, char *out) { static const char d[] = "0123456789abcdef"; for (int i = 0; i < n; i++) { out[2 * i] = d[in[i] >> 4]; out[2 * i + 1] = d[in[i] & 15]; } out[2 * n] = 0; }
static inline int bk_in_list(const char *ns, const char *const *list, int n) { for (int i = 0; i < n; i++) if (!strcmp(ns, list[i])) return 1; return 0; }

// One line, split in place into its words. Returns 'n', 'f', or 0 if it is not a valid line for this board.
// For 'n': w[1] namespace, w[2] key, w[3] type, w[4] value. For 'f': w[1] file name, w[2] hex.
static inline char bk_parse(char *line, char *w[5], const char *const *nsList, int nsN, const char *const *files, int filesN) {
  int k = 0; char *p = line;
  while (*p && k < 5) { while (*p == ' ') p++; if (!*p) break; w[k++] = p; while (*p && *p != ' ') p++; if (*p) *p++ = 0; }
  while (*p == ' ') p++;
  if (*p) return 0;   // extra words
  if (k == 3 && !strcmp(w[0], "f")) {
    if (!bk_in_list(w[1], files, filesN)) return 0;
    size_t n = strlen(w[2]); if (n % 2 || n / 2 > 200000) return 0;
    for (size_t i = 0; i < n; i++) if (bk_hexval(w[2][i]) < 0) return 0;
    return 'f';
  }
  if (k != 5 || strcmp(w[0], "n") || !bk_in_list(w[1], nsList, nsN) || !w[2][0] || strlen(w[2]) > 15) return 0;
  int t = bk_type(w[3]);
  if (t < 0) return 0;
  if (t == BK_STR || t == BK_BLOB) {
    size_t n = strlen(w[4]); if (n % 2 || n / 2 > BACKUP_MAX_VALUE) return 0;
    for (size_t i = 0; i < n; i++) if (bk_hexval(w[4][i]) < 0) return 0;
    return 'n';
  }
  char *e; const char *v = w[4];
  if (t == BK_U64) { strtoull(v, &e, 10); return *e || !*v || v[0] == '-' ? 0 : 'n'; }
  long long x = strtoll(v, &e, 10);
  if (*e || !*v) return 0;
  static const long long lo[] = { 0, -128, 0, -32768, 0, -2147483648LL, 0, (long long)(-9223372036854775807LL - 1) };
  static const long long hi[] = { 255, 127, 65535, 32767, 4294967295LL, 2147483647LL, 0, 9223372036854775807LL };
  return x < lo[t] || x > hi[t] ? 0 : 'n';
}
// Every line of a restore checked: returns the line number of the first bad one (1-based), or 0 if all are good.
// "" and lines starting with '#' are skipped. Works on a copy.
static inline int bk_check(const char *text, const char *const *nsList, int nsN, const char *const *files, int filesN) {
  int lineNo = 0; const char *p = text;
  while (*p) {
    const char *e = strchr(p, '\n'); size_t len = e ? (size_t)(e - p) : strlen(p);
    lineNo++;
    if (len && p[0] != '#' && !(len == 1 && p[0] == '\r')) {
      char *c = (char *)malloc(len + 1); if (!c) return lineNo;
      memcpy(c, p, len); c[len] = 0; if (len && c[len - 1] == '\r') c[len - 1] = 0;
      char *w[5]; char r = bk_parse(c, w, nsList, nsN, files, filesN); free(c);
      if (!r) return lineNo;
    }
    p += len; if (*p == '\n') p++;
  }
  return 0;
}

#ifdef ESP_PLATFORM
#include <Arduino.h>
#include "nvs.h"
#include "nvs_flash.h"
// Export the listed namespaces (secret ones only if withSecrets) as lines.
static inline String bk_export(const char *const *nsList, int nsN, const char *const *secret, int secretN, bool withSecrets) {
  String out;
  for (int i = 0; i < nsN; i++) {
    if (!withSecrets && bk_in_list(nsList[i], secret, secretN)) continue;
    nvs_handle_t h; if (nvs_open(nsList[i], NVS_READONLY, &h) != ESP_OK) continue;
    nvs_iterator_t it = NULL;
    esp_err_t r = nvs_entry_find(NVS_DEFAULT_PART_NAME, nsList[i], NVS_TYPE_ANY, &it);
    while (r == ESP_OK) {
      nvs_entry_info_t info; nvs_entry_info(it, &info);
      char line[64]; char num[24] = "";
      int t = -1;
      switch (info.type) {
        case NVS_TYPE_U8:  { uint8_t v;  if (nvs_get_u8(h, info.key, &v) == ESP_OK)  { t = BK_U8;  snprintf(num, sizeof num, "%u", v); } break; }
        case NVS_TYPE_I8:  { int8_t v;   if (nvs_get_i8(h, info.key, &v) == ESP_OK)  { t = BK_I8;  snprintf(num, sizeof num, "%d", v); } break; }
        case NVS_TYPE_U16: { uint16_t v; if (nvs_get_u16(h, info.key, &v) == ESP_OK) { t = BK_U16; snprintf(num, sizeof num, "%u", v); } break; }
        case NVS_TYPE_I16: { int16_t v;  if (nvs_get_i16(h, info.key, &v) == ESP_OK) { t = BK_I16; snprintf(num, sizeof num, "%d", v); } break; }
        case NVS_TYPE_U32: { uint32_t v; if (nvs_get_u32(h, info.key, &v) == ESP_OK) { t = BK_U32; snprintf(num, sizeof num, "%lu", (unsigned long)v); } break; }
        case NVS_TYPE_I32: { int32_t v;  if (nvs_get_i32(h, info.key, &v) == ESP_OK) { t = BK_I32; snprintf(num, sizeof num, "%ld", (long)v); } break; }
        case NVS_TYPE_U64: { uint64_t v; if (nvs_get_u64(h, info.key, &v) == ESP_OK) { t = BK_U64; snprintf(num, sizeof num, "%llu", (unsigned long long)v); } break; }
        case NVS_TYPE_I64: { int64_t v;  if (nvs_get_i64(h, info.key, &v) == ESP_OK) { t = BK_I64; snprintf(num, sizeof num, "%lld", (long long)v); } break; }
        case NVS_TYPE_STR: case NVS_TYPE_BLOB: {
          size_t len = 0; bool str = info.type == NVS_TYPE_STR;
          if ((str ? nvs_get_str(h, info.key, NULL, &len) : nvs_get_blob(h, info.key, NULL, &len)) != ESP_OK || len > BACKUP_MAX_VALUE) break;
          uint8_t *b = (uint8_t *)malloc(len + 1); char *hx = (char *)malloc(2 * len + 1);
          if (b && hx && (str ? nvs_get_str(h, info.key, (char *)b, &len) : nvs_get_blob(h, info.key, b, &len)) == ESP_OK) {
            int n = str ? (int)strlen((char *)b) : (int)len;   // a string without its end byte
            bk_hex(b, n, hx);
            snprintf(line, sizeof line, "n %s %s %s ", nsList[i], info.key, str ? "str" : "blob");
            out += line; out += hx; out += "\n";
          }
          free(b); free(hx);
          break;
        }
        default: break;
      }
      if (t >= 0) { snprintf(line, sizeof line, "n %s %s %s %s\n", nsList[i], info.key, BK_TYPE_NAMES[t], num); out += line; }
      r = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    nvs_close(h);
  }
  return out;
}
// Write a checked restore (bk_check said 0). Namespaces in it are cleared first. File lines go to fileWrite().
static inline bool bk_apply(char *text, const char *const *nsList, int nsN, const char *const *files, int filesN,
                            bool (*fileWrite)(const char *name, const uint8_t *data, size_t len)) {
  // first pass: which namespaces are in the file -> clear them
  for (int i = 0; i < nsN; i++) {
    char tag[24]; snprintf(tag, sizeof tag, "n %s ", nsList[i]);
    if (strstr(text, tag) == text || strstr(text, (String("\n") + tag).c_str())) {
      nvs_handle_t h; if (nvs_open(nsList[i], NVS_READWRITE, &h) == ESP_OK) { nvs_erase_all(h); nvs_commit(h); nvs_close(h); }
    }
  }
  bool ok = true; char *p = text;
  while (*p) {
    char *e = strchr(p, '\n'); if (e) *e = 0;
    size_t len = strlen(p); if (len && p[len - 1] == '\r') p[len - 1] = 0;
    if (*p && *p != '#') {
      char *w[5]; char r = bk_parse(p, w, nsList, nsN, files, filesN);
      if (r == 'f') {
        size_t n = strlen(w[2]) / 2; uint8_t *b = (uint8_t *)malloc(n + 1);
        if (!b || bk_unhex(w[2], b, (int)n) < 0 || !fileWrite || !fileWrite(w[1], b, n)) ok = false;
        free(b);
      } else if (r == 'n') {
        nvs_handle_t h; int t = bk_type(w[3]);
        if (nvs_open(w[1], NVS_READWRITE, &h) != ESP_OK) { ok = false; }
        else {
          esp_err_t er = ESP_OK; const char *v = w[4];
          switch (t) {
            case BK_U8: er = nvs_set_u8(h, w[2], (uint8_t)strtoul(v, 0, 10)); break;
            case BK_I8: er = nvs_set_i8(h, w[2], (int8_t)strtol(v, 0, 10)); break;
            case BK_U16: er = nvs_set_u16(h, w[2], (uint16_t)strtoul(v, 0, 10)); break;
            case BK_I16: er = nvs_set_i16(h, w[2], (int16_t)strtol(v, 0, 10)); break;
            case BK_U32: er = nvs_set_u32(h, w[2], (uint32_t)strtoul(v, 0, 10)); break;
            case BK_I32: er = nvs_set_i32(h, w[2], (int32_t)strtol(v, 0, 10)); break;
            case BK_U64: er = nvs_set_u64(h, w[2], strtoull(v, 0, 10)); break;
            case BK_I64: er = nvs_set_i64(h, w[2], strtoll(v, 0, 10)); break;
            default: {
              size_t n = strlen(v) / 2; uint8_t *b = (uint8_t *)malloc(n + 1);
              if (!b || bk_unhex(v, b, (int)n) < 0) er = ESP_FAIL;
              else { b[n] = 0; er = t == BK_STR ? nvs_set_str(h, w[2], (const char *)b) : nvs_set_blob(h, w[2], b, n); }
              free(b);
            }
          }
          if (er != ESP_OK) ok = false;
          nvs_commit(h); nvs_close(h);
        }
      }
    }
    if (!e) break;
    p = e + 1;
  }
  return ok;
}
// Factory reset: every saved setting gone (the whole NVS, Wi-Fi calibration included; it rebuilds itself).
static inline void bk_factory_wipe(void) { nvs_flash_deinit(); nvs_flash_erase(); nvs_flash_init(); }
#elif __has_include(<Arduino.h>)   // PC compile checks (tests/host stubs): same names, nothing behind them
#include <Arduino.h>
static inline String bk_export(const char *const *, int, const char *const *, int, bool) { return String(""); }
static inline bool bk_apply(char *, const char *const *, int, const char *const *, int, bool (*)(const char *, const uint8_t *, size_t)) { return true; }
static inline void bk_factory_wipe(void) {}
#endif
