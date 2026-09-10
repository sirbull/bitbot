import test from 'node:test';
import assert from 'node:assert/strict';
import { defaults, validateSettings, validateNetwork, updateDocument, publicSettings } from '../lib/settings.mjs';
import { Simulator, initialDocument } from '../lib/simulator.mjs';

test('Norwegian settings survive save/reload; API responses do not contain keys', async () => {
  let persisted;
  const bot = new Simulator({ persist: async doc => { persisted = structuredClone(doc); } });
  await bot.save({ settings: { name: 'Blåbær', personality: 'Vær nysgjerrig. Svar på norsk: æ ø å Æ Ø Å.' }, apiKey: 'dummy-secret' });
  const reloaded = new Simulator({ document: persisted });
  assert.equal(reloaded.settings().settings.name, 'Blåbær');
  assert.equal(reloaded.settings().hasApiKey, true);
  assert.ok(!JSON.stringify(reloaded.settings()).includes('dummy-secret'));
  await reloaded.save({ settings: { volume: 0 }, apiKey: '' });
  assert.equal(reloaded.document.keys.gemini, 'dummy-secret');
});

test('provider keys stay separate; clearing one never removes the others', () => {
  let doc = updateDocument(initialDocument(), { settings: {}, apiKey: 'gemini-secret' });
  doc = updateDocument(doc, { settings: { provider: 'openai' }, apiKey: 'openai-secret' });
  doc = updateDocument(doc, { settings: { provider: 'gemini' }, clearApiKey: true });
  assert.equal(doc.keys.openai, 'openai-secret');
  assert.equal(publicSettings(doc).hasApiKey, false);
  assert.throws(() => updateDocument(doc, { settings: {}, apiKey: 'new', clearApiKey: true }));
});

test('server enforces byte limits, type limits, URLs, and capabilities', () => {
  const invalid = [{ name: ' ' }, { name: 'å'.repeat(25) }, { volume: 101 }, { volume: '50' }, { idleSeconds: 15.5 }, { captions: 'true' }, { provider: 'invented' }, { pitch: 2 }, { endpoint: 'javascript:alert(1)' }, { endpoint: 'https://key:secret@example.com' }, { endpoint: 'https://example.com?key=secret' }, { endpoint: 'http://example.com' }, { unknown: 5 }, { name: 'a\0b' }];
  for (const patch of invalid) assert.throws(() => validateSettings(patch), JSON.stringify(patch));
  assert.equal(validateSettings({ provider: 'local', endpoint: 'http://192.168.1.4:8080', pitch: -3 }).pitch, -3);
  assert.deepEqual(validateSettings({}), defaults());
});

test('network names use UTF-8 byte limits; passwords/open mode are explicit', () => {
  assert.equal(validateNetwork({ ssid: 'æ'.repeat(16), password: '12345678' }).ssid.length, 16);
  assert.throws(() => validateNetwork({ ssid: 'æ'.repeat(17), password: '12345678' }));
  assert.throws(() => validateNetwork({ ssid: 'home', password: '' }));
  assert.throws(() => validateNetwork({ ssid: 'home', password: 'secret123', open: true }));
  assert.throws(() => validateNetwork({ ssid: 'home', password: 'a'.repeat(65) }));
  assert.equal(validateNetwork({ ssid: 'public', open: true }).password, '');
});

test('failed Wi-Fi changes preserve the existing profile and all assistant settings', async () => {
  const bot = new Simulator({ delay: 1 });
  await bot.save({ settings: { name: 'Travel companion' }, apiKey: 'dummy-secret' });
  bot.connect({ ssid: 'Home', password: 'old-password' }); await bot.connectionTask;
  const before = structuredClone(bot.document);
  bot.connect({ ssid: 'Home', password: 'wrong-password' }); await bot.connectionTask;
  assert.equal(bot.job.state, 'failed'); assert.deepEqual(bot.document, before);
  bot.connect({ ssid: 'Holiday', password: 'new-password' }); await bot.connectionTask;
  assert.equal(bot.networks().length, 2); assert.deepEqual(bot.document.settings, before.settings);
  assert.deepEqual(bot.document.keys, before.keys);
  assert.ok(!JSON.stringify(bot.networks()).includes('password'));
  bot.finish(); assert.equal(bot.status().connectedSsid, 'Holiday');
  assert.equal(bot.document.settings.name, 'Travel companion');
});

test('storage failure never publishes unsaved settings or networks', async () => {
  const bot = new Simulator({ delay: 1, persist: async () => { throw new Error('disk full'); } });
  await assert.rejects(bot.save({ settings: { name: 'Unstored' } }));
  assert.equal(bot.settings().settings.name, 'BitBot');
  bot.connect({ ssid: 'Home', password: 'test-password' }); await bot.connectionTask;
  assert.equal(bot.job.state, 'failed'); assert.equal(bot.networks().length, 0);
});

test('bounded network list, explicit forgetting, and overlapping operation protection', async () => {
  const bot = new Simulator({ delay: 1 });
  assert.throws(() => bot.finish());
  for (let i = 0; i < 5; i++) {
    bot.connect({ ssid: `Network ${i}`, password: 'test-password' });
    assert.throws(() => bot.connect({ ssid: 'Concurrent', password: 'test-password' }));
    assert.throws(() => bot.finish()); await bot.connectionTask;
  }
  assert.throws(() => bot.connect({ ssid: 'Sixth', password: 'test-password' }));
  bot.connect({ ssid: 'Network 0', password: 'replacement-password' }); await bot.connectionTask;
  assert.equal(bot.networks().length, 5);
  await bot.forget({ ssid: 'Network 0' });
  assert.equal(bot.networks().length, 4);
  await assert.rejects(bot.forget({ ssid: 'Unknown' }));
});

test('simultaneous writes serialize without losing a successful network', async () => {
  const bot = new Simulator({ delay: 1, persist: async () => new Promise(resolve => setTimeout(resolve, 10)) });
  bot.connect({ ssid: 'Home', password: 'test-password' });
  await Promise.all([bot.save({ settings: { name: 'First' } }), bot.save({ settings: { volume: 20 } }), bot.connectionTask]);
  assert.equal(bot.settings().settings.name, 'First'); assert.equal(bot.settings().settings.volume, 20);
  assert.equal(bot.networks()[0].ssid, 'Home');
});
