#pragma once
#include <string>
#include <map>
class Preferences {                    // a fake flash store that survives "reboots" within the test process
  static std::map<std::string, std::string> &store() { static std::map<std::string, std::string> m; return m; }
  std::string ns;
public:
  bool begin(const char *n, bool = false) { ns = n; return true; }
  std::string getString(const char *k, const char *def = "") { auto it = store().find(ns + "/" + k); return it == store().end() ? std::string(def) : it->second; }
  size_t putString(const char *k, const std::string &v) { store()[ns + "/" + k] = v; return v.size(); }
  bool getBool(const char *k, bool def) { auto it = store().find(ns + "/" + k); return it == store().end() ? def : it->second == "1"; }
  size_t putBool(const char *k, bool v) { store()[ns + "/" + k] = v ? "1" : "0"; return 1; }
  static void put_raw(const std::string &key, const std::string &v) { store()[key] = v; }
};
