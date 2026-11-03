// The site from a page that isn't a secure context (plain http to a host
// other than this computer: tools/standalone opened by its LAN address),
// where the browser gives no OPFS, Cache Storage or Web Locks. The site as
// tools/web/make_site.sh assembles it (every file the Worker imports),
// served by tools/standalone; Chromium reaches it as http://executor.test
// (a host-resolver rule), at devicePixelRatio 1.5. The server is the
// store, so that changes nothing but sound; a System image from the
// library runs the Finder, read by Range as needed (overlay.js:
// RemoteImage); a 50 MB library disk added from the Library menu mounts
// the same way, writable (its changes kept on the server), and a
// double-click on its icon opens it: the click lands on the guest pixel it
// was over at a fractional ratio (main.js: guestPoint, the whole-number
// scale).
//
//   node web/tests/tab_insecure.mjs BUILD_WASM IMAGE
//
// IMAGE: an HFS image with a System Folder, a Finder and its record
// (IMAGE.json; tests-7.5.5.img). Skips (77) without them, or hformat.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { launch, newPage, firstFrame, request, until, openDisks, closeDisks, diskState } from './harness.mjs';
import { startServer } from './server.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const [build, image] = process.argv.slice(2);
if (!image || !fs.existsSync(image) || !fs.existsSync(`${image}.json`)) {
    console.log(`SKIP: no System image with a record (${image || 'none given'})`);
    process.exit(77);
}
try {
    execFileSync('hformat', ['--version'], { stdio: 'ignore' });
} catch (e) {
    if (e.code === 'ENOENT') {
        console.log('SKIP: no hformat (hfsutils)');
        process.exit(77);
    }
}
const DPR = 1.5;
const LABEL = 'Library Disk';
const system = path.basename(image);

// The site, and a library of the System image and a fresh 50 MB disk.
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'executor-insecure-'));
const site = path.join(scratch, 'site'), lib = path.join(scratch, 'library');
execFileSync(path.join(ROOT, 'tools/web/make_site.sh'), [site, build], { stdio: 'inherit' });
fs.mkdirSync(lib);
fs.symlinkSync(path.resolve(image), path.join(lib, system));
fs.symlinkSync(path.resolve(`${image}.json`), path.join(lib, `${system}.json`));
const disk = path.join(lib, 'library-disk.img');
fs.writeFileSync(disk, Buffer.alloc(50 << 20));
const hfs = (...args) => execFileSync(args[0], args.slice(1), { env: { ...process.env, HOME: scratch } });
hfs('hformat', '-l', LABEL, disk);
fs.writeFileSync(path.join(scratch, 'Read Me'), 'A library disk, read over plain http.\r');
hfs('hcopy', '-t', path.join(scratch, 'Read Me'), ':');
hfs('humount');
fs.writeFileSync(`${disk}.json`, JSON.stringify({ title: 'Library disk (50 MB)', note: 'tab_insecure' }));

const server = await startServer(site, { images: lib, include: ['*.img'] });
if (!server) {
    console.log('SKIP: no go');
    process.exit(77);
}
const port = new URL(server.url).port;
const origin = `http://executor.test:${port}`;
const browser = await launch({ args: ['--host-resolver-rules=MAP executor.test 127.0.0.1'] });
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const out = process.env.TAB_INSECURE_SHOTS || build;
const titles = (page) => request(page, 'windows').then((ws) => ws.filter((w) => w.visible).map((w) => w.title));

try {
    const page = await newPage(browser, { log: !!process.env.TAB_LOG, scale: DPR });
    await page.goto(`${origin}/?disk=disks/${encodeURIComponent(system)}&system=${encodeURIComponent(system)}`);
    const env = await page.evaluate(() => ({ secure: window.isSecureContext, dpr: devicePixelRatio }));
    check(!env.secure && env.dpr === DPR, `not a secure context, devicePixelRatio ${env.dpr}`);
    await firstFrame(page, 60000);
    check(await until(() => request(page, 'app').then((a) => a === 'Finder'), 90000), `the Finder on ${system}'s System`);
    const first = await request(page, 'stats');
    console.log(`  ${system}: read ${(first.remoteBytes / 1e6).toFixed(1)} MB in ${first.remoteRequests} requests`);
    check(first.remoteBytes > 0, `${system} read as needed`);

    // The 50 MB disk from the Disks panel's library: into the running Mac.
    await openDisks(page);
    await page.click('#disks-library .lib-row[data-url="disks/library-disk.img"]');
    await closeDisks(page);
    check(await until(() => request(page, 'volumes').then((v) => Array.isArray(v) && v.includes(LABEL)), 90000),
          'the library disk is mounted, Executor running');
    check(await until(() => request(page, 'app').then((a) => a === 'Finder'), 90000), 'the Finder, with the library disk');
    await until(() => titles(page).then((t) => t.includes('Desktop')), 15000);
    await page.waitForTimeout(3000);
    const st = await diskState(page, 'library-disk.img');
    console.log(`  library-disk.img: ${st}`);
    check(await page.evaluate(() => imageState['library-disk.img'] && imageState['library-disk.img'].writable
                                    && imageState['library-disk.img'].onServer),
          'the library disk is writable, its changes kept on the server');

    // Its icon: the Finder's disk icons are down the right edge, in mount
    // order; the guest point to page pixels at the canvas's CSS size.
    const box = await page.locator('#screen').boundingBox();
    const at = (x, y) => [box.x + x * box.width / 640, box.y + y * box.height / 480];
    let opened = null;
    const tried = [];
    for (const y of [207, 259, 155, 103]) {
        await page.mouse.click(...at(10, 470));          // the desktop: nothing selected
        await page.mouse.dblclick(...at(600, y));
        const t = await until(() => titles(page).then((ts) => ts.find((w) => w && !['Desktop'].includes(w)
                                                                         && !tried.includes(w))), 6000);
        tried.push(t || `(nothing at ${y})`);
        if (t === LABEL) {
            opened = t;
            break;
        }
    }
    console.log(`  double-clicks opened: ${tried.join(', ')}`);
    check(opened === LABEL, `a double-click on its icon opens "${LABEL}"`);

    // A change to it: Read Me (the window's first icon, at the
    // window's top left) duplicated.
    const win = (await request(page, 'windows')).find((w) => w.title === LABEL);
    console.log(`  its window: ${JSON.stringify(win)}`);
    if (win && win.rect) {
        const [top, left] = win.rect;
        await page.mouse.click(...at(left + 32, top + 60));
        await page.keyboard.press('Meta+KeyD');
    }
    check(await until(() => page.evaluate(() => imageState['library-disk.img'] && imageState['library-disk.img'].modified), 15000),
          'a duplicated file marks the library disk changed');
    await page.waitForTimeout(1500);
    await page.screenshot({ path: path.join(out, 'tab_insecure.png') });
    const now = await request(page, 'stats');
    console.log(`  read ${(now.remoteBytes / 1e6).toFixed(1)} MB of the disks in all, ${now.remoteRequests} requests`);
    check(now.remoteBytes < (50 << 20) / 2 + fs.statSync(image).size / 2, 'the disks read as needed, not whole');
} finally {
    await browser.close();
    server.stop();
    fs.rmSync(scratch, { recursive: true, force: true });
}
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
