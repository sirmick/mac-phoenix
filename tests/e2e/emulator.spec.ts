import { test, expect } from './fixtures';

test.describe('Emulator Controls', () => {
  test('start button triggers POST /api/emulator/start', async ({ page, request, emulatorPort }) => {
    // Ensure emulator is stopped so button says "Start" (not "Reset")
    await request.post(`http://localhost:${emulatorPort}/api/emulator/stop`);

    await page.goto(`http://localhost:${emulatorPort}/`);

    // Wait for button to say "Start" (set by status polling)
    await expect(page.locator('#start-btn')).toHaveText('Start', { timeout: 5000 });

    const [startReq] = await Promise.all([
      page.waitForRequest(req =>
        req.url().includes('/api/emulator/start') && req.method() === 'POST'
      ),
      page.locator('#start-btn').click(),
    ]);

    expect(startReq.method()).toBe('POST');
  });

  test('stop button triggers POST /api/emulator/stop', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    await page.locator('#start-btn').click();
    await page.waitForTimeout(500);

    const [request] = await Promise.all([
      page.waitForRequest(req =>
        req.url().includes('/api/emulator/stop') && req.method() === 'POST'
      ),
      page.locator('#stop-btn').click(),
    ]);

    expect(request.method()).toBe('POST');
  });

  test('status endpoint has boot_phase field', async ({ request, emulatorPort }) => {
    const resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    const body = await resp.json();
    expect(body).toHaveProperty('boot_phase');
    expect(body).toHaveProperty('checkload_count');
    expect(body).toHaveProperty('boot_elapsed');
  });

  test('status reflects running state after start', async ({ request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required to start emulator');

    // Start via API
    const startResp = await request.post(`http://localhost:${emulatorPort}/api/emulator/start`);
    const startBody = await startResp.json();
    expect(startBody.success).toBe(true);

    // Check immediately — start is synchronous, status should reflect it
    const resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    const body = await resp.json();
    expect(body.emulator_running).toBe(true);
  });
});
