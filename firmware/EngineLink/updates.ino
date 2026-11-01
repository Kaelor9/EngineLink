// Firmware updates: from GitHub Releases over the router, or from a file,
// with rollback when a new version does not start properly.

// ---------- Firmware from GitHub ----------
// The worker task fetches the release list or downloads and installs one
// update file. The new image must confirm itself after a healthy minute,
// otherwise the bootloader goes back to the previous version.
void fwWorker(void *){
  for(;;){
    if(fwJob==FW_IDLE){vTaskDelay(pdMS_TO_TICKS(200));continue;}
    const uint8_t job=fwJob;
    NetworkClientSecure client;
    client.useBuiltinCACertBundle();
    HTTPClient http;
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("EngineLink/" EL_VERSION);
    if(job==FW_LIST){
      fwState=1;
      if(!http.begin(client,EL_RELEASES_URL)){fwError="Could not start the request";fwState=9;}
      else{
        const int code=http.GET();
        if(code==200&&http.getSize()<16000){String body=http.getString();fwList=body;fwState=2;}
        else{fwError=String("GitHub answered ")+code;fwState=9;}
        http.end();
      }
    }else if(job==FW_REPORT){
      String body="{\"title\":";
      jsonString(body,reportTitle.length()?reportTitle:String("Device report: ")+reportReason+" (EngineLink " EL_VERSION ")");
      reportTitle="";
      body+=",\"body\":";
      jsonString(body,String("Sent automatically by EngineLink.\n\n```text\n")+reportBody+"\n```\n");
      body+="}";
      reportBody="";
      if(!http.begin(client,"https://api.github.com/repos/Kaelor9/EngineLink/issues"))reportError="Could not start the request";
      else{
        http.addHeader("Authorization","Bearer "+cfg.ghToken);
        http.addHeader("Accept","application/vnd.github+json");
        http.addHeader("Content-Type","application/json");
        const int code=http.POST(body);
        if(code==201){
          const String r=http.getString();
          const int k=r.indexOf("\"number\":");
          reportLastIssue=k>=0?(uint32_t)r.substring(k+9).toInt():0;
          ++reportSent;reportError="";
        }else reportError=code<0?"No internet: EngineLink is not on the router right now":code==401?"GitHub refused the token":code==403||code==404?"The token may not create issues in Kaelor9/EngineLink":String("GitHub answered ")+code;
        http.end();
      }
      reportLastAt=millis()?millis():1;
    }else if(job==FW_INSTALL){
      fwState=3;fwBytes=0;fwTotal=0;
      if(!http.begin(client,fwUrl)){fwError="Could not start the download";fwState=9;}
      else{
        const int code=http.GET();
        const int len=http.getSize();
        if(code!=200||len<100000||len>3200000){fwError=String("Download failed (")+code+")";fwState=9;}
        else{
          fwTotal=len;
          NetworkClient *stream=http.getStreamPtr();
          bool ok=Update.begin(len,U_FLASH);
          if(!ok)fwError=Update.errorString();
          uint8_t buf[2048];OtaImageHeader imageHeader;bool checked=false;uint32_t idle=millis();
          while(ok&&fwBytes<(size_t)len){
            const size_t avail=stream->available();
            if(!avail){
              if(WiFi.status()!=WL_CONNECTED){fwError="Router disconnected during download";ok=false;break;}
              if(millis()-idle>20000){fwError="Download stalled (no data for 20 s)";ok=false;break;}
              vTaskDelay(pdMS_TO_TICKS(5));continue;
            }
            const int got=stream->readBytes(buf,min(avail,sizeof(buf)));
            if(got<=0)continue;
            idle=millis();
            // The first TCP read need not contain the entire ESP32 header.
            if(!checked){
              imageHeader.feed(buf,(size_t)got);
              if(imageHeader.complete()){
                checked=true;
                if(!imageHeader.valid()){fwError="Not an EngineLink update file";ok=false;break;}
              }
            }
            if(Update.write(buf,got)!=(size_t)got){fwError=Update.errorString();ok=false;break;}
            fwBytes+=got;
          }
          if(ok&&!checked){fwError="Firmware header incomplete";ok=false;}
          if(ok&&!Update.end(true)){fwError=Update.errorString();ok=false;}
          if(!ok){Update.abort();fwState=9;}else fwState=4;
        }
        http.end();
      }
    }
    fwJob=FW_IDLE;
  }
}
void handleFwCheck(){
  if(!routerConfigured()||WiFi.status()!=WL_CONNECTED){reply(false,"Connect EngineLink to a router with internet first",409);return;}
  if(fwJob!=FW_IDLE||fwState==3){reply(false,"Busy",409);return;}
  fwError="";fwJob=FW_LIST;
  reply(true,"Checking GitHub");
}
void handleFwInstall(){
  const String url=web.arg("url"),v=web.arg("v");
  if(!url.startsWith(EL_RELEASE_PREFIX)||!url.endsWith(".bin")){reply(false,"Only EngineLink releases from GitHub can be installed",400);return;}
  if(!routerConfigured()||WiFi.status()!=WL_CONNECTED){reply(false,"Connect EngineLink to a router with internet first",409);return;}
  if(calActive){reply(false,"Stop the motor test first",409);return;}
  if(fwJob!=FW_IDLE||fwState==3){reply(false,"Busy",409);return;}
  for(uint8_t c=0;c<2;c++)if(kad[c].polling)stopPolling(c,"Firmware update");
  fwUrl=url;fwTarget=v;fwError="";fwJob=FW_INSTALL;
  reply(true,"Downloading "+v);
}
void handleFwStatus(){
  String s="{\"state\":";s+=(unsigned)fwState;
  s+=",\"bytes\":";s+=(unsigned long)fwBytes;s+=",\"total\":";s+=(unsigned long)fwTotal;
  s+=",\"error\":";jsonString(s,fwError);
  s+=",\"target\":";jsonString(s,fwTarget);
  s+=",\"running\":\"" EL_VERSION "\"";
  s+=",\"pending\":";s+=rollbackPending?"true":"false";
  s+=",\"online\":";s+=(routerConfigured()&&WiFi.status()==WL_CONNECTED)?"true":"false";
  s+=",\"list\":";
  if(fwState==2&&fwList.length()&&fwList[0]=='{')s+=fwList;else s+="null";
  s+='}';
  sendJSON(200,s);
}
