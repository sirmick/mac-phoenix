// The scroll wheel on 8.6 in the tab (osevent/wheel.cpp, input.wheel_scroll):
// over the System Folder's window, a wheel turn down scrolls it (its
// scroll bar's down arrow clicked, the thumb lower), back up scrolls it
// back where it was, a big turn pages; sideways scrolls it across once the
// window is narrow enough to need it; with the setting off, nothing moves;
// the Worker lives on.
//
//   node web/tests/tab_wheel.mjs DIR SYSTEM
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
// its System Folder: 29 items, a window that scrolls (x 402-417 its
// vertical scroll bar, the up arrow's bottom outline at y 82, the thumb
// under it)
await page.mouse.dblclick(...at(64, 90));
check(await until(() => request(page, 'windows')
    .then((ws) => ws.some((w) => w.title === 'System Folder' && w.visible)), 15000), 'the System Folder\'s window');
await page.waitForTimeout(3000);

const pixels = () => request(page, 'pixels');
// the window's contents (x 14-400, y 66-236) that differ
const differ = (a, b, x0 = 14, y0 = 66, x1 = 400, y1 = 236) => {
    let n = 0;
    for (let y = y0; y < y1; y++)
        for (let x = x0; x < x1; x++) {
            const i = (y * a.width + x) * 4;
            if (a.data[i] !== b.data[i] || a.data[i + 1] !== b.data[i + 1] || a.data[i + 2] !== b.data[i + 2])
                n++;
        }
    return n;
};
// the thumb's top: its first blue (accent) pixel down the bar
const thumbTop = (p) => {
    for (let y = 83; y < 240; y++) {
        const i = (y * p.width + 404) * 4;
        if (p.data[i + 2] > p.data[i] + 40)
            return y;
    }
    return -1;
};

await page.mouse.move(...at(200, 150));
await page.waitForTimeout(500);
const before = await pixels();
console.log(`thumb at ${thumbTop(before)}`);
await page.mouse.wheel(0, 100);                      // a notch: 3 lines
await page.waitForTimeout(1500);
const down = await pixels();
await page.locator('#screen').screenshot({ path: path.join(shots, 'tab_wheel_down.png') });
console.log(`a notch down: ${differ(before, down)} pixels changed, thumb at ${thumbTop(down)}`);
check(differ(before, down) > 500 && thumbTop(down) > thumbTop(before), 'a notch down scrolls the window down');

await page.mouse.wheel(0, -100);
await page.waitForTimeout(1500);
const back = await pixels();
console.log(`a notch up: ${differ(before, back)} pixels differ from before, thumb at ${thumbTop(back)}`);
check(differ(before, back) < 50 && thumbTop(back) === thumbTop(before), 'a notch up scrolls it back');

await page.mouse.wheel(0, 600);                      // 18 lines: a page
await page.waitForTimeout(1500);
const paged = await pixels();
console.log(`a big turn: ${differ(back, paged)} pixels changed, thumb at ${thumbTop(paged)}`);
check(thumbTop(paged) > thumbTop(down), 'a big turn pages, further than a notch');
await page.mouse.wheel(0, -2000);                    // back to the top
await page.waitForTimeout(2500);

// sideways: the window made narrow by its grow box (412, 262 -> 250, 262),
// its horizontal bar then active (y 252-267)
await page.mouse.move(...at(412, 262));
await page.mouse.down();
for (const x of [380, 330, 280, 250]) {
    await page.mouse.move(...at(x, 262));
    await page.waitForTimeout(200);
}
await page.mouse.up();
await page.waitForTimeout(2000);
await page.mouse.move(...at(120, 150));
await page.waitForTimeout(500);
const narrow = await pixels();
await page.mouse.wheel(100, 0);
await page.waitForTimeout(1500);
const across = await pixels();
await page.locator('#screen').screenshot({ path: path.join(shots, 'tab_wheel_across.png') });
console.log(`a notch right: ${differ(narrow, across, 14, 66, 230, 236)} pixels changed`);
check(differ(narrow, across, 14, 66, 230, 236) > 300, 'a notch right scrolls the window across');

// the setting off: nothing scrolls
await page.evaluate(() => Settings.set({ input: { wheel_scroll: false } }));
const off0 = await pixels();
await page.mouse.wheel(0, 100);
await page.waitForTimeout(1500);
check(differ(off0, await pixels(), 14, 66, 230, 236) === 0, 'with input.wheel_scroll off, the wheel scrolls nothing');
await page.evaluate(() => Settings.set({ input: { wheel_scroll: null } }));

check(!died && await request(page, 'app') === 'Finder', 'the Worker lives on, the Finder in front');

await close();
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
