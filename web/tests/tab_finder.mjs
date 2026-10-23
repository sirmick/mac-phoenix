// W3's Finder check (docs/executor/DIRECTION.md): Apple's System and
// Finder from an uploaded disk image, in the tab.
//
//   node web/tests/tab_finder.mjs DIR IMAGE
//
// IMAGE: an HFS disk image with a blessed System Folder and a Finder (the
// tests use tests-7.5.5.img; it is only read: mounted read-only, its System
// Folder copied into the module's memory). Uploads it, chooses it as the
// System, waits for the Finder, then opens the image's disk icon with a
// double-click and checks its window came up. Skips (77) without IMAGE.
import fs from 'node:fs';
import path from 'node:path';
import { open, firstFrame, request, until } from './harness.mjs';

const [dir, image] = process.argv.slice(2);
if (!image || !fs.existsSync(image)) {
    console.log(`SKIP: no System image (${image || 'none given'})`);
    process.exit(77);
}
const { page, close } = await open(dir, { log: false });
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};

await firstFrame(page);
await page.setInputFiles('#add', image);
await firstFrame(page);
await page.selectOption('#system', path.basename(image));
await firstFrame(page, 60000);
const start = Date.now();
check(await until(() => request(page, 'app').then((a) => a === 'Finder'), 60000), 'the Finder is up');
console.log(`Finder after ${((Date.now() - start) / 1000).toFixed(1)} s`);
check(await until(() => request(page, 'windows').then((ws) => ws.some((w) => w.title === 'Desktop')), 15000),
      'its desktop window');

// The image's volume: the Finder's disk icons are down the right edge,
// the startup disk ("executor", Executor's own volume) first; the image is
// the one named after its volume, found by trying the icon slots.
await page.waitForTimeout(3000);
const box = await page.locator('#screen').boundingBox();
const at = (x, y) => [box.x + x * box.width / 640, box.y + y * box.height / 480];
let opened = null;
for (const y of [155, 103, 207]) {
    await page.mouse.dblclick(...at(600, y));
    opened = await until(() => request(page, 'windows')
        .then((ws) => ws.find((w) => w.title && !['Desktop', 'executor', 'Host'].includes(w.title))), 8000);
    if (opened)
        break;
}
console.log('opened:', JSON.stringify(opened));
check(opened && opened.visible, "a double-click opens the image's disk window");
await page.locator('#screen').screenshot({ path: path.join(dir, 'tab_finder.png') });
await close();
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
