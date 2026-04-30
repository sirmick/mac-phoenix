/**
 * Codec auto-fallback controller — integration tests against a running emulator.
 *
 * The controller's pure logic is covered by tests/unit/codec-fallback.test.js
 * (21 cases, ~40ms). These tests verify the wiring:
 *   - controller.start() actually drives the first connect
 *   - selectCodec emits land on the host's transport teardown/reconnect
 *   - onFrame() fires from real video tracks / WS frame bytes
 *   - onEmulatorState({running:false}) updates the overlay
 *   - manual onCodecChangeRequested() steps the chain end-to-end
 *
 * Run: xvfb-run npx playwright test tests/e2e/codec-fallback.spec.ts
 */
import { test, expect, waitForBootPhase } from './fixtures';

test.describe.configure({ mode: 'serial' });

test.describe('Codec fallback controller (integration)', () => {
  test.setTimeout(120_000);

  let booted = false;

  async function ensureBooted(port: number) {
    if (booted) return;
    await fetch(`http://localhost:${port}/api/emulator/start`, { method: 'POST' });
    await waitForBootPhase(port, 'Finder', 30_000).catch(() =>
      waitForBootPhase(port, 'desktop', 5_000),
    );
    await new Promise(r => setTimeout(r, 2000));
    booted = true;
  }

  async function readControllerState(page: any) {
    return page.evaluate(() => {
      const c = (window as any).client;
      const ctl = c?.fallbackCtl;
      if (!ctl) return null;
      return {
        phase: ctl.phase,
        chain: Array.isArray(ctl.chain) ? [...ctl.chain] : [],
        chainIdx: ctl.chainIdx,
        currentCodec: ctl.chain && ctl.chainIdx >= 0 ? ctl.chain[ctl.chainIdx] : null,
        firstFrameSeen: !!ctl.firstFrameSeen,
        emulatorRunning: !!ctl.emulatorRunning,
        clientConnected: !!c.connected,
      };
    });
  }

  // ── 1. Initial connect via controller ────────────────────────────────

  test('initial connect: controller bootstraps chain, first frame arrives', async ({
    page, emulatorPort,
  }) => {
    await ensureBooted(emulatorPort);
    await page.goto(`http://localhost:${emulatorPort}/`);

    const result = await page.evaluate(async () => {
      const t0 = performance.now();
      const deadline = t0 + 8000;
      while (performance.now() < deadline) {
        const c = (window as any).client;
        if (c?.fallbackCtl?.firstFrameSeen) {
          return {
            ttfMs: performance.now() - t0,
            chain: [...c.fallbackCtl.chain],
            currentCodec: c.fallbackCtl.chain[c.fallbackCtl.chainIdx],
            phase: c.fallbackCtl.phase,
          };
        }
        await new Promise(r => setTimeout(r, 50));
      }
      const c = (window as any).client;
      return {
        ttfMs: -1,
        chain: c?.fallbackCtl?.chain ? [...c.fallbackCtl.chain] : [],
        currentCodec: null,
        phase: c?.fallbackCtl?.phase || 'unknown',
      };
    });

    console.log('  initial connect:', result);
    expect(result.ttfMs, 'first frame must arrive within 8s of page load').toBeGreaterThan(0);
    // Tier 1 is the natural starting tier when WebRTC codecs are compiled in.
    // CI builds without OpenH264/libvpx will start at tier 2; either is fine.
    expect(result.chain.length, 'chain must be non-empty').toBeGreaterThan(0);
    expect(['connected', 'connecting']).toContain(result.phase);
  });

  // ── 2. Forced ICE fast-fail steps the chain within budget ────────────

  test('forced ICE-failed: chain steps and new codec delivers frames within 3s', async ({
    page, emulatorPort,
  }) => {
    await ensureBooted(emulatorPort);
    await page.goto(`http://localhost:${emulatorPort}/`);

    // Wait for the initial codec to land its first frame.
    await page.waitForFunction(
      () => !!(window as any).client?.fallbackCtl?.firstFrameSeen,
      { timeout: 10_000 },
    );

    const before = await readControllerState(page);
    console.log('  before fallback:', before);

    // Tier 1 fast-fail only makes sense if we're actually on tier 1. If the
    // build doesn't have a webrtc codec available, skip rather than fail.
    const onTier1 = before?.currentCodec === 'vp9' || before?.currentCodec === 'h264';
    test.skip(!onTier1, 'no WebRTC codec available — fast-fail path not exercised');

    const result = await page.evaluate(async () => {
      const c = (window as any).client;
      const ctl = c.fallbackCtl;
      const startIdx = ctl.chainIdx;

      // Fire the synthetic ICE-failed event the same way the real
      // RTCPeerConnection.oniceconnectionstatechange would.
      ctl.firstFrameSeen = false;  // re-arm so we wait for next codec's first frame
      const t0 = performance.now();
      ctl.onIceState('failed');

      const deadline = t0 + 5000;
      while (performance.now() < deadline) {
        if (ctl.firstFrameSeen && ctl.chainIdx > startIdx) {
          return {
            elapsedMs: performance.now() - t0,
            startIdx,
            endIdx: ctl.chainIdx,
            startCodec: ctl.chain[startIdx],
            endCodec: ctl.chain[ctl.chainIdx],
            chain: [...ctl.chain],
          };
        }
        await new Promise(r => setTimeout(r, 50));
      }
      return {
        elapsedMs: -1,
        startIdx, endIdx: ctl.chainIdx,
        startCodec: ctl.chain[startIdx],
        endCodec: ctl.chain[ctl.chainIdx] || null,
        chain: [...ctl.chain],
      };
    });

    console.log('  fast-fallback:', result);
    expect(result.elapsedMs, 'fallback codec must deliver a frame').toBeGreaterThan(0);
    expect(result.elapsedMs, 'fast-fail → next-codec first frame should fit 3s').toBeLessThan(3000);
    expect(result.endCodec).not.toBe(result.startCodec);
    expect(result.endIdx).toBeGreaterThan(result.startIdx);
  });

  // ── 3. Mac off → idle-mac-off overlay ────────────────────────────────

  test('Mac off: controller phase = idle-mac-off, overlay shows "Mac is off"', async ({
    page, emulatorPort,
  }) => {
    await ensureBooted(emulatorPort);
    await page.goto(`http://localhost:${emulatorPort}/`);

    // Confirm we're up before stopping.
    await page.waitForFunction(
      () => !!(window as any).client?.connected,
      { timeout: 10_000 },
    );

    // Stop the emulator and wait for the controller to notice
    // (pollEmulatorStatus runs every 125ms).
    await fetch(`http://localhost:${emulatorPort}/api/emulator/stop`, { method: 'POST' });

    const result = await page.evaluate(async () => {
      const deadline = performance.now() + 5000;
      while (performance.now() < deadline) {
        const c = (window as any).client;
        if (c?.fallbackCtl?.phase === 'idle-mac-off') {
          const statusEl = document.getElementById('overlay-status');
          const overlayEl = document.getElementById('overlay');
          const spinnerEl = document.getElementById('spinner');
          return {
            phase: 'idle-mac-off',
            overlayStatus: statusEl?.textContent || null,
            overlayHidden: !!overlayEl?.classList.contains('hidden'),
            spinnerHidden: spinnerEl ? spinnerEl.style.display === 'none' : null,
          };
        }
        await new Promise(r => setTimeout(r, 100));
      }
      const c = (window as any).client;
      return {
        phase: c?.fallbackCtl?.phase || 'unknown',
        overlayStatus: null, overlayHidden: null, spinnerHidden: null,
      };
    });

    console.log('  Mac-off state:', result);
    expect(result.phase).toBe('idle-mac-off');
    expect(result.overlayStatus || '').toMatch(/Mac is off/i);
    expect(result.overlayHidden, 'overlay should be visible').toBe(false);
    expect(result.spinnerHidden, 'spinner should be hidden when Mac is off').toBe(true);
  });

  // ── 4. Restart restores chain (cleans up after test 3) ───────────────

  test('Mac restart: chain re-bootstraps and frames flow again', async ({
    page, emulatorPort,
  }) => {
    // Test 3 left the emulator stopped. Bring it back up.
    await fetch(`http://localhost:${emulatorPort}/api/emulator/start`, { method: 'POST' });
    await waitForBootPhase(emulatorPort, 'Finder', 30_000).catch(() =>
      waitForBootPhase(emulatorPort, 'desktop', 5_000),
    );
    booted = true;  // for any subsequent tests

    // Reload the page so we exercise a fresh page-load bootstrap path.
    await page.goto(`http://localhost:${emulatorPort}/`);

    const result = await page.evaluate(async () => {
      const t0 = performance.now();
      const deadline = t0 + 10_000;
      while (performance.now() < deadline) {
        const c = (window as any).client;
        if (c?.fallbackCtl?.firstFrameSeen) {
          return {
            ttfMs: performance.now() - t0,
            phase: c.fallbackCtl.phase,
            currentCodec: c.fallbackCtl.chain[c.fallbackCtl.chainIdx],
          };
        }
        await new Promise(r => setTimeout(r, 100));
      }
      return { ttfMs: -1, phase: 'timeout', currentCodec: null };
    });

    console.log('  restart:', result);
    expect(result.ttfMs).toBeGreaterThan(0);
    expect(result.phase).toBe('connected');
  });
});
