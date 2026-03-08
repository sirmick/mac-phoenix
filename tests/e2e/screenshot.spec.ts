import { test, expect } from './fixtures';

test.describe('Screenshot API', () => {
  test('returns 503 before emulator starts', async ({ request, emulatorPort }) => {
    // Ensure emulator is stopped so no frames are available
    await request.post(`http://localhost:${emulatorPort}/api/emulator/stop`);
    await new Promise(r => setTimeout(r, 500));

    const resp = await request.get(`http://localhost:${emulatorPort}/api/screenshot`, {
      failOnStatusCode: false,
    });
    expect(resp.status()).toBe(503);
  });

  test('returns valid PNG after emulator starts', async ({ request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required to produce frames');

    await request.post(`http://localhost:${emulatorPort}/api/emulator/start`);

    // Wait for boot to produce frames
    let resp;
    for (let i = 0; i < 15; i++) {
      await new Promise(r => setTimeout(r, 1000));
      resp = await request.get(`http://localhost:${emulatorPort}/api/screenshot`, {
        failOnStatusCode: false,
      });
      if (resp.status() === 200) break;
    }

    expect(resp!.status()).toBe(200);
    expect(resp!.headers()['content-type']).toBe('image/png');

    // Check PNG magic bytes
    const body = await resp!.body();
    expect(body.length).toBeGreaterThan(8);
    expect(body[0]).toBe(0x89);
    expect(body[1]).toBe(0x50); // 'P'
    expect(body[2]).toBe(0x4e); // 'N'
    expect(body[3]).toBe(0x47); // 'G'
  });
});
