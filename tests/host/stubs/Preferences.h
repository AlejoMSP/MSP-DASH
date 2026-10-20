#pragma once
#include <string>
#include <map>
#include <cstdio>
#include <cstdint>
// Fake NVS. It enforces the REAL ESP-IDF rule - a stored string may not exceed 4000 bytes - and, like
// Arduino's Preferences::putString, reports a rejected write by returning 0. Nothing large belongs here.
class Preferences {
  static std::map<std::string, std::string> &store() { static std::map<std::string, std::string> m; return m; }
  std::string ns;
public:
  bool begin(const char *n, bool = false) { ns = n; return true; }
  void end() {}
  std::string getString(const char *k, const char *def = "") { auto it = store().find(ns + "/" + k); return it == store().end() ? std::string(def) : it->second; }
  size_t putString(const char *k, const std::string &v) {
    if (v.size() > 4000) { printf("   [NVS] putString(%s) REJECTED: %zu bytes > 4000\n", k, v.size()); return 0; }
    store()[ns + "/" + k] = v; return v.size(); }
  bool getBool(const char *k, bool def) { auto it = store().find(ns + "/" + k); return it == store().end() ? def : it->second == "1"; }
  size_t putBool(const char *k, bool v) { store()[ns + "/" + k] = v ? "1" : "0"; return 1; }
  uint8_t getUChar(const char *k, uint8_t def) { auto it = store().find(ns + "/" + k); return it == store().end() ? def : (uint8_t)std::stoi(it->second); }
  size_t putUChar(const char *k, uint8_t v) { store()[ns + "/" + k] = std::to_string(v); return 1; }
  int getInt(const char *k, int def) { auto it = store().find(ns + "/" + k); return it == store().end() ? def : std::stoi(it->second); }
  size_t putInt(const char *k, int v) { store()[ns + "/" + k] = std::to_string(v); return 4; }
  uint16_t getUShort(const char *k, uint16_t def) { auto it = store().find(ns + "/" + k); return it == store().end() ? def : (uint16_t)std::stoi(it->second); }
  size_t putUShort(const char *k, uint16_t v) { store()[ns + "/" + k] = std::to_string(v); return 2; }
  float getFloat(const char *k, float def) { auto it = store().find(ns + "/" + k); return it == store().end() ? def : std::stof(it->second); }
  size_t putFloat(const char *k, float v) { store()[ns + "/" + k] = std::to_string(v); return 4; }
  static void wipeAll() { store().clear(); }   // tests: a board with empty NVS
};
