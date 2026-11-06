// tools/web/server in standalone mode: the server is the tab's only store.
// A change to a library disk and a file on Executor's own volume survive a
// reload (the disk's at once: Executor's close is a flush point, and the
// pagehide beacon); nothing is in OPFS. A second tab is view-only: it
// shows the first's screen, runs no Executor, until it takes control; the
// first then watches. Executor's own System.
//
//   node executor/tests/web/tab_standalone.mjs DIR [SHOTS]
//
// SHOTS: a directory for screenshots (default: none). Skips (77) without
// hfsutils' hformat (the library disk is a blank HFS one).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { launch, newPage, firstFrame, request, until } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir, shots] = process.argv.slice(2);
const images = fs.mkdtempSync(path.join(os.tmpdir(), 'tab-standalone-'));
const disk = path.join(images, 'blank.img');
fs.writeFileSync(disk, Buffer.alloc(1440 * 1024));
try {
    execFileSync('hformat', ['-l', 'Blank', disk], { stdio: 'ignore', env: { ...process.env, HOME: images } });
} catch (e) {
    console.log(`SKIP: no hfsutils (${e.code || e.status})`);
    process.exit(77);
}
fs.writeFileSync(`${disk}.json`, JSON.stringify({ title: 'Blank', note: 'a blank disk', tags: [] }));
const original = fs.readFileSync(disk);

const server = await startServer(dir, { images });
if (!server) {
    console.log('SKIP: no go');
    process.exit(77);
}
const browser = await launch();
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const shot = async (page, name) => {
    if (shots) {
        fs.mkdirSync(shots, { recursive: true });
        await page.screenshot({ path: path.join(shots, name) });
    }
};
const URL_ = `${server.url}/?disk=disks/blank.img`;
const ready = async (page) => {
    await firstFrame(page, 60000);
    return until(() => request(page, 'app').then((a) => a === 'Browser'), 30000);
};
const kept = (page) => page.evaluate(() => fetch('api/sync').then((r) => r.json()));
const head = async (page) => {
    const t = await request(page, 'read', '/uploads/blank.img');
    return t && t.slice(0, 10);
};
const opfsEntries = (page) => page.evaluate(async () => {
    const names = [];
    for await (const [name] of (await navigator.storage.getDirectory()).entries())
        names.push(name);
    return names;
});
const watching = (page) => page.locator('#viewer').isVisible();

try {
    const a = await newPage(browser, { log: false, tag: ' A' });
    await a.goto(URL_);
    check(a.url() === URL_, 'no sign-in');
    check((await a.evaluate(() => fetch('api/me').then((r) => r.json()))).mode === 'standalone',
          'the server says standalone');
    check(await ready(a), 'Executor runs');
    check(!(await watching(a)), 'the first tab is in control');

    await request(a, 'write', ['/executor/note.txt', 'kept on the server']);
    check(await until(async () => (await kept(a)).volumes.some((v) => v.key === 'own' && v.files > 0), 10000, 300),
          "Executor's volume reaches the server within seconds");
    await request(a, 'write', ['/uploads/blank.img', 'STOREMARK!']);    // block 0 of the disk
    await a.reload();                                                   // at once
    check(await ready(a), 'Executor after a reload');
    check(await head(a) === 'STOREMARK!', "the disk's change survived the reload");
    check(await request(a, 'read', '/executor/note.txt') === 'kept on the server',
          "the file on Executor's disk survived the reload");
    check((await opfsEntries(a)).length === 0, 'nothing in OPFS');
    check(fs.readFileSync(disk).equals(original), 'the image itself is untouched');
    const stats = await request(a, 'stats');
    check(stats.remoteRequests > 0, `the disk read by Range (${stats.remoteRequests} requests)`);
    await shot(a, 'standalone-control.png');

    // A second tab: watching, no Executor of its own.
    const b = await a.context().newPage();
    await b.goto(URL_);
    check(await until(() => watching(b), 10000), 'a second tab is view-only');
    check(await until(() => b.evaluate(() => {
        const img = document.querySelector('#screen-box img');
        return img && img.naturalWidth > 0;
    }), 15000, 500), "it shows the first tab's screen");
    check(await b.evaluate(() => window.executor.frames) === 0, 'it runs no Executor');
    await shot(b, 'standalone-viewer.png');

    // B takes control: A saves, stops and watches; B has A's changes.
    await request(a, 'write', ['/uploads/blank.img', 'HANDOVER!!']);
    await b.click('#take');
    check(await ready(b), 'the second tab, having taken control, runs Executor');
    check(await until(() => watching(a), 10000), 'the first tab watches now');
    check(await head(b) === 'HANDOVER!!', "the first tab's last change was handed over");
    await shot(a, 'standalone-handover.png');

    // B closed: A gets control back (B let go at pagehide).
    await b.close({ runBeforeUnload: true });
    check(await until(async () => !(await watching(a)) && await ready(a), 30000, 500),
          'the second tab closed: the first is in control again');
    check(await head(a) === 'HANDOVER!!', 'with the change');
} finally {
    await browser.close();
    server.stop();
    fs.rmSync(images, { recursive: true, force: true });
}
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
