// Lets the demo page (tools/demo/page.html) jump the app to one of its screens.
(function(){
const wait=ms=>new Promise(r=>setTimeout(r,ms));
function scrollToHd(name){const h=[...document.querySelectorAll('.hd')].find(x=>x.textContent.trim().toLowerCase()===name.toLowerCase());if(h)h.scrollIntoView({block:'start'});}
async function reset(){if(S.sheet){closeSheet();}S.page=null;S.back=null;S.gear=null;S.fuelOpen=null;await wait(30);}
const scenes={
  tank:async()=>{await reset();goTab('tank');},
  log:async()=>{await reset();goTab('tank');S.d={};openSheet('fuel');await wait(40);S.d.fQ=2;renderSheet();},
  entry:async()=>{await reset();__demo.setOdd(false);goTab('tank');await load('fuel');S.fuelOpen=0;render(true);await wait(60);const b=document.querySelector('[data-a=fuelOpen]');if(b)b.scrollIntoView({block:'start'});},
  odd:async()=>{await reset();__demo.setOdd(true);goTab('tank');await load('fuel');S.fuelOpen=0;render(true);await wait(60);const b=document.querySelector('[data-a=fuelOpen]');if(b)b.scrollIntoView({block:'start'});},
  diag:async()=>{await reset();__demo.setOdd(false);goPage('diag','more');await wait(500);render(true);await wait(60);scrollToHd('Calibration');},
  quality:async()=>{await reset();goPage('diag','more');await wait(500);render(true);await wait(60);scrollToHd('Engine data');},
  gear:async()=>{await reset();__demo.resetGear();goPage('diag','more');await wait(400);openSheet('gear');},
  engines:async()=>{await reset();goTab('engines');},
  events:async()=>{await reset();goPage('events','more');await wait(500);const b=document.querySelector('[data-a=evOpen]');if(b)b.click();},
  balance:async()=>{await reset();goTab('engines');await wait(700);const c=[...document.querySelectorAll('.ct')].find(x=>x.textContent.trim()==='Port and starboard');if(c)c.scrollIntoView({block:'start'});},
  led:async()=>{await reset();goPage('led','more');},
  export:async()=>{await reset();goTab('history');await load('trips');openExport('trips',[2]);}
};
addEventListener('message',e=>{const s=e.data&&e.data.scene;if(scenes[s])scenes[s]();});
})();
