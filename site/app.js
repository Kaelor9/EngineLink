// EngineLink USB installer. The firmware selector is independent of USB support:
// selecting historical releases must work even if the Web Serial library/CDN
// cannot load. Only verified, complete, same-origin merged images can be flashed.
(async () => {
  const status = document.querySelector('#release-status');
  const waiting = document.querySelector('#installer-wait');
  const ready = document.querySelector('#installer-ready');
  const consent = document.querySelector('#accept-risk');
  const selector = document.querySelector('#firmware-version');
  const hint = document.querySelector('#version-help');
  if (!status || !waiting || !ready || !consent || !selector || !hint) return;

  const VERSION_RE = /^\d+\.\d+\.\d+(?:-[a-z0-9.-]+)?$/;
  const versions = new Map(); // version -> same-origin manifest path
  let latestVersion = '';
  let latestBuild = null;
  let selectedReady = false;
  let requestId = 0;
  let installerLibraryPromise = null;

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
  // Never offer an OTA/app-only .bin to the USB installer.
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
    // Do not compare HEAD Content-Length with the 8 MiB image size.
    // It describes the *HTTP representation*, which a Pages/CDN proxy may
    // compress, not necessarily the decoded firmware bytes Web Tools flashes.
    // The Pages release job already verified each archived USB image is
    // exactly 8 MiB, with a valid ESP boot header and release SHA-256.
    // The current image is checked for its full size during compilation.
    return { version: manifest.version, url: url.toString() };
  }

  // Only the actual flashing button requires ESP Web Tools. Loading this
  // module must never block listing or choosing historical firmware.
  function loadInstallerLibrary() {
    if (!installerLibraryPromise) {
      installerLibraryPromise = import('https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module')
        .catch(error => {
          installerLibraryPromise = null; // allow a later retry
          throw error;
        });
    }
    return installerLibraryPromise;
  }

  function showInstaller(build, older) {
    const button = document.createElement('esp-web-install-button');
    button.setAttribute('manifest', build.url);
    // Version strings come from a strictly validated manifest, not user input.
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
      const build = !older && latestBuild ? latestBuild : await inspectManifest(path, older ? choice : null);
      if (req !== requestId) return; // Ignore stale requests after a new selection.
      status.textContent = 'EngineLink v' + build.version + ' verified · Loading USB installer…';
      try {
        await loadInstallerLibrary();
      } catch (error) {
        if (req !== requestId) return;
        unavailable('EngineLink v' + build.version +
          ' is available, but the USB installer library could not load. Check your connection or try Chrome/Edge.');
        return;
      }
      if (req !== requestId) return;
      showInstaller(build, older);
    } catch (error) {
      if (req !== requestId) return;
      unavailable(error.message || 'Could not verify the selected firmware image.');
    }
  }

  // Register the selector before ANY network operation. A failed CDN import,
  // a missing manifest, or unsupported Web Serial must not freeze this control.
  selector.disabled = false;
  selector.options[0].textContent = 'Latest firmware · Checking…';
  selector.addEventListener('change', () => chooseVersion(selector.value));

  // Load the archive separately from the latest image and the flash library.
  // This way the dropdown still fills on browsers without Web Serial support.
  async function loadArchive() {
    try {
      const response = await fetch(new URL('versions/index.json', document.baseURI), { cache: 'no-store' });
      if (!response.ok) throw Error('Archive index unavailable');
      const catalog = await response.json();
      if (!Array.isArray(catalog.versions)) throw Error('Invalid archive index');
      for (const item of catalog.versions) {
        const version = item.v;
        if (!VERSION_RE.test(version || '') || version === latestVersion || versions.has(version)) continue;
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
      hint.textContent = 'Older versions could not be listed. Try reloading the page; the latest version is still available.';
    }
  }

  async function loadLatest() {
    try {
      const latest = await inspectManifest('manifest.json');
      latestVersion = latest.version;
      latestBuild = latest;
      document.querySelectorAll('[data-version]').forEach(
        e => { e.textContent = latestVersion.replace('-', ' '); }
      );
      selector.options[0].textContent = 'v' + latestVersion + ' · Latest build';

      // Do not overwrite the user's older-version choice when the latest
      // manifest finishes loading in the background.
      if (selector.value === 'latest') await chooseVersion('latest');
    } catch (error) {
      console.warn('Latest EngineLink firmware unavailable:', error);
      if (selector.value === 'latest') unavailable(
        'Latest firmware could not be verified. You can still select a listed older release.'
      );
      selector.options[0].textContent = 'Latest firmware · Currently unavailable';
    }
  }

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

  loadArchive();
  loadLatest();
})();
