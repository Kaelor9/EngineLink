// EngineLink USB installer. Only offer firmware that was compiled and
// published: never turn a GitHub release tag into an unchecked flash URL.
(async () => {
  const status = document.querySelector('#release-status');
  const waiting = document.querySelector('#installer-wait');
  const ready = document.querySelector('#installer-ready');
  const consent = document.querySelector('#accept-risk');
  const selector = document.querySelector('#firmware-version');
  const hint = document.querySelector('#version-help');
  if (!status || !waiting || !ready || !consent || !selector || !hint) return;

  const VERSION_RE = /^\d+\.\d+\.\d+(?:-[a-z0-9.-]+)?$/;
  const IMAGE_BYTES = 8 * 1024 * 1024;
  let latestVersion = '';
  let selectedReady = false;
  let requestId = 0;
  const versions = new Map(); // version -> same-origin manifest path

  function unavailable(message) {
    selectedReady = false;
    status.classList.remove('ready');
    status.textContent = message;
    ready.replaceChildren();
    ready.hidden = true;
    waiting.hidden = false;
    waiting.innerHTML = '<button class="btn disabled" disabled>Installer unavailable</button>';
  }

  function updateConsent() {
    ready.hidden = !selectedReady || !consent.checked;
    waiting.hidden = selectedReady && consent.checked;
  }

  consent.addEventListener('change', updateConsent);

  // A full 8 MB merged image at offset zero always overwrites internal data.
  // Never offer an OTA/app-only .bin to this USB installer.
  async function inspectManifest(path, expectedVersion) {
    const url = new URL(path, document.baseURI);
    const siteRoot = new URL('.', document.baseURI);
    if (url.origin !== location.origin || !url.pathname.startsWith(siteRoot.pathname)) {
      throw Error('Unexpected installer manifest origin.');
    }
    const response = await fetch(url.toString(), { cache: 'no-store' });
    if (!response.ok) throw Error('Firmware manifest is unavailable.');
    const manifest = await response.json();
    if (manifest.name !== 'EngineLink App (alpha)' ||
        !VERSION_RE.test(manifest.version || '') ||
        (expectedVersion && manifest.version !== expectedVersion)) {
      throw Error('Firmware manifest does not match the selected EngineLink version.');
    }
    const build = (manifest.builds || []).find(b => b.chipFamily === 'ESP32-S3');
    const part = build?.parts?.find(p => p.path === 'firmware.bin' && p.offset === 0);
    if (!part) throw Error('The release is missing its complete ESP32-S3 USB image.');
    const binary = new URL(part.path, url);
    if (binary.origin !== location.origin || !binary.pathname.startsWith(siteRoot.pathname)) {
      throw Error('Firmware image must be served by EngineLink.');
    }
    const probe = await fetch(binary.toString(), { method: 'HEAD', cache: 'no-store' });
    if (!probe.ok) throw Error('The firmware image could not be found.');
    const size = Number(probe.headers.get('content-length') || 0);
    if (size && size !== IMAGE_BYTES) throw Error('The USB firmware is not a complete 8 MB image.');
    return { version: manifest.version, url: url.toString() };
  }

  function showInstaller(build, older) {
    const button = document.createElement('esp-web-install-button');
    button.setAttribute('manifest', build.url);
    // Version strings come from a strictly validated manifest, not HTML input.
    button.innerHTML = '<button slot="activate" class="installbutton">Install EngineLink v' +
      build.version + '</button>' +
      '<span slot="unsupported" class="hint">Use Chrome or Edge with Web Serial on a desktop.</span>' +
      '<span slot="not-allowed" class="hint">Open the HTTPS GitHub Pages URL.</span>';
    ready.replaceChildren(button);
    selectedReady = true;
    waiting.innerHTML = '<button class="btn disabled" disabled>Confirm hardware safety warning above</button>';
    status.textContent = 'EngineLink v' + build.version +
      (older ? ' · Older version selected · Full USB reinstall erases saved data' :
        ' available · Hardware verification required');
    status.classList.add('ready');
    updateConsent();
  }

  async function chooseVersion(choice) {
    const req = ++requestId;
    selectedReady = false;
    ready.hidden = true;
    waiting.hidden = false;
    waiting.innerHTML = '<button class="btn disabled" disabled>Checking firmware image…</button>';
    status.classList.remove('ready');
    status.textContent = 'Verifying selected firmware…';
    try {
      const older = choice !== 'latest';
      const path = older ? versions.get(choice) : 'manifest.json';
      if (!path) throw Error('This version is not in the published firmware archive.');
      const build = await inspectManifest(path, older ? choice : latestVersion);
      if (req !== requestId) return; // User changed versions while checking.
      showInstaller(build, older);
    } catch (error) {
      if (req !== requestId) return;
      unavailable(error.message || 'Could not verify the selected firmware image.');
    }
  }

  try {
    // The latest image remains usable even if the optional archive is down.
    const latest = await inspectManifest('manifest.json');
    latestVersion = latest.version;
    document.querySelectorAll('[data-version]').forEach(
      e => { e.textContent = latestVersion.replace('-', ' '); }
    );

    const option = selector.options[0];
    option.textContent = 'v' + latestVersion + ' · Latest build';
    option.value = 'latest';

    fetch(new URL('release.json', document.baseURI), { cache: 'no-store' })
      .then(r => r.ok ? r.json() : null)
      .then(info => {
        const el = document.querySelector('#ota-info');
        if (info && el && VERSION_RE.test(info.version || '')) {
          el.textContent = 'enginelink-update.bin · v' + info.version +
            ' · ' + Math.round(info.updateBytes / 1024) +
            ' KB · install from Settings → Firmware update';
        }
      }).catch(() => {});

    await import('https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module');

    selector.disabled = false;
    selector.addEventListener('change', () => chooseVersion(selector.value));
    showInstaller(latest, false);

    // This JSON lists only full 8 MB images successfully copied from GitHub
    // Releases to GitHub Pages. Every old manifest + binary is same-origin,
    // avoiding cross-origin firmware download failures in ESP Web Tools.
    try {
      const url = new URL('versions/index.json', document.baseURI);
      const response = await fetch(url.toString(), { cache: 'no-store' });
      if (!response.ok) throw Error('Archive index unavailable');
      const catalog = await response.json();
      if (!Array.isArray(catalog.versions)) throw Error('Invalid archive index');
      for (const item of catalog.versions) {
        const version = item.v;
        if (!VERSION_RE.test(version || '') || version === latestVersion ||
            versions.has(version)) continue;
        const path = 'versions/v' + version + '/manifest.json';
        versions.set(version, path);
        const date = /^\d{4}-\d{2}-\d{2}$/.test(item.date || '') ? ' · ' + item.date : '';
        selector.add(new Option('v' + version + date, version));
      }
      hint.textContent = versions.size
        ? 'Choose the latest build or any listed older release. All USB versions erase settings and internal history; the microSD card is unaffected.'
        : 'The latest build is selected. There are no older USB images in this archive.';
    } catch (error) {
      console.warn('Older firmware archive unavailable:', error);
      hint.textContent = 'Latest firmware is available. Older versions are temporarily unavailable.';
    }
  } catch (error) {
    console.warn('EngineLink installer unavailable:', error);
    unavailable(error.message || 'The firmware installer could not be loaded.');
  }
})();
