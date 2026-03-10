/**
 * Full-stack codec tests — verifies the complete video pipeline for all 4 codecs:
 *   Framebuffer → encoder → transport → browser decode → visible pixels
 *
 * H264/VP9:  encoder → RTP video track → browser native decode → <video> element
 * PNG/WebP:  encoder → DataChannel binary → JS decode → <canvas> element
 *
 * The fixture starts the emulator with --screen 640x480, which now also limits
 * the maximum available video mode. This prevents Mac OS from mode-switching
 * to 1920x1080 and keeps keyframes small enough for reliable decode.
 *
 * Run: xvfb-run npx playwright test tests/e2e/codec-fullstack.spec.ts
 */
import { test, expect, waitForBootPhase } from './fixtures';

const ALL_CODECS = ['h264', 'vp9', 'png', 'webp'] as const;
type Codec = typeof ALL_CODECS[number];

interface CodecStats {
  codec: string;
  framesDecoded: number;
  framesReceived: number;
  codecName: string | null;
  bytesReceived: number;
  videoWidth: number;
  videoHeight: number;
}

function getUrls(port: number) {
  const sigPort = port + 1;
  return {
    API: `http://localhost:${port}`,
    PAGE_URL: `http://localhost:${port}/?ws=ws://localhost:${sigPort}/`,
  };
}

test.describe('Full-stack Codec Tests (640x480)', () => {
  test.setTimeout(120_000);

  let API: string;
  let PAGE_URL: string;
  let booted = false;

  async function ensureBooted(port: number) {
    if (booted) return;
    const urls = getUrls(port);
    API = urls.API;
    PAGE_URL = urls.PAGE_URL;
    await fetch(`${API}/api/emulator/start`, { method: 'POST' });
    await waitForBootPhase(port, 'Finder', 30_000).catch(() =>
      waitForBootPhase(port, 'desktop', 5_000)
    );
    await new Promise(r => setTimeout(r, 2000));
    booted = true;
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

  /** Poll RTP stats until framesDecoded > 0 or timeout.
   *  If expectedCodec is set, waits for the codec name to match before accepting. */
  async function pollRTPStats(page: any, timeoutMs = 15000, expectedCodec?: string): Promise<CodecStats> {
    return page.evaluate(async ({ timeout, expected }: { timeout: number; expected: string | null }) => {
      const deadline = Date.now() + timeout;
      let lastStats: any = null;

      while (Date.now() < deadline) {
        const c = (window as any).client;
        if (!c?.pc) {
          await new Promise(r => setTimeout(r, 500));
          continue;
        }

        const report = await c.pc.getStats();
        const codecMap = new Map<string, string>();
        let stats: any = {
          framesDecoded: 0, framesReceived: 0,
          codecName: null, bytesReceived: 0, videoWidth: 0, videoHeight: 0,
        };

        report.forEach((stat: any) => {
          if (stat.type === 'codec') codecMap.set(stat.id, stat.mimeType || '');
        });

        report.forEach((stat: any) => {
          if (stat.type === 'inbound-rtp' && stat.kind === 'video') {
            stats.framesDecoded = stat.framesDecoded || 0;
            stats.framesReceived = stat.framesReceived || 0;
            stats.bytesReceived = stat.bytesReceived || 0;
            stats.videoWidth = stat.frameWidth || 0;
            stats.videoHeight = stat.frameHeight || 0;
            if (stat.codecId && codecMap.has(stat.codecId)) {
              stats.codecName = codecMap.get(stat.codecId)!;
            }
          }
        });

        lastStats = stats;
        // If expecting a specific codec, wait for it to appear in stats
        if (expected && stats.codecName && !stats.codecName.toLowerCase().includes(expected.toLowerCase())) {
          await new Promise(r => setTimeout(r, 500));
          continue;
        }
        if (stats.framesDecoded > 0) return { ...stats, codec: '' };
        await new Promise(r => setTimeout(r, 500));
      }

      return { ...(lastStats || {
        framesDecoded: 0, framesReceived: 0,
        codecName: null, bytesReceived: 0, videoWidth: 0, videoHeight: 0,
      }), codec: '' };
    }, { timeout: timeoutMs, expected: expectedCodec || null });
  }

  /** Poll datachannel stats until framesReceived > 0 or timeout. */
  async function pollDCStats(page: any, timeoutMs = 15000): Promise<CodecStats> {
    const stats = await page.evaluate(async (timeout: number) => {
      const deadline = Date.now() + timeout;

      while (Date.now() < deadline) {
        const c = (window as any).client;
        const frames = c?.pngStats?.framesReceived || 0;
        if (frames > 0) {
          return {
            framesReceived: frames,
            bytesReceived: c?.pngStats?.bytesReceived || 0,
            canvasWidth: c?.canvas?.width || 0,
            canvasHeight: c?.canvas?.height || 0,
          };
        }
        await new Promise(r => setTimeout(r, 500));
      }

      const c = (window as any).client;
      return {
        framesReceived: c?.pngStats?.framesReceived || 0,
        bytesReceived: c?.pngStats?.bytesReceived || 0,
        canvasWidth: c?.canvas?.width || 0,
        canvasHeight: c?.canvas?.height || 0,
      };
    }, timeoutMs);

    return {
      codec: '',
      framesDecoded: stats.framesReceived,
      framesReceived: stats.framesReceived,
      codecName: null,
      bytesReceived: stats.bytesReceived,
      videoWidth: stats.canvasWidth,
      videoHeight: stats.canvasHeight,
    };
  }

  /** Switch codec via API, wait for client reconnection, poll until frames arrive. */
  async function switchAndCollectStats(page: any, codec: Codec): Promise<CodecStats> {
    const isRTP = codec === 'h264' || codec === 'vp9';

    const resp = await fetch(`${API}/api/codec`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ codec }),
    });
    const body = await resp.json();
    expect(body.ok).toBe(true);

    await waitForDataChannel(page);

    if (isRTP) {
      const stats = await pollRTPStats(page, 15000, codec);
      stats.codec = codec;
      return stats;
    } else {
      // Don't reset pngStats — with dirty rect tracking, the initial keyframe
      // strips may be the only frames sent if the desktop is static.
      // Poll for any frames that arrived during the DC setup.
      const stats = await pollDCStats(page, 15000);
      stats.codec = codec;
      stats.codecName = codec;
      return stats;
    }
  }

  function logStats(label: string, stats: CodecStats) {
    console.log(`  ${label}:`);
    console.log(`    codec: ${stats.codecName}`);
    console.log(`    framesDecoded: ${stats.framesDecoded}`);
    console.log(`    framesReceived: ${stats.framesReceived}`);
    console.log(`    bytesReceived: ${stats.bytesReceived}`);
    console.log(`    resolution: ${stats.videoWidth}x${stats.videoHeight}`);
  }

  /** Sample pixels from the video element (RTP codecs). */
  async function sampleVideoPixels(page: any): Promise<{
    width: number;
    height: number;
    menuBarNonBlack: number;
    menuBarTotal: number;
  } | null> {
    return page.evaluate(() => {
      const video = document.querySelector('video') as HTMLVideoElement;
      if (!video || video.readyState < 2) return null;
      const w = video.videoWidth;
      const h = video.videoHeight;
      const canvas = document.createElement('canvas');
      canvas.width = w;
      canvas.height = h;
      const ctx = canvas.getContext('2d')!;
      ctx.drawImage(video, 0, 0);

      const imgData = ctx.getImageData(10, 2, 100, 16);
      const data = imgData.data;
      let nonBlack = 0;
      for (let i = 0; i < data.length; i += 4) {
        if (data[i] > 5 || data[i + 1] > 5 || data[i + 2] > 5) nonBlack++;
      }
      return { width: w, height: h, menuBarNonBlack: nonBlack, menuBarTotal: 100 * 16 };
    });
  }

  async function navigateAndConnect(page: any, emulatorPort: number) {
    await ensureBooted(emulatorPort);
    await page.goto(PAGE_URL);
    await page.waitForLoadState('networkidle');
    await waitForDataChannel(page);
  }

  // ── VP9: Full decode at 640x480 ─────────────────────────────────────

  test('vp9: full pipeline produces decoded frames', async ({ page, emulatorPort }) => {
    await navigateAndConnect(page, emulatorPort);

    const stats = await switchAndCollectStats(page, 'vp9');
    logStats('vp9', stats);

    expect(stats.bytesReceived).toBeGreaterThan(0);
    expect(stats.framesDecoded).toBeGreaterThan(0);
    expect(stats.codecName).toContain('VP9');
  });

  test('vp9: displays non-black content', async ({ page, emulatorPort }) => {
    await navigateAndConnect(page, emulatorPort);

    await switchAndCollectStats(page, 'vp9');
    const pixels = await sampleVideoPixels(page);

    console.log(`  vp9 pixels: ${pixels?.width}x${pixels?.height}, menu bar: ${pixels?.menuBarNonBlack}/${pixels?.menuBarTotal}`);
    expect(pixels).not.toBeNull();
    expect(pixels!.width).toBeGreaterThan(0);
    expect(pixels!.menuBarNonBlack).toBeGreaterThan(0);
  });

  // ── H264: Full decode at 640x480 ────────────────────────────────────
  // At 640x480, H264 IDR keyframes should be small enough for Chrome to decode.

  test('h264: full pipeline produces decoded frames at 640x480', async ({ page, emulatorPort }) => {
    await navigateAndConnect(page, emulatorPort);

    const stats = await switchAndCollectStats(page, 'h264');
    logStats('h264', stats);

    expect(stats.bytesReceived).toBeGreaterThan(0);
    // At 640x480, IDR keyframes should be small enough for Chrome's RTP reassembly
    expect(stats.framesDecoded).toBeGreaterThan(0);
  });

  test('h264: displays non-black content at 640x480', async ({ page, emulatorPort }) => {
    await navigateAndConnect(page, emulatorPort);

    await switchAndCollectStats(page, 'h264');
    const pixels = await sampleVideoPixels(page);

    console.log(`  h264 pixels: ${pixels?.width}x${pixels?.height}, menu bar: ${pixels?.menuBarNonBlack}/${pixels?.menuBarTotal}`);
    expect(pixels).not.toBeNull();
    expect(pixels!.width).toBeGreaterThan(0);
    expect(pixels!.menuBarNonBlack).toBeGreaterThan(0);
  });

  // ── PNG: DataChannel decode at 640x480 ──────────────────────────────

  test('png: datachannel receives frames at 640x480', async ({ page, emulatorPort }) => {
    await navigateAndConnect(page, emulatorPort);

    const stats = await switchAndCollectStats(page, 'png');
    logStats('png', stats);

    // With strip encoding, PNG frames are split to fit under DC limit
    expect(stats.framesReceived).toBeGreaterThan(0);
    expect(stats.bytesReceived).toBeGreaterThan(0);
  });

  // ── WebP: DataChannel decode at 640x480 ─────────────────────────────

  test('webp: datachannel receives frames at 640x480', async ({ page, emulatorPort }) => {
    await navigateAndConnect(page, emulatorPort);

    const stats = await switchAndCollectStats(page, 'webp');
    logStats('webp', stats);

    // With strip encoding, WebP frames are split to fit under DC limit
    expect(stats.framesReceived).toBeGreaterThan(0);
    expect(stats.bytesReceived).toBeGreaterThan(0);
  });

  // ── Codec switching: all pairs ──────────────────────────────────────

  test('switch h264 → vp9: VP9 decodes after switch', async ({ page, emulatorPort }) => {
    await navigateAndConnect(page, emulatorPort);

    const h264Stats = await switchAndCollectStats(page, 'h264');
    console.log(`  h264 (before): ${h264Stats.bytesReceived} bytes, ${h264Stats.framesDecoded} frames`);
    expect(h264Stats.bytesReceived).toBeGreaterThan(0);

    const vp9Stats = await switchAndCollectStats(page, 'vp9');
    logStats('h264 → vp9', vp9Stats);
    expect(vp9Stats.framesDecoded).toBeGreaterThan(0);
    expect(vp9Stats.codecName).toContain('VP9');
  });

  test('switch vp9 → h264: H264 decodes after switch', async ({ page, emulatorPort }) => {
    await navigateAndConnect(page, emulatorPort);

    const vp9Stats = await switchAndCollectStats(page, 'vp9');
    console.log(`  vp9 (before): ${vp9Stats.framesDecoded} frames`);
    expect(vp9Stats.framesDecoded).toBeGreaterThan(0);

    const h264Stats = await switchAndCollectStats(page, 'h264');
    logStats('vp9 → h264', h264Stats);
    expect(h264Stats.bytesReceived).toBeGreaterThan(0);
    // At 640x480, H264 should decode
    expect(h264Stats.framesDecoded).toBeGreaterThan(0);
  });

  test('switch vp9 → png → vp9: VP9 recovers after DC codec', async ({ page, emulatorPort }) => {
    await navigateAndConnect(page, emulatorPort);

    const vp9First = await switchAndCollectStats(page, 'vp9');
    expect(vp9First.framesDecoded).toBeGreaterThan(0);

    // Switch to PNG (DC codec)
    await switchAndCollectStats(page, 'png');

    // Switch back to VP9 (RTP codec)
    const vp9After = await switchAndCollectStats(page, 'vp9');
    logStats('vp9 → png → vp9', vp9After);
    expect(vp9After.framesDecoded).toBeGreaterThan(0);
  });

  test('switch h264 → png → h264: H264 recovers after DC codec', async ({ page, emulatorPort }) => {
    await navigateAndConnect(page, emulatorPort);

    const h264First = await switchAndCollectStats(page, 'h264');
    expect(h264First.bytesReceived).toBeGreaterThan(0);

    await switchAndCollectStats(page, 'png');

    const h264After = await switchAndCollectStats(page, 'h264');
    logStats('h264 → png → h264', h264After);
    expect(h264After.bytesReceived).toBeGreaterThan(0);
  });

  // ── Codec API ───────────────────────────────────────────────────────

  test('codec API: all 4 codecs accept POST and return ok', async ({ page, emulatorPort }) => {
    await ensureBooted(emulatorPort);

    for (const codec of ALL_CODECS) {
      const resp = await fetch(`${API}/api/codec`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ codec }),
      });
      const body = await resp.json();
      expect(body.ok).toBe(true);
      console.log(`  POST /api/codec ${codec}: ok`);
    }
  });

  test('codec API: invalid codec returns error', async ({ page, emulatorPort }) => {
    await ensureBooted(emulatorPort);

    const resp = await fetch(`${API}/api/codec`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ codec: 'av1' }),
    });
    const body = await resp.json();
    expect(body.error).toBeDefined();
  });

  // ── Resolution verification ─────────────────────────────────────────

  test('resolution locked to 640x480 (no mode switch)', async ({ page, emulatorPort }) => {
    await navigateAndConnect(page, emulatorPort);

    const stats = await switchAndCollectStats(page, 'vp9');
    logStats('resolution check', stats);

    // With --screen 640x480 now limiting max mode, Mac should stay at 640x480
    expect(stats.videoWidth).toBeLessThanOrEqual(640);
    expect(stats.videoHeight).toBeLessThanOrEqual(480);
  });
});
