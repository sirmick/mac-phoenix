import { test, expect } from './fixtures';

// These tests require a running emulator with a ROM loaded.
// Run: npx playwright test tests/e2e/stop-reset.spec.ts

test.describe('Stop and Reset', () => {

  // The fixture spawns one emulator per worker and reuses it across tests, so
  // an earlier test can leave the CPU running. Reset to a known stopped state
  // before each test that drives the UI from the start button.
  test.beforeEach(async ({ request, emulatorPort, hasRom }) => {
    if (!hasRom) return;
    await request.post(`http://localhost:${emulatorPort}/api/emulator/stop`);
    await new Promise(r => setTimeout(r, 500));
  });

  test('stop button stops the emulator', async ({ page, request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    await page.goto(`http://localhost:${emulatorPort}/`);

    // Start the emulator
    await page.locator('#start-btn').click();
    await page.waitForTimeout(2000);

    // Verify it's running
    let resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    let body = await resp.json();
    expect(body.emulator_running).toBe(true);

    // Click stop
    await page.locator('#stop-btn').click();
    await page.waitForTimeout(1000);

    // Verify it's stopped
    resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    body = await resp.json();
    expect(body.emulator_running).toBe(false);
  });

  test('stop API works directly', async ({ request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    // Start via API
    await request.post(`http://localhost:${emulatorPort}/api/emulator/start`);
    await new Promise(r => setTimeout(r, 2000));

    let resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    let body = await resp.json();
    expect(body.emulator_running).toBe(true);

    // Stop via API
    const stopResp = await request.post(`http://localhost:${emulatorPort}/api/emulator/stop`);
    const stopBody = await stopResp.json();
    expect(stopBody.success).toBe(true);

    await new Promise(r => setTimeout(r, 1000));

    resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    body = await resp.json();
    expect(body.emulator_running).toBe(false);
  });

  test('stop actually halts CPU execution', async ({ request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    // Start and let it run
    await request.post(`http://localhost:${emulatorPort}/api/emulator/start`);
    await new Promise(r => setTimeout(r, 2000));

    // Stop
    await request.post(`http://localhost:${emulatorPort}/api/emulator/stop`);
    await new Promise(r => setTimeout(r, 500));

    // Record checkload_count
    let resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    let body = await resp.json();
    const countAfterStop = body.checkload_count;

    // Wait and check again — count must not increase
    await new Promise(r => setTimeout(r, 2000));
    resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    body = await resp.json();

    expect(body.emulator_running).toBe(false);
    expect(body.checkload_count).toBe(countAfterStop);
  });

  test('restart API works directly', async ({ request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    // Start via API
    await request.post(`http://localhost:${emulatorPort}/api/emulator/start`);
    await new Promise(r => setTimeout(r, 2000));

    let resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    let body = await resp.json();
    expect(body.emulator_running).toBe(true);

    // Restart via API
    const restartResp = await request.post(`http://localhost:${emulatorPort}/api/emulator/restart`);
    const restartBody = await restartResp.json();
    expect(restartBody.success).toBe(true);

    await new Promise(r => setTimeout(r, 1000));

    // Should be running again after restart
    resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    body = await resp.json();
    expect(body.emulator_running).toBe(true);
  });

  test('stop button click sends POST and page reflects stopped state', async ({ page, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    await page.goto(`http://localhost:${emulatorPort}/`);
    await expect(page.locator('#start-btn')).toBeVisible();

    // Start
    await page.locator('#start-btn').click();
    await page.waitForTimeout(2000);

    // Start hides + stop split appears once status polling sees running.
    await expect(page.locator('#start-btn')).toBeHidden({ timeout: 5000 });
    await expect(page.locator('#stop-btn')).toBeVisible({ timeout: 5000 });

    // Click stop and intercept the network request
    const [stopResponse] = await Promise.all([
      page.waitForResponse(resp =>
        resp.url().includes('/api/emulator/stop') && resp.status() === 200
      ),
      page.locator('#stop-btn').click(),
    ]);

    const stopBody = await stopResponse.json();
    expect(stopBody.success).toBe(true);

    // Polling flips back: Start visible, stop split hidden, reset disabled.
    await expect(page.locator('#start-btn')).toBeVisible({ timeout: 5000 });
    await expect(page.locator('#stop-btn')).toBeHidden({ timeout: 5000 });
    await expect(page.locator('#reset-btn')).toBeDisabled({ timeout: 5000 });
  });

  test('reset button click sends POST restart and page reflects running state', async ({ page, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    await page.goto(`http://localhost:${emulatorPort}/`);

    // Reset is disabled until the emulator is running.
    await expect(page.locator('#reset-btn')).toBeDisabled();

    // Start
    await page.locator('#start-btn').click();
    await page.waitForTimeout(2000);

    // Reset gets enabled once polling sees emulator_running.
    await expect(page.locator('#reset-btn')).toBeEnabled({ timeout: 5000 });

    // Click reset and intercept the network request
    const [restartResponse] = await Promise.all([
      page.waitForResponse(resp =>
        resp.url().includes('/api/emulator/restart') && resp.status() === 200
      ),
      page.locator('#reset-btn').click(),
    ]);

    const restartBody = await restartResponse.json();
    expect(restartBody.success).toBe(true);

    // Still running after reset → reset stays enabled, start stays hidden.
    await expect(page.locator('#start-btn')).toBeHidden({ timeout: 5000 });
    await expect(page.locator('#reset-btn')).toBeEnabled({ timeout: 5000 });
  });

  test('start after stop works', async ({ page, request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    await page.goto(`http://localhost:${emulatorPort}/`);
    await expect(page.locator('#start-btn')).toBeVisible();

    // Start
    await page.locator('#start-btn').click();
    await page.waitForTimeout(2000);

    // Stop
    await page.locator('#stop-btn').click();
    await page.waitForTimeout(1000);

    // Verify stopped
    let resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    let body = await resp.json();
    expect(body.emulator_running).toBe(false);

    // Start button should reappear once polling sees stopped state.
    await expect(page.locator('#start-btn')).toBeVisible({ timeout: 5000 });
    await expect(page.locator('#start-btn')).toHaveText('Start');

    // Start again
    await page.locator('#start-btn').click();
    await page.waitForTimeout(2000);

    resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    body = await resp.json();
    expect(body.emulator_running).toBe(true);
  });
});
