// The bar's HiDPI box (web/executor/main.js HiDpiBar; docs/executor/Settings.md,
// HiDPI.md): display.scale 2 ticked, 1 not, live, at devicePixelRatio 2.
//
//   node web/tests/tab_hidpibar.mjs DIR IMAGE
//
// IMAGE: a System image with outline fonts (tests-7.6.1.img; only read).
// On tools/standalone, a 1280x1000 window at dpr 2:
//   - at start (Sharpness Auto): ticked, the canvas 1280x960, the menu bar
//     drawn from outlines (it differs from the 1x frame doubled), the tooltip;
//   - each of: untick (the canvas 640x480, the 1x frame itself), tick again
//     (1280x960, from outlines again: the screen painted again at the
//     change) checks the setting, the canvas, the pointer on the Mac pixel
//     at points across the screen and a click on Special's title opening it;
//   - the dialog's Sharpness applies live (no "restarts Executor");
//   - unticked, the page loaded again: the setting kept, the box unticked,
//     the canvas 640x480.
// Skips (77) without IMAGE.
import fs from 'node:fs';
import path from 'node:path';
import { open, firstFrame, request, until, pickSystem } from './harness.mjs';

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
const out = process.env.TMPDIR || dir;
const viewport = { width: 1280, height: 1000 };

const { page, close } = await open(dir, { log: !!process.env.TAB_LOG, viewport, dpr: 2 });
try {
    const get = (p) => page.evaluate((p) => Settings.get(p), p);
    const box = () => page.evaluate(() => {
        const t = document.getElementById('hidpi');
        return { checked: t.checked, disabled: t.disabled, title: document.getElementById('hidpi-box').title };
    });
    const pixels = () => page.evaluate(() => executor.request('pixels'));
    const canvasIs = (w, h) => until(async () => {
        const p = await pixels();
        return p && p.width === w && p.height === h;
    }, 20000);

    await firstFrame(page);
    await page.setInputFiles('#add', image);
    await firstFrame(page, 120000);
    await pickSystem(page, path.basename(image));
    await firstFrame(page, 120000);
    check(await until(() => request(page, 'app').then((a) => a === 'Finder'), 180000), 'the Finder is up');
    await until(() => request(page, 'windows').then((ws) => ws.some((w) => w.title === 'Desktop')), 30000);
    await page.waitForTimeout(2000);

    // The menu bar strip: the canvas against the 1x frame doubled (nearest
    // neighbour). Outlines differ at the glyphs' edges; a plain upscale
    // doesn't.
    const fromOutlines = async (tag) => {
        await page.waitForTimeout(1500);        // (the repaint's text recorded and composed)
        const [d2, d1] = [await pixels(), await request(page, 'screen1x')];
        if (!d1 || !d2 || d2.width !== d1.width * 2) {
            check(false, `${tag}: the frames (${d1 && d1.width}, ${d2 && d2.width})`);
            return;
        }
        let differ = 0, compared = 0;
        for (let y = 0; y < 40; y++)
            for (let x = 0; x < d2.width; x++) {
                const i1 = ((y >> 1) * d1.width + (x >> 1)) * 4, i2 = (y * d2.width + x) * 4;
                compared++;
                differ += d1.data[i1] !== d2.data[i2] || d1.data[i1 + 1] !== d2.data[i2 + 1]
                    || d1.data[i1 + 2] !== d2.data[i2 + 2];
            }
        check(differ / compared > 0.01, `${tag}: the menu bar from outlines (${(100 * differ / compared).toFixed(1)}% differ from the 1x doubled)`);
    };
    const sameAs1x = async (tag) => {
        const [c, d1] = [await pixels(), await request(page, 'screen1x')];
        let differ = 0;
        for (let i = 0; i < d1.data.length; i += 4)
            differ += c.data[i] !== d1.data[i] || c.data[i + 1] !== d1.data[i + 1] || c.data[i + 2] !== d1.data[i + 2];
        check(c.width === d1.width && differ < c.width * 16, `${tag}: the canvas is the 1x frame (${differ} pixels differ)`);
    };

    // The pointer on a Mac pixel's centre is at that pixel on the Mac, and
    // a click on Special's title opens it.
    const clicks = async (tag) => {
        const W = 640, H = 480;
        const r = await page.locator('#screen').boundingBox();
        const at = (x, y) => [r.x + (x + 0.5) * r.width / W, r.y + (y + 0.5) * r.height / H];
        const misses = [];
        for (const [x, y] of [[3, 3], [W >> 1, H >> 1], [W - 4, H - 4], [W - 4, 3], [3, H - 4], [101, 377]]) {
            await page.mouse.move(...at(x, y));
            if (!await until(() => request(page, 'mouse').then((m) => m.x === x && m.y === y), 3000, 50)) {
                const m = await request(page, 'mouse');
                misses.push(`(${x},${y}) -> (${m.x},${m.y})`);
            }
        }
        check(!misses.length, `${tag}: the pointer lands on the Mac pixel${misses.length ? ' ' + misses.join(' ') : ''}`);
        const special = (await request(page, 'menus')).find((m) => m.title === 'Special');
        if (!special) {
            check(false, `${tag}: the Special menu`);
            return;
        }
        // ('drawn' keeps the last 512 strings: a new Restart counts)
        const restarts = () => request(page, 'drawn').then((d) => d.filter((s) => /Restart/.test(s)).length);
        const before = await restarts();
        await page.mouse.move(...at(special.left + 12, 10));
        await page.mouse.down();
        await page.waitForTimeout(1500);
        await page.mouse.move(...at(special.left + 12, H >> 1));
        await page.mouse.up();
        check(await until(async () => await restarts() > before, 5000),
              `${tag}: a click on Special's title opens it`);
        await page.waitForTimeout(500);
    };

    // At start: Auto at dpr 2, so 2x.
    let b = await box();
    check(b.checked && !b.disabled, `start: the box ticked (Auto at dpr 2) (${JSON.stringify(b)})`);
    check(/^draw the Mac at 2x: sharp on high-DPI screens; costs speed and memory/.test(b.title) && /Now 2×/.test(b.title),
          `start: the tooltip (${b.title.split('\n').join(' | ')})`);
    check(await page.isVisible('#hidpi') && await page.evaluate(() =>
        document.getElementById('res-box').nextElementSibling === document.getElementById('hidpi-box')),
          'the box is in the bar, after the screen size');
    check(await canvasIs(1280, 960), 'start: the canvas is 1280x960');
    await fromOutlines('start');
    await clicks('start, 2x');
    await page.locator('#bar').screenshot({ path: path.join(out, 'tab_hidpibar_bar.png') }).catch(() => {});

    // Unticked: 1x.
    await page.click('#hidpi');
    check(await until(async () => await get('display.scale') === 1, 10000), 'untick: display.scale 1');
    check(await canvasIs(640, 480), 'untick: the canvas is 640x480');
    b = await box();
    check(!b.checked && /Now 1×/.test(b.title), `untick: the box (${b.title.split('\n')[1]})`);
    await page.waitForTimeout(1000);
    await sameAs1x('untick');
    await clicks('1x');
    await page.locator('#screen').screenshot({ path: path.join(out, 'tab_hidpibar_1x.png') });

    // Ticked again: 2x, from outlines again (the change painted the screen).
    await page.click('#hidpi');
    check(await until(async () => await get('display.scale') === 2, 10000), 'tick: display.scale 2');
    check(await canvasIs(1280, 960), 'tick: the canvas is 1280x960');
    await fromOutlines('tick');
    await clicks('tick, 2x');
    await page.locator('#screen').screenshot({ path: path.join(out, 'tab_hidpibar_2x.png') });

    // The dialog's Sharpness: live, not a restart.
    await page.evaluate(() => Settings.open('display'));
    await page.waitForSelector('dialog#settings[open]');
    const row = await page.$eval('[data-path="display.scale"]', (r) => r.textContent);
    check(!/restarts Executor/.test(row), `the dialog's Sharpness applies live (${row.slice(0, 80)})`);
    await page.click('dialog#settings .settings-cancel');

    // Saved: unticked, the page again.
    await page.click('#hidpi');
    check(await until(async () => await get('display.scale') === 1, 10000), 'untick again: display.scale 1');
    const saved = await page.evaluate(async () => (await (await fetch('api/settings', { cache: 'no-store' })).json()).settings.display.scale);
    check(saved === 1, `untick: saved on the server (${saved})`);
    await page.reload();
    await page.evaluate(() => Settings.ready);
    await firstFrame(page, 120000);
    b = await box();
    check(await get('display.scale') === 1 && !b.checked, `the page again: 1, the box unticked (${JSON.stringify(b.checked)})`);
    check(await canvasIs(640, 480), 'the page again: the canvas is 640x480');
} catch (e) {
    check(false, `${e.message}`);
} finally {
    await close();
}
process.exit(failed ? 1 : 0);
