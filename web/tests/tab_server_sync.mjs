// tools/web/server: signing in, an invited user, and sync. A file on
// Executor's own volume and a change to a library disk's overlay, made in
// one browser, are there in another signed in as the same user (a fresh
// context: OPFS empty); not for the invited user. Both browsers open, both
// changing the disk: the second to upload is refused and asked; taking the
// account's, it has the first's; keeping its own, the other gets it.
// Reset and Revert in one browser reach the other. Executor's own System.
//
//   node web/tests/tab_server_sync.mjs DIR
//
// Skips (77) without hfsutils' hformat (the library disk is a blank HFS one).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { launch, newPage, firstFrame, request, until, openDisks, closeDisks, diskButton } from './harness.mjs';
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
const diskVersion = async (page) => ((await kept(page)).overlays.find((o) => o.name === 'blank.img') || {}).version || 0;
// Dialogs: Reset and Revert are confirmed; a conflict gets answers[page]
// ('mine': OK, 'account': Cancel), and is counted.
const answers = new Map(), asked = new Map();
const dialogs = (page) => page.on('dialog', (d) => {
    if (d.message().includes('another browser')) {
        asked.set(page, (asked.get(page) || 0) + 1);
        return answers.get(page) === 'mine' ? d.accept() : d.dismiss();
    }
    return d.accept();
});
const mark = async (page, text) => {
    const before = await diskVersion(page);
    await request(page, 'write', ['/uploads/blank.img', text]);
    return before;
};
const head = async (page) => {
    const t = await request(page, 'read', '/uploads/blank.img');
    return t && t.slice(0, 9);
};

try {
    const a = await newPage(browser, { log: false, tag: ' A' });
    dialogs(a);
    await a.goto(`${server.url}/`);
    check(a.url().includes('/login'), 'signed out, the tab sends you to sign in');
    await a.goto(`${server.url}/login?next=${encodeURIComponent('/\\evil.example/')}`);
    await a.fill('#name', 'owner');
    await a.fill('#password', OWNER_PASSWORD);
    await Promise.all([a.waitForURL((u) => !u.pathname.startsWith('/login')), a.click('#go')]);
    check(a.url().startsWith(`${server.url}/`), "sign-in's next= stays on the site");
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
    dialogs(b);
    const opened = await diskVersion(a);
    await signIn(b, server.url, 'owner', OWNER_PASSWORD, { query: QUERY });
    check(await ready(b), 'another browser: Executor runs');
    check(await request(b, 'read', '/executor/note.txt') === 'from browser A',
          "the file on Executor's disk is there");
    check(await head(b) === 'SYNCMARK!', "the disk's change is there");
    // Mounting writes the volume's header back as it was: not a change, so
    // nothing goes up (another browser on the disk would be behind).
    await b.waitForTimeout(7000);                       // past SYNC_EVERY
    check(await diskVersion(b) === opened && (await request(b, 'stats')).unsynced === 0,
          'opening the disk changes nothing on the account');

    const c = await newPage(browser, { log: false, tag: ' C' });
    dialogs(c);
    await signIn(c, server.url, 'carol', 'carol-password', { invite: server.invite(), query: QUERY });
    check(await ready(c), 'an invited user: Executor runs');
    check(await request(c, 'read', '/executor/note.txt') === null, "not the owner's file");
    check(await head(c) !== 'SYNCMARK!', "not the owner's disk change");

    // Both open. A changes the disk and uploads; then B, still on the
    // version before: refused, asked, takes the account's (A's).
    let v = await mark(a, 'AAAAMARK!');
    check(await until(async () => await diskVersion(a) > v, 15000, 300), "A's change on the account");
    answers.set(b, 'account');
    await mark(b, 'BBBBMARK!');
    check(await until(() => asked.get(b) === 1, 15000, 300), 'B, behind, is asked');
    check(await ready(b), 'B restarts on the account\'s');
    check(await head(b) === 'AAAAMARK!', "B has A's change, not its own");
    // Now B changes it; A, behind, keeps its own: B gets A's at its next start.
    v = await mark(b, 'CCCCMARK!');
    check(await until(async () => await diskVersion(b) > v, 15000, 300), "B's change on the account");
    answers.set(a, 'mine');
    v = await diskVersion(a);
    await mark(a, 'DDDDMARK!');
    check(await until(() => asked.get(a) === 1, 15000, 300), 'A, behind, is asked');
    check(await until(async () => await diskVersion(a) > v, 15000, 300), "A's copy replaces the account's");
    check(await head(a) === 'DDDDMARK!', 'A kept its own');
    await b.reload();
    check(await ready(b), 'B again');
    check(await head(b) === 'DDDDMARK!', "B has A's");

    await openDisks(b);
    await b.locator('#reset').evaluate((r) => r.click());
    await closeDisks(b);
    check(await ready(b), 'Reset in B');
    await a.reload();
    check(await ready(a), 'A again');
    check(await request(a, 'read', '/executor/note.txt') === null, "Reset in B reaches A's Executor disk");
    check(await head(a) === 'DDDDMARK!', 'the disk change is still there (Reset is the volume only)');

    // Revert in B: the disk as it was, in A too. A's Executor writes the
    // volume's header back at mounting, unchanged: no change of A's
    // (overlay.js), so A, behind, isn't asked (it would keep its own).
    check(await until(() => request(a, 'stats').then((s) => s.unsynced === 0), 15000), "A's writes on the account");
    await diskButton(b, 'blank.img', 'revert');
    check(await ready(b), 'Revert in B');
    check(await head(b) !== 'DDDDMARK!', 'B has the disk as it was');
    await a.reload();
    check(await ready(a), 'A again');
    check(await head(a) !== 'DDDDMARK!', "Revert in B reaches A's disk");
} finally {
    await browser.close();
    server.stop();
    fs.rmSync(images, { recursive: true, force: true });
}
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
