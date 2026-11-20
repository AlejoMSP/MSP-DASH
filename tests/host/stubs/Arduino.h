#pragma once
#include <string>
#include <functional>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#define PROGMEM
#define pdMS_TO_TICKS(x) (x)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
// minimal Arduino String: std::string plus the few extras the sketch uses
class String : public std::string {
public:
  String() {}
  String(const char *s) : std::string(s ? s : "") {}
  String(const std::string &s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  String(unsigned v) : std::string(std::to_string(v)) {}
  String(long v) : std::string(std::to_string(v)) {}
  String(unsigned long v) : std::string(std::to_string(v)) {}
  String(float v) : std::string(std::to_string(v)) {}
  String(double v) : std::string(std::to_string(v)) {}
  void trim() { size_t a = find_first_not_of(" \t\r\n"); if (a == npos) { clear(); return; } erase(0, a); erase(find_last_not_of(" \t\r\n") + 1); }
  long toInt() const { return atol(c_str()); }
  double toFloat() const { return atof(c_str()); }
  unsigned char concat(const char *s, unsigned n) { append(s, n); return 1; }
  unsigned char concat(const char *s) { append(s); return 1; }
  unsigned char concat(char c) { push_back(c); return 1; }
  unsigned char reserve(unsigned n) { std::string::reserve(n); return 1; }
  String operator+(const String &o) const { return String(std::string(*this) + std::string(o)); }
  String operator+(const char *o) const { return String(std::string(*this) + o); }
  String &operator+=(const String &o) { append(o); return *this; }
  String &operator+=(const char *o) { append(o); return *this; }
  bool startsWith(const String &p) const { return rfind(p, 0) == 0; }
  void replace(const char *from, const char *to) { size_t n = strlen(from), m = strlen(to), i = 0; if (!n) return; while ((i = find(from, i)) != npos) { std::string::replace(i, n, to); i += m; } }
  int indexOf(char c) const { size_t i = find(c); return i == npos ? -1 : (int)i; }
  String substring(unsigned a, unsigned b = 0xFFFFFFFF) const { return String(substr(a, b == 0xFFFFFFFF ? npos : b - a)); }
};
inline String operator+(const char *a, const String &b) { return String(std::string(a) + std::string(b)); }
struct SerialClass { int available() { return 0; } int read() { return -1; } template <typename... A> int printf(const char *f, A... a) { return ::printf(f, a...); }
  void println(const char *s = "") {} void print(const char *s) {} void print(int) {} void begin(int) {} void println(const String &) {} template <typename T> void println(const T &, int = 0) {} void println(int) {} };
extern SerialClass Serial;
struct EspClass { void restart() {} uint32_t getFreePsram() { return 0; } uint32_t getFreeHeap() { return 0; } uint32_t getMinFreeHeap() { return 0; } } extern ESP;
inline float temperatureRead() { return 40; }
inline uint32_t getCpuFrequencyMhz() { return 240; }
uint32_t millis(); void delay(uint32_t);
static const int HIGH = 1, LOW = 0;

#define INPUT_PULLUP 2
void pinMode(int, int); int digitalRead(int);
typedef void (*TaskFunction_t)(void *); typedef void *TaskHandle_t;
int xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, int, TaskHandle_t *, int);
void vTaskDelay(int);
