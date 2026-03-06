import { test, expect } from './fixtures';

test.describe('Emulator Controls', () => {
  test('start button triggers POST /api/emulator/start', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    const [request] = await Promise.all([
      page.waitForRequest(req =>
        req.url().includes('/api/emulator/start') && req.method() === 'POST'
      ),
      page.locator('#start-btn').click(),
    ]);

    expect(request.method()).toBe('POST');
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

  test('status reflects running state after start', async ({ page, request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required to start emulator');

    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.locator('#start-btn').click();
    await page.waitForTimeout(1000);

    const resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    const body = await resp.json();
    expect(body.emulator_running).toBe(true);
  });
});
