import http from 'node:http';
import { readFile, mkdir, writeFile, rename } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { randomBytes } from 'node:crypto';
import { Simulator, demoNetworks, initialDocument } from '../lib/simulator.mjs';
import { InputError } from '../lib/settings.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const routes = new Map([['/', ['index.html', 'text/html']], ['/style.css', ['style.css', 'text/css']], ['/app.js', ['app.js', 'text/javascript']]]);
export function createServer(simulator = new Simulator()) {
  return http.createServer(async (req, res) => {
    res.setHeader('Cache-Control', 'no-store');
    res.setHeader('X-Content-Type-Options', 'nosniff');
    res.setHeader('Content-Security-Policy', "default-src 'self'; style-src 'self'; script-src 'self'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
    const json = (code, body) => { res.writeHead(code, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(body)); };
    const host = req.headers.host;
    if (!/^(127\.0\.0\.1|localhost):\d+$/u.test(host ?? '')) return json(403, { error: 'Unrecognized host.' });
    if (req.headers.origin && req.headers.origin !== `http://${host}`) return json(403, { error: 'Unrecognized origin.' });
    const uri = new URL(req.url, `http://${host}`).pathname;
    try {
      if (req.method === 'GET') {
        if (uri === '/api/session') return json(200, { token: simulator.sessionToken });
        if (uri === '/api/status') return json(200, simulator.status());
        if (uri === '/api/settings') return json(200, simulator.settings());
        if (uri === '/api/networks') return json(200, simulator.networks());
        if (uri === '/api/scan') return json(200, demoNetworks);
        if (routes.has(uri)) {
          const [file, mime] = routes.get(uri);
          res.writeHead(200, { 'Content-Type': `${mime}; charset=utf-8` });
          return res.end(await readFile(path.join(root, 'web', file)));
        }
      }
      if (req.method === 'POST') {
        if (req.headers['x-bitbot-token'] !== simulator.sessionToken) return json(403, { error: 'Setup session expired. Reload this page.' });
        if (!req.headers['content-type']?.startsWith('application/json')) return json(415, { error: 'JSON is required.' });
        const chunks = []; let size = 0;
        for await (const chunk of req) {
          size += chunk.length;
          if (size > 12288) return json(413, { error: 'Request is too large.' });
          chunks.push(chunk);
        }
        let body;
        try { body = JSON.parse(Buffer.concat(chunks).toString('utf8')); } catch { throw new InputError('Invalid JSON.'); }
        if (uri === '/api/settings') return json(200, await simulator.save(body));
        if (uri === '/api/connect') return json(202, simulator.connect(body));
        if (uri === '/api/forget') return json(200, await simulator.forget(body));
        if (uri === '/api/finish') return json(200, simulator.finish());
      }
      return json(404, { error: 'Not found.' });
    } catch (error) {
      return json(error instanceof InputError ? 400 : 500, { error: error instanceof InputError ? error.message : 'Could not save changes. Please try again.' });
    }
  });
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const store = path.join(root, '.local', 'simulator.json');
  let document;
  try {
    document = JSON.parse(await readFile(store, 'utf8'));
    if (document.schemaVersion !== 1) throw new Error('Unsupported simulator data version.');
  } catch (error) {
    if (error.code !== 'ENOENT') throw error;
    document = initialDocument();
  }
  const simulator = new Simulator({ document, persist: async value => {
    await mkdir(path.dirname(store), { recursive: true });
    const temporary = `${store}.${randomBytes(4).toString('hex')}.tmp`;
    await writeFile(temporary, JSON.stringify(value, null, 2), { mode: 0o600 });
    await rename(temporary, store);
  } });
  const port = Number(process.env.PORT || 4173);
  createServer(simulator).listen(port, '127.0.0.1', () => console.log(`BitBot simulator: http://127.0.0.1:${port}\nUse dummy credentials. No real Wi-Fi or AI calls are made.`));
}
