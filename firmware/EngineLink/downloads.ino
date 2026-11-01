// Streaming downloads: chunked, optionally deflate-compressed responses
// that let the engine task run while the phone reads.

// ---------- Downloads: large pieces, compressed, engines kept running ----------
// A download is served in one go, so without care EngineLink does nothing
// else meanwhile and a weak link makes it slow. Three things help:
//  * replies go out in 8 kB pieces instead of row by row,
//  * when the browser accepts it the reply is deflate-compressed (the
//    compressor is in the chip's ROM): CSV shrinks 5-8 times, so a weak link
//    carries that much more,
//  * between pieces the engine work goes on (frames, read requests, trip,
//    diagnosis, LED), so reading the engines no longer pauses for a download.
// The last download is timed, so Diagnostics can say whether the SD card or
// the network took the time.
// Engine work between pieces of a download (never the web server itself).
void downloadBreath(){
  if(engineTaskOn)return;  // the engine task runs while a download waits for the network
  static uint32_t last=0;static bool busy=false;
  if(busy||(uint32_t)(millis()-last)<40)return;
  busy=true;last=millis();
  Frame f;int handled=0;
  while(framesQueue&&handled<64&&xQueueReceive(framesQueue,&f,0)==pdTRUE){processFrame(f);handled++;}
  serviceKad();serviceEngines();serviceSD();updateLED();
  busy=false;
}
static mz_bool zPut(const void *p,int len,void *user){((ChunkOut*)user)->send((const char*)p,(size_t)len);return MZ_TRUE;}
bool acceptsDeflate(){
  String a=web.header("Accept-Encoding");a.toLowerCase();
  return a.indexOf("deflate")>=0;
}
// Starts a streamed download. filename may be null (shown in the browser).
ChunkOut &beginDownload(const char *what,const char *type,const char *filename,bool compress){
  dlStat=DlStat();strlcpy(dlStat.what,what,sizeof(dlStat.what));dlStat.active=true;dlStat.ms=millis();
  dlStat.ap=web.client().localIP()==WiFi.softAPIP();
  dlStat.rssi=(!dlStat.ap&&WiFi.status()==WL_CONNECTED)?WiFi.RSSI():0;
  if(compress&&acceptsDeflate()){
    if(!zComp)zComp=(tdefl_compressor*)heap_caps_malloc(sizeof(tdefl_compressor),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    compress=zComp!=nullptr;
  }else compress=false;
  if(filename){String d="attachment; filename=";d+=filename;web.sendHeader("Content-Disposition",d);}
  if(compress){web.sendHeader("Content-Encoding","deflate");web.sendHeader("Vary","Accept-Encoding");}
  web.sendHeader("Cache-Control","no-store");
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200,type,"");
  ChunkOut &out=chunkOut;out.n=0;out.z=compress;dlStat.z=compress;
  // zlib wrapper (what "deflate" means to browsers); greedy parsing with few
  // probes is fast and still finds the repeats in CSV rows.
  if(compress)tdefl_init(zComp,zPut,&out,TDEFL_WRITE_ZLIB_HEADER|TDEFL_GREEDY_PARSING_FLAG|24);
  return out;
}
void endDownload(ChunkOut &out){out.finish();dlStat.ms=millis()-dlStat.ms;dlStat.active=false;}
// A file from the SD card, read in blocks.
void streamFileOut(File &f,const char *what,const char *filename){
  ChunkOut &out=beginDownload(what,"text/csv",filename);
  for(;;){
    const uint32_t t=millis();const int got=f.read(rawIn,sizeof(rawIn));dlStat.sdMs+=millis()-t;
    if(got<=0)break;
    out.add(rawIn,(size_t)got);
  }
  endDownload(out);
}
