/**
 * Mouse input and position API tests
 *
 * Tests:
 *   1. /api/mouse returns position after boot
 *   2. Data channel mouse events don't crash
 *   3. Native mouse interaction on display element
 */
import { test, expect } from './fixtures';

test.describe('Mouse API', () => {

  test('GET /api/mouse returns 503 when not running', async ({ request, emulatorPort }) => {
    const resp = await request.get(`http://localhost:${emulatorPort}/api/mouse`, {
      failOnStatusCode: false,
    });
    expect(resp.status()).toBe(503);
  });

  test('GET /api/mouse returns position after boot', async ({ request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    // Start emulator
    await request.post(`http://localhost:${emulatorPort}/api/emulator/start`);

    // Wait for boot to reach at least warm start
    let mouseResp;
    for (let i = 0; i < 20; i++) {
      await new Promise(r => setTimeout(r, 1000));
      const status = await request.get(`http://localhost:${emulatorPort}/api/status`);
      const body = await status.json();
      if (body.boot_phase === 'Finder' || body.boot_phase === 'desktop') {
        mouseResp = await request.get(`http://localhost:${emulatorPort}/api/mouse`);
        break;
      }
    }

    expect(mouseResp).toBeDefined();
    expect(mouseResp!.status()).toBe(200);

    const mouse = await mouseResp!.json();
    expect(mouse).toHaveProperty('x');
    expect(mouse).toHaveProperty('y');
    expect(typeof mouse.x).toBe('number');
    expect(typeof mouse.y).toBe('number');
  });
});

test.describe('Mouse Input Pipeline', () => {

  test('data channel opens and accepts mouse events', async ({ page, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required for WebRTC');

    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.waitForLoadState('networkidle');

    const dcOpen = await page.evaluate(async () => {
      const deadline = Date.now() + 10000;
      while (Date.now() < deadline) {
        // @ts-ignore
        const client = window.client;
        if (client && client.dataChannel && client.dataChannel.readyState === 'open') {
          return true;
        }
        await new Promise(r => setTimeout(r, 200));
      }
      return false;
    });

    expect(dcOpen).toBe(true);
  });

  test('sends mouse events without errors', async ({ page, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required for WebRTC');

    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.waitForLoadState('networkidle');

    // Wait for data channel
    await page.evaluate(async () => {
      const deadline = Date.now() + 10000;
      while (Date.now() < deadline) {
        // @ts-ignore
        const c = window.client;
        if (c?.dataChannel?.readyState === 'open') return;
        await new Promise(r => setTimeout(r, 200));
      }
    });

    const errors: string[] = [];
    page.on('pageerror', (err) => errors.push(err.message));

    const sent = await page.evaluate(() => {
      // @ts-ignore
      const client = window.client;
      if (!client?.dataChannel || client.dataChannel.readyState !== 'open') return false;

      // Send relative mouse moves
      for (let i = 0; i < 5; i++) {
        client.sendMouseMove(5, 3, performance.now());
      }

      // Send button events
      client.sendMouseButton(0, true, performance.now());
      client.sendMouseButton(0, false, performance.now());
      return true;
    });

    expect(sent).toBe(true);
    expect(errors).toHaveLength(0);

    await page.waitForTimeout(500);

    const stillOpen = await page.evaluate(() => {
      // @ts-ignore
      return window.client?.dataChannel?.readyState === 'open';
    });
    expect(stillOpen).toBe(true);
  });
});
