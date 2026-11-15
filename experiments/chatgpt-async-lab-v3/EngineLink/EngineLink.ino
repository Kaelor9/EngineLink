/*
 * EngineLink ASYNC LAB v3 — non-production, independent firmware.
 * ESP32-S3-WROOM-1-N8R8, Arduino-ESP32 3.3.12.
 * ESP32Async/ESPAsyncWebServer 3.12.1 + AsyncTCP 3.5.0.
 *
 * Never configure engine/CAN pins. Radio + async HTTP/WebSocket only.
 * The original web UI (ui.h) is a visual reference, not the backend.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_system.h>
#include "lab_ui.h"
#include "ui.h"

#define LAB_VERSION "0.0.3-chatgpt-async-lab"
static const char *AP_NAME="EngineLink-ChatGPT";
static const char *AP_PASS="12345678";
static constexpr size_t OTA_MIN=100000,OTA_MAX=3200000,OTA_HEADER=0x70;
static AsyncWebServer web(80);
static AsyncWebSocket ws("/ws");
static char routerName[33]={},routerPassword[65]={};
static volatile uint32_t apJoins=0,apLeaves=0,routerJoins=0,routerLeaves=0,dhcpCount=0,wsJoins=0,wsLeaves=0;
static volatile uint8_t lastStaReason=0;
static volatile uint32_t httpRequests=0,httpErrors=0,speedRequests=0,liveRequests=0;
static uint64_t speedTotalBytes=0;
static uint32_t lastSpeedKb=0;
static uint32_t startMs=0,routerRetryMs=0,rebootMs=0,lastOtaActivity=0;
static bool apStarted=false,httpStarted=false,mdnsStarted=false,bootVerified=false;
static uint32_t telemetryFrames=0,lastTelemetryMs=0,successfulStatus=0;
static String otaStatus="none";
struct OtaSession {
  AsyncWebServerRequest *owner=nullptr;
  size_t expected=0,processed=0,headCount=0;
  uint8_t head[OTA_HEADER]={};
  bool begun=false,done=false,failed=false;
  char error[112]={};
};
static OtaSession ota;
static void resetOta(){ota=OtaSession();}
static void failOta(const char *why){
  ota.failed=true;strlcpy(ota.error,why,sizeof(ota.error));
  if(ota.begun)Update.abort();
  ota.begun=false;
}
static String jsonString(const String &str){
  String out="\"";
  for(size_t i=0;i<str.length();++i){
    const char c=str[i];
    if(c=='"'||c=='\\'){out+='\\';out+=c;}
    else if((uint8_t)c<32)out+=' ';
    else out+=c;
  }
  return out+'"';
}
static String valueOrNull(int value,bool valid){
  return valid?String(value):String("null");
}
static String report(){
  uint8_t primary=0;
  wifi_second_chan_t secondary=WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&primary,&secondary);
  int8_t txQuarter=0;
  esp_wifi_get_max_tx_power(&txQuarter);
  wifi_sta_list_t stations={};
  bool staListOk=esp_wifi_ap_get_sta_list(&stations)==ESP_OK;
  bool routerOnline=WiFi.status()==WL_CONNECTED;
  String s;s.reserve(2700);
  s="{\"version\":\"" LAB_VERSION "\",\"experimental\":true,\"server\":\"ESPAsyncWebServer\"";
  s+=",\"uptimeMs\":";s+=(uint32_t)(millis()-startMs);
  s+=",\"network\":{\"mode\":\"AP+STA\",\"ap\":{\"ssid\":";
  s+=jsonString(AP_NAME);
  s+=",\"ip\":";s+=jsonString(WiFi.softAPIP().toString());
  s+=",\"channel\":";s+=(unsigned)primary;
  s+=",\"clients\":";s+=WiFi.softAPgetStationNum();
  s+=",\"clientRssi\":";s+=valueOrNull(stations.sta[0].rssi,staListOk&&stations.num>0);
  s+=",\"joins\":";s+=apJoins;
  s+=",\"leaves\":";s+=apLeaves;
  s+=",\"running\":";s+=apStarted?"true":"false";s+="}";
  s+=",\"sta\":{\"configured\":";s+=routerName[0]?"true":"false";
  s+=",\"ssid\":";s+=jsonString(routerName);
  s+=",\"connected\":";s+=routerOnline?"true":"false";
  s+=",\"ip\":";s+=jsonString(routerOnline?WiFi.localIP().toString():"");
  s+=",\"rssi\":";s+=routerOnline?String(WiFi.RSSI()):"null";
  s+=",\"joins\":";s+=routerJoins;
  s+=",\"leaves\":";s+=routerLeaves;
  s+=",\"dhcp\":";s+=dhcpCount;
  s+=",\"lastDisconnectReason\":";s+=(unsigned)lastStaReason;s+="}";
  s+=",\"mdns\":\"enginelink-chatgpt.local\",\"mdnsInitialized\":";
  s+=mdnsStarted?"true":"false";s+="}";
  s+=",\"hardware\":{\"chip\":\"ESP32-S3\",\"cpuMHz\":";s+=ESP.getCpuFreqMHz();
  s+=",\"flashBytes\":";s+=ESP.getFlashChipSize();
  s+=",\"psramBytes\":";s+=psramFound()?ESP.getPsramSize():0;
  s+=",\"freePsram\":";s+=psramFound()?ESP.getFreePsram():0;
  s+=",\"freeHeap\":";s+=ESP.getFreeHeap();
  s+=",\"minHeap\":";s+=ESP.getMinFreeHeap();
  s+=",\"dmaHeap\":";s+=heap_caps_get_free_size(MALLOC_CAP_DMA);
  s+=",\"txPowerQuarterDbm\":";s+=(int)txQuarter;
  s+=",\"resetReason\":";s+=(int)esp_reset_reason();s+="}";
  s+=",\"http\":{\"started\":";s+=httpStarted?"true":"false";
  s+=",\"requests\":";s+=httpRequests;
  s+=",\"errors\":";s+=httpErrors;
  s+=",\"speedTests\":";s+=speedRequests;
  s+=",\"bytesScheduled\":";s+=(unsigned long long)speedTotalBytes;
  s+=",\"lastSpeedKib\":";s+=lastSpeedKb;s+="}";
  s+=",\"websocket\":{\"path\":\"/ws\",\"clients\":";s+=ws.count();
  s+=",\"joins\":";s+=wsJoins;
  s+=",\"leaves\":";s+=wsLeaves;
  s+=",\"frames\":";s+=telemetryFrames;
  s+=",\"targetIntervalMs\":1000}";
  s+=",\"ota\":{\"enabled\":true,\"mode\":\"raw app-only\",\"lastResult\":";
  s+=jsonString(otaStatus);
  s+=",\"pending\":";s+=ota.owner?"true":"false";s+="}";
  s+=",\"engines\":{\"available\":false,\"reason\":\"Wi-Fi-only isolation: no engine pins initialized\"}";
  s+=",\"nmea\":{\"available\":false,\"reason\":\"Wi-Fi-only isolation: CAN disabled\"}";
  s+="}";
  return s;
}
static void wifiEvent(WiFiEvent_t e,WiFiEventInfo_t info){
  switch(e){
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: ++apJoins;break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: ++apLeaves;break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: ++routerJoins;break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: ++routerLeaves;lastStaReason=info.wifi_sta_disconnected.reason;break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: ++dhcpCount;break;
    default:break;
  }
}
static void startNetwork(){
  WiFi.persistent(false);
  WiFi.onEvent(wifiEvent);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  apStarted=WiFi.softAP(AP_NAME,AP_PASS,1,false,4);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_bandwidth(WIFI_IF_AP,WIFI_BW_HT20);
  if(routerName[0])WiFi.begin(routerName,routerPassword);
  routerRetryMs=millis();
  mdnsStarted=MDNS.begin("enginelink-chatgpt");
}
static void sendJson(AsyncWebServerRequest *req,const String &body,int code=200){
  auto *res=req->beginResponse(code,"application/json; charset=utf-8",body);
  res->addHeader("Cache-Control","no-store");
  req->send(res);
}
static void sendResult(AsyncWebServerRequest *req,bool ok,const String &message,int code=200){
  if(code>=400)++httpErrors;
  sendJson(req,String("{\"ok\":")+(ok?"true":"false")+",\"message\":"+jsonString(message)+"}",code);
}
static String queryParam(AsyncWebServerRequest *req,const char *name){
  if(!req->hasParam(name,true))return "";
  return req->getParam(name,true)->value();
}
static bool validOtaHeader(const uint8_t *head){
  if(head[0]!=0xE9||memcmp(head+0x20,"\x32\x54\xCD\xAB",4)!=0)return false;
  // Project name may be Arduino's generic arduino-lib-builder, not proof of provenance.
  return strncmp((const char*)(head+0x50),esp_app_get_description()->project_name,32)==0;
}
// Called from AsyncTCP callback context. Keep normal HTTP handlers minimal;
// OTA necessarily writes flash and can cause a temporary network stall.
static void otaBody(AsyncWebServerRequest *req,uint8_t *data,size_t len,size_t index,size_t total){
  if(index==0){
    if(ota.owner){
      // A second writer is never allowed to touch the first writer's session.
      return;
    }
    resetOta();ota.owner=req;ota.expected=total;
    lastOtaActivity=millis();otaStatus="upload in progress";
    if(!req->hasHeader("X-EngineLink-Test") ||
       req->header("X-EngineLink-Test")!="CONFIRM-OTA"){
      failOta("Missing explicit OTA confirmation");return;
    }
    if(total<OTA_MIN||total>OTA_MAX){failOta("Firmware app size invalid");return;}
    Update.runAsync(true);
  }
  if(req!=ota.owner)return;
  lastOtaActivity=millis();
  if(ota.failed)return;
  if(index!=ota.processed||index+len>ota.expected){failOta("Out of sequence OTA data");return;}
  size_t n=0;
  while(n<len&&ota.headCount<OTA_HEADER)ota.head[ota.headCount++]=data[n++];
  if(ota.headCount==OTA_HEADER&&!ota.begun){
    if(!validOtaHeader(ota.head)){failOta("Not a compatible ESP32 application image");return;}
    if(!Update.begin(ota.expected,U_FLASH)){failOta("Could not allocate OTA partition");return;}
    ota.begun=true;
    if(Update.write(ota.head,OTA_HEADER)!=OTA_HEADER){failOta("OTA flash header write failed");return;}
  }
  if(ota.begun&&n<len&&Update.write(data+n,len-n)!=len-n){
    failOta("OTA flash write failed");return;
  }
  ota.processed+=len;
  if(ota.processed==ota.expected){
    if(!ota.begun||!Update.end(false)){failOta("OTA image verification failed");return;}
    ota.done=true;otaStatus="image validated; reboot scheduled";
    rebootMs=millis()+2200;
  }
}
static void otaResponse(AsyncWebServerRequest *req){
  ++httpRequests;
  if(ota.owner!=req){sendResult(req,false,"Another OTA upload is running",409);return;}
  if(ota.failed||!ota.done){
    const String why=ota.error[0]?String(ota.error):String("OTA upload incomplete");
    sendResult(req,false,why,400);
    resetOta();otaStatus=why;return;
  }
  sendResult(req,true,"OTA app installed. Reboot scheduled. AP should be EngineLink-ChatGPT");
  // Do not reset state until reboot; no second flash should be accepted.
}
static void prepareServer(){
  ws.onEvent([](AsyncWebSocket *server,AsyncWebSocketClient *client,AwsEventType type,void *arg,uint8_t *data,size_t len){
    (void)server;(void)client;(void)arg;(void)data;(void)len;
    if(type==WS_EVT_CONNECT)++wsJoins;
    else if(type==WS_EVT_DISCONNECT)++wsLeaves;
  });
  web.addHandler(&ws);
  auto sendPage=[](AsyncWebServerRequest *req){
    ++httpRequests;
    auto *res=req->beginResponse_P(200,"text/html; charset=utf-8",WIFI_LAB_HTML);
    res->addHeader("Cache-Control","no-store");req->send(res);
  };
  web.on("/",HTTP_GET,sendPage);
  web.on("/lab",HTTP_GET,sendPage);
  web.on("/original",HTTP_GET,[](AsyncWebServerRequest *req){
    ++httpRequests;
    auto *res=req->beginResponse_P(200,"text/html; charset=utf-8",ENGINE_UI_GZ,ENGINE_UI_GZ_LEN);
    res->addHeader("Content-Encoding","gzip");
    res->addHeader("Cache-Control","no-store");req->send(res);
  });
  auto status=[](AsyncWebServerRequest *req){
    ++httpRequests;++successfulStatus;
    sendJson(req,report());
  };
  web.on("/lab/status",HTTP_GET,status);
  web.on("/api/state",HTTP_GET,status);
  web.on("/api/live",HTTP_GET,status);
  web.on("/lab/report",HTTP_GET,[](AsyncWebServerRequest *req){
    ++httpRequests;++successfulStatus;
    auto *res=req->beginResponse(200,"application/json; charset=utf-8",report());
    res->addHeader("Content-Disposition","attachment; filename=EngineLink-ASYNC-LAB-v3-report.json");
    res->addHeader("Cache-Control","no-store");req->send(res);
  });
  web.on("/api/rtt",HTTP_GET,[](AsyncWebServerRequest *req){
    ++httpRequests;
    auto *res=req->beginResponse(204);
    res->addHeader("Cache-Control","no-store");req->send(res);
  });
  web.on("/api/speedtest",HTTP_GET,[](AsyncWebServerRequest *req){
    ++httpRequests;++speedRequests;
    int kb=req->hasParam("kb")?req->getParam("kb")->value().toInt():512;
    if(kb!=64&&kb!=512&&kb!=2048){sendResult(req,false,"Use kb=64, 512 or 2048",400);return;}
    const size_t total=(size_t)kb*1024;
    // Chunk callback is called as TCP send buffers become available.
    auto *res=req->beginChunkedResponse("application/octet-stream",
      [total](uint8_t *buf,size_t maxLen,size_t index)->size_t{
        if(index>=total)return 0;
        const size_t n=min(maxLen,total-index);
        memset(buf,0x54,n);return n;
      });
    res->addHeader("Cache-Control","no-store");
    lastSpeedKb=kb;speedTotalBytes+=total;
    req->send(res);
  });
  web.on("/lab/router",HTTP_POST,[](AsyncWebServerRequest *req){
    ++httpRequests;
    String ssid=queryParam(req,"ssid"),pass=queryParam(req,"password");
    if(!ssid.length()||ssid.length()>32||pass.length()>64){
      sendResult(req,false,"Invalid SSID or password length",400);return;
    }
    Preferences p;
    if(!p.begin("cgpt_wifi",false)){sendResult(req,false,"Cannot access settings",500);return;}
    bool ok=p.putString("ssid",ssid)>0;
    if(pass.length())ok=ok&&p.putString("pass",pass)>0;
    else {p.putString("pass","");}
    p.end();
    if(!ok){sendResult(req,false,"Failed to save router configuration",500);return;}
    strlcpy(routerName,ssid.c_str(),sizeof(routerName));
    strlcpy(routerPassword,pass.c_str(),sizeof(routerPassword));
    WiFi.begin(routerName,routerPassword);routerRetryMs=millis();
    sendResult(req,true,"Router connection started; independent test AP stays active");
  });
  web.on("/lab/forget",HTTP_POST,[](AsyncWebServerRequest *req){
    ++httpRequests;
    Preferences p;if(p.begin("cgpt_wifi",false)){p.clear();p.end();}
    routerName[0]=routerPassword[0]=0;
    WiFi.disconnect(false,false);
    sendResult(req,true,"Router credentials cleared");
  });
  web.on("/lab/ota",HTTP_POST,otaResponse,nullptr,otaBody);
  web.on("/api/update",HTTP_POST,otaResponse,nullptr,otaBody);
  web.onNotFound([](AsyncWebServerRequest *req){sendResult(req,false,"Endpoint not implemented in isolated Wi-Fi lab",404);});
  web.begin();httpStarted=true;
}
static bool needsRollback(){
  const esp_partition_t *part=esp_ota_get_running_partition();
  esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;
  return part&&esp_ota_get_state_partition(part,&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY;
}
void setup(){
  Serial.begin(115200);Serial.setTxTimeoutMs(0);
  startMs=millis();delay(1000);
  Preferences p;
  if(p.begin("cgpt_wifi",true)){
    String n=p.getString("ssid",""),pw=p.getString("pass","");
    p.end();
    strlcpy(routerName,n.c_str(),sizeof(routerName));
    strlcpy(routerPassword,pw.c_str(),sizeof(routerPassword));
  }
  startNetwork();prepareServer();
  Serial.printf("EngineLink ASYNC LAB v3: AP %s IP %s HTTP %d\n",
    AP_NAME,WiFi.softAPIP().toString().c_str(),httpStarted);
}
void loop(){
  const uint32_t now=millis();
  // AsyncTCP callbacks handle requests; loop only publishes telemetry.
  if(now-lastTelemetryMs>=1000){
    lastTelemetryMs=now;
    ws.cleanupClients(3);
    if(ws.count()>0){ws.textAll(report());++telemetryFrames;}
  }
  if(routerName[0]&&WiFi.status()!=WL_CONNECTED &&
     WiFi.softAPgetStationNum()==0 && (uint32_t)(now-routerRetryMs)>30000){
    WiFi.begin(routerName,routerPassword);routerRetryMs=now;
  }
  if(ota.owner&&!ota.done&&now-lastOtaActivity>60000){
    // Only a timed-out upload is aborted. Do not reboot or overwrite live slot.
    if(ota.begun)Update.abort();
    otaStatus="OTA transfer timeout";
    resetOta();
  }
  if(!bootVerified&&(uint32_t)(now-startMs)>60000 &&
     httpStarted&&apStarted&&(successfulStatus>=2 ||
        (successfulStatus>=1&&wsJoins>0&&telemetryFrames>=30))){
    if(needsRollback())esp_ota_mark_app_valid_cancel_rollback();
    bootVerified=true;
  }
  if(!bootVerified&&(uint32_t)(now-startMs)>180000&&needsRollback()){
    Serial.println("ASYNC LAB v3 health failed; attempting OTA rollback");
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
  if(rebootMs&&(int32_t)(now-rebootMs)>=0)ESP.restart();
  delay(10);
}
