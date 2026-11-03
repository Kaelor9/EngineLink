// Browser-level regression for the installer dropdown without network/USB.
// Run with: node tools/test_firmware_selector.js
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const source = fs.readFileSync('site/app.js', 'utf8');
const importStatement = "import('https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module')";
assert.ok(source.includes(importStatement));
const testSource = source.replace(importStatement, 'loadFakeInstaller()');

const manifest = version => ({
  name: 'EngineLink App (alpha)',
  version,
  builds: [{chipFamily: 'ESP32-S3', parts: [{path: 'firmware.bin', offset: 0}]}]
});

function mockElement() {
  const classes = new Set();
  return {
    textContent: '', innerHTML: '', hidden: false, disabled: false,
    attributes: {}, children: [], events: {}, classList: {
      add: x => classes.add(x), remove: x => classes.delete(x)
    },
    addEventListener(event, fn) { this.events[event] = fn; },
    replaceChildren(...children) { this.children = children; },
    setAttribute(k, v) { this.attributes[k] = v; }
  };
}

async function runCase({blockedLibrary, missingLatest}) {
  const elements = new Map();
  for (const id of ['release-status','installer-wait','installer-ready',
    'accept-risk','firmware-version','version-help','ota-info']) {
    elements.set('#' + id, mockElement());
  }
  const selector = elements.get('#firmware-version');
  selector.options = [{value: 'latest', textContent: 'Checking available versions…'}];
  selector.value = 'latest';
  selector.disabled = true;
  selector.add = opt => selector.options.push(opt);

  const htmlVersions = [mockElement()];
  const documents = {
    baseURI: 'https://kaelor9.github.io/EngineLink/',
    querySelector: sel => elements.get(sel),
    querySelectorAll: sel => sel === '[data-version]' ? htmlVersions : [],
    createElement: () => mockElement()
  };
  const fetch = async (url, init={}) => {
    const path = new URL(url).pathname;
    if (path.endsWith('/versions/index.json')) {
      return {ok:true, json: async () => ({versions: [
        {v: '0.13.4', date: '2026-11-02'},
        {v: '0.12.1', date: '2026-11-01'}
      ]})};
    }
    if (path.endsWith('release.json')) {
      return {ok:true,json:async()=>({version:'0.14.1',updateBytes:1600000})};
    }
    if (path.endsWith('/manifest.json')) {
      if (missingLatest && path === '/EngineLink/manifest.json') return {ok:false};
      return {ok:true,json:async()=>manifest(
        path.includes('/v0.13.4/') ? '0.13.4' :
        path.includes('/v0.12.1/') ? '0.12.1' : '0.14.1'
      )};
    }
    if (init.method === 'HEAD' && path.endsWith('/firmware.bin')) {
      // A compressed CDN response may advertise the transferred byte count,
      // not the uncompressed 8 MiB firmware size from the GitHub release.
      return {ok:true,headers:{get:header =>
        header === 'content-length' ? '1660176' : 'gzip'}};
    }
    throw Error('Unexpected fetch: ' + path);
  };

  const context = {
    document: documents,
    location: {origin:'https://kaelor9.github.io'},
    fetch, URL,
    Option: function(label,value) {this.textContent=label;this.value=value;},
    loadFakeInstaller: () => blockedLibrary
      ? Promise.reject(Error('CDN blocked')) : Promise.resolve(),
    console: {warn() {}}
  };
  vm.runInNewContext(testSource, context, {filename:'installer.js'});
  // The selector must be enabled synchronously, BEFORE any fetch/CDN import.
  assert.equal(selector.disabled, false);
  for (let i = 0; i < 8; i++) await new Promise(resolve => setImmediate(resolve));

  assert.equal(selector.options.length, 3, 'Older releases must load independent of installer');
  assert.equal(selector.options[1].value, '0.13.4');
  assert.equal(selector.disabled, false);
  selector.value = '0.13.4';
  selector.events.change();
  for (let i = 0; i < 8; i++) await new Promise(resolve => setImmediate(resolve));

  assert.equal(selector.disabled, false, 'A CDN failure must not lock the dropdown');
  if (blockedLibrary) {
    assert.match(elements.get('#release-status').textContent, /library could not load/);
    assert.equal(elements.get('#installer-ready').children.length, 0);
  } else {
    const button = elements.get('#installer-ready').children[0];
    assert.ok(button, 'Install button should be present');
    assert.match(button.attributes.manifest, /versions\/v0\.13\.4\/manifest\.json$/);
    assert.equal(elements.get('#installer-ready').hidden, true, 'Consent required');
    elements.get('#accept-risk').checked = true;
    elements.get('#accept-risk').events.change();
    assert.equal(elements.get('#installer-ready').hidden, false);
  }
}

(async () => {
  await runCase({blockedLibrary:true, missingLatest:false});
  await runCase({blockedLibrary:false, missingLatest:false});
  await runCase({blockedLibrary:true, missingLatest:true});
  console.log('Firmware selector handles compressed CDN content length, blocked CDN, normal USB and missing latest manifest');
})().catch(e => {console.error(e);process.exitCode=1;});
