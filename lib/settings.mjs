import schema from '../config/settings-schema.json' with { type: 'json' };

export const defaults = () => Object.fromEntries(Object.entries(schema.fields).map(([key, rule]) => [key, rule.default]));
const bytes = value => Buffer.byteLength(value, 'utf8');
export class InputError extends Error {}
export function assert(condition, message) { if (!condition) throw new InputError(message); }
export function object(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }

export function validateSettings(patch, current = defaults()) {
  assert(object(patch), 'Settings must be an object.');
  const result = { ...current };
  for (const [key, value] of Object.entries(patch)) {
    const rule = schema.fields[key];
    assert(Boolean(rule), `Unknown setting: ${key}.`);
    if (['string', 'url', 'enum'].includes(rule.type)) {
      assert(typeof value === 'string' && !/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/u.test(value), `Invalid ${key}.`);
      assert(rule.maxBytes === undefined || bytes(value) <= rule.maxBytes, `${key} is too long.`);
      assert(!rule.minBytes || bytes(value.trim()) >= rule.minBytes, `${key} is required.`);
      assert(!rule.values || rule.values.includes(value), `Unsupported ${key}.`);
      if (rule.type === 'url' && value) {
        let url;
        try { url = new URL(value); } catch { throw new InputError('Enter a valid service URL.'); }
        assert(['https:', 'http:', 'wss:', 'ws:'].includes(url.protocol) && Boolean(url.hostname) && !url.username && !url.password && !url.search && !url.hash, 'Use a service URL without credentials, query, or fragment.');
      }
    } else if (rule.type === 'boolean') {
      assert(typeof value === 'boolean', `Invalid ${key}.`);
    } else {
      assert(typeof value === 'number' && Number.isFinite(value) && value >= rule.min && value <= rule.max && (rule.type !== 'integer' || Number.isInteger(value)), `Invalid ${key}.`);
    }
    result[key] = value;
  }
  assert(result.provider === 'local' || result.pitch === 0, 'Numerical pitch is only reserved for the self-hosted adapter.');
  assert(result.provider === 'local' || !/^(http|ws):/u.test(result.endpoint), 'Cloud service URLs must use HTTPS or WSS.');
  return result;
}

export function validateNetwork(input) {
  assert(object(input) && Object.keys(input).every(key => ['ssid', 'password', 'open'].includes(key)), 'Invalid network request.');
  const { ssid, password = '', open = false } = input;
  assert(typeof ssid === 'string' && bytes(ssid) > 0 && bytes(ssid) <= 32 && !/[\u0000-\u001f\u007f]/u.test(ssid), 'Network name must be 1–32 UTF-8 bytes.');
  assert(typeof open === 'boolean' && typeof password === 'string', 'Invalid Wi-Fi password.');
  assert(open ? password === '' : /^[\x20-\x7e]{8,63}$/u.test(password) || /^[a-f\d]{64}$/iu.test(password), 'Use an 8–63 character Wi-Fi password, or select an open network.');
  return { ssid, password, open };
}

export function updateDocument(document, input) {
  assert(object(input) && Object.keys(input).every(key => ['settings', 'apiKey', 'clearApiKey'].includes(key)), 'Invalid settings request.');
  const settings = validateSettings(input.settings, document.settings);
  assert(input.apiKey === undefined || (typeof input.apiKey === 'string' && bytes(input.apiKey) <= 256 && !/[\u0000-\u0020\u007f]/u.test(input.apiKey)), 'Invalid API key.');
  assert(input.clearApiKey === undefined || typeof input.clearApiKey === 'boolean', 'Invalid key removal request.');
  assert(!(input.apiKey && input.clearApiKey), 'Choose either replacement or removal of the key.');
  const keys = { ...document.keys };
  if (input.clearApiKey) delete keys[settings.provider];
  else if (input.apiKey) keys[settings.provider] = input.apiKey;
  return { ...document, settings, keys };
}

export function publicSettings(document) {
  return { settings: document.settings, hasApiKey: Boolean(document.keys[document.settings.provider]) };
}
