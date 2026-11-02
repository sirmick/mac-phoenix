// The Disks panel (web/executor/disks.js): disks in and out of the running
// Mac. On a System picked in the panel from the library (standalone: the
// server keeps the changes), a blank library disk is put in while the
// Finder runs (no restart), a folder made on it in the Finder, the disk
// reverted (out, its changes forgotten, back in: the folder gone), then
// put away in the Finder (Cmd-Y): out of the panel and of the
// disks.mounted setting. A library CD goes in read-only and comes out by
// the panel's eject button.
//
//   node web/tests/tab_disks.mjs DIR SYSTEM [SHOTS]
//
// DIR the wasm build's page, SYSTEM an HFS image with a System Folder and
// Finder (7.x); SHOTS a directory for screenshots. Skips (77) without
// them or hfsutils' hformat.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { launch, firstFrame, request, until } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir, system, shots] = process.argv.slice(2);
if (!system || !fs.existsSync(system)) {
    console.log(`SKIP: no System image (${system})`);
    process.exit(77);
}
const images = fs.mkdtempSync(path.join(os.tmpdir(), 'tab-disks-'));
const hformat = (name, label) => {
    const f = path.join(images, name);
    fs.writeFileSync(f, Buffer.alloc(2 << 20));
    execFileSync('hformat', ['-l', label, f], { stdio: 'ignore', env: { ...process.env, HOME: images } });
};
try {
    hformat('blank.img', 'Blank');
    hformat('disc.iso', 'Disc');
} catch (e) {
    console.log(`SKIP: no hfsutils (${e.code || e.status})`);
    process.exit(77);
}
fs.symlinkSync(path.resolve(system), path.join(images, 'sys.img'));
const record = (name, r) => fs.writeFileSync(path.join(images, `${name}.json`), JSON.stringify(r));
record('sys.img', { title: 'The System', tags: ['system'] });
record('blank.img', { title: 'Blank', tags: [] });
record('disc.iso', { title: 'Disc', tags: ['cd'] });

const server = await startServer(dir, { images, include: ['*.img', '*.iso'], standalone: true });
const browser = await launch();
const context = await browser.newContext({ viewport: { width: 1400, height: 800 } });
const page = await context.newPage();
page.on('pageerror', (e) => console.log('[page error]', e.stack || e.message));
page.on('dialog', (d) => d.accept());           // Revert's confirm
if (process.env.DISKS_LOG)
    page.on('worker', (w) => w.on('console', (m) => console.log('[worker]', m.text())));
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const shot = async (name) => {
    if (shots) {
        fs.mkdirSync(shots, { recursive: true });
        await page.screenshot({ path: path.join(shots, name) });
    }
};
const volumes = () => request(page, 'volumes');
const hasVolume = (name) => volumes().then((v) => Array.isArray(v) && v.includes(name));
const row = (name) => page.locator(`#disks-panel .disk[data-name="${name}"]`);
const picked = () => page.evaluate(() => Settings.get('disks.mounted'));
const keys = (...codes) => page.evaluate((codes) => worker.postMessage({ type: 'input',
    events: [[3, 1, 0x37], ...codes.flatMap((c) => [[3, 1, c], [3, 0, c]]), [3, 0, 0x37]] }), codes);
// The image's bytes as they are now (the overlay over the original).
const contains = (name, text) => page.evaluate(async ([name, text]) => {
    const blob = await executor.request('download', name);
    const bytes = new Uint8Array(await blob.arrayBuffer());
    const want = new TextEncoder().encode(text);
    outer: for (let i = 0; i + want.length <= bytes.length; i++) {
        for (let j = 0; j < want.length; j++)
            if (bytes[i + j] !== want[j])
                continue outer;
        return true;
    }
    return false;
}, [name, text]);

try {
    await page.goto(server.url);
    await firstFrame(page, 90000);
    check(!(await page.locator('#bar select').count()), 'the bar has no System choice');
    await page.click('#disks-button');
    check(await page.locator('#disks-panel').isVisible(), 'the Disks button opens the panel');
    check(await page.locator('#disks-system #system').isVisible(), 'the System choice is in the panel');

    // The library's System disk, picked in the panel: Executor starts on it.
    await page.selectOption('#system', 'disks/sys.img');
    await until(() => page.evaluate(() => executor.frames === 0), 20000, 100);
    await firstFrame(page, 90000);
    check(await until(() => request(page, 'app').then((a) => a === 'Finder'), 90000), 'the Finder is up on the System from the library');
    await page.waitForTimeout(3000);

    // In, live: no restart (the same Worker).
    await page.evaluate(() => { window.before = worker; });
    await page.click('#disks-library .lib-card[data-url="disks/blank.img"]');
    check(await until(() => hasVolume('Blank'), 60000), 'the blank disk is mounted while Executor runs');
    check(await page.evaluate(() => worker === window.before), 'without a restart');
    check(await until(() => row('blank.img').count(), 5000), 'the panel shows it in the Mac');
    check((await picked()).includes('disks/blank.img'), 'the setting has it');
    await shot('1-inserted.png');

    // A folder on it, in the Finder: its window (the icon down the right
    // edge), Cmd-N, Cmd-W.
    await page.waitForTimeout(2000);
    const box = await page.locator('#screen').boundingBox();
    const at = (x, y) => [box.x + x * box.width / 640, box.y + y * box.height / 480];
    // The disk's window, opened from its icon (the Finder shows the icon
    // once it has read the disk's desktop: a few seconds; a disk put in
    // again goes to the next free slot).
    const openBlank = async () => {
        for (const end = Date.now() + 40000; Date.now() < end;)
            for (const y of [207, 259, 311]) {
                await page.mouse.dblclick(...at(600, y));
                if (await until(() => request(page, 'windows').then((ws) => ws.some((w) => w.title === 'Blank')), 3000))
                    return true;
            }
        return false;
    };
    check(await openBlank(), "a double-click opens the disk's window");
    keys(0x2D);                                 // Cmd-N: untitled folder
    await page.waitForTimeout(2000);
    keys(0x0D);                                 // Cmd-W
    check(await until(() => contains('blank.img', 'untitled folder'), 20000), 'the folder is on the disk');
    await page.locator('#disks-panel').evaluate((p) => p.querySelector('.scroll').scrollTo(0, 0));
    check(await until(() => row('blank.img').locator('.badge.changed').count(), 10000), 'the panel says it is changed');
    await shot('2-changed.png');

    // Revert: out, forgotten, back in.
    await row('blank.img').locator('button.revert').click();
    check(await until(async () => !(await contains('blank.img', 'untitled folder').catch(() => true))
                                   && await hasVolume('Blank'), 30000), 'reverted: the folder gone, the disk back in');
    await shot('3-reverted.png');

    // Put away in the Finder: its window opened and closed (the icon stays
    // selected), Cmd-Y.
    check(await openBlank(), "its window again, after Revert");
    keys(0x0D);                                 // Cmd-W
    await page.waitForTimeout(1000);
    keys(0x10);                                 // Cmd-Y
    check(await until(async () => !(await hasVolume('Blank')), 20000), 'put away in the Finder: unmounted');
    check(await until(async () => !(await row('blank.img').count()), 10000), 'and out of the panel');
    check(!(await picked()).includes('disks/blank.img'), 'and of the setting');
    check(!(await page.locator('.lib-card.mounted[data-url="disks/blank.img"]').count()), 'the library offers it again');
    await shot('4-put-away.png');

    // A CD: read-only; out by the panel's eject.
    await page.click('#disks-library .lib-card[data-url="disks/disc.iso"]');
    check(await until(() => hasVolume('Disc'), 60000), 'the CD is mounted');
    check(await until(() => row('disc.iso').locator('.badge.cd').count(), 5000), 'shown as a read-only CD');
    check(await page.evaluate(() => imageState['disc.iso'] && imageState['disc.iso'].writable === false), 'not writable');
    await row('disc.iso').locator('button.eject').click();
    check(await until(async () => !(await hasVolume('Disc')), 20000), "the panel's eject unmounts it");
    check(await until(async () => !(await row('disc.iso').count()), 10000), 'and it is out of the panel');
    check(await request(page, 'app') === 'Finder', 'the Finder still runs');
    await shot('5-cd-ejected.png');
} finally {
    await browser.close();
    server.stop();
    fs.rmSync(images, { recursive: true, force: true });
}
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
