/*
 * EngineLink Wi-Fi Lab v2 — original bench-test implementation.
 * Target: ESP32-S3-WROOM-1-N8R8, 8 MiB flash, 8 MiB OPI PSRAM.
 * Inspired by Espressif esp_http_server + wifi/iperf examples.
 *
 * Safety: GPIO47/18 (J1708 TX), GPIO21/17 (J1708 RX), GPIO15/16 (NMEA)
 * are NEVER configured. This firmware cannot control or read the motors.
 * Only the existing original web UI is copied as a cosmetics reference.
 *
 * This is an AP+STA / HTTP / OTA diagnostic test, not production firmware.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_http_server.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_system.h>
#include <string.h>
#include "lab_ui.h"
#include "ui.h"

#define LAB_VERSION "0.0.2-chatgpt-wifi-lab"
static const char *AP_NAME="EngineLink-ChatGPT";
static const char *AP_PASS="12345678";
static constexpr size_t OTA_MAX=3200000,OTA_MIN=100000,OTA_HDR=0x70;
static httpd_handle_t httpServer=nullptr;
static char wifiName[33]={},wifiPassword[65]={};
static volatile uint32_t apConnected=0,apDisconnected=0,staConnected=0,staDisconnected=0,ipAssigned=0;
static volatile uint8_t lastStaReason=0;
static uint32_t bootMs=0,lastRouterTry=0,rebootAt=0,statusSeen=0;
static bool mdnsOk=false,httpOk=false,otaStarted=false,otaSucceeded=false;
static String lastOtaResult="none";
static uint32_t httpRequests=0,httpFailed=0,speedRequests=0;
static uint64_t speedBytes=0;
static uint32_t lastSpeedElapsedMs=0,lastSpeedKib=0;
static const char SPEED_BUF[8192] = {0};

static String quote(const String &value){
  String s="\"";
  for (size_t i=0;i<value.length();++i){
    const char c=value[i];
    if(c=='"'||c=='\\'){s+='\\';s+=c;}
    else if((uint8_t)c<0x20)s+=' ';
    else s+=c;
  }
  s+='"';return s;
}
static void wifiEvent(WiFiEvent_t event,WiFiEventInfo_t info){
  switch(event){
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: ++apConnected;break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: ++apDisconnected;break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: ++staConnected;break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: ++staDisconnected;lastStaReason=info.wifi_sta_disconnected.reason;break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: ++ipAssigned;break;
    default:break;
  }
}
static void startRadio(){
  WiFi.persistent(false);
  WiFi.onEvent(wifiEvent);
  WiFi.mode(WIFI_AP_STA);        // one shared 2.4 GHz radio
  WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  const bool started=WiFi.softAP(AP_NAME,AP_PASS,1,false,4);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_bandwidth(WIFI_IF_AP,WIFI_BW_HT20);
  if(wifiName[0])WiFi.begin(wifiName,wifiPassword);
  lastRouterTry=millis();
  mdnsOk=MDNS.begin("enginelink-chatgpt");
  Serial.printf("LAB AP: %s; IP: %s; mDNS: %d\n",
    started?"ready":"FAILED",WiFi.softAPIP().toString().c_str(),mdnsOk);
}
static String snapshot(){
  String s;s.reserve(2600);
  const wifi_mode_t mode=WiFi.getMode();
  uint8_t primary=0;wifi_second_chan_t secondary=WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&primary,&secondary);
  int8_t txQuarter=0;
  esp_wifi_get_max_tx_power(&txQuarter);
  wifi_sta_list_t stations={};
  const bool stationListOk=esp_wifi_ap_get_sta_list(&stations)==ESP_OK;
  const int apRssi=stationListOk&&stations.num>0?stations.sta[0].rssi:0;
  const bool routerConnected=WiFi.status()==WL_CONNECTED;
  s="{\"version\":\"" LAB_VERSION "\",\"experimental\":true,\"server\":\"esp_http_server\"";
  s+=",\"uptimeMs\":";s+=(uint32_t)(millis()-bootMs);
  s+=",\"network\":{\"mode\":";s+=(unsigned)mode;
  s+=",\"ap\":{\"ssid\":";s+=quote(AP_NAME);
  s+=",\"ip\":";s+=quote(WiFi.softAPIP().toString());
  s+=",\"channel\":";s+=(unsigned)primary;
  s+=",\"clients\":";s+=(unsigned)WiFi.softAPgetStationNum();
  s+=",\"clientRssi\":";s+=stations.num&&stationListOk?String(apRssi):String("null");
  s+=",\"joins\":";s+=apConnected;s+=",\"leaves\":";s+=apDisconnected;s+="}";
  s+=",\"sta\":{\"configured\":";s+=wifiName[0]?"true":"false";
  s+=",\"connected\":";s+=routerConnected?"true":"false";
  s+=",\"ssid\":";s+=quote(wifiName);
  s+=",\"ip\":";s+=quote(routerConnected?WiFi.localIP().toString():"");
  s+=",\"rssi\":";s+=routerConnected?String(WiFi.RSSI()):String("null");
  s+=",\"joins\":";s+=staConnected;s+=",\"leaves\":";s+=staDisconnected;
  s+=",\"dhcp\":";s+=ipAssigned;
  s+=",\"lastDisconnectReason\":";s+=(unsigned)lastStaReason;
  s+="},\"mdns\":\"enginelink-chatgpt.local\",\"mdnsInitialized\":";
  s+=mdnsOk?"true":"false";s+="}";
  s+=",\"hardware\":{\"chip\":\"ESP32-S3\",\"cpuMHz\":";s+=ESP.getCpuFreqMHz();
  s+=",\"flashBytes\":";s+=ESP.getFlashChipSize();
  s+=",\"psramBytes\":";s+=psramFound()?ESP.getPsramSize():0;
  s+=",\"freePsram\":";s+=psramFound()?ESP.getFreePsram():0;
  s+=",\"freeHeap\":";s+=ESP.getFreeHeap();
  s+=",\"minHeap\":";s+=ESP.getMinFreeHeap();
  s+=",\"dmaHeap\":";s+=heap_caps_get_free_size(MALLOC_CAP_DMA);
  s+=",\"txPowerQuarterDbm\":";s+=(int)txQuarter;
  s+=",\"resetReason\":";s+=(int)esp_reset_reason();s+="}";
  s+=",\"http\":{\"started\":";s+=httpOk?"true":"false";
  s+=",\"requests\":";s+=httpRequests;
  s+=",\"errors\":";s+=httpFailed;
  s+=",\"speedTests\":";s+=speedRequests;
  s+=",\"bytesSent\":";s+=(unsigned long long)speedBytes;
  s+=",\"lastSpeedKib\":";s+=lastSpeedKib;
  s+=",\"lastSpeedMs\":";s+=lastSpeedElapsedMs;s+="}";
  s+=",\"ota\":{\"enabled\":true,\"mode\":\"app-only raw upload\",\"lastResult\":";
  s+=quote(lastOtaResult);s+="}";
  s+=",\"engines\":{\"available\":false,\"reason\":\"Not implemented: wireless isolation test\"}";
  s+=",\"nmea\":{\"available\":false,\"reason\":\"Not configured: hardware isolation test\"}";
  s+="}";
  return s;
}
static esp_err_t responseJson(httpd_req_t *req,int status,const String &body){
  httpd_resp_set_type(req,"application/json; charset=utf-8");
  httpd_resp_set_hdr(req,"Cache-Control","no-store");
  if(status==400)httpd_resp_set_status(req,"400 Bad Request");
  else if(status==409)httpd_resp_set_status(req,"409 Conflict");
  else if(status==413)httpd_resp_set_status(req,"413 Payload Too Large");
  else if(status==500)httpd_resp_set_status(req,"500 Internal Server Error");
  if(status>=400)++httpFailed;
  return httpd_resp_send(req,body.c_str(),body.length());
}
static esp_err_t result(httpd_req_t *req,bool ok,const String &message,int status=200){
  return responseJson(req,status,String("{\"ok\":")+(ok?"true":"false")+",\"message\":"+quote(message)+"}");
}
static esp_err_t page(httpd_req_t *req){
  ++httpRequests;
  httpd_resp_set_type(req,"text/html; charset=utf-8");
  httpd_resp_set_hdr(req,"Cache-Control","no-store");
  return httpd_resp_send(req,WIFI_LAB_HTML,strlen(WIFI_LAB_HTML));
}
static esp_err_t originalPage(httpd_req_t *req){
  ++httpRequests;
  httpd_resp_set_type(req,"text/html; charset=utf-8");
  httpd_resp_set_hdr(req,"Content-Encoding","gzip");
  httpd_resp_set_hdr(req,"Cache-Control","no-store");
  return httpd_resp_send(req,(const char*)ENGINE_UI_GZ,ENGINE_UI_GZ_LEN);
}
static esp_err_t statusPage(httpd_req_t *req){
  ++httpRequests;++statusSeen;
  return responseJson(req,200,snapshot());
}
static esp_err_t reportPage(httpd_req_t *req){
  ++httpRequests;
  httpd_resp_set_hdr(req,"Content-Disposition","attachment; filename=EngineLink-WiFi-Lab-v2-report.json");
  return responseJson(req,200,snapshot());
}
static esp_err_t rttPage(httpd_req_t *req){
  ++httpRequests;
  httpd_resp_set_status(req,"204 No Content");
  httpd_resp_set_hdr(req,"Cache-Control","no-store");
  return httpd_resp_send(req,nullptr,0);
}
static esp_err_t speedPage(httpd_req_t *req){
  ++httpRequests;++speedRequests;
  char query[80]={},value[16]={};
  int kb=512;
  if(httpd_req_get_url_query_str(req,query,sizeof(query))==ESP_OK &&
    httpd_query_key_value(query,"kb",value,sizeof(value))==ESP_OK)kb=atoi(value);
  if(kb!=64&&kb!=512&&kb!=2048)return result(req,false,"Use kb=64, 512 or 2048",400);
  httpd_resp_set_type(req,"application/octet-stream");
  httpd_resp_set_hdr(req,"Cache-Control","no-store");
  const uint32_t started=millis();
  int remaining=kb*1024;
  // Exact byte count checked by browser, no giant allocation.
  while(remaining>0){
    const int n=min(remaining,(int)sizeof(SPEED_BUF));
    if(httpd_resp_send_chunk(req,SPEED_BUF,n)!=ESP_OK){++httpFailed;return ESP_FAIL;}
    remaining-=n;
  }
  lastSpeedElapsedMs=millis()-started;lastSpeedKib=kb;speedBytes+=(uint64_t)kb*1024;
  return httpd_resp_send_chunk(req,nullptr,0);
}
static int fromHex(char c){
  if(c>='0'&&c<='9')return c-'0';
  if(c>='a'&&c<='f')return c-'a'+10;
  if(c>='A'&&c<='F')return c-'A'+10;
  return -1;
}
static String urlDecode(const String &s){
  String v;v.reserve(s.length());
  for(size_t i=0;i<s.length();++i){
    if(s[i]=='+')v+=' ';
    else if(s[i]=='%'&&i+2<s.length()){
      const int a=fromHex(s[i+1]),b=fromHex(s[i+2]);
      if(a>=0&&b>=0){v+=(char)((a<<4)|b);i+=2;}else v+=s[i];
    }else v+=s[i];
  }
  return v;
}
static String formValue(const String &body,const char *name){
  const String key=String(name)+"=";
  int start=0;
  while(start<(int)body.length()){
    int end=body.indexOf('&',start);
    if(end<0)end=body.length();
    if(body.substring(start,start+key.length())==key)
      return urlDecode(body.substring(start+key.length(),end));
    start=end+1;
  }
  return "";
}
static esp_err_t readSmallBody(httpd_req_t *req,String &out){
  if(req->content_len<0||req->content_len>240)return ESP_FAIL;
  char buf[241]={};int remaining=req->content_len,got=0;
  while(remaining){
    int n=httpd_req_recv(req,buf+got,remaining);
    if(n<=0)return ESP_FAIL;
    got+=n;remaining-=n;
  }
  out=String(buf);return ESP_OK;
}
static esp_err_t routerPage(httpd_req_t *req){
  ++httpRequests;
  String body;
  if(readSmallBody(req,body)!=ESP_OK)return result(req,false,"Invalid router request",400);
  const String ssid=formValue(body,"ssid"),password=formValue(body,"password");
  if(ssid.isEmpty()||ssid.length()>32||password.length()>64)
    return result(req,false,"Invalid SSID or password length",400);
  Preferences p;
  if(!p.begin("cgpt_wifi",false))return result(req,false,"Cannot save credentials",500);
  const bool a=p.putString("ssid",ssid)>0;
  const bool b=p.putString("pass",password)>0 || password.isEmpty();
  p.end();
  if(!a||!b)return result(req,false,"NVS could not save router settings",500);
  strlcpy(wifiName,ssid.c_str(),sizeof(wifiName));
  strlcpy(wifiPassword,password.c_str(),sizeof(wifiPassword));
  WiFi.begin(wifiName,wifiPassword);lastRouterTry=millis();
  return result(req,true,"Router connection requested. Test AP remains enabled.");
}
static esp_err_t forgetPage(httpd_req_t *req){
  ++httpRequests;
  Preferences p;
  if(p.begin("cgpt_wifi",false)){p.clear();p.end();}
  wifiName[0]=wifiPassword[0]=0;
  WiFi.disconnect(false,false);
  return result(req,true,"Router settings removed; test AP remains enabled.");
}
// A compact unambiguous app-only image header. Do not start erasing flash
// until chip app-descriptor and size have passed validation.
static bool validOtaHeader(const uint8_t *p){
  if(p[0]!=0xE9 || memcmp(p+0x20,"\x32\x54\xCD\xAB",4)!=0)return false;
  // Arduino 3.3.12 typically uses project name arduino-lib-builder:
  // this is compatibility ONLY, NOT proof of provenance/authenticity.
  return strncmp((const char*)(p+0x50),esp_app_get_description()->project_name,32)==0;
}
static esp_err_t otaPage(httpd_req_t *req){
  ++httpRequests;
  // Explicit user-confirmation header to avoid accidental uploads.
  char consent[24]={};
  if(httpd_req_get_hdr_value_str(req,"X-EngineLink-Test",consent,sizeof(consent))!=ESP_OK ||
     strcmp(consent,"CONFIRM-OTA")!=0)
    return result(req,false,"Missing explicit OTA confirmation header",400);
  const size_t total=req->content_len;
  if(total<OTA_MIN || total>OTA_MAX)return result(req,false,"Expected app-only image between 100 KB and 3.2 MB",413);
  if(otaStarted)return result(req,false,"OTA is already active",409);
  otaStarted=true;
  uint8_t buf[4096];uint8_t start[OTA_HDR];size_t hcount=0,received=0;
  bool begun=false,ok=true;String error;
  uint8_t timeouts=0;
  // Incrementally inspect the first 0x70 bytes, which may span TCP packets.
  while(ok&&received<total){
    const size_t want=min(total-received,sizeof(buf));
    const int n=httpd_req_recv(req,(char*)buf,want);
    if(n==HTTPD_SOCK_ERR_TIMEOUT&&timeouts++<4)continue;
    if(n<=0){error="Upload connection lost or timed out";ok=false;break;}
    timeouts=0;received+=n;
    size_t offset=0;
    while(offset<(size_t)n && hcount<OTA_HDR)start[hcount++]=buf[offset++];
    if(hcount==OTA_HDR&&!begun){
      if(!validOtaHeader(start)){error="Not compatible with this EngineLink Arduino OTA image";ok=false;break;}
      // The correct OTA partition size is checked by Update.begin.
      if(!Update.begin(total,U_FLASH)){error=Update.errorString();ok=false;break;}
      begun=true;
      if(Update.write(start,OTA_HDR)!=OTA_HDR){error=Update.errorString();ok=false;break;}
    }
    if(begun&&offset<(size_t)n){
      const size_t bytes=n-offset;
      if(Update.write(buf+offset,bytes)!=bytes){error=Update.errorString();ok=false;break;}
    }
  }
  if(ok&&!begun){error="Firmware header incomplete";ok=false;}
  if(ok && !Update.end(true)){error=Update.errorString();ok=false;}
  if(!ok){if(begun)Update.abort();otaStarted=false;lastOtaResult=error;return result(req,false,error,400);}
  otaSucceeded=true;lastOtaResult="OTA image accepted, reboot scheduled";
  rebootAt=millis()+2000;
  otaStarted=false;
  return result(req,true,"App-only OTA installed; reconnect to EngineLink-ChatGPT after reboot");
}
static bool addRoute(const char *uri,httpd_method_t method,esp_err_t (*fn)(httpd_req_t *)){
  httpd_uri_t u={};u.uri=uri;u.method=method;u.handler=fn;u.user_ctx=nullptr;
  return httpd_register_uri_handler(httpServer,&u)==ESP_OK;
}
static bool startHttp(){
  httpd_config_t config=HTTPD_DEFAULT_CONFIG();
  config.server_port=80;
  config.max_uri_handlers=18;
  config.max_open_sockets=7;
  config.lru_purge_enable=true;
  config.stack_size=10240;
  config.recv_wait_timeout=12;
  config.send_wait_timeout=12;
  if(httpd_start(&httpServer,&config)!=ESP_OK)return false;
  bool ok=true;
  ok &= addRoute("/",HTTP_GET,page);
  ok &= addRoute("/lab",HTTP_GET,page);
  ok &= addRoute("/original",HTTP_GET,originalPage);
  ok &= addRoute("/lab/status",HTTP_GET,statusPage);
  ok &= addRoute("/lab/report",HTTP_GET,reportPage);
  ok &= addRoute("/api/state",HTTP_GET,statusPage);
  ok &= addRoute("/api/live",HTTP_GET,statusPage);
  ok &= addRoute("/api/rtt",HTTP_GET,rttPage);
  ok &= addRoute("/api/speedtest",HTTP_GET,speedPage);
  ok &= addRoute("/lab/router",HTTP_POST,routerPage);
  ok &= addRoute("/lab/forget",HTTP_POST,forgetPage);
  ok &= addRoute("/lab/ota",HTTP_POST,otaPage);
  ok &= addRoute("/api/update",HTTP_POST,otaPage);
  return ok;
}
static bool pendingVerify(){
  const esp_partition_t *running=esp_ota_get_running_partition();
  esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;
  return running && esp_ota_get_state_partition(running,&state)==ESP_OK
    && state==ESP_OTA_IMG_PENDING_VERIFY;
}
void setup(){
  Serial.begin(115200);Serial.setTxTimeoutMs(0);bootMs=millis();
  delay(1000);
  Preferences p;
  if(p.begin("cgpt_wifi",true)){
    const String ssid=p.getString("ssid",""),pass=p.getString("pass","");
    p.end();
    strlcpy(wifiName,ssid.c_str(),sizeof(wifiName));
    strlcpy(wifiPassword,pass.c_str(),sizeof(wifiPassword));
  }
  startRadio();
  httpOk=startHttp();
  Serial.printf("EngineLink Wi-Fi Lab v2 HTTP: %s, free PSRAM: %u\n",
    httpOk?"ready":"ERROR",(unsigned)ESP.getFreePsram());
}
void loop(){
  // Wi-Fi callback and Espressif HTTP run in independent tasks. Keep loop free.
  if(wifiName[0] && WiFi.status()!=WL_CONNECTED &&
     WiFi.softAPgetStationNum()==0 &&
     (uint32_t)(millis()-lastRouterTry)>30000){
    WiFi.begin(wifiName,wifiPassword);lastRouterTry=millis();
  }
  static bool bootConfirmed=false;
  if(!bootConfirmed && (uint32_t)(millis()-bootMs)>60000 && httpOk && statusSeen>=2){
    // Confirm ONLY after the actual diagnostics endpoint was reachable.
    if(pendingVerify())esp_ota_mark_app_valid_cancel_rollback();
    bootConfirmed=true;
  }
  if(!bootConfirmed && (uint32_t)(millis()-bootMs)>180000 && pendingVerify()){
    Serial.println("No HTTP health confirmation; returning to previous OTA firmware");
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
  if(rebootAt&&(int32_t)(millis()-rebootAt)>=0)ESP.restart();
  delay(20);
}
