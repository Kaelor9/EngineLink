// EngineLink browser installer.
// The page must never present an unbuilt download as a validated release.
// GitHub Pages publishes the merged binary only after successful compilation;
// the version shown comes from the generated manifest, never from this file.
(async () => {
  const status = document.querySelector('#release-status');
  const waiting = document.querySelector('#installer-wait');
  const ready = document.querySelector('#installer-ready');
  const consent = document.querySelector('#accept-risk');
  if (!status || !waiting || !ready || !consent) return;

  const unavailable = (message) => {
    status.textContent = message;
    waiting.innerHTML = '<button class="btn disabled" disabled>Installer unavailable</button>';
  };
  try {
    const manifestUrl = new URL('manifest.json', document.baseURI);
    const response = await fetch(manifestUrl.toString(), { cache: 'no-store' });
    if (!response.ok) return unavailable('No compiled EngineLink firmware has been published yet.');
    const manifest = await response.json();
    if (manifest.name !== 'EngineLink App (alpha)' || !/^\d+\.\d+\.\d+(-[a-z]+)?$/.test(manifest.version || '')) {
      return unavailable('Installer metadata does not match the EngineLink application build.');
    }
    const version = manifest.version;
    document.querySelectorAll('[data-version]').forEach(e => { e.textContent = version.replace('-', ' '); });

    const build = (manifest.builds || []).find(b => b.chipFamily === 'ESP32-S3');
    const part = build?.parts?.find(p => p.path === 'firmware.bin' && p.offset === 0);
    if (!part) return unavailable('A merged ESP32-S3 binary is required.');
    const binaryUrl = new URL(part.path, manifestUrl);
    if (binaryUrl.origin !== location.origin) return unavailable('Unexpected firmware download origin.');
    const binaryResponse = await fetch(binaryUrl.toString(), { method: 'HEAD', cache: 'no-store' });
    if (!binaryResponse.ok) return unavailable('Compiled firmware binary is missing.');
    const size = Number(binaryResponse.headers.get('content-length') || 0);
    if (size > 0 && size < 1024) return unavailable('Firmware file is invalid.');

    fetch(new URL('release.json', document.baseURI), { cache: 'no-store' })
      .then(r => r.ok ? r.json() : null)
      .then(info => {
        const el = document.querySelector('#ota-info');
        if (info && el) el.textContent = 'enginelink-update.bin · v' + info.version + ' · ' + Math.round(info.updateBytes / 1024) + ' KB · install from Settings → Firmware update';
      }).catch(() => {});

    await import('https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module');
    const install = document.createElement('esp-web-install-button');
    install.setAttribute('manifest', manifestUrl.toString());
    install.innerHTML = '<button slot="activate" class="installbutton">Install EngineLink ' + version.replace('-', ' ') + '</button>' +
       '<span slot="unsupported" class="hint">Use Chrome or Edge with Web Serial on a desktop.</span>' +
       '<span slot="not-allowed" class="hint">Open the HTTPS GitHub Pages URL.</span>';
    ready.appendChild(install);
    ready.hidden = !consent.checked;
    waiting.hidden = consent.checked;

    const prompt = waiting.querySelector('button');
    prompt.textContent = 'Confirm hardware safety warning above';
    status.textContent = 'EngineLink ' + version + ' available · Hardware verification required';
    status.classList.add('ready');

    consent.addEventListener('change', () => {
      ready.hidden = !consent.checked;
      waiting.hidden = consent.checked;
    });
  } catch (error) {
    console.warn('EngineLink installer unavailable:', error);
    unavailable('The firmware installer could not be loaded.');
  }
})();
