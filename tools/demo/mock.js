// Simulated EngineLink for the browser demo (site/demo, built by
// tools/build_demo.py): answers the app's /api calls without a board. Example
// fuel log and trips from a twin KAD300 boat; the engines are a simulated
// cruise. Nothing is sent anywhere.
(function(){
const now=()=>Math.floor(Date.now()/1000), t0=Date.now();
const hs=[];history.pushState=function(s){hs.push(s);};
history.back=function(){hs.pop();dispatchEvent(new PopStateEvent('popstate',{state:null}));};
try{localStorage.getItem('x');}catch(e){Object.defineProperty(window,'localStorage',{value:{getItem(){return null;},setItem(){},removeItem(){}}});}
const cfg={router:true,portChannel:1,apChannel:6,timePref:'auto',canListen:false,defaultPassword:true,protocol:'kad',kadBigEndian:true,
  kadFuelDiv:128,kadVolt:0,fuelCorr:[1,1],apName:'EngineLink',host:'enginelink',ssid:'Boat router',tankL:575,reserve:10,price:20.03,
  tripEnd:5,autoPoll:true,rawKeep:90,ntp:true,ntpServer:'pool.ntp.org',scaleOk:false,voltOk:false,fineTunes:1,gear:[0,0,0,0]};
let prefs='{}',gearStep=-1,odd=false;
// Example fill-ups: 45 L (full) then 12.1 L (full), logged as "nozzle clicked
// off" so the demo can show what "topped up" changes.
const fuel=[{utc:1793199780,l:12.1,p:20.03,full:true,q:1,use:0},{utc:1793198263,l:45,p:20.03,full:true,q:1,use:0}];
const BASE=13.9; // what the engines reported between the two full tanks (1/128)
function fillErr(q){return q===2?0.3:2.0;}
// Same fit as fuelcal.h for one equation without a band split.
function cal(){
  const r=fuel[0],o=fuel[1],y=r.l,X=BASE;
  const v=fillErr(r.q)**2+fillErr(o.q)**2+(0.005*y)**2+(0.015*X)**2;
  if(r.use===2||(odd&&r.use!==1))return {f:1,sig:0,n:0,v};
  const A=1/0.09+X*X/v,b=1/0.09+X*y/v,f=b/A,sf=Math.sqrt(1/A);
  return {f,sig:sf/f,n:1,v};
}
const trips0=[
  {id:3,g:0,s:1793199842,e:1793200237,run:396,l:[0.23,0.25],max:1121,p:0,sea:0,w:0,x:0,n:'',b:1},
  {id:2,g:0,s:1793198415,e:1793199429,run:1003,l:[6.81,6.65],max:3104,p:1,sea:1,w:1,x:0,n:'',b:1},
  {id:1,g:0,s:1793196901,e:1793197943,run:1042,l:[0.67,0.77],max:794,p:0,sea:0,w:0,x:0,n:'',b:1}];
function live(){
  const c=cal(),t=(Date.now()-t0)/1000,f=c.f;
  const e=(ch)=>{const rpm=2850+ch*30+Math.round(Math.sin(t/3+ch)*6),raw=(41.2-ch*0.6+Math.sin(t/4)*0.4);
    return {run:true,rpm,lph:+(raw*f).toFixed(1),seen:0,poll:true,why:'',hrs:772.3+ch*0.8,
      x:{256:rpm,291:2900,259:79+ch,260:31+ch,261:47+ch,265:92-ch*4,258:194,288:61,289:+(raw*f).toFixed(2),293:13.9,264:cfg.gear[0]?0x0955|(ch?0x4000:0):0x0945|(ch?0x4000:0),266:772.3+ch*0.8,295:63,263:0x24}};};
  const tripRaw=0.48+(1500+t)/3600*82;
  const used=(0.48+tripRaw-0.48)*f,since=used;
  const rel=c.n?Math.max(c.sig,0.01):0.15,pm=2*Math.sqrt(fillErr(fuel[0].q)**2+(rel*since)**2+(0.005*since)**2);
  return {v:'__VERSION__',up:Math.round(1800+t),utc:now(),ts:'ntp',proto:'kad',port:0,e:[e(0),e(1)],
    trip:{act:true,start:now()-1500-Math.round(t),run:1500+Math.round(t),l:[+(tripRaw/2*f*1.01).toFixed(2),+(tripRaw/2*f*0.99).toFixed(2)],max:2912},
    tank:{cap:575,aboard:+(575-since).toFixed(1),pm:+pm.toFixed(1),reserve:10},price:20.03,sog:null,
    net:{ap:'EngineLink',host:'enginelink',cl:1,ch:1,rt:true,sta:true,ssid:'Boat router',rssi:-42,ip:'192.168.1.20',conflict:false},
    sd:{ok:true,log:true},test:false,rb:false,fw:0};
}
function fuelList(){
  const c=cal(),r0=fuel[0];
  const cs0=r0.use===2?3:(odd&&r0.use!==1)?2:1;
  return {log:[
    {utc:r0.utc,l:r0.l,p:r0.p,full:true,c:13.9,r128:13.9,r256:7,q:r0.q,use:r0.use,cs:cs0,z:cs0===2?-4.2:0.0,pump:12.1,now:+(BASE*c.f).toFixed(1),base:BASE},
    {utc:fuel[1].utc,l:45,p:20.03,full:true,c:null,r128:0,r256:0,q:fuel[1].q,use:0,cs:5,z:0,pump:0,now:null,base:null}],
    cal:{f:c.f,b:[1,1,1],sig:c.sig,n:c.n,l:c.n?12.1:0,bands:[0.4,0.7,1.2],pm:live().tank.pm,gaps:0},
    tunes:c.n,tuneDev:0,since:+((Date.now()-t0)/3600000*82+34).toFixed(1),missed:{n:0,l:0,last:0,lastL:0}};
}
const econ={season:1793196901,b:[[1000,30,0.1,0,0,0],[1250,24,0.13,0,0,0],[1500,46,0.36,0,0,0],[1750,95,1.15,0,0,0],[2000,49,0.7,0,0,0],[2250,79,1.47,0,0,0],[2500,36,0.84,0,0,0],[2750,207,5.41,0,0,0],[3000,92,2.55,0,0,0],[3250,0,0,0,0,0],[3500,0,0,0,0,0],[3750,0,0,0,0,0]],
  z:[[2.41,1733,0],[1.74,195,0],[2.17,128,0],[6.25,243,0],[2.55,92,0]],trips:3,h:{dev:[{trips:1,n:0,rpm:0,d:[0,0,0,0],base:[null,null,null,null]},{trips:1,n:0,rpm:0,d:[0,0,0,0],base:[null,null,null,null]}],bal:-0.0365,upd:now()}};
function diag(){
  const gearRaw=[0x0945,0x0955,0x0945,0x0965,0x0945][Math.max(0,gearStep)%5];
  return {t:[0,1].map(c=>({utc:now()-3600,req:140,rep:139,err:0,ms:34,running:false,prog:0,poll:true,frames:23844,invalid:c,replyAge:120,
      raw:{256:[11400+c*120,2850+c*30,90],259:[316+c*4,79+c,700],289:[5274-c*77,41.2-c*0.6,90],293:[1063,13.9,900],264:[gearRaw|(c?0x4000:0),0,300],266:[278025+c*290,772.3+c*0.8,1200]}})),
    quality:{rejected:[0,1],held:[3,2],bridged:[1,0],bridgedSec:[6,0],gaps:[2,2],focus:0},
    nmea:0,can:false,sog:null,fuelDiv:128,scaleOk:false,volt:0,voltOk:false,hrs:[772.3,773.1],missed:0,autoPoll:true,proto:'kad',
    wifi:{apRestarts:0,joins:1,leaves:0,reason:0,staFails:0,staDrops:0,staConnects:1,ch:1,conflict:false,apStops:1,chMoves:0,httpServed:4126,httpReused:4061,httpEvicted:1,httpSlowMs:226,httpOpen:2,loopMaxMs:41},
    heap:173488,minHeap:164424,reset:3,up:1800};
}
function reply(o,code){return Promise.resolve(new Response(JSON.stringify(o),{status:code||200,headers:{'Content-Type':'application/json'}}));}
function form(b){try{return new URLSearchParams(b);}catch(e){return new URLSearchParams();}}
window.fetch=function(url,opt){
  const u=new URL(String(url),'http://enginelink.local'),p=u.pathname,m=(opt&&opt.method)||'GET',b=form(opt&&opt.body);
  if(m==='GET'){
    if(p==='/api/live')return reply(live());
    if(p==='/api/prefs')return Promise.resolve(new Response(prefs,{headers:{'Content-Type':'application/json'}}));
    if(p==='/api/trips'){const f=cal().f;return reply({trips:trips0.map(r=>Object.assign({},r,{l:r.l.map(x=>+(x*f).toFixed(2))})),port:0});}
    if(p==='/api/fuel')return reply(fuelList());
    if(p==='/api/economy')return reply(econ);
    if(p==='/api/diag')return reply(diag());
    if(p==='/api/sd/info')return reply({ok:true,inserted:true,logging:true,usedMB:2150,totalMB:30400,files:12,rawMB:1900,rawFiles:8,exports:0,trips:3,fuel:2,status:'Logging raw J1708'});
    if(p==='/api/state'){cfg.fuelCorr=[cal().f,cal().f];return reply({config:cfg,test:{running:false,available:false,rows:0,step:0,phase:'idle',seconds:0},recent:[]});}
    if(p==='/api/fw/status')return reply({state:0,bytes:0,total:0,error:'',target:'',running:'__VERSION__',pending:false,online:true,list:null});
    if(p==='/api/wifi/scan')return reply({busy:false,nets:[{s:'Boat router',r:-42,lock:true,ch:1}]});
    return reply({ok:false},404);
  }
  if(p==='/api/prefs'){prefs=b.get('p')||'{}';return reply({ok:true,message:'Saved'});}
  if(p==='/api/fuel/edit'){const r=fuel[+b.get('i')];if(b.get('q'))r.q=+b.get('q');if(b.get('use'))r.use=+b.get('use');const c=cal();
    return reply({ok:true,message:c.n?'Fuel figures now within about '+(c.sig*200).toFixed(1)+' %':'Saved'});}
  if(p==='/api/fuel/add'){return reply({ok:true,message:'Demo: nothing was saved'});}
  if(p==='/api/diag/focus'){gearStep++;return reply({ok:true,message:'OK'});}
  if(p==='/api/cfg'){for(const [k,v] of b){if(k==='gearF'||k==='gearR'){const [gm,gv]=String(v).split(':').map(Number);cfg.gear[k==='gearF'?0:2]=gm||0;cfg.gear[k==='gearF'?1:3]=gv||0;}}return reply({ok:true,message:'Saved'});}
  return reply({ok:true,message:'Demo: nothing is changed'});
};
window.__demo={setOdd(v){odd=v;},resetGear(){gearStep=-1;cfg.gear=[0,0,0,0];},resetFuel(){fuel[0].q=1;fuel[1].q=1;fuel[0].use=0;odd=false;}};
})();
