#pragma once
#include "Arduino.h"
enum WebRequestMethod { HTTP_GET = 1, HTTP_POST = 2 };
struct AsyncWebParameter { String value() const { return ""; } String name() const { return ""; } };
struct AsyncWebServerResponse { void addHeader(const char *, const String &) {} };
class AsyncWebServerRequest {
public:
  void *_tempObject = nullptr;
  void send(int, const char *, const String &) {}
  void send(int, const String &, const String &) {}
  void send(AsyncWebServerResponse *) {}
  void send_P(int, const char *, const char *) {}
  void send_P(int, const String &, const char *) {}
  AsyncWebServerResponse *beginResponse(int, const char *, const String &) { return nullptr; }
  AsyncWebServerResponse *beginResponse(const char *, size_t, std::function<size_t(uint8_t *, size_t, size_t)>) { return nullptr; }
  AsyncWebServerResponse *beginResponse(int, const String &, const String &) { return nullptr; }
  bool hasParam(const char *, bool = false) const { return false; }
  bool hasParam(const String &, bool = false) const { return false; }
  AsyncWebParameter *getParam(const char *, bool = false) const { return nullptr; }
  AsyncWebParameter *getParam(const String &, bool = false) const { return nullptr; }
  String url() const { return ""; }
};
typedef std::function<void(AsyncWebServerRequest *)> ArRequestHandlerFunction;
typedef std::function<void(AsyncWebServerRequest *, String, size_t, uint8_t *, size_t, bool)> ArUploadHandlerFunction;
typedef std::function<void(AsyncWebServerRequest *, uint8_t *, size_t, size_t, size_t)> ArBodyHandlerFunction;
class AsyncWebHandler {};
class AsyncWebSocket : public AsyncWebHandler { public: AsyncWebSocket(const char *) {} void textAll(const String &) {} void textAll(const char *) {} size_t count() { return 0; } void cleanupClients() {} };
class AsyncWebServer {
public:
  AsyncWebServer(int) {}
  void on(const char *, int, ArRequestHandlerFunction) {}
  void on(const char *, int, ArRequestHandlerFunction, ArUploadHandlerFunction) {}
  void on(const char *, int, ArRequestHandlerFunction, ArUploadHandlerFunction, ArBodyHandlerFunction) {}
  void on(const char *, int, ArRequestHandlerFunction, std::nullptr_t, ArBodyHandlerFunction) {}
  void addHandler(AsyncWebHandler *) {}
  void begin() {}
  void onNotFound(ArRequestHandlerFunction) {}
};
