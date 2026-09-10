import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from '../tools/dev-server.mjs';
import { Simulator } from '../lib/simulator.mjs';

test('HTTP API rejects foreign origins, missing tokens, malformed/oversized requests', async t => {
  const bot = new Simulator({ delay: 1 });
  const server = createServer(bot);
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  t.after(() => new Promise(resolve => server.close(resolve)));
  const url = `http://127.0.0.1:${server.address().port}`;
  const send = (body, extra = {}) => fetch(`${url}/api/settings`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-BitBot-Token': bot.sessionToken, ...extra }, body: typeof body === 'string' ? body : JSON.stringify(body) });
  assert.equal((await send({ settings: {} }, { Origin: 'https://foreign.example' })).status, 403);
  assert.equal((await send({ settings: {} }, { 'X-BitBot-Token': 'wrong' })).status, 403);
  assert.equal((await send({ settings: {} }, { Host: 'foreign.example' })).status, 403);
  assert.equal((await send({ settings: {} }, { 'Content-Type': 'text/plain' })).status, 415);
  assert.equal((await send('{bad json')).status, 400);
  assert.equal((await send('x'.repeat(13000))).status, 413);
  assert.equal((await send({ settings: { name: 'Hei' }, apiKey: 'dummy-private-key' })).status, 200);
  const publicResponse = await fetch(`${url}/api/settings`);
  assert.equal(publicResponse.headers.get('cache-control'), 'no-store');
  assert.ok(!(await publicResponse.text()).includes('dummy-private-key'));
  const response = await fetch(url);
  assert.equal(response.status, 200);
  assert.match(response.headers.get('content-security-policy'), /frame-ancestors 'none'/);
  assert.equal((await fetch(`${url}/../package.json`)).status, 404);
});
