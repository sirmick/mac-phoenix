// The 8.6 Finder's folder windows in the tab (qGWorld.cpp): it asks for
// each window's offscreen (350K at 32 bits) with useTempMem. Put in its
// 1 MB partition, two filled it; the next GWorld got an empty handle for
// its pixels, the Finder drew a window into it, and the Worker died (the
// System Folder's window left empty, then a wasm trap in FMSwapFont).
// The startup disk, its System Folder (zoomed) and two folders down opened: the
// Finder's heap never runs out, every window draws, the Worker lives on.
//
//   node web/tests/tab_finder_heap.mjs DIR SYSTEM
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
const heapFull = [];
page.on('worker', (w) => w.on('console', (m) => {
    const t = m.text();
    if (/unhandled rejection|RuntimeError|Executor aborted/.test(t))
        died = true;
    if (/\[mm\] New(Handle|Ptr)\(\d+\) failed in zone .*\(ApplZone\)/.test(t))
        heapFull.push(t.slice(0, 160));
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
// the Finder's own start (a 1.8 MB NewPtr it tries first) is not this
heapFull.length = 0;

const box = await page.locator('#screen').boundingBox();
const at = (x, y) => [box.x + x * box.width / 640, box.y + y * box.height / 480];
const shown = (title, ms) => until(() => request(page, 'windows')
    .then((ws) => ws.some((w) => w.title === title && w.visible)), ms);
let opened = false;
for (const y of [115, 125]) {
    await page.mouse.dblclick(...at(593, y));
    if ((opened = await shown('Macintosh HD', 8000)))
        break;
}
check(opened, 'the startup disk\'s window');
await page.waitForTimeout(2000);
await page.mouse.dblclick(...at(64, 90));
check(await shown('System Folder', 15000), 'the System Folder\'s window');
await page.waitForTimeout(3000);
// its window's contents drawn: folder icons (blue-grey) in x 14-400, y 66-236
const drawn = async () => {
    const p = await request(page, 'pixels');
    let n = 0;
    for (let y = 66; y < 236; y++)
        for (let x = 14; x < 400; x++) {
            const i = (y * p.width + x) * 4;
            if (p.data[i + 2] > p.data[i] + 40)
                n++;
        }
    return n;
};
check(await drawn() > 1000, 'the System Folder\'s window shows its folders');
// the rest in a try: the tab itself went (Target crashed) before
try {
    // zoomed: 13 rows of icons, its offscreen 650K (the System Folder's window
    // 8-416 x 27-460: Fonts at y 300, MacsBug at y 430)
    await page.mouse.click(...at(396, 35));
    await page.waitForTimeout(4000);
    const lower = async () => {
        const p = await request(page, 'pixels');
        let n = 0;
        for (let y = 280; y < 440; y++)
            for (let x = 14; x < 400; x++) {
                const i = (y * p.width + x) * 4;
                if (p.data[i + 2] > p.data[i] + 40)
                    n++;
            }
        return n;
    };
    await page.locator('#screen').screenshot({ path: path.join(shots, 'tab_finder_heap_zoomed.png') });
    check(await lower() > 1000, 'the zoomed window shows the folders further down');
    // a folder in it and one in that, each window opening over the last
    // (Appearance, then its Desktop Pictures)
    for (const [x, title] of [[78, 'Appearance'], [55, 'Desktop Pictures']]) {
        await page.mouse.dblclick(...at(x, 90));
        check(await shown(title, 15000), `the ${title} window`);
        await page.waitForTimeout(3000);
    }
    await page.locator('#screen').screenshot({ path: path.join(shots, 'tab_finder_heap.png') });
    for (const l of heapFull)
        console.log(`  ${l}`);
    check(heapFull.length === 0, 'the Finder\'s heap never ran out');
    check(!died && await request(page, 'app') === 'Finder', 'the Worker lives on, the Finder in front');
} catch (e) {
    for (const l of heapFull)
        console.log(`  ${l}`);
    check(false, `the tab lives on (${e.message.split('\n')[0]})`);
}
await close().catch(() => {});
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
