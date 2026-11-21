// Settings backup (settings_backup.h): hex, one line's check, a whole restore's check (nothing written if one line is
// wrong), and that the hub's copy matches the screens'. Build: g++ -std=c++17 -I../../esp32s3-amoled-bsp-base test_settings_backup.cpp
#include "settings_backup.h"
#include <stdio.h>
#include <fstream>
#include <sstream>
#include <string>
static int pass = 0, fail = 0;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("  FAIL: %s (line %d)\n", m, __LINE__); } } while (0)
static std::string slurp(const char *p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
static const char *const NS[] = { "link", "disp", "slots" };
static const char *const FILES[] = { "cfg_display.json" };
static char one(const char *line) { char b[512]; snprintf(b, sizeof b, "%s", line); char *w[5]; return bk_parse(b, w, NS, 3, FILES, 1); }
int main() {
  printf("1. hex\n");
  uint8_t in[] = { 0x00, 0x7f, 0xab, 0xff }, out[8]; char h[16];
  bk_hex(in, 4, h); CHECK(!strcmp(h, "007fabff"), "to hex");
  CHECK(bk_unhex("007FABff", out, 8) == 4 && !memcmp(in, out, 4), "back, any case");
  CHECK(bk_unhex("abc", out, 8) == -1 && bk_unhex("zz", out, 8) == -1 && bk_unhex("00112233", out, 2) == -1, "odd length, not hex, too long: refused");
  printf("2. one line\n");
  CHECK(one("n link name str 4c656674") == 'n', "a string (hex)");
  CHECK(one("n disp diam i32 520") == 'n' && one("n disp sweep u8 1") == 'n' && one("n slots cfg blob 0a0b") == 'n', "numbers and a blob");
  CHECK(one("n wifi ssid str 41") == 0, "a namespace this board does not keep: refused");
  CHECK(one("n disp sweep u8 300") == 0 && one("n disp x i8 -129") == 0 && one("n disp x u64 -1") == 0, "out of range: refused");
  CHECK(one("n disp x i32 12a") == 0 && one("n link name str 4c6") == 0 && one("n link name text 41") == 0, "not a number, odd hex, unknown type: refused");
  CHECK(one("n link name str 41 extra") == 0 && one("n link") == 0, "extra or missing words: refused");
  CHECK(one("n link averyveryverylongkey u8 1") == 0, "a key longer than NVS allows: refused");
  CHECK(one("f cfg_display.json 7b7d") == 'f' && one("f other.json 7b7d") == 0, "the gauge file; no other file");
  printf("3. a whole restore\n");
  CHECK(bk_check("# msp-dash screen Left build 224\nn link name str 4c656674\nn disp diam i32 520\n\nf cfg_display.json 7b7d\n", NS, 3, FILES, 1) == 0, "a good file (comments and blank lines skipped)");
  CHECK(bk_check("n link name str 4c656674\r\nn disp diam i32 520\r\n", NS, 3, FILES, 1) == 0, "Windows line ends");
  CHECK(bk_check("n link name str 4c656674\nn disp diam i32 abc\nn disp sweep u8 1\n", NS, 3, FILES, 1) == 2, "a bad line: its number (2), so nothing is written");
  CHECK(bk_check("hello\n", NS, 3, FILES, 1) == 1, "not a backup at all: line 1");
  printf("4. one file for both boards\n");
  std::string a = slurp("../../esp32s3-amoled-bsp-base/settings_backup.h"), b = slurp("../../esp32-input/settings_backup.h");
  CHECK(a.size() > 1000 && a == b, "the hub's copy matches the screens'");
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
