/**
 * Mouse input and position API tests
 *
 * Tests:
 *   1. /api/mouse returns position after boot
 *   2. POST /api/mouse absolute move and readback
 *   3. POST /api/mouse relative move (dx/dy) — verifies direction
 */
import { test, expect } from './fixtures';

test.describe('Mouse API', () => {

  test('GET /api/mouse returns position after boot', async ({ request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    // Ensure clean start (stop first in case prior test left it stuck)
    await request.post(`http://localhost:${emulatorPort}/api/emulator/stop`);
    await new Promise(r => setTimeout(r, 500));
    await request.post(`http://localhost:${emulatorPort}/api/emulator/start`);

    // Wait for boot to reach Finder
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

  test('absolute mouse: move and verify Mac OS reflects change', async ({ request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    // Start emulator
    await request.post(`http://localhost:${emulatorPort}/api/emulator/start`);

    // Wait for boot to reach Finder
    for (let i = 0; i < 20; i++) {
      await new Promise(r => setTimeout(r, 1000));
      const status = await request.get(`http://localhost:${emulatorPort}/api/status`);
      const body = await status.json();
      if (body.boot_phase === 'Finder' || body.boot_phase === 'desktop') break;
    }

    // Move mouse to a known position
    const moveResp = await request.post(`http://localhost:${emulatorPort}/api/mouse`, {
      data: { x: 200, y: 150 },
    });
    expect(moveResp.status()).toBe(200);
    const moveBody = await moveResp.json();
    expect(moveBody.success).toBe(true);
    expect(moveBody.mode).toBe('absolute');

    // Wait for Mac OS to process the ADB interrupt
    await new Promise(r => setTimeout(r, 500));

    // Read back — Mac OS low-memory globals should reflect the move
    const mouse = await (await request.get(`http://localhost:${emulatorPort}/api/mouse`)).json();
    expect(mouse.x).toBe(200);
    expect(mouse.y).toBe(150);

    // Move to a different position to confirm tracking
    await request.post(`http://localhost:${emulatorPort}/api/mouse`, {
      data: { x: 400, y: 300 },
    });
    await new Promise(r => setTimeout(r, 500));

    const mouse2 = await (await request.get(`http://localhost:${emulatorPort}/api/mouse`)).json();
    expect(mouse2.x).toBe(400);
    expect(mouse2.y).toBe(300);
  });

  test('relative mouse: dx/dy deltas move cursor in correct direction', async ({ request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required');

    // Start emulator
    await request.post(`http://localhost:${emulatorPort}/api/emulator/start`);

    // Wait for boot to reach Finder
    for (let i = 0; i < 20; i++) {
      await new Promise(r => setTimeout(r, 1000));
      const status = await request.get(`http://localhost:${emulatorPort}/api/status`);
      const body = await status.json();
      if (body.boot_phase === 'Finder' || body.boot_phase === 'desktop') break;
    }

    // Set a known absolute baseline
    await request.post(`http://localhost:${emulatorPort}/api/mouse`, {
      data: { x: 200, y: 200 },
    });
    await new Promise(r => setTimeout(r, 500));

    const baseline = await (await request.get(`http://localhost:${emulatorPort}/api/mouse`)).json();
    expect(baseline.x).toBe(200);
    expect(baseline.y).toBe(200);

    // Relative move with positive deltas — cursor should move right and down
    // (Mac OS applies acceleration so exact values are unpredictable)
    const relResp = await request.post(`http://localhost:${emulatorPort}/api/mouse`, {
      data: { dx: 20, dy: 20 },
    });
    const relBody = await relResp.json();
    expect(relBody.success).toBe(true);
    expect(relBody.mode).toBe('relative');

    await new Promise(r => setTimeout(r, 500));

    const after1 = await (await request.get(`http://localhost:${emulatorPort}/api/mouse`)).json();
    expect(after1.x).toBeGreaterThan(baseline.x);
    expect(after1.y).toBeGreaterThan(baseline.y);

    // Relative move with negative deltas — cursor should move left and up
    await request.post(`http://localhost:${emulatorPort}/api/mouse`, {
      data: { dx: -20, dy: -20 },
    });
    await new Promise(r => setTimeout(r, 500));

    const after2 = await (await request.get(`http://localhost:${emulatorPort}/api/mouse`)).json();
    expect(after2.x).toBeLessThan(after1.x);
    expect(after2.y).toBeLessThan(after1.y);
  });
});
