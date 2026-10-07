#pragma once
#include <cstddef>
extern bool g_ffat_mounts;                       // host tests flip this to simulate a partition that will not mount
struct FFatClass {
  bool begin(bool formatOnFail, const char *basePath) { (void)formatOnFail; (void)basePath; return g_ffat_mounts; }
  size_t freeBytes() { return 9000000; }
  size_t totalBytes() { return 10000000; }
};
extern FFatClass FFat;
