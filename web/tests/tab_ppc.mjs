// W4's check (docs/executor/DIRECTION.md): PowerPC in the tab, on Apple's
// Mac OS 8.6 System (HFS+) with the page's PowerPC box (--executor-ppc).
//
//   node web/tests/tab_ppc.mjs DIR GUEST_DIR SYSTEM [IMAGE APP_PATH]...
//
// SYSTEM: an HFS+ image with a blessed 8.x System Folder (8.6.img; only
// read). Uploads it with PPCHello, ticks PowerPC, chooses the image as the
// System, and times: the Finder (its name, then the screen quiet), the
// startup disk's window on a double-click, PPCHello's report (its PowerPC
// code on mame-ppc's C back-end), and each APP_PATH ("Volume:Folder:App",
// on its IMAGE) until its screen is quiet. Prints the times as a JSON line
// ("W4 {...}"); fails if the Finder, the window or PPCHello's PASS lines
// don't come. Skips (77) without SYSTEM.
import fs from 'node:fs';
import path from 'node:path';
import { open, firstFrame, request, until } from './harness.mjs';

const [dir, guest, system, ...rest] = process.argv.slice(2);
const apps = [];
for (let i = 0; i + 1 < rest.length; i += 2)
    apps.push({ image: rest[i], path: rest[i + 1] });
if (!system || !fs.existsSync(system)) {
    console.log(`SKIP: no System image (${system || 'none given'})`);
    process.exit(77);
}
const { page, close } = await open(dir, { log: !!process.env.TAB_LOG });
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const times = {};
const secs = (t0) => +((Date.now() - t0) / 1000).toFixed(2);
const frames = () => page.evaluate(() => window.executor.frames);

// Until no frame has come for `quiet` ms (Executor sends one only when
// the screen changed), after at least one since `since` frames.
async function quietAfter(since, ms, quiet = 1000) {
    const end = Date.now() + ms;
    let n = await frames(), changed = Date.now();
    while (Date.now() < end) {
        await page.waitForTimeout(50);
        const m = await frames();
        if (m !== n) {
            n = m;
            changed = Date.now();
        } else if (n > since && Date.now() - changed >= quiet)
            return changed;
    }
    return null;
}

await firstFrame(page);
await page.check('#ppc');
await firstFrame(page);
const files = [system, path.join(guest, 'ppc-hello/PPCHello.bin')];
files.push(...new Set(apps.map((a) => a.image)));
let t0 = Date.now();
await page.setInputFiles('#add', files);
await firstFrame(page, 120000);
times.upload_s = secs(t0);       // stored in OPFS, restarted

// TAB_TRACE=FILE: a Chromium trace (the Worker's CPU samples) from the
// System choice to the desktop quiet.
if (process.env.TAB_TRACE)
    await page.context().browser().startTracing(page, {
        path: process.env.TAB_TRACE,
        categories: ['devtools.timeline', 'v8', 'disabled-by-default-v8.cpu_profiler'] });
t0 = Date.now();
await page.selectOption('#system', path.basename(system));
await firstFrame(page, 120000);
times.first_frame_s = secs(t0);
check(await until(() => request(page, 'app').then((a) => a === 'Finder'), 120000), 'the Finder is up');
times.finder_s = secs(t0);
const desk = await quietAfter(0, 120000);
times.desktop_quiet_s = desk ? +((desk - t0) / 1000).toFixed(2) : null;
if (process.env.TAB_TRACE)
    await page.context().browser().stopTracing();
check(await until(() => request(page, 'windows').then((ws) => ws.some((w) => w.title === 'Desktop')), 15000),
      'its desktop window');
await page.locator('#screen').screenshot({ path: path.join(dir, 'tab_ppc_desktop.png') });

// The startup disk's window: the icons down the right edge, found by trying
// the slots; timed from the double-click to the window in the list and to
// the screen quiet (the Finder fills it from a thread).
const box = await page.locator('#screen').boundingBox();
const at = (x, y) => [box.x + x * box.width / 640, box.y + y * box.height / 480];
let opened = null;
for (const y of [43, 103, 155, 207]) {
    await page.mouse.click(...at(593, y));          // the Finder to the front
    await page.waitForTimeout(800);
    const n = await frames();
    t0 = Date.now();
    await page.mouse.dblclick(...at(593, y));
    opened = await until(() => request(page, 'windows')
        .then((ws) => ws.find((w) => w.title && !['Desktop', 'executor', 'Host'].includes(w.title) && w.visible)), 8000, 50);
    if (opened) {
        times.window_listed_s = secs(t0);
        const q = await quietAfter(n, 60000);
        times.window_quiet_s = q ? +((q - t0) / 1000).toFixed(2) : null;
        break;
    }
}
console.log('opened:', JSON.stringify(opened));
check(opened, "a double-click opens a disk's window");
await page.locator('#screen').screenshot({ path: path.join(dir, 'tab_ppc_window.png') });

// PPCHello: PowerPC code, its report on the Host volume (TAB_NO_HELLO: not).
if (!process.env.TAB_NO_HELLO) {
t0 = Date.now();
check(await request(page, 'launch', 'executor:Uploads:PPCHello.bin') === 0, 'launch PPCHello');
const ppc = await until(() => request(page, 'read', '/host/ppc_hello.txt')
                                  .then((t) => t && t.includes('---') && t), 120000, 50);
times.ppchello_s = secs(t0);
console.log(JSON.stringify(ppc));
check(ppc && !/FAIL/.test(ppc) && (ppc.match(/PASS/g) || []).length >= 7, 'PPCHello reports all PASS');
}

// PowerPC applications, each until its screen is quiet.
for (const [i, app] of apps.entries()) {
    await page.waitForTimeout(2000);
    const n = await frames();
    t0 = Date.now();
    const r = await request(page, 'launch', app.path);
    const name = app.path.split(':').pop();
    const front = await until(() => request(page, 'app').then((a) => a === name), 120000, 50);
    const q = await quietAfter(n, 120000, 2000);
    times[name] = { front_s: front ? secs(t0) : null, quiet_s: q ? +((q - t0) / 1000).toFixed(2) : null };
    console.log(`${name}: launch ${r}, front ${front}, windows ${JSON.stringify(await request(page, 'windows'))}`);
    await page.locator('#screen').screenshot({ path: path.join(dir, `tab_ppc_app${i}.png`) });
}

console.log('W4 ' + JSON.stringify(times));
await close();
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
