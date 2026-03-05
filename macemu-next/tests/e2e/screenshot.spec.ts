import { test, expect } from './fixtures';

test.describe('Screenshot API', () => {
  test('returns 503 before emulator starts', async ({ request, emulatorPort }) => {
    const resp = await request.get(`http://localhost:${emulatorPort}/api/screenshot`, {
      failOnStatusCode: false,
    });
    // Before the emulator starts, no video output or no frames yet → 503
    expect(resp.status()).toBe(503);
  });

  test('returns valid PNG after emulator starts', async ({ request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required to produce frames');

    // Start emulator
    await request.post(`http://localhost:${emulatorPort}/api/emulator/start`);

    // Wait for frames to be produced (emulator needs time to boot and render)
    let resp;
    for (let i = 0; i < 25; i++) {
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
    // PNG magic: 0x89 0x50 0x4E 0x47 0x0D 0x0A 0x1A 0x0A
    expect(body[0]).toBe(0x89);
    expect(body[1]).toBe(0x50); // 'P'
    expect(body[2]).toBe(0x4e); // 'N'
    expect(body[3]).toBe(0x47); // 'G'
  });

  test('PNG has correct dimensions in IHDR', async ({ request, emulatorPort, hasRom }) => {
    test.skip(!hasRom, 'ROM required to produce frames');

    // Start emulator if needed
    await request.post(`http://localhost:${emulatorPort}/api/emulator/start`);

    let resp;
    for (let i = 0; i < 25; i++) {
      await new Promise(r => setTimeout(r, 1000));
      resp = await request.get(`http://localhost:${emulatorPort}/api/screenshot`, {
        failOnStatusCode: false,
      });
      if (resp.status() === 200) break;
    }

    expect(resp!.status()).toBe(200);

    const body = await resp!.body();
    // IHDR chunk starts at offset 8 (after 8-byte PNG signature)
    // Chunk: 4 bytes length + 4 bytes "IHDR" + 4 bytes width + 4 bytes height + ...
    // Width at offset 16, Height at offset 20 (big-endian uint32)
    const width = body.readUInt32BE(16);
    const height = body.readUInt32BE(20);

    // Default resolution is 1024x768
    expect(width).toBe(1024);
    expect(height).toBe(768);
  });
});
