// HiDPI gate (docs/executor/HiDPI.md) for the wasm tab: boots the same
// System image to the Finder twice, once at ?scale=1 and once at
// ?scale=2, and checks that the 2x run is actually redrawing text from
// outlines -- not just a canvas the browser (or Skia) blew up with
// nearest-neighbor duplication, which would look identical to a naive
// pixel-doubling of the 1x run at the sub-pixel level.
//
//   node web/tests/tab_hidpi.mjs DIR IMAGE
//
// IMAGE: an HFS image with a blessed System Folder and a Finder, 640x480
// (the tests' tests-7.6.1.img). Skips (77) without IMAGE, or without a
// -DEXECUTOR_SKIA=ON build (reported, not a skip: that's the point of
// the gate -- see "two canvases report the same backing size" below).
import fs from 'node:fs';
import path from 'node:path';
import { open, firstFrame, request, until } from './harness.mjs';

const [dir, image] = process.argv.slice(2);
if (!image || !fs.existsSync(image)) {
    console.log(`SKIP: no System image (${image || 'none given'})`);
    process.exit(77);
}
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};

// Boots IMAGE's Finder at the given ?scale=, returns its canvas pixels
// (worker.js's 'pixels' op: the canvas's own backing buffer, not a
// screenshot, which a browser could resample at its own discretion) and
// Module.stats (frames, drawMs: the Worker's own render-thread cost).
async function bootAt(scale) {
    const { page, close } = await open(dir, { log: false, query: `scale=${scale}` });
    await firstFrame(page);
    await page.setInputFiles('#add', image);
    await firstFrame(page);
    await page.selectOption('#system', path.basename(image));
    await firstFrame(page, 60000);
    const up = await until(() => request(page, 'app').then((a) => a === 'Finder'), 60000);
    if (!up)
        console.log(`FAIL: scale=${scale}: the Finder never came up`);
    await until(() => request(page, 'windows').then((ws) => ws.some((w) => w.title === 'Desktop')), 15000);
    await page.waitForTimeout(2000);     // menu bar settled, no blinking caret etc.
    const stats = await request(page, 'stats');
    const pix = await page.evaluate(() => executor.request('pixels'));
    await page.locator('#screen').screenshot({ path: path.join(dir, `tab_hidpi-${scale}x.png`) });
    await close();
    return { pix, stats, up };
}

const r1 = await bootAt(1);
const r2 = await bootAt(2);
check(r1.up, 'scale=1: the Finder is up');
check(r2.up, 'scale=2: the Finder is up');

const { width: w1, height: h1, data: d1 } = r1.pix;
const { width: w2, height: h2, data: d2 } = r2.pix;
console.log(`canvas: ${w1}x${h1} at scale=1, ${w2}x${h2} at scale=2`);
check(w2 === w1 * 2 && h2 === h1 * 2,
      'the canvas backing buffer is twice the size at scale=2 (hidpi::compose() ran)');

// The menu bar strip (text, every run): nearest-neighbor-duplicate the
// 1x pixels there and count how many differ from the real 2x pixels. A
// plain upscale -- no antialiasing, no hinting, the glyph edges exactly
// doubled -- would match almost everywhere; outlines redrawn at 2x (this
// gate's whole point) do not.
function naiveUpscaleDiffers(stripH1) {
    if (w2 !== w1 * 2 || h2 < stripH1 * 2)
        return { compared: 0, differ: 0 };
    let differ = 0, compared = 0;
    for (let y2 = 0; y2 < stripH1 * 2; y2++) {
        const y1 = y2 >> 1;
        for (let x2 = 0; x2 < w2; x2++) {
            const x1 = x2 >> 1;
            const i1 = (y1 * w1 + x1) * 4, i2 = (y2 * w2 + x2) * 4;
            compared++;
            if (d1[i1] !== d2[i2] || d1[i1 + 1] !== d2[i2 + 1] || d1[i1 + 2] !== d2[i2 + 2])
                differ++;
        }
    }
    return { compared, differ };
}
const { compared, differ } = naiveUpscaleDiffers(20);   // the menu bar's height, 1x
const pct = compared ? (100 * differ / compared).toFixed(1) : 'n/a';
console.log(`menu bar: ${differ}/${compared} pixels (${pct}%) differ from a naive 2x of the 1x run`);
check(compared > 0 && differ / compared > 0.01,
      "the menu bar's text was redrawn from outlines, not just upscaled (>1% of pixels differ)");

const perFrame1 = r1.stats.frames ? r1.stats.drawMs / r1.stats.frames : 0;
const perFrame2 = r2.stats.frames ? r2.stats.drawMs / r2.stats.frames : 0;
console.log(`per-frame draw cost: ${perFrame1.toFixed(2)} ms/frame at scale=1 ` +
            `(${r1.stats.frames} frames), ${perFrame2.toFixed(2)} ms/frame at scale=2 ` +
            `(${r2.stats.frames} frames)`);

console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
