(() => {
  'use strict';
  const $ = selector => document.querySelector(selector);
  const $$ = selector => [...document.querySelectorAll(selector)];
  const form = $('#settings-form');
  const providerHelp = {
    gemini: 'A cloud option with limited free tiers on eligible models. A Gemini API key is needed for use; quotas and availability depend on your account.',
    openai: 'A usage-based cloud option. An API key and API billing are required. A chat subscription does not configure this device.',
    local: 'Use a reachable computer or server. No per-call cloud fee, but Ollama alone does not supply speech recognition and speech synthesis. A voice gateway is needed.',
    xiaozhi: 'Uses a managed assistant service with account/device pairing. Its model and voice choices may need to be configured on the service. The adapter is not connected yet.',
  };
  let token = '', settings = null, dirty = false, busy = false, testing = false, lastJob = '', simulator = false, activePage = 'connection', pollFailures = 0, finished = false;

  async function api(path, body) {
    const response = await fetch(`/api/${path}`, {
      method: body === undefined ? 'GET' : 'POST', cache: 'no-store',
      headers: body === undefined ? {} : { 'Content-Type': 'application/json', 'X-BitBot-Token': token },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(15000),
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'The device could not complete this request.');
    return result;
  }
  function notice(message, error = false) {
    $('#notice').textContent = message;
    $('#notice').classList.toggle('error', error);
    $('#notice').hidden = false;
  }
  function showPage(name, focus = true) {
    activePage = name;
    $$('[data-panel]').forEach(panel => { panel.hidden = panel.dataset.panel !== name; });
    $$('.nav-item').forEach(button => {
      const active = button.dataset.page === name;
      button.classList.toggle('active', active);
      if (active) button.setAttribute('aria-current', 'page'); else button.removeAttribute('aria-current');
    });
    $('#save-bar').hidden = name === 'connection';
    if (focus) $(`[data-panel="${name}"] h2`).focus({ preventScroll: true });
  }
  function setDirty(value) {
    dirty = value;
    $('#save-state').textContent = dirty ? 'You have unsaved preferences' : 'All changes saved';
  }
  function currentProvider() { return form.elements.provider.value; }
  function updateControls() {
    for (const key of ['volume', 'brightness', 'speed', 'pitch']) {
      const value = Number(form.elements[key].value);
      $(`#${key}-output`).textContent = ['volume', 'brightness'].includes(key) ? `${value}%` : key === 'speed' ? `${value.toFixed(2)}×` : `${value > 0 ? '+' : ''}${value} semitones`;
    }
    const provider = currentProvider();
    $('#provider-help').textContent = providerHelp[provider];
    form.elements.pitch.disabled = provider !== 'local';
    $('#pitch-help').textContent = provider === 'local' ? 'Reserved for a self-hosted TTS adapter with pitch support. The adapter is not connected yet.' : 'Numerical pitch is unavailable for this provider. Its speech adapter may offer style instructions later.';
    $('#preview-name').textContent = form.elements.name.value.trim().toUpperCase() || 'BITBOT';
  }
  function fillSettings(result) {
    settings = result.settings;
    for (const [name, value] of Object.entries(settings)) {
      const control = form.elements[name];
      if (!control) continue;
      if (control.type === 'checkbox') control.checked = value; else control.value = value;
    }
    $('#api-key').value = '';
    $('#clear-key').checked = false;
    $('#key-status').textContent = result.hasApiKey ? 'key saved for this provider' : 'no saved key for this provider';
    updateControls();
    setDirty(false);
  }
  function collectSettings() {
    const result = {};
    for (const [key, initial] of Object.entries(settings)) {
      const control = form.elements[key];
      result[key] = typeof initial === 'boolean' ? control.checked : typeof initial === 'number' ? Number(control.value) : control.value;
    }
    return result;
  }
  async function saveSettings() {
    if (!settings) throw new Error('Wait for the device settings to load.');
    const invalid = [...form.querySelectorAll('input,select,textarea')].find(control => !control.disabled && !control.checkValidity());
    if (invalid) { showPage(invalid.closest('[data-panel]').dataset.panel); invalid.reportValidity(); throw new Error('Please check the highlighted field.'); }
    const body = { settings: collectSettings(), clearApiKey: $('#clear-key').checked };
    if ($('#api-key').value) body.apiKey = $('#api-key').value;
    const result = await api('settings', body);
    fillSettings(result);
  }
  function setBusy(value) {
    busy = value;
    $('#save-settings').disabled = value;
    $('#finish').disabled = value || testing;
    $('#connect').disabled = value || testing;
    $('#scan').disabled = value || testing;
  }
  function element(tag, className, content) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (content !== undefined) node.textContent = content;
    return node;
  }
  function selectNetwork(network) {
    $('#ssid').value = network.ssid;
    $('#wifi-password').value = '';
    $('#open-network').checked = network.open;
    togglePassword();
    $$('.network-item').forEach(button => {
      const selected = button.dataset.ssid === network.ssid;
      button.classList.toggle('selected', selected);
      button.setAttribute('aria-pressed', String(selected));
    });
    if (!network.open) $('#wifi-password').focus({ preventScroll: true });
  }
  function togglePassword() {
    $('#wifi-password').disabled = $('#open-network').checked;
    $('#wifi-password').required = !$('#open-network').checked;
    if ($('#open-network').checked) $('#wifi-password').value = '';
  }
  async function scan() {
    $('#scan').disabled = true;
    $('#scan-hint').textContent = 'Looking for nearby 2.4 GHz networks…';
    try {
      const networks = await api('scan');
      const list = $('#network-list'); list.replaceChildren();
      for (const network of networks) {
        const button = element('button', 'network-item'); button.type = 'button'; button.dataset.ssid = network.ssid; button.setAttribute('aria-pressed', 'false');
        const signal = element('span', 'signal', network.rssi > -55 ? '▂▅▇' : network.rssi > -70 ? '▂▅' : '▂'); signal.setAttribute('aria-hidden', 'true');
        const marker = element('span', 'radio-mark'); marker.setAttribute('aria-hidden', 'true');
        button.append(signal, element('span', 'network-name', network.ssid), element('span', 'network-meta', network.open ? 'OPEN' : 'SECURED'), marker);
        button.addEventListener('click', () => selectNetwork(network)); list.append(button);
      }
      $('#scan-hint').textContent = networks.length ? 'Choose a network below, or enter a hidden network manually.' : 'No networks found yet. Scan again, or enter a hidden network manually.';
    } catch (error) { $('#scan-hint').textContent = error.message; }
    finally { $('#scan').disabled = busy || testing; }
  }
  async function refreshSaved() {
    const networks = await api('networks'); const container = $('#saved-networks'); container.replaceChildren();
    $('#saved-count').textContent = `${networks.length} / 5`;
    if (!networks.length) container.append(element('p', 'hint', 'No networks saved yet.'));
    for (const network of networks) {
      const row = element('div', 'saved-row');
      const forget = element('button', 'text-button', 'Forget'); forget.type = 'button'; forget.setAttribute('aria-label', `Forget ${network.ssid}`);
      forget.addEventListener('click', () => {
        $('#confirm-text').textContent = `BitBot will no longer reconnect automatically to “${network.ssid}”.`;
        $('#confirm-dialog').dataset.ssid = network.ssid; $('#confirm-dialog').showModal();
      });
      row.append(element('span', '', network.ssid), forget); container.append(row);
    }
  }
  function renderStatus(status) {
    simulator = status.simulator;
    $('#demo-banner').hidden = !simulator;
    $('#firmware-version').textContent = `${status.firmware}${simulator ? ' · simulator' : ' · commissioning'}`;
    $('#connection-badge').classList.toggle('online', Boolean(status.connectedSsid));
    $('#connection-label').textContent = status.connectedSsid ? 'Wi-Fi connected' : status.state === 'setup' ? 'Setup mode' : 'Offline';
    $('#network-title').textContent = status.connectedSsid || 'Ready for a connection';
    $('#network-detail').textContent = status.connectedSsid ? `${status.ip} · Internet access not checked${simulator ? ' · simulated' : ''}` : 'Connect to Wi-Fi to get started.';
    $('#network-state').textContent = status.connectedSsid ? 'CONNECTED' : 'SETUP';
    testing = status.job.state === 'testing';
    $('#connect').textContent = testing ? 'Testing connection…' : 'Connect & save ↗';
    setBusy(busy);
    if (status.job.state !== 'idle') {
      $('#connection-result').hidden = false;
      $('#connection-result').textContent = status.job.message;
      $('#connection-result').classList.toggle('error', status.job.state === 'failed');
    }
    if (status.job.state === 'connected' && lastJob !== 'connected') refreshSaved().catch(error => notice(error.message, true));
    lastJob = status.job.state;
  }
  async function poll() {
    if (finished) return;
    try { renderStatus(await api('status')); pollFailures = 0; }
    catch {
      pollFailures++;
      if (pollFailures >= 2) {
        $('#connection-label').textContent = 'Device unreachable';
        $('#connection-badge').classList.remove('online');
        notice('Connection to BitBot was interrupted. Rejoin its setup hotspot and reload this page. Unsaved preferences are still shown here.', true);
      }
    }
    setTimeout(poll, pollFailures ? 5000 : testing ? 1000 : 3500);
  }

  $$('.nav-item').forEach(button => button.addEventListener('click', () => showPage(button.dataset.page)));
  $('#travel-link').addEventListener('click', () => { showPage('connection'); $('#travel-help').open = true; });
  $('.manual-network').addEventListener('toggle', event => { if (event.target.open) $('#ssid').focus(); });
  $('#scan').addEventListener('click', scan);
  $('#open-network').addEventListener('change', togglePassword);
  $$('[data-reveal]').forEach(button => button.addEventListener('click', () => {
    const input = document.getElementById(button.dataset.reveal); const reveal = input.type === 'password'; input.type = reveal ? 'text' : 'password'; button.textContent = reveal ? 'Hide' : 'Show'; button.setAttribute('aria-label', `${reveal ? 'Hide' : 'Show'} ${input.id === 'api-key' ? 'API key' : 'Wi-Fi password'}`);
  }));
  form.addEventListener('input', event => {
    if (event.target.name === 'provider') {
      if (currentProvider() !== 'local') form.elements.pitch.value = 0;
      $('#api-key').value = ''; $('#clear-key').checked = false;
      $('#key-status').textContent = 'blank keeps any key saved for this provider';
    }
    setDirty(true); updateControls();
  });
  form.addEventListener('submit', async event => {
    event.preventDefault(); if (busy) return;
    setBusy(true);
    try { await saveSettings(); notice('Preferences saved. They will be used when the assistant and hardware adapters are integrated.'); }
    catch (error) { notice(error.message, true); }
    finally { setBusy(false); }
  });
  $('#network-form').addEventListener('submit', async event => {
    event.preventDefault(); if (testing || busy) return;
    setBusy(true);
    try {
      const job = await api('connect', { ssid: $('#ssid').value, password: $('#wifi-password').value, open: $('#open-network').checked });
      $('#wifi-password').value = ''; testing = true; lastJob = '';
      $('#connection-result').hidden = false; $('#connection-result').classList.remove('error'); $('#connection-result').textContent = job.message;
      $('#connect').textContent = 'Testing connection…';
    } catch (error) { notice(error.message, true); }
    finally { setBusy(false); }
  });
  $('#cancel-forget').addEventListener('click', () => $('#confirm-dialog').close());
  $('#confirm-forget').addEventListener('click', async () => {
    const ssid = $('#confirm-dialog').dataset.ssid; $('#confirm-dialog').close();
    try { await api('forget', { ssid }); await refreshSaved(); notice('Network forgotten. Other settings are unchanged.'); }
    catch (error) { notice(error.message, true); }
  });
  $('#finish').addEventListener('click', async () => {
    if (busy || testing || finished) return; setBusy(true);
    try {
      if (dirty) await saveSettings();
      const result = await api('finish', {});
      notice(simulator ? result.message : 'Setup complete. BitBot is restarting and its hotspot will close. Reconnect your phone to your usual Wi-Fi. Hold BOOT after boot to reopen setup.');
      if (!simulator) { finished = true; $('#connection-label').textContent = 'Restarting'; }
    } catch (error) { notice(error.message, true); }
    finally { if (!finished) setBusy(false); }
  });
  $('#greet').addEventListener('click', () => {
    $('#face-caption').textContent = form.elements.language.value === 'en' ? 'Nice to meet you!' : 'Hei, hyggelig å møte deg!';
    $('#robot').classList.add('greeting');
    setTimeout(() => { $('#robot').classList.remove('greeting'); $('#face-caption').textContent = 'Hello, world.'; }, 2600);
  });
  window.addEventListener('beforeunload', event => { if (dirty) { event.preventDefault(); event.returnValue = ''; } });
  async function init() {
    setBusy(true); togglePassword();
    try {
      token = (await api('session')).token;
      fillSettings(await api('settings'));
      renderStatus(await api('status'));
      await refreshSaved(); await scan();
      poll();
    } catch (error) { notice(`Could not load BitBot: ${error.message} Reload to try again.`, true); }
    finally { setBusy(false); }
  }
  init();
})();
