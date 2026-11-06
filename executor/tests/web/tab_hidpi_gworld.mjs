// HiDPI GWorld shadows in the wasm tab (docs/executor/HiDPI.md "Phase 4"):
// at ?scale=2 on Apple's 8.6 System with PowerPC, launches GWorldGallery
// (tests/guest/gworld: text drawn in GWorlds and copied to its window) and
// checks the canvas's own pixels: the copied text is anti-aliased (greys a
// scaled-up 1x frame of black text on white hasn't), the text whose GWorld
// the program inverted itself and CopyMask's masked-out part are not (the
// 1x pixels, scaled up). The boxes are GWorldGallery's, as in
// tests/test_hidpi_gworld.sh.
//
//   node executor/tests/web/tab_hidpi_gworld.mjs DIR GUEST_DIR SYSTEM
//
// SYSTEM: 8.6.img (only read). Skips (77) without it.
import fs from 'node:fs';
import path from 'node:path';
import { open, firstFrame, request, until, pickSystem } from './harness.mjs';

const [dir, guest, system] = process.argv.slice(2);
if (!system || !fs.existsSync(system)) {
    console.log(`SKIP: no System image (${system || 'none given'})`);
    process.exit(77);
}
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};

const { page, close } = await open(dir, { log: !!process.env.TAB_LOG, query: 'scale=2' });
await firstFrame(page);
await page.check('#ppc');
await firstFrame(page);
await page.setInputFiles('#add', [system, path.join(guest, 'gworld/GWorldGalleryPPC.bin')]);
await firstFrame(page, 120000);
await pickSystem(page, path.basename(system));
await firstFrame(page, 120000);
check(await until(() => request(page, 'app').then((a) => a === 'Finder'), 120000), 'the Finder is up');
check(await request(page, 'launch', 'executor:Uploads:GWorldGalleryPPC.bin') === 0, 'launch GWorldGallery');
check(await until(() => request(page, 'windows').then((ws) => ws.some((w) => w.title === 'GWorld Gallery')), 120000),
      'its window');
await page.waitForTimeout(3000);
const { width, height, data } = await page.evaluate(() => executor.request('pixels'));
await page.locator('#screen').screenshot({ path: path.join(dir, 'tab_hidpi_gworld.png') });
await close();
// TAB_PIXELS=FILE: the canvas's pixels, raw RGBA (WIDTHxHEIGHT)
if (process.env.TAB_PIXELS)
    fs.writeFileSync(process.env.TAB_PIXELS, Buffer.from(Object.values(data)));
check(width === 1280 && height === 960, `the canvas is 2x (${width}x${height})`);

// greys in BOX (1x screen coordinates x0 y0 x1 y1): neither black nor white
const greys = ([x0, y0, x1, y1]) => {
    let n = 0;
    for (let y = 2 * y0; y < 2 * y1; y++)
        for (let x = 2 * x0; x < 2 * x1; x++) {
            const i = 4 * (y * width + x);
            const hi = Math.max(data[i], data[i + 1], data[i + 2]);
            n += hi > 0x30 && hi < 0xD0;
        }
    return n;
};
// GWorldGallery's window: content at (20, 60)
for (const [name, box, text] of [
    ['Geneva 12 copied from a GWorld', [28, 66, 300, 84], true],
    ['New York 18 bold copied from a GWorld', [28, 90, 220, 116], true],
    ['copied through a second GWorld', [28, 167, 230, 184], true],
    ['CopyMask, the mask\'s part', [28, 217, 218, 234], true],
    ['written behind QuickDraw (1x)', [20, 190, 460, 210], false],
    ['CopyMask, masked out', [221, 215, 460, 235], false],
]) {
    const n = greys(box);
    check(text ? n > 100 : n === 0, `${name}: ${n} grey pixels (${text ? 'anti-aliased' : 'none'} expected)`);
}
process.exit(failed ? 1 : 0);
