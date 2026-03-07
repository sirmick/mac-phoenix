import { test, expect } from './fixtures';

// These tests require a running emulator with a ROM loaded.
// Run: npx playwright test tests/e2e/stop-reset.spec.ts

test.describe('Stop and Reset', () => {

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

    // Start
    await page.locator('#start-btn').click();
    await page.waitForTimeout(2000);

    // Wait for button to change to "Reset" (confirms running state)
    await expect(page.locator('#start-btn')).toHaveText('Reset', { timeout: 5000 });

    // Click stop and intercept the network request
    const [stopResponse] = await Promise.all([
      page.waitForResponse(resp =>
        resp.url().includes('/api/emulator/stop') && resp.status() === 200
      ),
      page.locator('#stop-btn').click(),
    ]);

    const stopBody = await stopResponse.json();
    expect(stopBody.success).toBe(true);

    // Wait for status polling to update the button back to "Start"
    await expect(page.locator('#start-btn')).toHaveText('Start', { timeout: 5000 });
  });

  test('reset button click sends POST restart and page reflects running state', async ({ page, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    await page.goto(`http://localhost:${emulatorPort}/`);

    // Start
    await page.locator('#start-btn').click();
    await page.waitForTimeout(2000);

    // Wait for button to change to "Reset"
    await expect(page.locator('#start-btn')).toHaveText('Reset', { timeout: 5000 });

    // Click reset and intercept the network request
    const [restartResponse] = await Promise.all([
      page.waitForResponse(resp =>
        resp.url().includes('/api/emulator/restart') && resp.status() === 200
      ),
      page.locator('#start-btn').click(),  // Button says "Reset" now
    ]);

    const restartBody = await restartResponse.json();
    expect(restartBody.success).toBe(true);

    // Should remain running after reset
    await expect(page.locator('#start-btn')).toHaveText('Reset', { timeout: 5000 });
  });

  test('start after stop works', async ({ page, request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    await page.goto(`http://localhost:${emulatorPort}/`);

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

    // Button should say "Start" again
    await expect(page.locator('#start-btn')).toHaveText('Start', { timeout: 5000 });

    // Start again
    await page.locator('#start-btn').click();
    await page.waitForTimeout(2000);

    resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    body = await resp.json();
    expect(body.emulator_running).toBe(true);
  });
});
