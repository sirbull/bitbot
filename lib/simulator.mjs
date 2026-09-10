import { randomBytes } from 'node:crypto';
import { defaults, validateNetwork, updateDocument, publicSettings, assert } from './settings.mjs';

export const initialDocument = () => ({ schemaVersion: 1, settings: defaults(), keys: {}, networks: [] });
export const demoNetworks = [
  { ssid: 'Home network', rssi: -42, open: false },
  { ssid: 'Phone hotspot', rssi: -61, open: false },
  { ssid: 'Workshop guest', rssi: -76, open: true },
];

export class Simulator {
  constructor({ document = initialDocument(), persist = async () => {}, delay = 1200 } = {}) {
    this.document = structuredClone(document);
    this.document.settings = { ...defaults(), ...this.document.settings };
    this.persist = persist;
    this.delay = delay;
    this.sessionToken = randomBytes(24).toString('hex');
    this.job = { state: 'idle', message: '' };
    this.connectedSsid = '';
    this.setup = true;
    this.started = Date.now();
    this.queue = Promise.resolve();
  }
  mutate(fn) {
    const pending = this.queue.then(async () => {
      const next = fn(this.document);
      await this.persist(next);
      this.document = next;
    });
    this.queue = pending.catch(() => {});
    return pending;
  }
  status() {
    return {
      simulator: true, firmware: '0.1.0', state: this.setup ? 'setup' : this.connectedSsid ? 'idle' : 'offline',
      connectedSsid: this.connectedSsid, ip: this.connectedSsid ? '192.0.2.42' : '',
      internet: 'unchecked', setupSsid: 'BitBot-DEMO', job: this.job,
      uptimeSeconds: Math.floor((Date.now() - this.started) / 1000),
      capabilities: { assistant: false, display: false, audio: false, camera: false },
    };
  }
  settings() { return publicSettings(this.document); }
  networks() { return this.document.networks.map(({ ssid, open }) => ({ ssid, open })); }
  async save(input) { await this.mutate(doc => updateDocument(doc, input)); return this.settings(); }
  connect(input) {
    assert(this.job.state !== 'testing', 'A connection test is already running.');
    const network = validateNetwork(input);
    assert(this.document.networks.length < 5 || this.document.networks.some(n => n.ssid === network.ssid), 'Five networks are saved. Forget one before adding another.');
    this.job = { state: 'testing', message: 'Testing Wi-Fi connection…' };
    this.connectionTask = (async () => {
      await new Promise(resolve => setTimeout(resolve, this.delay));
      if (network.password === 'wrong-password') {
        this.job = { state: 'failed', message: 'Could not connect. Check the password and try again. Saved networks are unchanged.' };
        return;
      }
      try {
        await this.mutate(doc => ({ ...doc, networks: [network, ...doc.networks.filter(n => n.ssid !== network.ssid)] }));
        this.connectedSsid = network.ssid;
        this.job = { state: 'connected', message: 'Network saved. Wi-Fi and IP address confirmed; internet access has not been checked.' };
      } catch {
        this.job = { state: 'failed', message: 'Could not save the network. Previous settings are unchanged.' };
      }
    })();
    return this.job;
  }
  async forget(input) {
    assert(this.job.state !== 'testing', 'Wait for the connection test to finish.');
    assert(typeof input?.ssid === 'string' && this.document.networks.some(n => n.ssid === input.ssid), 'Saved network not found.');
    await this.mutate(doc => ({ ...doc, networks: doc.networks.filter(n => n.ssid !== input.ssid) }));
    if (this.connectedSsid === input.ssid) this.connectedSsid = '';
    return this.networks();
  }
  finish() {
    assert(this.job.state !== 'testing', 'Wait for the connection test to finish.');
    assert(this.document.networks.length > 0, 'Connect to a network first.');
    this.setup = false;
    this.connectedSsid = this.document.networks[0].ssid;
    return { message: 'Simulator restarted. Your settings and networks are still saved.' };
  }
}
