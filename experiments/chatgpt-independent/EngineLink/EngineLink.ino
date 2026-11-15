/*
 * EngineLink ChatGPT standalone experiment v0.0.1.
 * This sketch does not import or copy the production backend.
 * Only the compressed visual UI (ui.h) is reused by the build.
 *
 * SAFETY: J1708 TX pins are never configured. CAN is listen-only.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <driver/uart.h>
#include <driver/twai.h>
#include <esp_ota_ops.h>
#include "ui.h"

#define EXP_VERSION "0.0.1-chatgpt-test"
static WebServer server(80);
static char routerSsid[33]={},routerPass[65]={};
static volatile uint32_t apJoins=0,apLeaves=0,staJoins=0,staLeaves=0;
static volatile uint8_t staReason=0;
static uint32_t reconnectAt=0,rebootAt=0,bootAt=0;
static bool nmeaReady=false,updateOK=false,updateError=false;
static String updateMessage;
static uint8_t header[0x70];
static size_t headerLen=0,flashBytes=0;
static float sogKn=NAN;
static uint32_t sogAt=0,canFrames=0;
struct Packet {
  uint8_t ch=0,len=0,b[64]={};
  uint32_t ms=0;
};
struct Port {
  uart_port_t id;
  uint8_t ch,rx,len=0,b[64]={};
  QueueHandle_t ev=nullptr;
  volatile uint32_t drops=0,errors=0;
};
static Port ports[2]={{UART_NUM_1,0,21},{UART_NUM_2,1,17}};
static QueueHandle_t packets=nullptr;
struct Engine {
  uint32_t frames=0,bad=0,at=0,rpmAt=0,fuelAt=0,requestAt=0;
  uint16_t request=0;
  float rpm=NAN,lph=NAN;
  uint8_t lastLen=0,last[64]={};
};
static Engine eng[2];
static String q(const String &s) {
  String out="\"";
  for(size_t i=0;i<s.length();++i) {
    const char c=s[i];
    if(c=='"' || c=='\\'){out+='\\';out+=c;}
    else if((unsigned char)c<32)out+=' ';
    else out+=c;
  }
  return out+"\"";
}
static void reply(bool ok,const String &msg,int code=200) {
  server.sendHeader("Cache-Control","no-store");
  server.send(code,"application/json",String("{\"ok\":")+(ok?"true":"false")+",\"message\":"+q(msg)+"}");
}
static String fresh(float v,uint32_t at,uint32_t maxAge=10000) {
  return isfinite(v)&&at&&(uint32_t)(millis()-at)<maxAge?String(v,2):String("null");
}
static void onWifi(WiFiEvent_t e,WiFiEventInfo_t info) {
  switch(e) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: ++apJoins;break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: ++apLeaves;break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: ++staJoins;break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      ++staLeaves;staReason=info.wifi_sta_disconnected.reason;break;
    default: break;
  }
}
static void startWifi() {
  WiFi.persistent(false);
  WiFi.onEvent(onWifi);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  WiFi.softAP("EngineLink-ChatGPT","12345678",1,false,4);
  if(routerSsid[0])WiFi.begin(routerSsid,routerPass);
  reconnectAt=millis();
  MDNS.begin("enginelink-chatgpt");
}
static void push(Port &p) {
  if(!p.len)return;
  Packet f;f.ch=p.ch;f.len=p.len;f.ms=millis();
  memcpy(f.b,p.b,p.len);
  if(xQueueSend(packets,&f,0)!=pdTRUE)++p.drops;
  p.len=0;
}
static void rxTask(void *param) {
  Port &p=*(Port*)param;
  uart_event_t ev;uint8_t buf[128];
  for(;;) {
    if(xQueueReceive(p.ev,&ev,pdMS_TO_TICKS(250))!=pdTRUE)continue;
    if(ev.type==UART_DATA){
      size_t left=ev.size;
      while(left){
        int n=uart_read_bytes(p.id,buf,min(left,sizeof(buf)),pdMS_TO_TICKS(20));
        if(n<=0)break;
        left-=n;
        for(int i=0;i<n;++i){
          if(p.len<sizeof(p.b))p.b[p.len++]=buf[i];
          else {++p.errors;p.len=0;}
        }
      }
      if(ev.timeout_flag)push(p);
    }else if(ev.type==UART_FIFO_OVF||ev.type==UART_BUFFER_FULL){
      ++p.errors;uart_flush_input(p.id);xQueueReset(p.ev);p.len=0;
    }else if(ev.type==UART_FRAME_ERR||ev.type==UART_PARITY_ERR)++p.errors;
  }
}
static bool startPort(Port &p) {
  uart_config_t c={};
  c.baud_rate=9600;c.data_bits=UART_DATA_8_BITS;
  c.parity=UART_PARITY_DISABLE;c.stop_bits=UART_STOP_BITS_1;
  c.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;c.source_clk=UART_SCLK_DEFAULT;
  if(uart_param_config(p.id,&c)!=ESP_OK)return false;
  // Explicitly leave TX unassigned, including GPIO47 and GPIO18.
  if(uart_set_pin(p.id,UART_PIN_NO_CHANGE,p.rx,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK)return false;
  if(uart_driver_install(p.id,4096,0,24,&p.ev,0)!=ESP_OK)return false;
  if(uart_set_rx_timeout(p.id,2)!=ESP_OK)return false;
  return xTaskCreatePinnedToCore(rxTask,p.ch?"j6-rx":"j7-rx",4096,&p,3,nullptr,0)==pdPASS;
}
static void decodeJ1587(Engine &e,const Packet &p) {
  size_t i=1,end=p.len-1;
  while(i<end){
    uint8_t pid=p.b[i++],n;
    if(pid<128)n=1;
    else if(pid<192)n=2;
    else if(pid<254){if(i>=end)return;n=p.b[i++];}
    else return;
    if(n>end-i)return;
    if(n==2&&(pid==190||pid==183)){
      const uint16_t v=p.b[i]|((uint16_t)p.b[i+1]<<8);
      if(v!=0xFFFF&&v<60000){
        if(pid==190&&v<20000){e.rpm=v*0.25f;e.rpmAt=p.ms;}
        if(pid==183){e.lph=v*(3.785411784f/64.0f);e.fuelAt=p.ms;}
      }
    }
    i+=n;
  }
}
static void decode(const Packet &p){
  Engine &e=eng[p.ch];++e.frames;e.at=p.ms;
  uint8_t sum=0;
  for(uint8_t i=0;i<p.len;++i)sum+=p.b[i];
  if(p.len<3||sum){++e.bad;return;}
  e.lastLen=p.len;memcpy(e.last,p.b,p.len);
  if(p.len==6&&p.b[0]==0x01&&p.b[1]==0x43&&p.b[2]==0x21){
    e.request=((uint16_t)p.b[3]<<8)|p.b[4];e.requestAt=p.ms;return;
  }
  if(e.requestAt&&(uint32_t)(p.ms-e.requestAt)<650&&p.len==6&&p.b[0]==0x83&&p.b[1]==0xC1) {
    const uint16_t raw=((uint16_t)p.b[3]<<8)|p.b[4];
    if(raw!=0xFFFF&&raw<60000){
      if(e.request==256&&raw<20000){e.rpm=raw*0.25f;e.rpmAt=p.ms;}
      if(e.request==289){e.lph=raw/128.0f;e.fuelAt=p.ms;}
    }
    e.requestAt=0;return;
  }
  decodeJ1587(e,p);
}
static bool nmeaStart(){
  if(nmeaReady)return true;
  twai_general_config_t g=TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_15,GPIO_NUM_16,TWAI_MODE_LISTEN_ONLY);
  g.rx_queue_len=64;
  twai_timing_config_t t=TWAI_TIMING_CONFIG_250KBITS();
  twai_filter_config_t f=TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if(twai_driver_install(&g,&t,&f)!=ESP_OK)return false;
  if(twai_start()!=ESP_OK){twai_driver_uninstall();return false;}
  nmeaReady=true;return true;
}
static void nmeaStop(){
  if(nmeaReady){twai_stop();twai_driver_uninstall();nmeaReady=false;}
}
static void nmeaService(){
  if(!nmeaReady)return;
  twai_message_t m;
  for(int i=0;i<24&&twai_receive(&m,0)==ESP_OK;++i){
    ++canFrames;
    if(!m.extd||m.rtr||m.data_length_code<6)continue;
    const uint32_t id=m.identifier;
    uint8_t pf=(id>>16)&0xFF;
    uint32_t pgn=pf<240?((id>>8)&0x1FF00):((id>>8)&0x1FFFF);
    if(pgn!=129026)continue;
    uint16_t v=m.data[4]|((uint16_t)m.data[5]<<8);
    if(v!=0xFFFF){sogKn=v*0.019438445f;sogAt=millis();}
  }
}
static String status(){
  String s;s.reserve(1800);
  s="{\"experiment\":true,\"version\":\"" EXP_VERSION "\",\"j1708Tx\":false";
  s+=",\"uptimeMs\":";s+=millis()-bootAt;
  s+=",\"heap\":";s+=ESP.getFreeHeap();
  s+=",\"ap\":{\"ip\":";s+=q(WiFi.softAPIP().toString());
  s+=",\"ssid\":\"EngineLink-ChatGPT\",\"clients\":";s+=WiFi.softAPgetStationNum();
  s+=",\"joins\":";s+=apJoins;s+=",\"leaves\":";s+=apLeaves;s+="}";
  s+=",\"sta\":{\"connected\":";s+=WiFi.status()==WL_CONNECTED?"true":"false";
  s+=",\"ssid\":";s+=q(routerSsid);
  s+=",\"ip\":";s+=q(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"");
  s+=",\"rssi\":";s+=WiFi.status()==WL_CONNECTED?String(WiFi.RSSI()):"null";
  s+=",\"joins\":";s+=staJoins;s+=",\"leaves\":";s+=staLeaves;
  s+=",\"reason\":";s+=staReason;s+="}";
  s+=",\"engines\":[";
  for(int i=0;i<2;++i){
    if(i)s+=",";
    const Engine &e=eng[i];
    s+="{\"ch\":";s+=i;
    s+=",\"frames\":";s+=e.frames;
    s+=",\"checksumErrors\":";s+=e.bad;
    s+=",\"uartErrors\":";s+=ports[i].errors;
    s+=",\"dropped\":";s+=ports[i].drops;
    s+=",\"rpm\":";s+=fresh(e.rpm,e.rpmAt);
    s+=",\"fuelLph\":";s+=fresh(e.lph,e.fuelAt);
    String hex;
    for(uint8_t j=0;j<e.lastLen;++j){
      char b[4];snprintf(b,sizeof(b),"%02X",e.last[j]);hex+=b;
      if(j+1<e.lastLen)hex+=" ";
    }
    s+=",\"lastHex\":";s+=q(hex);s+="}";
  }
  s+="],\"nmea\":{\"enabled\":";s+=nmeaReady?"true":"false";
  s+=",\"frames\":";s+=canFrames;
  s+=",\"sogKn\":";s+=fresh(sogKn,sogAt,7000);
  s+="}}";
  return s;
}
static const char LAB_UI[] PROGMEM=R"LAB(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EngineLink independent test</title><style>
:root{color-scheme:dark;font:15px system-ui;background:#081620;color:#e5f5fb}
body{max-width:800px;margin:auto;padding:18px}section{background:#132a39;border-radius:12px;padding:15px;margin:12px 0}
h1{font-size:25px}h2{font-size:17px;color:#80d7d9}a{color:#70d8e0}
b{color:#ffd293}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}
button,input{padding:10px;border-radius:6px;margin:5px;border:1px solid #42727d}
button{background:#6fded7;color:#06353b;font-weight:bold}
</style></head><body>
<h1>EngineLink · ChatGPT TEST</h1>
<p><b>Experimental firmware. J1708 TX disabled. NMEA listen-only.</b> Not a validated boat application.</p>
<p><a href="/">View original UI (cosmetic comparison only; most original API routes are not implemented)</a></p>
<section><h2>Device & network</h2><pre id="net">Connecting…</pre></section>
<section><h2>Direct download benchmark</h2><button onclick="bench()">Test 512 KiB transfer</button><span id="speed"></span></section>
<section><h2>Boat router</h2>
<input id="ssid" placeholder="Router SSID"><input id="pw" type="password" placeholder="Router password">
<button onclick="join()">Connect</button><button onclick="post('/api/wifi/forget')">Forget</button></section>
<section><h2>Passive interfaces</h2><button onclick="post('/lab/nmea?on=1')">Enable NMEA</button>
<button onclick="post('/lab/nmea?on=0')">Disable NMEA</button>
<a href="/lab/report">Save JSON test report</a><pre id="eng"></pre></section>
<section><h2>OTA image upload / return to normal firmware</h2>
<p>Use an application-only EngineLink .bin, not a full 8 MB merged USB image.</p>
<form id="ota"><input type="file" name="update" accept=".bin" required>
<button>Flash OTA app image</button></form><pre id="message"></pre></section>
<script>
async function refresh(){try{let d=await (await fetch('/lab/status',{cache:'no-store'})).json();
document.querySelector('#net').textContent=JSON.stringify({version:d.version,uptime:d.uptimeMs,heap:d.heap,ap:d.ap,sta:d.sta},null,2);
document.querySelector('#eng').textContent=JSON.stringify({engines:d.engines,nmea:d.nmea},null,2);
}catch(e){document.querySelector('#net').textContent=String(e)}}
async function post(path,body=''){try{let r=await fetch(path,{method:'POST',
headers:{'Content-Type':'application/x-www-form-urlencoded'},body});alert((await r.json()).message);refresh()}catch(e){alert(e)}}
function join(){post('/api/wifi/connect','ssid='+encodeURIComponent(document.querySelector('#ssid').value)+'&password='+encodeURIComponent(document.querySelector('#pw').value))}
async function bench(){let out=document.querySelector('#speed');out.textContent=' Measuring…';
let t=performance.now(),b=await(await fetch('/lab/speedtest?kb=512')).arrayBuffer();
out.textContent=' '+(b.byteLength/1024/(performance.now()-t)*1000).toFixed(1)+' KiB/s'}
document.querySelector('#ota').onsubmit=async e=>{e.preventDefault();let o=document.querySelector('#message');o.textContent='Uploading…';
try{o.textContent=JSON.stringify(await (await fetch('/api/update',{method:'POST',body:new FormData(e.target)})).json())}
catch(e){o.textContent=String(e)}};
refresh();setInterval(refresh,2000);
</script></body></html>
)LAB";
static void updateUpload(){
  HTTPUpload &u=server.upload();
  if(u.status==UPLOAD_FILE_START){
    updateOK=false;updateError=false;updateMessage="";
    headerLen=0;flashBytes=0;
    if(!u.filename.endsWith(".bin")){updateError=true;updateMessage="Expected .bin image";}
  }else if(u.status==UPLOAD_FILE_WRITE&&!updateError){
    size_t i=0;
    while(i<u.currentSize&&headerLen<sizeof(header))header[headerLen++]=u.buf[i++];
    if(headerLen==sizeof(header)&&flashBytes==0){
      if(header[0]!=0xE9||memcmp(header+0x20,"\x32\x54\xCD\xAB",4)!=0||
        strncmp((const char*)header+0x50,"EngineLink",32)!=0){
        updateError=true;updateMessage="Not an EngineLink app-only image";return;
      }
      if(!Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH)){
        updateError=true;updateMessage=Update.errorString();return;
      }
      if(Update.write(header,sizeof(header))!=sizeof(header)){
        updateError=true;updateMessage=Update.errorString();Update.abort();return;
      }
      flashBytes=sizeof(header);
    }
    if(flashBytes&&i<u.currentSize){
      size_t len=u.currentSize-i;
      if(Update.write(u.buf+i,len)!=len){
        updateError=true;updateMessage=Update.errorString();Update.abort();return;
      }
      flashBytes+=len;
    }
  }else if(u.status==UPLOAD_FILE_END){
    if(!updateError&&flashBytes>100000&&Update.end(true)){
      updateOK=true;updateMessage="Image written. Rebooting.";
      rebootAt=millis()+1800;
    }else{
      if(!updateError)updateMessage=Update.errorString();
      updateError=true;Update.abort();
    }
  }else if(u.status==UPLOAD_FILE_ABORTED){
    updateError=true;updateMessage="Upload aborted";Update.abort();
  }
}
static void webSetup(){
  server.on("/",HTTP_GET,[](){
    server.sendHeader("Content-Encoding","gzip");
    server.sendHeader("Cache-Control","no-store");
    server.send_P(200,PSTR("text/html; charset=utf-8"),(PGM_P)ENGINE_UI_GZ,ENGINE_UI_GZ_LEN);
  });
  server.on("/lab",HTTP_GET,[](){server.send_P(200,PSTR("text/html; charset=utf-8"),LAB_UI);});
  server.on("/lab/status",HTTP_GET,[](){server.send(200,"application/json",status());});
  server.on("/lab/report",HTTP_GET,[](){
    server.sendHeader("Content-Disposition","attachment; filename=chatgpt-lab-report.json");
    server.send(200,"application/json",status());
  });
  server.on("/api/state",HTTP_GET,[](){server.send(200,"application/json",status());});
  server.on("/api/live",HTTP_GET,[](){server.send(200,"application/json",status());});
  server.on("/lab/nmea",HTTP_POST,[](){
    if(server.arg("on")=="1"){
      const bool ok=nmeaStart();reply(ok,ok?"Receive-only NMEA enabled":"CAN initialization failed",ok?200:500);
    }else{nmeaStop();reply(true,"NMEA disabled");}
  });
  server.on("/api/wifi/connect",HTTP_POST,[](){
    const String ssid=server.arg("ssid"),password=server.arg("password");
    if(!ssid.length()||ssid.length()>32||password.length()>64){
      reply(false,"Invalid network name or password length",400);return;
    }
    strlcpy(routerSsid,ssid.c_str(),sizeof(routerSsid));
    strlcpy(routerPass,password.c_str(),sizeof(routerPass));
    Preferences p;p.begin("cgpt_wifi",false);
    p.putString("ssid",ssid);p.putString("pass",password);p.end();
    WiFi.begin(routerSsid,routerPass);reconnectAt=millis();
    reply(true,"Router connection started; test AP remains on");
  });
  server.on("/api/wifi/forget",HTTP_POST,[](){
    routerSsid[0]=routerPass[0]=0;
    Preferences p;p.begin("cgpt_wifi",false);p.clear();p.end();
    WiFi.disconnect(false,false);
    reply(true,"Test router credentials cleared");
  });
  server.on("/lab/speedtest",HTTP_GET,[](){
    int kb=constrain(server.arg("kb").toInt(),16,1024);
    static String data;
    if(data.length()!=4096){data.reserve(4096);for(int k=0;k<4096;++k)data+='X';}
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200,"application/octet-stream","");
    for(int i=0;i<kb/4&&server.client().connected();++i)server.sendContent(data);
    server.sendContent("");
  });
  server.on("/api/update",HTTP_POST,[](){
    reply(updateOK,updateMessage.length()?updateMessage:"Incomplete upload",updateOK?200:400);
  },updateUpload);
  server.on("/api/reboot",HTTP_POST,[](){rebootAt=millis()+900;reply(true,"Rebooting");});
  server.onNotFound([](){reply(false,"Not implemented in ChatGPT experimental firmware",404);});
  server.begin();
}
void setup(){
  Serial.begin(115200);Serial.setTxTimeoutMs(0);
  bootAt=millis();delay(1000);
  Preferences p;p.begin("cgpt_wifi",true);
  String s=p.getString("ssid",""),pw=p.getString("pass","");p.end();
  strlcpy(routerSsid,s.c_str(),sizeof(routerSsid));
  strlcpy(routerPass,pw.c_str(),sizeof(routerPass));
  startWifi();
  packets=xQueueCreate(256,sizeof(Packet));
  if(packets)for(Port &port:ports)if(!startPort(port))
    Serial.printf("RX port %u failed\n",(unsigned)port.ch);
  webSetup();
  Serial.println("ChatGPT TEST AP 192.168.4.1; diagnostics at /lab");
}
void loop(){
  server.handleClient();
  for(int i=0;i<64;++i){
    Packet p;
    if(!packets||xQueueReceive(packets,&p,0)!=pdTRUE)break;
    decode(p);
  }
  nmeaService();
  if(routerSsid[0]&&WiFi.status()!=WL_CONNECTED&&
    (uint32_t)(millis()-reconnectAt)>30000&&WiFi.softAPgetStationNum()==0){
    WiFi.disconnect(false,false);WiFi.begin(routerSsid,routerPass);reconnectAt=millis();
  }
  static bool bootOK=false;
  if(!bootOK&&(uint32_t)(millis()-bootAt)>60000){
    esp_ota_mark_app_valid_cancel_rollback();bootOK=true;
  }
  if(rebootAt&&(int32_t)(millis()-rebootAt)>=0)ESP.restart();
  delay(1);
}
