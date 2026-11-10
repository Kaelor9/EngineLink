// Streaming downloads: chunked, uncompressed responses (plan D26) sent from
// the xfer task (httpd.ino), so the main loop and the engine task keep
// running while the phone reads.
//
// Replies go out in 8 kB pieces instead of row by row. Every export keeps
// its route, content, columns and file name. The last download is timed, so
// Diagnostics can say whether the SD card or the network took the time.

// Starts a streamed download. filename may be null (shown in the browser).
ChunkOut &beginDownload(const char *what,const char *type,const char *filename){
  dlStat=DlStat();strlcpy(dlStat.what,what,sizeof(dlStat.what));dlStat.active=true;dlStat.ms=millis();
  dlStat.ap=web.clientIsOnAp();
  dlStat.rssi=(!dlStat.ap&&WiFi.status()==WL_CONNECTED)?WiFi.RSSI():0;
  if(filename){String d="attachment; filename=";d+=filename;web.sendHeader("Content-Disposition",d);}
  web.sendHeader("Cache-Control","no-store");
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200,type,"");
  ChunkOut &out=chunkOut;out.n=0;
  return out;
}
void endDownload(ChunkOut &out){out.finish();dlStat.ms=millis()-dlStat.ms;dlStat.active=false;}
// A file from the SD card, read in blocks.
void streamFileOut(File &f,const char *what,const char *filename){
  ChunkOut &out=beginDownload(what,"text/csv",filename);
  for(;;){
    const uint32_t t=millis();const int got=f.read(rawIn,sizeof(rawIn));dlStat.sdMs+=millis()-t;
    if(got<=0||web.sendFailed())break;
    out.add(rawIn,(size_t)got);
  }
  endDownload(out);
}
