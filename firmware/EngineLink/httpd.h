// The handler-facing web object (docs/WIFI_REWRITE_PLAN.md 6.6). A thin
// adapter over esp_http_server: the handlers keep calling web.arg(),
// web.send(), ... and each call maps to one server API. Declarations only,
// in a header so Arduino's generated prototypes and the .ino files that come
// before httpd.ino see them; the code is in httpd.ino.
#pragma once
#include <esp_http_server.h>
#include <WString.h>

// WebServer's name for "length unknown, send chunks" (setContentLength).
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)

typedef void (*WebFn)();
// Route flags.
constexpr uint8_t R_ASYNC = 1;    // long transfer: runs in the xfer task (6.6)
constexpr uint8_t R_NOLOCK = 2;   // touches no shared data: no engineMx
constexpr uint8_t R_RAWBODY = 4;  // the handler reads the body itself (firmware upload)
constexpr uint8_t R_BIGBODY = 8;  // form body up to WEB_BIG_BODY (app preferences)
constexpr size_t WEB_BODY = 2048;       // plan 6.6: form bodies <= 2 KB
constexpr size_t WEB_BIG_BODY = 12288;  // /api/prefs: 4000 chars of JSON, URL-encoded (plan section 12)
constexpr size_t WEB_QUERY = 512;       // CONFIG_HTTPD_MAX_URI_LEN
constexpr uint8_t WEB_MAX_ROUTES = 96;
constexpr uint8_t WEB_MAX_HDRS = 8;

struct WebRoute { const char *uri; httpd_method_t method; WebFn fn; uint8_t flags; };

// One request being answered: the server task has one, the xfer task one.
struct WebCtx {
  httpd_req_t *r = nullptr;
  bool locked = false, chunked = false, started = false, failed = false, ended = false, close = false;
  bool onAp = false, external = false;   // came in on 192.168.4.1; not the board's own loopback
  int code = 200;
  char status[40] = {}, type[64] = {}, origin[72] = {};
  String hName[WEB_MAX_HDRS], hVal[WEB_MAX_HDRS];
  uint8_t nHdr = 0;
  char *query = nullptr, *body = nullptr, *val = nullptr;  // PSRAM buffers
  size_t bodyCap = 0;
};

// Counters for the report (plan 6.8 HTTP block).
struct HttpdStats {
  uint32_t requests = 0, async = 0, busy503 = 0, rejected = 0, sendFails = 0;
  uint32_t opens = 0, closes = 0, openNow = 0, openMax = 0, external2xx = 0, xferWaitMs = 0;
  uint32_t slowMs = 0; char slowPath[40] = {};
};

class Web {
public:
  void on(const char *uri, httpd_method_t m, WebFn fn, uint8_t flags = 0);
  bool begin();
  String arg(const String &name);
  bool hasArg(const String &name);
  String header(const char *name);
  void sendHeader(const char *name, const String &value);
  void setContentLength(size_t len);
  void send(int code, const char *type, const String &content);
  void send_P(int code, const char *type, const char *data, size_t len);
  void sendContent(const String &s);
  void sendContent(const char *p, size_t len);
  int recv(char *buf, size_t len);       // raw body (R_RAWBODY), engineMx released meanwhile
  size_t contentLength();
  bool clientIsOnAp();
  bool sendFailed();
  void closeAfter();                     // close the connection after this reply (no body drain)
  httpd_handle_t handle = nullptr;
  WebRoute routes[WEB_MAX_ROUTES];
  uint8_t nRoutes = 0;
};
extern Web web;
extern HttpdStats httpdStats;
