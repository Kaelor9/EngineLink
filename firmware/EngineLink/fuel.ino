// Fuel: rate integration per rpm band, calibration from fill-ups, the fuel
// log and the tank estimate.

uint8_t fuelBandFor(float rpm){return rpm<BAND_PART_RPM?0:rpm<BAND_CRUISE_RPM?1:2;}
// rawRate < 0 means J1587 (liters are stored directly); otherwise the KAD raw
// count is integrated too, so later scale/correction changes stay exact.
// Samples are joined with the trapezoid rule. A gap of up to 2 minutes while
// the engine kept answering for rpm (fuel requests lost, rpm still coming) is
// bridged the same way and counted as interpolated; longer gaps are not.
void updateFuel(Motor &m,uint8_t ch,float newRate,uint64_t timestamp,double rawRate){
  if(m.fuelEver && timestamp>m.prevFuelAt) {
    const uint64_t dt=timestamp-m.prevFuelAt;
    // Only bridge if rpm kept coming during the whole gap (at least one
    // answer per 2 s): then the engine ran and only the fuel answers were lost.
    const bool rpmKept=m.rpmEver&&timestamp>=m.rpmAt&&timestamp-m.rpmAt<3000&&m.rpm>=ENGINE_RUN_RPM&&
                       (uint64_t)(m.rpmSamples-m.rpmSamplesAtFuel)*2000ULL>=dt;
    const bool bridge=dt>5000&&dt<=120000&&rpmKept;
    if(bridge){++gapFills[ch];totals.gapFillSec[ch]+=(uint32_t)(dt/1000);}
    if(dt<=5000||bridge) {
      const double hours=(double)dt/3600000.0;
      const double liters=((double)m.previousFuelLph+(double)newRate)*0.5*hours;
      m.observedLiters+=liters;
      if(rawRate>=0){
        const double rawH=(prevFuelRaw[ch]+rawRate)*0.5*hours;
        totals.lifeRawH[ch]+=rawH;totals.tripRawH[ch]+=rawH;
        const uint8_t b=fuelBandFor(m.rpm);
        totals.lifeRawB[ch][b]+=rawH;totals.tripRawB[ch][b]+=rawH;
      }else{
        totals.lifetimeL[ch]+=liters;
        totals.tripL[ch]+=liters;
      }
      totalsDirty=true;
    }else ++m.gaps;
  }
  if(rawRate>=0)prevFuelRaw[ch]=rawRate;
  m.fuelLph=newRate;m.fuelAt=timestamp;m.prevFuelAt=timestamp;
  m.previousFuelLph=newRate;m.fuelEver=true;++m.fuelSamples;m.rpmSamplesAtFuel=m.rpmSamples;
}
double kadFuelScale(uint8_t ch){return (1.0/(double)cfg.kadFuelDiv)*(double)cfg.fuelCorr[ch];}
// Litres from a raw integral and its part per band, with today's scale,
// correction and band factors. Raw without a band (before 0.6.0) gets the
// common correction only.
double rawToLitres(uint8_t ch,double total,double b0,double b1,double b2){
  const double legacy=max(0.0,total-(b0+b1+b2));
  return (legacy+b0*cfg.bandAdj[0]+b1*cfg.bandAdj[1]+b2*cfg.bandAdj[2])*kadFuelScale(ch);
}
double tripLitersFor(uint8_t ch){
  if(cfg.protocol!=PROTO_KAD)return totals.tripL[ch];
  const double *b=totals.tripRawB[ch];return rawToLitres(ch,totals.tripRawH[ch],b[0],b[1],b[2]);
}
double lifetimeLitersFor(uint8_t ch){
  if(cfg.protocol!=PROTO_KAD)return totals.lifetimeL[ch];
  const double *b=totals.lifeRawB[ch];return rawToLitres(ch,totals.lifeRawH[ch],b[0],b[1],b[2]);
}
// ---------- Fuel counters, tank ----------
FuelSnap fuelSnapNow(){
  FuelSnap s;
  for(uint8_t c=0;c<2;c++){s.raw[c]=totals.lifeRawH[c];s.lit[c]=totals.lifetimeL[c];for(uint8_t b=0;b<FUEL_BANDS;b++)s.rawB[c][b]=totals.lifeRawB[c][b];}
  s.missed=totals.estMissedL;
  return s;
}
// Liters used since a snapshot, with today's scale and correction.
double usedSince(const FuelSnap &a){
  double l=0;
  for(uint8_t c=0;c<2;c++){
    if(cfg.protocol==PROTO_KAD){
      const double *n=totals.lifeRawB[c];
      l+=rawToLitres(c,totals.lifeRawH[c]-a.raw[c],n[0]-a.rawB[c][0],n[1]-a.rawB[c][1],n[2]-a.rawB[c][2]);
    }
    else l+=totals.lifetimeL[c]-a.lit[c];
  }
  return max(0.0,l+(totals.estMissedL-a.missed));
}
bool tankKnown(){return cfg.tankL>0&&totals.tankRefL>=0;}
double tankAboard(){
  if(!tankKnown())return -1;
  return constrain(totals.tankRefL-usedSince(totals.tankRef),0.0,(double)cfg.tankL);
}
int lastFullIndex(){for(uint8_t i=0;i<fuelCount;i++)if(fuelLog[i].full)return i;return -1;}
// Uncertainty of the fuel aboard, carried like a Kalman filter: the variance
// of the last reference (how exactly the tank was filled) plus what the
// counting can be off by since then. Uncalibrated engine figures are taken
// as +/-15 %, after calibration their own fitted uncertainty.
double tankVariance(){
  if(!tankKnown())return 0;
  const double since=usedSince(totals.tankRef);
  const double rel=cfg.calFills?max((double)cfg.calSig,0.01):0.15;
  return totals.tankRefVar+(rel*since)*(rel*since)+(0.005*since)*(0.005*since);
}
// About 95 % of the time the fuel aboard is within this many litres.
double tankPlusMinus(){return tankKnown()?2.0*sqrt(tankVariance()):0;}
double latestPrice(){
  for(uint8_t i=0;i<fuelCount;i++)if(fuelLog[i].price>0)return fuelLog[i].price;
  return cfg.defaultPrice;
}
// The equation for full tank i against the next older full tank; false if
// there is none.
bool calRow(uint8_t i,fuelcal::Row &row,uint8_t &older){
  older=0xFF;
  for(uint8_t k=i+1;k<fuelCount;k++)if(fuelLog[k].full){older=k;break;}
  if(older==0xFF)return false;
  const FuelRec &a=fuelLog[i],&o=fuelLog[older];
  double pump=a.litres;
  for(uint8_t k=i+1;k<older;k++)pump+=fuelLog[k].litres;
  const double div=(double)cfg.kadFuelDiv;
  double X=0,x[FUEL_BANDS]={0,0,0};
  for(uint8_t c=0;c<2;c++){
    if(cfg.protocol==PROTO_KAD){
      X+=(a.at.raw[c]-o.at.raw[c])/div;
      for(uint8_t b=0;b<FUEL_BANDS;b++)x[b]+=(a.at.rawB[c][b]-o.at.rawB[c][b])/div;
    }else X+=a.at.lit[c]-o.at.lit[c];
  }
  X+=a.at.missed-o.at.missed;
  for(uint8_t b=0;b<FUEL_BANDS;b++)x[b]=max(0.0,x[b]);
  row.a[0]=max(0.0,X);row.a[1]=x[0];row.a[2]=x[1];row.a[3]=x[2];
  row.y=pump;
  row.var=fuelcal::rowVariance(a.quality,o.quality,pump,row.a[0]);
  return true;
}
void recalibrate(){
  const float before[7]={cfg.fuelCorr[0],cfg.fuelCorr[1],cfg.bandAdj[0],cfg.bandAdj[1],cfg.bandAdj[2],cfg.calSig,(float)cfg.calFills};
  fuelcal::Row rows[MAX_FUEL];
  bool use[MAX_FUEL];
  int8_t rowOf[MAX_FUEL];
  int n=0;
  for(uint8_t i=0;i<fuelCount;i++){
    calState[i]=CAL_NONE;calZ[i]=0;calPump[i]=0;calCounted[i]=-1;calBase[i]=-1;rowOf[i]=-1;
    if(!fuelLog[i].full)continue;
    uint8_t older;
    fuelcal::Row r;
    if(!calRow(i,r,older)){calState[i]=CAL_FIRST;continue;}
    calPump[i]=(float)r.y;calBase[i]=(float)r.a[0];
    if(r.a[0]<3||r.y<3){calState[i]=CAL_SMALL;continue;}
    const double ratio=r.y/r.a[0];
    if(fuelLog[i].calUse==2)calState[i]=CAL_OFF;
    else if(fuelLog[i].calUse!=1&&(ratio<0.5||ratio>1.6))calState[i]=CAL_ODD;
    else calState[i]=CAL_USED;
    rows[n]=r;use[n]=calState[i]==CAL_USED;rowOf[i]=n;n++;
  }
  // Each equation is checked against all the others; one that does not fit
  // (more than 3 sigma) is set aside and the owner is asked about it.
  for(uint8_t i=0;i<fuelCount;i++){
    if(rowOf[i]<0||calState[i]!=CAL_USED)continue;
    const double z=fuelcal::zScore(rows,use,n,rowOf[i]);
    calZ[i]=(float)z;
    if(fuelLog[i].calUse!=1&&fabs(z)>3.0){calState[i]=CAL_ODD;use[rowOf[i]]=false;}
  }
  uint8_t used=0;double litres=0;
  fuelcal::Row mix{{0,0,0,0},0,0};
  for(uint8_t i=0;i<fuelCount;i++)if(rowOf[i]>=0&&use[rowOf[i]]){
    ++used;litres+=rows[rowOf[i]].y;
    for(int k=0;k<fuelcal::P;k++)mix.a[k]+=rows[rowOf[i]].a[k];
  }
  if(used){
    const fuelcal::Fit f=fuelcal::solve(rows,use,n);
    if(f.ok){
      const double common=constrain(f.th[0],0.5,2.0);
      for(uint8_t c=0;c<2;c++)cfg.fuelCorr[c]=(float)common;
      for(uint8_t b=0;b<FUEL_BANDS;b++)cfg.bandAdj[b]=(float)constrain((common+f.th[1+b])/common,0.8,1.25);
      // Uncertainty of the owner's own typical mix of trips.
      const double est=fuelcal::predict(f,mix);
      cfg.calSig=est>0?(float)(sqrt(fuelcal::predictVar(f,mix))/est):0.1f;
    }
  }else if(cfg.calFills){
    // The last fill-up that calibrated was removed: back to the plain scale.
    for(uint8_t c=0;c<2;c++)cfg.fuelCorr[c]=1.0f;
    for(uint8_t b=0;b<FUEL_BANDS;b++)cfg.bandAdj[b]=1.0f;
    cfg.calSig=0;
  }
  cfg.calFills=used;cfg.calLitres=(float)litres;
  cfg.fineTunes=used;
  // What EngineLink counts now (with the new figures) between full tanks.
  for(uint8_t i=0;i<fuelCount;i++)if(fuelLog[i].full&&calState[i]!=CAL_FIRST){
    uint8_t older;fuelcal::Row r;
    if(calRow(i,r,older)){
      const FuelRec &a=fuelLog[i],&o=fuelLog[older];
      double l=0;
      for(uint8_t c=0;c<2;c++){
        if(cfg.protocol==PROTO_KAD)l+=rawToLitres(c,a.at.raw[c]-o.at.raw[c],a.at.rawB[c][0]-o.at.rawB[c][0],a.at.rawB[c][1]-o.at.rawB[c][1],a.at.rawB[c][2]-o.at.rawB[c][2]);
        else l+=a.at.lit[c]-o.at.lit[c];
      }
      calCounted[i]=(float)(l+a.at.missed-o.at.missed);
    }
  }
  const float after[7]={cfg.fuelCorr[0],cfg.fuelCorr[1],cfg.bandAdj[0],cfg.bandAdj[1],cfg.bandAdj[2],cfg.calSig,(float)cfg.calFills};
  if(memcmp(before,after,sizeof(before)))saveSettings();
}
// quality: 0 not full, 1 nozzle clicked off, 2 topped up to the filler pipe.
bool addFuel(float litres,float price,uint8_t quality,int64_t utc,String &msg){
  if(!(litres>=0&&litres<=5000)){msg="Litres out of range";return false;}
  if(!(price>=0&&price<1000)){msg="Price out of range";return false;}
  if(quality>2)quality=1;
  const bool full=quality>0;
  if(!full&&litres<=0){msg="Enter the litres you added";return false;}
  FuelRec r;
  r.utc=utc>1700000000?utc:(validWall()?(int64_t)time(nullptr):0);
  r.litres=litres;r.price=price;r.full=full?1:0;r.quality=quality;
  r.prevRefL=totals.tankRefL;r.prevRef=totals.tankRef;
  // Old fields, kept for the scale check (1/128 against 1/256).
  const int pf=lastFullIndex();
  if(full&&pf>=0){
    r.counted=(float)usedSince(fuelLog[pf].at);
    double rd=0;for(uint8_t c=0;c<2;c++)rd+=totals.lifeRawH[c]-fuelLog[pf].at.raw[c];
    r.rawDelta=(float)rd;
  }
  if(full){
    totals.tankRefL=cfg.tankL>0?cfg.tankL:-1;
    totals.tankRefVar=fuelcal::fillError(quality)*fuelcal::fillError(quality);
  }else{
    const double a=tankAboard();
    if(a>=0){totals.tankRefVar=tankVariance()+(0.005*litres)*(0.005*litres);totals.tankRefL=min((double)cfg.tankL,a+litres);}
  }
  r.at=fuelSnapNow();
  totals.tankRef=r.at;
  if(fuelCount==MAX_FUEL)--fuelCount;
  memmove(&fuelLog[1],&fuelLog[0],sizeof(FuelRec)*fuelCount);
  fuelLog[0]=r;++fuelCount;
  recalibrate();
  saveFuel();saveTotals();
  msg="Saved";
  if(full)switch(calState[0]){
    case CAL_USED:
      msg="Saved. EngineLink counted "+String(calBase[0]*cfg.fuelCorr[0],1)+" L, the pump "+String(calPump[0],1)+" L. Fuel figures now within about "+String(cfg.calSig*200.0f,1)+" %";
      break;
    case CAL_ODD:
      msg="EngineLink counted "+String(calBase[0]*cfg.fuelCorr[0],1)+" L but the pump "+String(calPump[0],1)+" L. Was the tank really full? See the fuel log";
      break;
    case CAL_SMALL:msg="Saved. Too little fuel since the last full tank to calibrate";break;
    case CAL_FIRST:msg="Saved. From the next full tank on, EngineLink compares its count with the pump";break;
    default:break;
  }
  return true;
}
// Deleting the newest entry also restores the tank estimate before it.
bool deleteFuel(uint8_t i){
  if(i>=fuelCount)return false;
  if(i==0){totals.tankRefL=fuelLog[0].prevRefL;totals.tankRef=fuelLog[0].prevRef;saveTotals();}
  memmove(&fuelLog[i],&fuelLog[i+1],sizeof(FuelRec)*(fuelCount-i-1));
  --fuelCount;
  recalibrate();
  saveFuel();
  return true;
}
// The owner corrects how full the tank was, or answers "was it really full?".
bool editFuel(uint8_t i,int quality,int use){
  if(i>=fuelCount)return false;
  FuelRec &r=fuelLog[i];
  if(quality>=1&&quality<=2&&r.full){
    r.quality=(uint8_t)quality;
    if(i==0)totals.tankRefVar=fuelcal::fillError(r.quality)*fuelcal::fillError(r.quality);
  }
  if(use>=0&&use<=2)r.calUse=(uint8_t)use;
  recalibrate();
  saveFuel();saveTotals();
  return true;
}
