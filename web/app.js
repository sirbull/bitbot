(() => {
  'use strict';
  const $ = selector => document.querySelector(selector);
  const $$ = selector => [...document.querySelectorAll(selector)];
  const form = $('#settings-form');
  const providerInfo = {
    xiaozhi: {
      requirement: 'Nothing else to enter for XiaoZhi',
      help: 'This is the recommended default. No model ID, service URL, or API key is needed on this page. Account and device pairing will be handled through the XiaoZhi service when its adapter is added.',
      fields: false,
    },
    gemini: {
      requirement: 'You will need a Gemini API key',
      help: 'Create a key in Google AI Studio. A free allowance may be available, depending on the model, account, and current quotas. You can add the key later.',
      model: 'Optional', endpoint: 'Optional', key: 'Required to use', fields: true,
    },
    openai: {
      requirement: 'You will need an OpenAI API key and API billing',
      help: 'A ChatGPT subscription does not supply an API key or API credit. You can add the key later.',
      model: 'Optional', endpoint: 'Optional', key: 'Required to use', fields: true,
    },
    local: {
      requirement: 'You will need the address of your own service',
      help: 'This avoids per-call cloud fees, but a reachable voice gateway is required. Ollama by itself does not provide speech recognition or speech synthesis.',
      model: 'Optional', endpoint: 'Required to use', key: 'Usually not needed', fields: true,
    },
  };
  const languageCatalogs = {
    xiaozhi: 'zh,zh-Hant,en,ja,ko,vi,th,de,fr,es,it,ru,ar,hi,mr,pt,pt-BR,pl,cs,fi,tr,id,uk,ro,bg,ca,da,el,fa,fil,he,hr,hu,ms,nb,nl,sk,sl,sv,sr'.split(','),
    geminiLive: 'af,ak,sq,am,ar,hy,as,az,eu,be,bn,bs,bg,my,ca,ceb,zh,hr,cs,da,nl,en,et,fo,fil,fi,fr,gl,ka,de,el,gu,ha,he,hi,hu,is,id,ga,it,ja,kn,kk,km,rw,ko,ku,ky,lo,lv,lt,mk,ms,ml,mt,mi,mn,mr,ne,no,or,om,ps,fa,pl,pt,pa,qu,ro,rm,ru,sr,sd,si,sk,sl,so,st,es,sw,sv,tg,ta,te,th,tn,tr,tk,uk,ur,uz,vi,cy,fy,wo,yo,zu'.split(','),
    geminiTts: 'ar,bn,nl,en,fr,de,hi,id,it,ja,ko,mr,pl,pt,ro,ru,es,ta,te,th,tr,uk,vi,af,sq,am,hy,az,eu,be,bg,my,ca,ceb,cmn,hr,cs,da,et,fil,fi,gl,ka,el,gu,ht,he,hu,is,jv,kn,kok,lo,la,lv,lt,lb,mk,mai,mg,ms,ml,mn,ne,nb,nn,or,ps,fa,pa,sr,sd,si,sk,sl,sw,sv,ur'.split(','),
    openai: 'af,ar,hy,az,be,bs,bg,ca,zh,hr,cs,da,nl,en,et,fi,fr,gl,de,el,he,hi,hu,is,id,it,ja,kn,kk,ko,lv,lt,mk,ms,mr,mi,ne,no,fa,pl,pt,ro,ru,sr,sk,sl,es,sw,sv,tl,ta,th,tr,uk,ur,vi,cy'.split(','),
  };
  languageCatalogs.local = [...new Set([...languageCatalogs.geminiLive, ...languageCatalogs.geminiTts, ...languageCatalogs.openai, ...languageCatalogs.xiaozhi])];
  const languageNames = typeof Intl.DisplayNames === 'function' ? new Intl.DisplayNames(['en'], { type: 'language' }) : null;
  const languageFallbacks = { nb: 'Norwegian Bokmål', nn: 'Norwegian Nynorsk', no: 'Norwegian', fil: 'Filipino', cmn: 'Chinese, Mandarin', 'zh-Hant': 'Chinese, Traditional' };
  let token = '', settings = null, dirty = false, busy = false, testing = false, lastJob = '', simulator = false, activePage = 'connection', pollFailures = 0, finished = false, savedNetworkCount = 0, keySaved = false;

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
    $$('.nav-item').forEach(button => {
      const active = button.dataset.page === name;
      button.classList.toggle('active', active);
      if (active) button.setAttribute('aria-current', 'page'); else button.removeAttribute('aria-current');
    });
    if (focus) {
      const heading = $(`[data-panel="${name}"] h2`);
      heading.focus({ preventScroll: true });
      heading.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }
  }
  function setDirty(value) {
    dirty = value;
    $('#save-state').textContent = dirty ? 'You have unsaved preferences' : 'All changes saved';
  }
  function currentProvider() { return form.elements.provider.value; }
  function languageProfile() {
    const provider = currentProvider();
    const model = form.elements.model.value.trim().toLowerCase();
    if (provider === 'xiaozhi') return { codes: languageCatalogs.xiaozhi, label: 'XiaoZhi firmware locales', note: `XiaoZhi offers ${languageCatalogs.xiaozhi.length} device locales. Spoken output also depends on the voice configured in the XiaoZhi service.` };
    if (provider === 'gemini' && model.includes('tts')) return { codes: languageCatalogs.geminiTts, label: 'Gemini TTS languages', note: `This Gemini TTS catalog contains ${languageCatalogs.geminiTts.length} documented output languages for current TTS models.` };
    if (provider === 'gemini') return { codes: languageCatalogs.geminiLive, label: 'Gemini Live languages', note: `Gemini Live documents ${languageCatalogs.geminiLive.length} languages. Native audio detects language automatically; the preference is supplied as an instruction.` };
    if (provider === 'openai') return { codes: languageCatalogs.openai, label: 'OpenAI speech languages', note: `OpenAI documents ${languageCatalogs.openai.length} TTS languages. Voice quality varies, and current voices are optimized for English.` };
    return { codes: languageCatalogs.local, label: 'Languages for a custom adapter', note: 'A self-hosted model has no universal language list. These common BCP-47 choices are available as preferences; the future adapter will replace them with capabilities reported by your voice gateway.' };
  }
  function updateLanguageOptions() {
    const select = form.elements.language;
    const selected = select.value || settings?.language || 'auto';
    const profile = languageProfile();
    select.replaceChildren();
    const automatic = document.createElement('optgroup'); automatic.label = 'Automatic';
    for (const [value, label] of [['auto', 'Automatic · Norwegian + English'], ['auto-all', 'Automatic · any supported language']]) {
      const option = document.createElement('option'); option.value = value; option.textContent = label; automatic.append(option);
    }
    const supported = document.createElement('optgroup'); supported.label = profile.label;
    const codes = [...profile.codes].sort((a, b) => {
      const aName = languageFallbacks[a] || languageNames?.of(a) || a;
      const bName = languageFallbacks[b] || languageNames?.of(b) || b;
      return aName.localeCompare(bName, 'en');
    });
    for (const code of codes) {
      const option = document.createElement('option'); option.value = code;
      option.textContent = `${languageFallbacks[code] || languageNames?.of(code) || code.toUpperCase()} · ${code}`;
      supported.append(option);
    }
    select.append(automatic, supported);
    if (![...select.options].some(option => option.value === selected)) {
      const custom = document.createElement('option'); custom.value = selected; custom.textContent = `${selected} · saved, support not verified`;
      select.append(custom);
    }
    select.value = selected;
    $('#language-support-note').textContent = profile.note;
  }
  function updateControls() {
    for (const key of ['volume', 'brightness', 'speed', 'pitch']) {
      const value = Number(form.elements[key].value);
      $(`#${key}-output`).textContent = ['volume', 'brightness'].includes(key) ? `${value}%` : key === 'speed' ? `${value.toFixed(2)}×` : `${value > 0 ? '+' : ''}${value} semitones`;
    }
    const provider = currentProvider();
    const info = providerInfo[provider];
    $('#provider-requirement').textContent = info.requirement;
    $('#provider-help').textContent = info.help;
    $('#provider-fields').hidden = !info.fields;
    if (info.fields) {
      $('#model-status').textContent = info.model;
      $('#endpoint-status').textContent = info.endpoint;
      $('#key-status').textContent = keySaved ? 'Saved' : info.key;
    }
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
    keySaved = result.hasApiKey;
    updateControls();
    updateLanguageOptions();
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
    form.inert = value;
    $('#network-form').inert = value;
    $('#save-settings').disabled = value;
    $('#finish').disabled = value || testing;
    $('#connect').disabled = value || testing;
    $('#scan').disabled = value || testing;
    $('#continue-personality').disabled = value || testing || savedNetworkCount === 0;
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
    savedNetworkCount = networks.length;
    $('#saved-count').textContent = `${networks.length} / 5`;
    $('#continue-personality').disabled = busy || testing || networks.length === 0;
    $('#connection-next-help').textContent = networks.length ? 'Network saved. You are ready to continue.' : 'Connect and save a network first.';
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
  $$('[data-continue]').forEach(button => button.addEventListener('click', () => showPage(button.dataset.continue)));
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
      keySaved = false;
    }
    setDirty(true); updateControls();
    if (event.target.name === 'provider' || event.target.name === 'model') updateLanguageOptions();
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
    $('#face-caption').textContent = ['nb', 'nn', 'no'].includes(form.elements.language.value) ? 'Hei, hyggelig å møte deg!' : 'Nice to meet you!';
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
