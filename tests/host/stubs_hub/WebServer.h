#pragma once
#include <Arduino.h>
#include <functional>
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload { HTTPUploadStatus status; String filename; String name; String type; size_t totalSize; size_t currentSize; uint8_t buf[2048]; };
class WebServer {
public:
  WebServer(int) {}
  void on(const char *, HTTPMethod, std::function<void()>) {}
  void on(const char *, HTTPMethod, std::function<void()>, std::function<void()>) {}
  void onNotFound(std::function<void()>) {}
  void send(int, const char *, const char *) {}
  void send(int, const char *, const String &) {}
  void send(int) {}
  void send_P(int, const char *, const char *) {}
  void send_P(int, const char *, const char *, size_t) {}
  void sendHeader(const char *, const char *, bool = false) {}
  void sendHeader(const String &, const String &, bool = false) {}
  void setContentLength(size_t) {}
  void sendContent(const String &) {}
  void sendContent(const char *, size_t) {}
  String arg(const char *) { return ""; }
  String arg(int) { return ""; }
  String argName(int) { return ""; }
  int args() { return 0; }
  bool hasArg(const char *) { return false; }
  String uri() { return ""; }
  HTTPMethod method() { return HTTP_GET; }
  HTTPUpload &upload() { static HTTPUpload u; return u; }
  void begin() {}
  void handleClient() {}
};
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
