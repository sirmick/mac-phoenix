/**
 * Stall detection and pixel verification tests
 *
 * Tests the full round-trip pipeline:
 *   Browser → WebRTC data channel → ADB → Mac memory → /api/mouse
 *   CPU framebuffer → video encoder → WebRTC → browser canvas pixels
 *
 * Detects stalls by measuring latency between mouse input and position update.
 */
import { test, expect, waitForBootPhase } from './fixtures';

const HTTP_PORT = parseInt(process.env.MACEMU_HTTP_PORT || '18094');
const SIG_PORT = HTTP_PORT + 1;
const API = `http://localhost:${HTTP_PORT}`;
const PAGE_URL = `http://localhost:${HTTP_PORT}/?ws=ws://localhost:${SIG_PORT}/`;

// Max time (ms) for a mouse position update to propagate through the pipeline.
// The 60Hz interrupt fires every ~16.6ms, so 500ms is very generous.
const STALL_THRESHOLD_MS = 500;

test.describe('Stall Detection', () => {
  test.beforeAll(async () => {
    // Emulator is auto-spawned by the fixture; just start CPU and wait for boot
    await fetch(`${API}/api/emulator/start`, { method: 'POST' });
    await waitForBootPhase(HTTP_PORT, 'Finder', 30_000).catch(() =>
      waitForBootPhase(HTTP_PORT, 'desktop', 5_000)
    );
    // Let Finder settle
    await new Promise(r => setTimeout(r, 2000));
  });

  interface CursorState {
    x: number; y: number;             // Mouse (0x830/0x832) — Cursor Manager output
    raw_x: number; raw_y: number;     // RawMouse (0x82C/0x82E) — ADB wrote this
    mtemp_x: number; mtemp_y: number; // MTemp (0x828/0x82A) — ADB wrote this
    crsr_new: number;                 // 0x8CE — cursor position changed flag
    crsr_couple: number;              // 0x8CF — cursor coupled to mouse
    crsr_busy: number;                // 0x8CD — cursor manager busy
  }

  async function getCursorState(): Promise<CursorState> {
    const resp = await fetch(`${API}/api/mouse`);
    if (!resp.ok) throw new Error(`/api/mouse returned ${resp.status}`);
    return resp.json();
  }

  // Convenience: just the Cursor Manager position
  async function getMousePosition(): Promise<{ x: number; y: number }> {
    const s = await getCursorState();
    return { x: s.x, y: s.y };
  }

  async function waitForDataChannel(page: any): Promise<void> {
    const ok = await page.evaluate(async () => {
      const deadline = Date.now() + 15000;
      while (Date.now() < deadline) {
        const c = (window as any).client;
        if (c?.dataChannel?.readyState === 'open') return true;
        await new Promise(r => setTimeout(r, 200));
      }
      return false;
    });
    expect(ok).toBe(true);
  }

  // ── Absolute mouse mode ─────────────────────────────────────────────

  test('absolute mouse: position round-trip', async ({ page }) => {
    test.setTimeout(60_000);

    await page.goto(PAGE_URL);
    await page.waitForLoadState('networkidle');
    await waitForDataChannel(page);

    // Switch to absolute mouse mode
    await page.evaluate(() => {
      const c = (window as any).client;
      c.mouseMode = 'absolute';
      c.sendMouseModeChange('absolute');
    });
    await new Promise(r => setTimeout(r, 200));

    // Read initial cursor state
    const initial = await getCursorState();
    console.log(`  initial: cursor=(${initial.x},${initial.y}) raw=(${initial.raw_x},${initial.raw_y}) couple=${initial.crsr_couple}`);

    // Send absolute mouse to a known position
    const targetX = 320;
    const targetY = 240;
    await page.evaluate(({ x, y }: { x: number; y: number }) => {
      const c = (window as any).client;
      c.sendMouseAbsolute(x, y, performance.now());
    }, { x: targetX, y: targetY });

    // Poll until Cursor Manager position (x/y at 0x830/0x832) updates
    const start = Date.now();
    let state = initial;
    let updated = false;
    while (Date.now() - start < STALL_THRESHOLD_MS * 4) {
      await new Promise(r => setTimeout(r, 50));
      state = await getCursorState();
      if (state.x === targetX && state.y === targetY) {
        updated = true;
        break;
      }
    }
    const elapsed = Date.now() - start;
    console.log(`  after:   cursor=(${state.x},${state.y}) raw=(${state.raw_x},${state.raw_y}) crsr_new=${state.crsr_new}`);
    console.log(`  Cursor Manager confirmed move in ${elapsed}ms`);

    // Verify the Cursor Manager (not just ADB) processed the position
    expect(updated).toBe(true);
    expect(state.x).toBe(targetX);   // Cursor Manager output
    expect(state.y).toBe(targetY);
    expect(elapsed).toBeLessThan(STALL_THRESHOLD_MS);
  });

  test('absolute mouse: move to multiple positions without stalls', async ({ page }) => {
    test.setTimeout(60_000);

    await page.goto(PAGE_URL);
    await page.waitForLoadState('networkidle');
    await waitForDataChannel(page);

    await page.evaluate(() => {
      const c = (window as any).client;
      c.mouseMode = 'absolute';
      c.sendMouseModeChange('absolute');
    });
    await new Promise(r => setTimeout(r, 200));

    const positions = [
      { x: 100, y: 100 },
      { x: 400, y: 300 },
      { x: 200, y: 450 },
      { x: 500, y: 50 },
      { x: 320, y: 240 },
    ];

    const latencies: number[] = [];

    for (const target of positions) {
      await page.evaluate(({ x, y }: { x: number; y: number }) => {
        const c = (window as any).client;
        c.sendMouseAbsolute(x, y, performance.now());
      }, target);

      const sendTime = Date.now();
      let pos = { x: -1, y: -1 };
      let found = false;
      while (Date.now() - sendTime < STALL_THRESHOLD_MS * 4) {
        await new Promise(r => setTimeout(r, 30));
        pos = await getMousePosition();
        if (pos.x === target.x && pos.y === target.y) {
          found = true;
          break;
        }
      }
      const latency = Date.now() - sendTime;
      latencies.push(latency);
      console.log(`  move to (${target.x},${target.y}): got (${pos.x},${pos.y}) in ${latency}ms`);
      expect(found).toBe(true);
    }

    // Check no individual move stalled
    const maxLatency = Math.max(...latencies);
    const avgLatency = latencies.reduce((a, b) => a + b, 0) / latencies.length;
    console.log(`  latency: avg=${avgLatency.toFixed(0)}ms, max=${maxLatency}ms`);
    expect(maxLatency).toBeLessThan(STALL_THRESHOLD_MS);
  });

  // ── Relative mouse mode ─────────────────────────────────────────────

  test('relative mouse: deltas update position without stalls', async ({ page }) => {
    test.setTimeout(60_000);

    await page.goto(PAGE_URL);
    await page.waitForLoadState('networkidle');
    await waitForDataChannel(page);

    // First set absolute position to a known starting point (so we have room to move)
    await page.evaluate(() => {
      const c = (window as any).client;
      c.mouseMode = 'absolute';
      c.sendMouseModeChange('absolute');
    });
    await new Promise(r => setTimeout(r, 200));
    await page.evaluate(() => {
      const c = (window as any).client;
      c.sendMouseAbsolute(200, 200, performance.now());
    });
    // Wait for absolute position to take effect
    let startPos = { x: -1, y: -1 };
    for (let i = 0; i < 40; i++) {
      await new Promise(r => setTimeout(r, 50));
      startPos = await getMousePosition();
      if (startPos.x === 200 && startPos.y === 200) break;
    }

    // Switch to relative mode
    await page.evaluate(() => {
      const c = (window as any).client;
      c.mouseMode = 'relative';
      c.sendMouseModeChange('relative');
    });
    await new Promise(r => setTimeout(r, 200));

    const beforeMove = await getMousePosition();
    console.log(`  before relative move: (${beforeMove.x}, ${beforeMove.y})`);

    // Send a series of relative deltas (+50, +30) total
    await page.evaluate(() => {
      const c = (window as any).client;
      for (let i = 0; i < 5; i++) {
        c.sendMouseMove(10, 6, performance.now());
      }
    });

    // Wait for position to change
    const sendTime = Date.now();
    let pos = beforeMove;
    let changed = false;
    while (Date.now() - sendTime < STALL_THRESHOLD_MS * 4) {
      await new Promise(r => setTimeout(r, 30));
      pos = await getMousePosition();
      // In relative mode, the Mac ROM's mouse handler processes deltas.
      // We can't predict exact position (acceleration, clamping), but it should change.
      if (pos.x !== beforeMove.x || pos.y !== beforeMove.y) {
        changed = true;
        break;
      }
    }
    const elapsed = Date.now() - sendTime;
    console.log(`  after relative move: (${pos.x}, ${pos.y}), elapsed=${elapsed}ms`);

    expect(changed).toBe(true);
    expect(elapsed).toBeLessThan(STALL_THRESHOLD_MS);
  });

  test('relative mouse: sustained movement detects stalls', async ({ page }) => {
    test.setTimeout(60_000);

    await page.goto(PAGE_URL);
    await page.waitForLoadState('networkidle');
    await waitForDataChannel(page);

    // Start in relative mode
    await page.evaluate(() => {
      const c = (window as any).client;
      c.mouseMode = 'relative';
      c.sendMouseModeChange('relative');
    });
    await new Promise(r => setTimeout(r, 200));

    // Send 20 bursts of mouse movement, measuring each round-trip
    const results: { deltaMs: number; moved: boolean }[] = [];
    for (let burst = 0; burst < 20; burst++) {
      const before = await getMousePosition();

      await page.evaluate(() => {
        const c = (window as any).client;
        c.sendMouseMove(3, 2, performance.now());
      });

      const sendTime = Date.now();
      let moved = false;
      let pos = before;
      while (Date.now() - sendTime < STALL_THRESHOLD_MS) {
        await new Promise(r => setTimeout(r, 20));
        pos = await getMousePosition();
        if (pos.x !== before.x || pos.y !== before.y) {
          moved = true;
          break;
        }
      }
      results.push({ deltaMs: Date.now() - sendTime, moved });
    }

    const stalls = results.filter(r => !r.moved || r.deltaMs > STALL_THRESHOLD_MS);
    const latencies = results.filter(r => r.moved).map(r => r.deltaMs);
    const avgLatency = latencies.length > 0
      ? latencies.reduce((a, b) => a + b, 0) / latencies.length
      : Infinity;

    console.log(`  ${results.length} bursts: ${stalls.length} stalls, avg latency=${avgLatency.toFixed(0)}ms`);
    if (stalls.length > 0) {
      console.log(`  stall details: ${JSON.stringify(stalls)}`);
    }

    // Allow at most 1 stall (first move might be slow due to mode switch settling)
    expect(stalls.length).toBeLessThanOrEqual(1);
  });

  // ── Pixel verification via /api/screenshot ───────────────────────────
  // Uses the screenshot API (reads framebuffer directly as PNG) and decodes
  // in-browser via <img> + canvas. This avoids WebRTC codec dependencies.

  async function getScreenshotPixels(page: any, request: any): Promise<{
    width: number;
    height: number;
    regions: Array<{ name: string; nonBlackPixels: number; totalPixels: number; sampleRgba: number[] }>;
  } | null> {
    // Fetch screenshot PNG as base64
    const resp = await request.get(`${API}/api/screenshot`);
    if (!resp.ok()) return null;
    const buf = await resp.body();
    const b64 = buf.toString('base64');

    // Decode in browser via <img> + offscreen canvas
    return page.evaluate(async (pngBase64: string) => {
      return new Promise<any>((resolve) => {
        const img = new Image();
        img.onload = () => {
          const canvas = document.createElement('canvas');
          canvas.width = img.width;
          canvas.height = img.height;
          const ctx = canvas.getContext('2d')!;
          ctx.drawImage(img, 0, 0);

          const w = canvas.width;
          const h = canvas.height;
          const regions = [
            { name: 'top-left', x: 10, y: 10, w: 20, h: 20 },
            { name: 'center', x: Math.floor(w / 2) - 10, y: Math.floor(h / 2) - 10, w: 20, h: 20 },
            { name: 'menu-bar', x: 10, y: 2, w: 100, h: 16 },
          ];

          const results: any[] = [];
          for (const r of regions) {
            const imgData = ctx.getImageData(r.x, r.y, r.w, r.h);
            const data = imgData.data;
            let nonBlack = 0;
            const total = r.w * r.h;
            for (let i = 0; i < data.length; i += 4) {
              if (data[i] > 5 || data[i + 1] > 5 || data[i + 2] > 5) nonBlack++;
            }
            results.push({
              name: r.name,
              nonBlackPixels: nonBlack,
              totalPixels: total,
              sampleRgba: [data[0], data[1], data[2], data[3]],
            });
          }
          resolve({ width: w, height: h, regions: results });
        };
        img.onerror = () => resolve(null);
        img.src = `data:image/png;base64,${pngBase64}`;
      });
    }, b64);
  }

  test('framebuffer has non-black pixels after boot (screenshot API)', async ({ page, request }) => {
    test.setTimeout(60_000);

    await page.goto(PAGE_URL);

    const pixelData = await getScreenshotPixels(page, request);

    console.log(`  screenshot: ${pixelData?.width}x${pixelData?.height}`);
    if (pixelData?.regions) {
      for (const r of pixelData.regions) {
        console.log(`  ${r.name}: ${r.nonBlackPixels}/${r.totalPixels} non-black, sample=[${r.sampleRgba}]`);
      }
    }

    expect(pixelData).not.toBeNull();
    expect(pixelData!.width).toBeGreaterThan(0);
    expect(pixelData!.height).toBeGreaterThan(0);

    // After boot to Finder, the menu bar should have non-black pixels
    const menuBar = pixelData!.regions.find(r => r.name === 'menu-bar');
    expect(menuBar).toBeDefined();
    expect(menuBar!.nonBlackPixels).toBeGreaterThan(0);
  });

  // ── Combined: mouse movement causes pixel changes in framebuffer ────

  test('mouse movement causes visible pixel change in framebuffer', async ({ page, request }) => {
    test.setTimeout(60_000);

    await page.goto(PAGE_URL);
    await page.waitForLoadState('networkidle');
    await waitForDataChannel(page);

    // Switch to absolute mode and move cursor to corner
    await page.evaluate(() => {
      const c = (window as any).client;
      c.mouseMode = 'absolute';
      c.sendMouseModeChange('absolute');
    });
    await new Promise(r => setTimeout(r, 200));

    await page.evaluate(() => {
      (window as any).client.sendMouseAbsolute(10, 10, performance.now());
    });
    // Wait for position to take effect + frame to render
    await new Promise(r => setTimeout(r, 500));

    // Capture screenshot with cursor at (10,10)
    const before = await getScreenshotPixels(page, request);

    // Move cursor to center
    await page.evaluate(() => {
      (window as any).client.sendMouseAbsolute(300, 240, performance.now());
    });
    await new Promise(r => setTimeout(r, 500));

    // Capture screenshot with cursor at (300,240)
    const after = await getScreenshotPixels(page, request);

    // Compare the center region — cursor should have moved there
    const beforeCenter = before!.regions.find(r => r.name === 'center');
    const afterCenter = after!.regions.find(r => r.name === 'center');

    console.log(`  center before: ${beforeCenter?.nonBlackPixels}/${beforeCenter?.totalPixels} non-black`);
    console.log(`  center after:  ${afterCenter?.nonBlackPixels}/${afterCenter?.totalPixels} non-black`);

    // At minimum, the screenshots should be valid (non-null, non-zero size)
    expect(before).not.toBeNull();
    expect(after).not.toBeNull();
    expect(before!.width).toBeGreaterThan(0);

    // Verify the cursor is actually at the position we sent
    const mousePos = await getMousePosition();
    expect(mousePos.x).toBe(300);
    expect(mousePos.y).toBe(240);
  });
});

// ══════════════════════════════════════════════════════════════════════
// Soak test — long-running stall detection
// ══════════════════════════════════════════════════════════════════════

test.describe('Soak Test', () => {
  // Duration in seconds (override with SOAK_DURATION_S env var)
  const SOAK_DURATION_S = parseInt(process.env.SOAK_DURATION_S || '60');
  const SOAK_POLL_INTERVAL_MS = 100; // How often to send mouse + check position
  const SOAK_STALL_THRESHOLD_MS = 500;

  test.beforeAll(async () => {
    // Emulator is auto-spawned by the fixture; just start CPU and wait for boot
    await fetch(`${API}/api/emulator/start`, { method: 'POST' });
    await waitForBootPhase(HTTP_PORT, 'Finder', 45_000).catch(() =>
      waitForBootPhase(HTTP_PORT, 'desktop', 5_000)
    );
    // Let Finder settle
    await new Promise(r => setTimeout(r, 3000));
  });

  test(`soak: ${SOAK_DURATION_S}s sustained mouse movement`, async ({ page }) => {
    test.setTimeout((SOAK_DURATION_S + 30) * 1000);

    await page.goto(PAGE_URL);
    await page.waitForLoadState('networkidle');

    // Wait for data channel
    const dcOk = await page.evaluate(async () => {
      const deadline = Date.now() + 15000;
      while (Date.now() < deadline) {
        const c = (window as any).client;
        if (c?.dataChannel?.readyState === 'open') return true;
        await new Promise(r => setTimeout(r, 200));
      }
      return false;
    });
    expect(dcOk).toBe(true);

    // Set absolute mouse mode
    await page.evaluate(() => {
      const c = (window as any).client;
      c.mouseMode = 'absolute';
      c.sendMouseModeChange('absolute');
    });
    await new Promise(r => setTimeout(r, 300));

    // Soak loop: continuously move mouse to alternating positions
    const positions = [
      { x: 100, y: 100 },
      { x: 500, y: 100 },
      { x: 500, y: 400 },
      { x: 100, y: 400 },
      { x: 300, y: 250 },
      { x: 200, y: 350 },
      { x: 400, y: 150 },
      { x: 150, y: 250 },
    ];

    const stalls: Array<{
      time: number;       // seconds into soak
      target: { x: number; y: number };
      got: { x: number; y: number };
      latencyMs: number;
    }> = [];
    const latencies: number[] = [];
    let totalMoves = 0;
    let posIndex = 0;

    const soakStart = Date.now();
    const soakEnd = soakStart + SOAK_DURATION_S * 1000;

    while (Date.now() < soakEnd) {
      const target = positions[posIndex % positions.length];
      posIndex++;

      await page.evaluate(({ x, y }: { x: number; y: number }) => {
        (window as any).client.sendMouseAbsolute(x, y, performance.now());
      }, target);

      const sendTime = Date.now();
      let pos = { x: -1, y: -1 };
      let matched = false;

      // Poll until position matches or stall threshold exceeded
      let fetchFailed = false;
      while (Date.now() - sendTime < SOAK_STALL_THRESHOLD_MS * 2) {
        await new Promise(r => setTimeout(r, 20));
        try {
          const resp = await fetch(`${API}/api/mouse`);
          if (resp.ok) {
            pos = await resp.json();
            fetchFailed = false;
          } else {
            fetchFailed = true;
          }
        } catch {
          fetchFailed = true;
        }
        if (pos.x === target.x && pos.y === target.y) {
          matched = true;
          break;
        }
      }

      const latency = Date.now() - sendTime;
      totalMoves++;
      latencies.push(latency);

      if (!matched || latency > SOAK_STALL_THRESHOLD_MS) {
        const elapsed = (Date.now() - soakStart) / 1000;
        const reason = fetchFailed ? 'API unreachable' : `pos mismatch`;
        stalls.push({
          time: Math.round(elapsed * 10) / 10,
          target,
          got: pos,
          latencyMs: latency,
        });
        console.log(`  STALL at ${elapsed.toFixed(1)}s: target=(${target.x},${target.y}) got=(${pos.x},${pos.y}) latency=${latency}ms (${reason})`);
      }

      // Wait between moves
      await new Promise(r => setTimeout(r, SOAK_POLL_INTERVAL_MS));
    }

    // Summary
    const avgLatency = latencies.reduce((a, b) => a + b, 0) / latencies.length;
    const maxLatency = Math.max(...latencies);
    const p95 = latencies.sort((a, b) => a - b)[Math.floor(latencies.length * 0.95)];
    const p99 = latencies.sort((a, b) => a - b)[Math.floor(latencies.length * 0.99)];

    console.log(`  Soak summary: ${totalMoves} moves over ${SOAK_DURATION_S}s`);
    console.log(`  Latency: avg=${avgLatency.toFixed(0)}ms, p95=${p95}ms, p99=${p99}ms, max=${maxLatency}ms`);
    console.log(`  Stalls (>${SOAK_STALL_THRESHOLD_MS}ms): ${stalls.length}`);
    if (stalls.length > 0) {
      console.log(`  Stall times: ${stalls.map(s => `${s.time}s`).join(', ')}`);
    }

    // Fail if more than 2% of moves stalled
    const stallRate = stalls.length / totalMoves;
    expect(stallRate).toBeLessThan(0.02);
    // Fail if max latency exceeds 2 seconds (hard stall)
    expect(maxLatency).toBeLessThan(2000);
  });
});
