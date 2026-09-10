import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { gunzipSync } from 'node:zlib';

test('ESP flash assets decompress to the exact portal tested in the browser', async () => {
  const header = await readFile(new URL('../firmware/main/generated/portal_assets.h', import.meta.url), 'utf8');
  let size = 0;
  for (const [file, symbol] of [['index.html', 'index'], ['style.css', 'style'], ['app.js', 'app']]) {
    const text = header.match(new RegExp(`uint8_t ${symbol}\\[\\] = \\{([\\s\\S]*?)\\};`))?.[1];
    assert.ok(text, `${file} is embedded`);
    const compressed = Buffer.from(text.split(',').map(x => x.trim()).filter(Boolean).map(Number));
    assert.equal(compressed[9], 255, 'gzip output is independent of host OS');
    const original = (await readFile(new URL(`../web/${file}`, import.meta.url), 'utf8')).replaceAll('\r\n', '\n');
    assert.equal(gunzipSync(compressed).toString('utf8'), original);
    size += compressed.length;
  }
  assert.ok(size < 96 * 1024, 'portal fits within the flash budget');
});
