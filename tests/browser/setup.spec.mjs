import { test, expect } from '@playwright/test';

test('portable setup, persistence, safe text rendering, and responsive layout', async ({ page }, info) => {
  const errors = [], external = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (!request.url().startsWith('http://127.0.0.1:4174')) external.push(request.url()); });
  await page.goto('/');
  await expect(page.getByText('DESKTOP SIMULATOR', { exact: true })).toBeVisible();
  await expect(page.getByRole('button', { name: /Home network/ })).toBeVisible();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await page.screenshot({ path: `test-results/${info.project.name}-setup.png`, fullPage: true });

  const ssid = `Travel <b>${info.project.name}</b>`;
  await page.locator('#ssid').fill(ssid);
  await page.locator('#wifi-password').fill('test-password');
  await page.locator('#connect').click();
  await expect(page.locator('#connection-result')).toContainText('Network saved', { timeout: 12000 });
  await expect(page.locator('#saved-networks')).toContainText(ssid);
  expect(await page.locator('#saved-networks b').count()).toBe(0);

  await page.getByRole('button', { name: /Mind & personality/ }).click();
  await page.locator('[name="name"]').fill('Blåbær');
  await page.locator('[name="personality"]').fill('Svar på norsk og engelsk. Vær nysgjerrig: æ ø å Æ Ø Å.');
  await page.locator('#api-key').fill('dummy-browser-key');
  await page.getByRole('button', { name: /Save preferences/ }).click();
  await expect(page.locator('#notice')).toContainText('Preferences saved');
  await expect(page.locator('#api-key')).toHaveValue('');
  await page.reload();
  await page.getByRole('button', { name: /Mind & personality/ }).click();
  await expect(page.locator('[name="name"]')).toHaveValue('Blåbær');
  await expect(page.locator('#key-status')).toContainText('key saved');
  expect(await page.evaluate(() => localStorage.length)).toBe(0);

  await page.getByRole('button', { name: /Voice & language/ }).click();
  await expect(page.locator('[name="pitch"]')).toBeDisabled();
  await page.getByRole('button', { name: /Mind & personality/ }).click();
  await page.locator('[name="provider"][value="local"]').check();
  await page.getByRole('button', { name: /Voice & language/ }).click();
  await expect(page.locator('[name="pitch"]')).toBeEnabled();
  await page.getByRole('button', { name: /Finish setup/ }).click();
  await expect(page.locator('#notice')).toContainText('Simulator restarted');

  await page.getByRole('button', { name: /01Connection/ }).click();
  await page.getByRole('button', { name: `Forget ${ssid}`, exact: true }).click();
  await page.getByRole('button', { name: 'Keep network' }).click();
  await expect(page.locator('#saved-networks')).toContainText(ssid);
  await page.getByRole('button', { name: `Forget ${ssid}`, exact: true }).click();
  await page.getByRole('button', { name: 'Forget network', exact: true }).click();
  await expect(page.locator('#saved-networks')).not.toContainText(ssid);
  expect(errors).toEqual([]); expect(external).toEqual([]);
});

test('failed connection and validation errors keep settings editable', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByRole('button', { name: /Home network/ })).toBeVisible();
  await page.getByRole('button', { name: /Home network/ }).click();
  await page.locator('#wifi-password').fill('wrong-password');
  await page.locator('#connect').click();
  await expect(page.locator('#connection-result')).toContainText('Could not connect', { timeout: 12000 });
  await expect(page.locator('#connect')).toBeEnabled();
  await page.getByRole('button', { name: /Mind & personality/ }).click();
  await page.locator('[name="name"]').fill('å'.repeat(25));
  await page.getByRole('button', { name: /Save preferences/ }).click();
  await expect(page.locator('#notice')).toContainText('too long');
  await expect(page.locator('#save-settings')).toBeEnabled();
  await page.locator('[name="name"]').fill('BitBot');
  await page.getByRole('button', { name: /Save preferences/ }).click();
});
