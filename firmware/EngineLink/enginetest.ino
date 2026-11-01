// Engine test & calibration capture: passive J1708 recording with stage
// markers and reference readings, saved to the SD card.

// A calibration capture consists of original RX bytes plus operator reference
// markers. Nothing in this workflow transmits or commands an engine.
void finishCalibration(bool resume){
  if(calFile){if(cardInserted())calFile.flush();calFile.close();}
  calActive=false;
  refreshTestAvailable();
  if(resume && calResumeLogging && sdReady && cardInserted()){
    logFile=SD.open(sessionName,FILE_APPEND);
    sdLogging=(bool)logFile;
    if(!sdLogging){sdFaultLatched=true;sdMessage="Could not resume raw logging";}
  }
  calResumeLogging=false;
}
void calibrationMarker(){
  if(!calActive)return;
  const long long epoch=validWall()?(long long)time(nullptr):0;
  char entry[240];
  int n=snprintf(entry,sizeof(entry),"MARK,%08lX,%llu,%lld,%lu,%s,%ld,%.3f,,,,,,,,,,\n",
    (unsigned long)bootId,(unsigned long long)elapsedMs(),epoch,
    (unsigned long)calSteps,calPhase.c_str(),(long)calReferenceRPM,
    calReferenceFuel);
  if(n<=0 || n >= (int)sizeof(entry) || calFile.write((const uint8_t*)entry,n)!=(size_t)n){
    sdFaultLatched=true;sdMessage="Calibration marker write failed";finishCalibration(false);
    return;
  }
  calFile.flush();
  calStepStartMs=millis();
}
bool startCalibration(int channel){
  if(calActive||!sdReady||!cardInserted())return false;
  if(channel<-1||channel>1)return false;
  calChannel=channel;
  calRows=0;calInvalid=0;calSteps=1;calReferenceRPM=-1;calReferenceFuel=-1;
  calPhase="unlabelled";calStartMs=millis();
  char name[28];
  snprintf(name,sizeof(name),"/TEST%08lX.CSV",(unsigned long)bootId);
  calName=name;
  if(SD.exists(calName)){ // Do not overwrite an older test if a boot ID collides.
    for(uint8_t i=1;i<50;i++){
      snprintf(name,sizeof(name),"/T%08lX%02u.CSV",(unsigned long)bootId,(unsigned)i);
      if(!SD.exists(name)){calName=name;break;}
    }
    if(SD.exists(calName))return false;
  }
  calFile=SD.open(calName,FILE_WRITE);
  if(!calFile){sdFaultLatched=true;sdMessage="Could not create motor test file";return false;}
  // Only pause session logging once the test file exists, so a failed start
  // never leaves ordinary logging silently switched off.
  calResumeLogging=sdLogging;
  if(sdLogging)stopLog(); // Do not duplicate high-rate writes to the same SD card.
  testFileAvailable=false;
  calFile.println("kind,boot_id,uptime_ms,utc_epoch,step,phase,reference_rpm,reference_fuel_lph,channel,mid,length,checksum,raw_hex,decoded_fuel_lph,decoded_rpm,direction,kad_index,kad_value");
  calActive=true;
  lastTestFlush=millis();
  const bool resumeAfterFailure=calResumeLogging;
  calibrationMarker();
  if(!calActive){
    if(resumeAfterFailure)initSD();
    return false;
  }
  pref.begin("enginelink",false);pref.putString("testfile",calName);pref.end();
  sdMessage="Motor calibration test capturing";
  return true;
}
void appendCalibrationFrame(const Frame &f,const Decoded &d,bool valid,const char *hex){
  if(!calActive || (calChannel>=0 && f.channel!=(uint8_t)calChannel))return;
  const long long epoch=validWall()?(long long)time(nullptr):0;
  char entry[520];
  int n=snprintf(entry,sizeof(entry),"FRAME,%08lX,%llu,%lld,%lu,%s,%ld,%.3f,J%u,%u,%u,%u,%s,",
    (unsigned long)bootId,(unsigned long long)f.bootMs,epoch,
    (unsigned long)calSteps,calPhase.c_str(),(long)calReferenceRPM,calReferenceFuel,
    f.channel==0?7:6,(unsigned)d.mid,(unsigned)f.length,valid?1:0,hex);
  if(n<=0||n>(int)sizeof(entry)-80){++sdErrors;sdFaultLatched=true;sdMessage="Motor test row too large";finishCalibration(false);return;}
  if(d.gotFuel)n+=snprintf(entry+n,sizeof(entry)-n,"%.3f",d.lph);
  entry[n++]=',';
  if(d.gotRPM)n+=snprintf(entry+n,sizeof(entry)-n,"%.2f",d.rpm);
  n+=snprintf(entry+n,sizeof(entry)-n,",%s,",frameDirection(d));
  if(d.kadIndex>=0)n+=snprintf(entry+n,sizeof(entry)-n,"%ld",(long)d.kadIndex);
  entry[n++]=',';
  if(d.kadValueOk)n+=snprintf(entry+n,sizeof(entry)-n,"%ld",(long)d.kadValue);
  entry[n++]='\n';
  if(calFile.write((const uint8_t*)entry,n)!=(size_t)n){
    ++sdErrors;sdFaultLatched=true;sdMessage="Motor test SD write error";finishCalibration(false);
  }else{++calRows;if(!valid)++calInvalid;}
}
void handleCalibrationStart(){
  int channel=web.arg("channel").toInt(); // 0=both,1=J7,2=J6
  if(channel<0||channel>2){reply(false,"Invalid channel",400);return;}
  if(!sdReady||!cardInserted()){reply(false,"Insert a microSD card before motor test",409);return;}
  bool ok=startCalibration(channel-1);
  reply(ok,ok?"Recording started. Hold each RPM stage 30-60 s and mark it.":"Could not start motor test",ok?200:409);
}
void handleCalibrationMark(){
  if(!calActive){reply(false,"No active motor test",409);return;}
  String phase=web.arg("phase");
  if(phase!="idle"&&phase!="low"&&phase!="medium"&&phase!="high"&&phase!="cruise"&&phase!="custom"){
    reply(false,"Invalid phase",400);return;
  }
  String refRpm=web.arg("rpm");
  for(size_t i=0;i<refRpm.length();i++)if(!isDigit(refRpm[i])){reply(false,"Invalid reference RPM",400);return;}
  int rpm=refRpm.length()?refRpm.toInt():-1;
  if(rpm<-1||rpm>6000){reply(false,"Reference RPM must be 0-6000",400);return;}
  String fuel=web.arg("fuel");
  float flow=-1;
  if(fuel.length()){
    for(size_t i=0;i<fuel.length();i++)if(!isDigit(fuel[i])&&fuel[i]!='.'){reply(false,"Invalid reference fuel rate",400);return;}
    flow=fuel.toFloat();
    if(flow<0||flow>2000){reply(false,"Reference fuel rate out of range",400);return;}
  }
  calPhase=phase;calReferenceRPM=rpm;calReferenceFuel=flow;
  ++calSteps;calibrationMarker();
  reply(calActive,calActive?"Stage marked. Hold it steady while data is recorded.":"SD write error stopped recording",calActive?200:500);
}
void handleCalibrationStop(){
  if(!calActive){reply(false,"No motor test is running",409);return;}
  finishCalibration(true);
  reply(true,"Motor test saved. You can now download the CSV.");
}
void handleCalibrationDownload(){
  if(calActive){web.send(409,"text/plain","Stop the motor test before downloading its CSV");return;}
  if(!sdReady || !calName.length() || !SD.exists(calName)){
    web.send(404,"text/plain","No saved motor test on the inserted SD card");return;
  }
  File f=SD.open(calName,FILE_READ);
  if(!f){web.send(500,"text/plain","Unable to open test log");return;}
  streamFileOut(f,"motor test","EngineLink-Motor-Test.csv");f.close();
}
