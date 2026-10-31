// tools/web/server: signing in, an invited user, and sync. A file on
// Executor's own volume and a change to a library disk's overlay, made in
// one browser, are there in another signed in as the same user (a fresh
// context: OPFS empty); not for the invited user; Reset in one browser
// empties the volume in the other too. Executor's own System.
//
//   node web/tests/tab_server_sync.mjs DIR
//
// Skips (77) without hfsutils' hformat (the library disk is a blank HFS one).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { launch, newPage, firstFrame, request, until } from './harness.mjs';
import { startServer, signIn, OWNER_PASSWORD } from './server.mjs';

const [dir] = process.argv.slice(2);
const images = fs.mkdtempSync(path.join(os.tmpdir(), 'tab-sync-'));
const disk = path.join(images, 'blank.img');
fs.writeFileSync(disk, Buffer.alloc(1440 * 1024));
try {
    execFileSync('hformat', ['-l', 'Blank', disk], { stdio: 'ignore', env: { ...process.env, HOME: images } });
} catch (e) {
    console.log(`SKIP: no hfsutils (${e.code || e.status})`);
    process.exit(77);
}
fs.writeFileSync(`${disk}.json`, JSON.stringify({ title: 'Blank', note: 'a blank disk', tags: [] }));

const server = await startServer(dir, { images });
const browser = await launch();
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const QUERY = '?disk=disks/blank.img';
const ready = async (page) => {
    await firstFrame(page);
    return until(() => request(page, 'app').then((a) => a === 'Browser'), 30000);
};
const kept = (page) => page.evaluate(() => fetch('api/sync').then((r) => r.json()));
const head = async (page) => {
    const t = await request(page, 'read', '/uploads/blank.img');
    return t && t.slice(0, 9);
};

try {
    const a = await newPage(browser, { log: false, tag: ' A' });
    await a.goto(`${server.url}/`);
    check(a.url().includes('/login'), 'signed out, the tab sends you to sign in');
    await signIn(a, server.url, 'owner', OWNER_PASSWORD, { query: QUERY });
    check(await ready(a), 'signed in: Executor runs');
    check(await a.locator('#account').textContent() === 'owner', 'the bar says who');
    await request(a, 'write', ['/executor/note.txt', 'from browser A']);
    await request(a, 'write', ['/uploads/blank.img', 'SYNCMARK!']);    // block 0 of the disk
    const up = await until(async () => {
        const k = await kept(a);
        return k.overlays.some((o) => o.name === 'blank.img' && o.blocks > 0)
            && k.volumes.some((v) => v.key === 'own' && v.files > 0) && k;
    }, 20000, 500);
    check(!!up, 'the volume and the overlay reach the account');

    const b = await newPage(browser, { log: false, tag: ' B' });
    await signIn(b, server.url, 'owner', OWNER_PASSWORD, { query: QUERY });
    check(await ready(b), 'another browser: Executor runs');
    check(await request(b, 'read', '/executor/note.txt') === 'from browser A',
          "the file on Executor's disk is there");
    check(await head(b) === 'SYNCMARK!', "the disk's change is there");

    const c = await newPage(browser, { log: false, tag: ' C' });
    await signIn(c, server.url, 'carol', 'carol-password', { invite: server.invite(), query: QUERY });
    check(await ready(c), 'an invited user: Executor runs');
    check(await request(c, 'read', '/executor/note.txt') === null, "not the owner's file");
    check(await head(c) !== 'SYNCMARK!', "not the owner's disk change");

    b.on('dialog', (d) => d.accept());
    await b.click('#reset');
    check(await ready(b), 'Reset in B');
    await a.reload();
    check(await ready(a), 'A again');
    check(await request(a, 'read', '/executor/note.txt') === null, "Reset in B reaches A's Executor disk");
    check(await head(a) === 'SYNCMARK!', 'the disk change is still there (Reset is the volume only)');
} finally {
    await browser.close();
    server.stop();
    fs.rmSync(images, { recursive: true, force: true });
}
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
