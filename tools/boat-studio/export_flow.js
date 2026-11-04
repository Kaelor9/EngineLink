// Copies the owner's chosen drive test pictures (safety screens and the lever
// picture for every step) from drive-test-flow.html into the phone app
// (firmware/ui/app.html, between /*DTF*/ and /*/DTF*/).
// Run from the repository root:  node tools/boat-studio/export_flow.js
// then  python3 tools/build_ui.py . Needs Playwright with Chromium.
// The owner's choice (2026-11-04), as "Kopiér valget" printed it:
// Ingen i vandet 1 · Stop 14 · Tomgang 5 · Udkig 1 · Bekræft 8 · Start 1 ·
// Trinbillede 18 · Omdrejningstællere 1/1 · Opbygning 2 · Farve 1 ·
// Nedtælling 8 · Fremskridt 7 · Værdier 8 · Stopknap 6 ·
// Vagter 1,2,3,4,5,6,7,8,9,10,12 · Advarsel 1 · Start-skærm 1 · Resultat 1 · Tekst 1
// The pictures are chosen in exportFlow() on the page; the rest is app code.
const path = require('path'), fs = require('fs');
const { chromium } = require('playwright');
(async () => {
  const root = path.resolve(__dirname, '..', '..');
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(e.message));
  await page.goto('file://' + path.join(__dirname, 'drive-test-flow.html'));
  const data = await page.evaluate(() => exportFlow());
  await browser.close();
  if (errors.length) throw new Error('Page errors: ' + errors.join('; '));
  const appPath = path.join(root, 'firmware', 'ui', 'app.html');
  const app = fs.readFileSync(appPath, 'utf8');
  const a = app.indexOf('/*DTF*/'), b = app.indexOf('/*/DTF*/');
  if (a < 0 || b < a) throw new Error('Markers /*DTF*/ ... /*/DTF*/ not found in app.html');
  fs.writeFileSync(appPath, app.slice(0, a + 7) + JSON.stringify(data) + app.slice(b));
  console.log('Drive test pictures written to app.html:', Object.keys(data).join(', '));
})().catch(e => { console.error(e); process.exit(1); });
