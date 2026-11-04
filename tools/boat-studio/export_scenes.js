// Copies the owner's chosen drive test scenes from drive-test-scenes.html
// into the phone app (firmware/ui/app.html, between /*DTS*/ and /*/DTS*/).
// Run from the repository root:  node tools/boat-studio/export_scenes.js
// then  python3 tools/build_ui.py . Needs Playwright with Chromium.
const path = require('path'), fs = require('fs');
const { chromium } = require('playwright');
// The owner's choice on the page (2026-11-04), as "Kopiér valget" printed it:
// Farver 1 · Båden 3 (fart 3) · Fortøjninger 1 (fart 4) · Fendere 13 (fart 5) ·
// Rækkefølge 2 · Ottetalsstik 2, vises 1 (fart 3) · Bølger 4 ·
// Bølgernes bevægelse 2 (fart 3) · Mellem pæle 1 · Langs kajen 1 · Åbent vand 1
// (the page numbers from 1, these values from 0).
const CHOICE = {
  sel: { pal: 0, boat: 2, spBoat: 2, rope: 0, spRope: 3, fend: 12, spFend: 4, order: 1,
         klook: 1, kshow: 0, spKnot: 2, wave: 3, wmode: 1, spWave: 2 },
  scenes: [['moorA', 0, 0], ['moorB', 1, 0], ['sea', 2, 0]],
};
(async () => {
  const root = path.resolve(__dirname, '..', '..');
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(e.message));
  await page.goto('file://' + path.join(__dirname, 'drive-test-scenes.html'));
  const data = await page.evaluate(c => exportApp(c), CHOICE);
  await browser.close();
  if (errors.length) throw new Error('Page errors: ' + errors.join('; '));
  const appPath = path.join(root, 'firmware', 'ui', 'app.html');
  const app = fs.readFileSync(appPath, 'utf8');
  const a = app.indexOf('/*DTS*/'), b = app.indexOf('/*/DTS*/');
  if (a < 0 || b < a) throw new Error('Markers /*DTS*/ ... /*/DTS*/ not found in app.html');
  fs.writeFileSync(appPath, app.slice(0, a + 7) + JSON.stringify(data) + app.slice(b));
  console.log('Drive test scenes written to app.html:', Object.keys(data).join(', '));
})().catch(e => { console.error(e); process.exit(1); });
