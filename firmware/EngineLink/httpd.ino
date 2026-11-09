// The web server: ESP-IDF's esp_http_server in its own task (plan 6.6), the
// adapter that keeps the handlers' web.arg()/web.send() calls, the
// own-origin security checks (plan 6.9) and the worker for long transfers.
// Sessions, keep-alive, timeouts, LRU purge, chunking, routing and query
// parsing are the server's; this file only maps calls onto its API.
HttpdStats httpdStats;
static WebCtx srvCtx, xferCtx;
static TaskHandle_t xferTask = nullptr;
static QueueHandle_t xferQ = nullptr;
static volatile bool xferBusy = false, xferAbort = false;
static volatile int xferFd = -1;   // the socket a long transfer is using
static volatile uint8_t sendsOffLock = 0;   // replies sending with engineMx let go (an SD file may be open: sdUnmountBlocked)
struct XferJob { httpd_req_t *r; WebFn fn; uint8_t flags; };
static const WebRoute optionsRoute = {"/*", HTTP_OPTIONS, nullptr, R_NOLOCK};

// The request of the task that calls: the server task or the xfer task.
static WebCtx &cx() { return (xferTask && xTaskGetCurrentTaskHandle() == xferTask) ? xferCtx : srvCtx; }
bool webXferBusy() { return xferBusy; }
bool webSendInFlight() { return sendsOffLock != 0; }
void webXferAbort() { if (xferBusy) xferAbort = true; }   // the transfer stops at its next piece
static void count503(uint32_t &cause, httpd_req_t *r) {   // by cause, for the report (plan 13.6.1)
  ++httpdStats.busy503; ++cause; strlcpy(httpdStats.busyPath, r->uri, sizeof(httpdStats.busyPath)); httpdStats.busyAtMs = millis();
}
// Bytes of stack never used so far (report, plan 6.6 "Memory").
uint32_t webStackLeft(bool xfer) {
  const TaskHandle_t t = xfer ? xferTask : xTaskGetHandle("httpd");
  return t ? (uint32_t)uxTaskGetStackHighWaterMark(t) : 0;
}

// engineMx: held while a handler reads or changes shared data, released
// whenever the server waits for the network (plan 6.6 "Locking").
static bool ctxLock(WebCtx &c, uint32_t ms) {
  if (!engineMx || c.locked) return true;
  if (xSemaphoreTake(engineMx, pdMS_TO_TICKS(ms)) != pdTRUE) return false;
  c.locked = true; return true;
}
static void ctxUnlock(WebCtx &c) { if (c.locked) { c.locked = false; xSemaphoreGive(engineMx); } }

static const char *reasonPhrase(int code) {
  switch (code) {
    case 200: return "OK"; case 204: return "No Content"; case 304: return "Not Modified";
    case 400: return "Bad Request"; case 403: return "Forbidden"; case 404: return "Not Found";
    case 409: return "Conflict"; case 413: return "Payload Too Large"; case 421: return "Misdirected Request";
    case 503: return "Service Unavailable"; default: return code < 400 ? "OK" : "Error";
  }
}
// Status, type and headers go to the server once, before the first byte.
static void prepare(WebCtx &c) {
  if (c.started) return;
  c.started = true;
  snprintf(c.status, sizeof(c.status), "%d %s", c.code, reasonPhrase(c.code));
  httpd_resp_set_status(c.r, c.status);
  httpd_resp_set_type(c.r, c.type);
  for (uint8_t i = 0; i < c.nHdr; i++) httpd_resp_set_hdr(c.r, c.hName[i].c_str(), c.hVal[i].c_str());
  if (c.origin[0]) { httpd_resp_set_hdr(c.r, "Access-Control-Allow-Origin", c.origin); httpd_resp_set_hdr(c.r, "Vary", "Origin"); }
}
// One server send, with engineMx released while the phone is slow.
static esp_err_t io(WebCtx &c, const char *p, size_t n, bool chunk) {
  if (&c == &xferCtx && xferAbort) c.failed = true;
  if (c.failed) return ESP_FAIL;
  const bool had = c.locked;
  if (had) sendsOffLock = sendsOffLock + 1;   // changed only while holding engineMx
  ctxUnlock(c);
  const esp_err_t e = chunk ? httpd_resp_send_chunk(c.r, p, n) : httpd_resp_send(c.r, p, n);
  if (had && xSemaphoreTake(engineMx, portMAX_DELAY) == pdTRUE) { c.locked = true; if (sendsOffLock) sendsOffLock = sendsOffLock - 1; }
  if (e != ESP_OK) { c.failed = true; ++httpdStats.sendFails; }
  return e;
}
static void resetCtx(WebCtx &c, httpd_req_t *r) {
  c.r = r; c.locked = c.chunked = c.started = c.failed = c.ended = c.close = c.onAp = c.external = false;
  c.code = 200; strcpy(c.type, "text/plain"); c.origin[0] = 0;
  for (uint8_t i = 0; i < c.nHdr; i++) { c.hName[i] = String(); c.hVal[i] = String(); }
  c.nHdr = 0; c.query[0] = 0; if (c.body) c.body[0] = 0;
}
static uint32_t ip4Of(const struct sockaddr_storage &a) {
  if (a.ss_family == AF_INET) return ((const struct sockaddr_in &)a).sin_addr.s_addr;
  if (a.ss_family == AF_INET6) return ((const struct sockaddr_in6 &)a).sin6_addr.un.u32_addr[3];  // IPv4-mapped
  return 0;
}
// Which network the client came in on, and whether it is the board itself.
static void whoIsAsking(WebCtx &c) {
  const int fd = httpd_req_to_sockfd(c.r);
  struct sockaddr_storage a = {}, b = {}; socklen_t al = sizeof(a), bl = sizeof(b);
  if (fd < 0 || getsockname(fd, (struct sockaddr *)&a, &al) != 0 || getpeername(fd, (struct sockaddr *)&b, &bl) != 0) return;
  c.onAp = ip4Of(a) == lwip_htonl(0xC0A80401);       // 192.168.4.1
  c.external = ip4Of(b) != lwip_htonl(0x7F000001);   // not 127.0.0.1
}
// End of a handler: close the chunk stream, count, release the lock.
static esp_err_t finish(WebCtx &c, uint32_t t0) {
  if (!c.started && !c.failed) { c.code = 500; prepare(c); io(c, "No reply", 8, false); }
  else if (c.chunked && !c.ended && !c.failed) io(c, nullptr, 0, true);
  if (!c.failed && c.code >= 200 && c.code < 300 && c.external) ++httpdStats.external2xx;
  ctxUnlock(c);
  const uint32_t ms = millis() - t0;
  if (ms > httpdStats.slowMs) { httpdStats.slowMs = ms; strlcpy(httpdStats.slowPath, c.r->uri, sizeof(httpdStats.slowPath)); }
  return (c.failed || c.close) ? ESP_FAIL : ESP_OK;   // ESP_FAIL: the server closes the socket, nothing is drained
}
static esp_err_t answer(WebCtx &c, int code, const char *msg) {
  if (c.r->content_len > 16384) c.close = true;   // an unread large body is not drained (plan 12.1 I4)
  web.send(code, "text/plain", msg);
  return finish(c, millis());
}

static void xferWorker(void *) {
  XferJob j;
  for (;;) {
    if (xQueueReceive(xferQ, &j, portMAX_DELAY) != pdTRUE) continue;
    WebCtx &x = xferCtx;
    const uint32_t t0 = millis();
    if ((j.flags & R_NOLOCK) || ctxLock(x, 10000)) { httpdStats.xferWaitMs = millis() - t0; j.fn(); }
    else { count503(httpdStats.busyXferLock, j.r); x.code = 503; web.send(503, "text/plain", "EngineLink is busy; try again"); }
    const int fd = httpd_req_to_sockfd(j.r);
    // A broken reply: end the connection before the server watches it again.
    if (finish(x, t0) != ESP_OK && fd >= 0) shutdown(fd, SHUT_RDWR);
    httpd_req_async_handler_complete(j.r);   // on every path, or the server stops accepting (esp_http_server.h)
    xferFd = -1; xferBusy = false; xferAbort = false;
  }
}

static esp_err_t webDispatch(httpd_req_t *r) {
  const WebRoute &rt = *(const WebRoute *)r->user_ctx;
  WebCtx &c = srvCtx;
  resetCtx(c, r);
  ++httpdStats.requests;
  const uint32_t t0 = millis();
  char hv[96];
  // Plan 6.9 item 3: only our own names and addresses (defeats DNS rebinding).
  const String rip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
  if (httpd_req_get_hdr_value_str(r, "Host", hv, sizeof(hv)) != ESP_OK) hv[0] = 0;
  if (!httputil::hostAllowed(hv, cfg.hostName.c_str(), rip.c_str())) { ++httpdStats.rejected; return answer(c, 421, "Use http://enginelink.local or EngineLink's IP address"); }
  // Item 1: CORS only for our own origins.
  if (httpd_req_get_hdr_value_str(r, "Origin", hv, sizeof(hv)) == ESP_OK && httputil::originAllowed(hv, cfg.hostName.c_str(), rip.c_str()))
    strlcpy(c.origin, hv, sizeof(c.origin));
  if (r->method == HTTP_OPTIONS) {
    if (!c.origin[0]) { ++httpdStats.rejected; return answer(c, 403, "Origin not allowed"); }
    web.sendHeader("Access-Control-Allow-Methods", "GET, POST");
    web.sendHeader("Access-Control-Allow-Headers", "X-EngineLink, Content-Type");
    web.sendHeader("Access-Control-Max-Age", "600");
    return answer(c, 204, "");
  }
  // Item 2: a state change needs a header no cross-site page can add.
  if (r->method == HTTP_POST && (httpd_req_get_hdr_value_str(r, "X-EngineLink", hv, sizeof(hv)) != ESP_OK || strcmp(hv, "1") != 0)) {
    ++httpdStats.rejected; c.close = r->content_len > 16384;
    return answer(c, 403, "Missing X-EngineLink header");
  }
  if (httpd_req_get_url_query_str(r, c.query, WEB_QUERY) != ESP_OK) c.query[0] = 0;
  if (r->method == HTTP_POST && !(rt.flags & R_RAWBODY)) {
    const size_t cap = (rt.flags & R_BIGBODY) ? WEB_BIG_BODY : WEB_BODY;
    if (r->content_len > cap) { ++httpdStats.rejected; c.close = r->content_len > 16384; return answer(c, 413, "Request too large"); }
    size_t got = 0; uint8_t waits = 0;
    while (got < r->content_len) {
      const int n = httpd_req_recv(r, c.body + got, r->content_len - got);
      if (n == HTTPD_SOCK_ERR_TIMEOUT && ++waits < 2) continue;
      if (n <= 0) return ESP_FAIL;
      got += n;
    }
    c.body[got] = 0;
  }
  whoIsAsking(c);
  if (rt.flags & R_ASYNC) {
    httpd_req_t *copy = nullptr;
    if (xferBusy || httpd_req_async_handler_begin(r, &copy) != ESP_OK) { count503(httpdStats.busyXfer, r); return answer(c, 503, "Another download is running; try again when it has finished"); }
    WebCtx &x = xferCtx;
    resetCtx(x, copy);
    strlcpy(x.query, c.query, WEB_QUERY); strlcpy(x.origin, c.origin, sizeof(x.origin));
    x.onAp = c.onAp; x.external = c.external;
    xferFd = httpd_req_to_sockfd(copy); xferBusy = true;
    const XferJob j = {copy, rt.fn, rt.flags};
    if (xQueueSend(xferQ, &j, 0) != pdTRUE) { xferBusy = false; httpd_req_async_handler_complete(copy); count503(httpdStats.busyXfer, r); return answer(c, 503, "Busy"); }
    ++httpdStats.async;
    return ESP_OK;
  }
  if (!(rt.flags & R_NOLOCK) && !ctxLock(c, 2000)) { count503(httpdStats.busyLock, r); return answer(c, 503, "EngineLink is busy; try again"); }
  rt.fn();
  return finish(c, t0);
}
static esp_err_t web404(httpd_req_t *r, httpd_err_code_t) {
  httpd_resp_set_status(r, "404 Not Found");
  httpd_resp_set_type(r, "text/plain");
  return httpd_resp_send(r, "Not found", HTTPD_RESP_USE_STRLEN);
}
static esp_err_t webOpen(httpd_handle_t, int fd) {
  int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  ++httpdStats.opens; if (++httpdStats.openNow > httpdStats.openMax) httpdStats.openMax = httpdStats.openNow;
  return ESP_OK;
}
static void webClose(httpd_handle_t, int fd) {
  ++httpdStats.closes; if (httpdStats.openNow) --httpdStats.openNow;
  close(fd);   // with a close_fn the server leaves the close to it
}

// Closes every session before the Wi-Fi driver stops (plan 6.2): data still
// waiting in a socket points into the driver's buffers. Bounded wait.
void httpdSuspend() {
  if (!web.handle) return;
  xferAbort = xferBusy;   // a transfer stops at its next piece
  size_t n = 8; int fds[8];
  // shutdown() ends every connection at once, also one a transfer is
  // blocked on or one that sent nothing yet; the server then closes them.
  if (httpd_get_client_list(web.handle, &n, fds) == ESP_OK)
    for (size_t i = 0; i < n; i++) { shutdown(fds[i], SHUT_RDWR); if (fds[i] != xferFd) httpd_sess_trigger_close(web.handle, fds[i]); }
  netWaitBegin();
  for (uint8_t i = 0; i < 30; i++) { n = 8; if (httpd_get_client_list(web.handle, &n, fds) != ESP_OK || !n) break; delay(10); }
  netWaitEnd();
}

void Web::on(const char *uri, httpd_method_t m, WebFn fn, uint8_t flags) {
  if (nRoutes < WEB_MAX_ROUTES) routes[nRoutes++] = {uri, m, fn, flags};
}
bool Web::begin() {
  for (WebCtx *c : {&srvCtx, &xferCtx}) {
    c->bodyCap = c == &srvCtx ? WEB_BIG_BODY : 0;
    c->query = (char *)heap_caps_calloc(1, WEB_QUERY, MALLOC_CAP_SPIRAM);
    c->val = (char *)heap_caps_calloc(1, WEB_BIG_BODY + 1, MALLOC_CAP_SPIRAM);
    if (c->bodyCap) c->body = (char *)heap_caps_calloc(1, c->bodyCap + 1, MALLOC_CAP_SPIRAM);
    if (!c->query || !c->val || (c->bodyCap && !c->body)) return false;
  }
  xferQ = xQueueCreate(1, sizeof(XferJob));
  if (!xferQ || xTaskCreatePinnedToCore(xferWorker, "xfer", 8192, nullptr, tskIDLE_PRIORITY + 4, &xferTask, 1) != pdPASS) return false;
  httpd_config_t h = HTTPD_DEFAULT_CONFIG();
  h.server_port = 80; h.core_id = 1; h.task_priority = tskIDLE_PRIORITY + 5; h.stack_size = 8192;
  h.max_uri_handlers = WEB_MAX_ROUTES; h.max_open_sockets = 7; h.max_resp_headers = WEB_MAX_HDRS + 2;
  h.lru_purge_enable = true; h.recv_wait_timeout = 5; h.send_wait_timeout = 5;
  h.keep_alive_enable = true; h.keep_alive_idle = 5; h.keep_alive_interval = 5; h.keep_alive_count = 3;
  h.uri_match_fn = httpd_uri_match_wildcard; h.open_fn = webOpen; h.close_fn = webClose;
  if (httpd_start(&handle, &h) != ESP_OK) { handle = nullptr; return false; }
  for (uint8_t i = 0; i < nRoutes; i++) {
    httpd_uri_t u = {}; u.uri = routes[i].uri; u.method = routes[i].method; u.handler = webDispatch; u.user_ctx = (void *)&routes[i];
    httpd_register_uri_handler(handle, &u);
  }
  httpd_uri_t o = {}; o.uri = optionsRoute.uri; o.method = HTTP_OPTIONS; o.handler = webDispatch; o.user_ctx = (void *)&optionsRoute;
  httpd_register_uri_handler(handle, &o);
  httpd_register_err_handler(handle, HTTPD_404_NOT_FOUND, web404);
  return true;
}
// A query value, then a form-body value, URL-decoded (httputil.h).
static bool findArg(WebCtx &c, const String &k, String *out) {
  for (const char *src : {(const char *)c.query, (const char *)c.body}) {
    if (!src || !src[0]) continue;
    const esp_err_t e = httpd_query_key_value(src, k.c_str(), c.val, WEB_BIG_BODY + 1);
    if (e != ESP_OK && e != ESP_ERR_HTTPD_RESULT_TRUNC) continue;
    if (out) { const size_t n = httputil::urlDecode(c.val, strlen(c.val), c.val, WEB_BIG_BODY + 1); *out = String(c.val, n); }
    return true;
  }
  return false;
}
String Web::arg(const String &k) { String v; findArg(cx(), k, &v); return v; }
bool Web::hasArg(const String &k) { return findArg(cx(), k, nullptr); }
String Web::header(const char *name) {
  WebCtx &c = cx();
  const size_t n = httpd_req_get_hdr_value_len(c.r, name);
  if (!n) return String();
  String s; s.reserve(n + 1);
  char *b = (char *)malloc(n + 1);
  if (b && httpd_req_get_hdr_value_str(c.r, name, b, n + 1) == ESP_OK) s = b;
  free(b);
  return s;
}
void Web::sendHeader(const char *name, const String &value) {
  WebCtx &c = cx();
  if (c.nHdr < WEB_MAX_HDRS) { c.hName[c.nHdr] = name; c.hVal[c.nHdr] = value; ++c.nHdr; }
}
void Web::setContentLength(size_t len) { if (len == CONTENT_LENGTH_UNKNOWN) cx().chunked = true; }
void Web::send_P(int code, const char *type, const char *data, size_t len) {
  WebCtx &c = cx();
  if (c.started && !c.chunked) return;
  c.code = code; strlcpy(c.type, type, sizeof(c.type));
  prepare(c);
  if (c.chunked) { if (len) io(c, data, len, true); }
  else io(c, data, len, false);
}
void Web::send(int code, const char *type, const String &s) { send_P(code, type, s.c_str(), s.length()); }
void Web::sendContent(const char *p, size_t len) {
  WebCtx &c = cx();
  if (!c.started) { c.chunked = true; prepare(c); }
  if (!c.chunked || c.ended) return;
  if (!len) { c.ended = true; io(c, nullptr, 0, true); return; }
  io(c, p, len, true);
}
void Web::sendContent(const String &s) { sendContent(s.c_str(), s.length()); }
int Web::recv(char *buf, size_t len) {
  WebCtx &c = cx();
  const bool had = c.locked;
  ctxUnlock(c);
  const int n = httpd_req_recv(c.r, buf, len);
  if (had && xSemaphoreTake(engineMx, portMAX_DELAY) == pdTRUE) c.locked = true;
  return n;
}
size_t Web::contentLength() { return cx().r->content_len; }
bool Web::clientIsOnAp() { return cx().onAp; }
bool Web::sendFailed() { return cx().failed; }
void Web::closeAfter() { cx().close = true; }
