// The Finder's live scrolling on 8.6 in the tab (ctlMouse.cpp): the System
// Folder's window, its thumb dragged with the button held; the window
// scrolls before the button is let go, and the Worker lives on (the
// Finder's action procedure called with nothing read a garbage
// ControlHandle: a wasm trap in C_GetControlReference).
//
//   node executor/tests/web/tab_scroll.mjs DIR SYSTEM
//
// SYSTEM: Apple's 8.6 (HFS+, the tests' 8.6.img). Skips (77) without it.
import fs from 'node:fs';
import path from 'node:path';
import { open, firstFrame, request, until, pickSystem } from './harness.mjs';

const [dir, system] = process.argv.slice(2);
if (!system || !fs.existsSync(system)) {
    console.log(`SKIP: no System image (${system || 'none given'})`);
    process.exit(77);
}
const { page, close } = await open(dir, { log: !!process.env.TAB_LOG });
let died = false;
page.on('worker', (w) => w.on('console', (m) => {
    if (/unhandled rejection|RuntimeError|Executor aborted/.test(m.text()))
        died = true;
}));
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const shots = process.env.SHOTS || dir;

await firstFrame(page);
await page.check('#ppc');
await firstFrame(page);
await page.setInputFiles('#add', system);
await firstFrame(page, 120000);
await pickSystem(page, path.basename(system));
await firstFrame(page, 120000);
check(await until(() => request(page, 'app').then((a) => a === 'Finder'), 120000), 'the Finder is up');
await page.waitForTimeout(3000);

const box = await page.locator('#screen').boundingBox();
const at = (x, y) => [box.x + x * box.width / 640, box.y + y * box.height / 480];
let opened = null;
for (const y of [115, 125]) {
    await page.mouse.dblclick(...at(593, y));
    opened = await until(() => request(page, 'windows')
        .then((ws) => ws.find((w) => w.title === 'Macintosh HD' && w.visible)), 8000);
    if (opened)
        break;
}
check(opened, 'the startup disk\'s window');
await page.waitForTimeout(2000);
// its System Folder: 29 items, a window that scrolls (x 403-418 its scroll
// bar, the thumb at the top of the track)
await page.mouse.dblclick(...at(64, 90));
check(await until(() => request(page, 'windows')
    .then((ws) => ws.some((w) => w.title === 'System Folder' && w.visible)), 15000), 'the System Folder\'s window');
await page.waitForTimeout(3000);

const pixels = () => request(page, 'pixels');
const before = await pixels();
await page.mouse.move(...at(410, 90));
await page.mouse.down();
for (const y of [100, 110, 120, 130]) {
    await page.mouse.move(...at(410, y));
    await page.waitForTimeout(250);
}
await page.waitForTimeout(1000);
const during = await pixels();
await page.locator('#screen').screenshot({ path: path.join(shots, 'tab_scroll_dragging.png') });
await page.mouse.up();
await page.waitForTimeout(1000);
// the window's contents (x 14-400, y 66-236) with the button still held
let changed = 0;
for (let y = 66; y < 236; y++)
    for (let x = 14; x < 400; x++) {
        const i = (y * before.width + x) * 4;
        if (before.data[i] !== during.data[i] || before.data[i + 1] !== during.data[i + 1])
            changed++;
    }
console.log(`${changed} pixels changed with the thumb held`);
check(changed > 500, 'the window scrolls while its thumb is dragged');
check(!died && await request(page, 'app') === 'Finder', 'the Worker lives on, the Finder in front');

await close();
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
