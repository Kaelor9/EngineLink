// The main web server (port 80), included once from EngineLink.ino after the
// globals it uses. In a header so that Arduino's generated prototypes never
// end up inside the class.
#pragma once

class EngineLinkWebServer : public WebServer {
public:
  explicit EngineLinkWebServer(int port):WebServer(port){}
  uint32_t served=0,reused=0,evicted=0,slowestMs=0;
  uint32_t slowestInteractiveMs=0,slowestTransferMs=0;
  uint32_t worstParseMs=0,worstHandlerMs=0;
  String slowPath="";
  uint8_t openSockets()const{uint8_t n=0;for(const Slot &s:pool)if(s.used)n++;return n;}
  uint32_t listenerRebinds=0;
  // All request handlers run on the main loop, so this is called only
  // between HTTP requests. Clients from the old interface must not be
  // kept after switching between standalone AP and router STA.
  // Call before the Wi-Fi driver stops: received data still waiting in a
  // socket points into the driver's buffers, and reading it after the driver
  // restarted crashes in pbuf_free.
  void closeClients(){
    for(Slot &slot:pool){if(slot.used){slot.c.stop();slot.used=false;}slot.since=0;slot.requests=0;}
    _currentClient=NetworkClient();_currentStatus=HC_NONE;
  }
  void rebindNetwork(){
    for(Slot &slot:pool){
      if(slot.used){slot.c.stop();slot.used=false;}
      slot.since=0;slot.requests=0;
    }
    _currentClient=NetworkClient();
    _currentStatus=HC_NONE;
    WebServer::begin();
    ++listenerRebinds;
  }
  void handleClient() override {
    const uint32_t now=millis();
    for(;;){
      NetworkClient c=_server.accept();
      if(!c)break;
      c.setNoDelay(true);
      elBoostSocket(c.fd(),11488);   // 8 packets in flight instead of 4
      int slot=-1,oldest=0;
      for(int i=0;i<HTTP_POOL;i++){
        if(!pool[i].used){slot=i;break;}
        if((int32_t)(pool[oldest].since-pool[i].since)>0)oldest=i;
      }
      if(slot<0){slot=oldest;pool[slot].c.stop();++evicted;}
      pool[slot].c=c;pool[slot].since=now;pool[slot].used=true;pool[slot].requests=0;
    }
    for(Slot &s:pool){
      if(!s.used||s.c.available())continue;
      if(!s.c.connected()||now-s.since>HTTP_IDLE_MS){s.c.stop();s.used=false;}
    }
    for(int k=0;k<HTTP_POOL;k++){
      const int i=(nextSlot+k)%HTTP_POOL;
      if(pool[i].used&&pool[i].c.available()){nextSlot=(i+1)%HTTP_POOL;serve(pool[i]);break;}
    }
  }
private:
  struct Slot{NetworkClient c;uint32_t since=0;uint16_t requests=0;bool used=false;};
  Slot pool[HTTP_POOL];
  int nextSlot=0;
  bool wrote=false,keepAlive=false,rewriteHeader=false;
  void serve(Slot &s){
    const uint32_t t0=millis();
    lastHttpMs=t0;
    _currentClient=s.c;
    _currentClient.setTimeout(HTTP_IO_TIMEOUT_MS);
    _currentStatus=HC_WAIT_READ;_statusChange=t0;
    wrote=false;keepAlive=false;
    bool largeTransfer=false;
    netWaitBegin();const bool parsed=_parseRequest(_currentClient);netWaitEnd();
    const uint32_t parsedAt=millis();
    if(parsedAt-t0>worstParseMs)worstParseMs=parsedAt-t0;
    if(parsed){
      _contentLength=CONTENT_LENGTH_NOT_SET;_responseCode=0;_clearResponseHeaders();
      // Only HTTP/1.1 frames every response (length or chunks) for reuse.
      keepAlive=_currentVersion==1&&!header("Connection").equalsIgnoreCase("close");
      rewriteHeader=keepAlive;
      // Separate intentional large downloads from ordinary UI latency.
      // A slow export should not disguise a Wi-Fi/API responsiveness fault.
      const String &path=uri();
      largeTransfer=(path=="/api/speedtest"||path=="/api/log"||path=="/api/segments.csv"||path.indexOf("/download")>=0);
      const uint32_t handleStart=millis();
      httpBusySince=handleStart?handleStart:1;
      _handleRequest();
      httpBusySince=0;
      const uint32_t handlerMs=millis()-handleStart;
      if(handlerMs>worstHandlerMs)worstHandlerMs=handlerMs;
      ++served;if(s.requests)++reused;
      if(s.requests<65535)s.requests++;
    }
    rewriteHeader=false;
    const bool keep=keepAlive&&wrote&&_currentClient.connected();
    _currentClient=NetworkClient();
    _currentStatus=HC_NONE;_currentUpload.reset();_currentRaw.reset();
    if(keep)s.since=millis();else{s.c.stop();s.used=false;}
    const uint32_t ms=millis()-t0;
    if(ms>slowestMs)slowestMs=ms;
    if(!largeTransfer && parsed && ms>slowestInteractiveMs)
      slowPath=uri(); // the actual worst interactive request, not the latest
    uint32_t &bucket=largeTransfer?slowestTransferMs:slowestInteractiveMs;
    if(ms>bucket)bucket=ms;
  }
protected:
  // The response header is written in one piece. WebServer always says
  // "Connection: close"; for a keep-alive request that line is swapped.
  size_t _currentClientWrite(const char *b,size_t l) override {
    wrote=true;
    if(rewriteHeader&&l>8&&memcmp(b,"HTTP/1.",7)==0){
      rewriteHeader=false;
      String h(b,(unsigned int)l);
      if(h.indexOf("\r\nConnection: close\r\n")<0){keepAlive=false;return netWrite(b,l);}
      h.replace("\r\nConnection: close\r\n","\r\nConnection: keep-alive\r\nKeep-Alive: timeout=2\r\n");
      return netWrite(h.c_str(),h.length())==h.length()?l:0;
    }
    return netWrite(b,l);
  }
  size_t _currentClientWrite_P(PGM_P b,size_t l) override {wrote=true;netWaitBegin();const size_t r=_currentClient.write_P(b,l);netWaitEnd();return r;}
  size_t netWrite(const char *b,size_t l){netWaitBegin();const size_t r=_currentClient.write(b,l);netWaitEnd();return r;}
};
