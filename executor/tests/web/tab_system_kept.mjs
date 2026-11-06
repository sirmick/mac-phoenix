// Apple's System on executor/server: sideloaded from its image once, then
// Executor's own volume is the System Folder (web_main.cpp: "the saved
// System Folder"), a preference in it kept. The page hidden while the
// volume is still coming down (its 'save' then) saves nothing of it: a
// save of what had come so far was a volume without most of its files,
// System among them (the next start sideloaded again, over the
// preferences). A volume saved without System's data fork (%System, no
// System: an earlier such save) recovers (worker.js).
//
//   node executor/tests/web/tab_system_kept.mjs DIR SYSTEM_IMAGE
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { launch, newPage, firstFrame, request, until } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir, system] = process.argv.slice(2);
if (!system || !fs.existsSync(system)) {
    console.log(`SKIP: no System image (${system})`);
    process.exit(77);
}
const images = fs.mkdtempSync(path.join(os.tmpdir(), 'tab-system-'));
fs.symlinkSync(path.resolve(system), path.join(images, 'sys.img'));
fs.writeFileSync(path.join(images, 'sys.img.json'), JSON.stringify({ title: 'The System', tags: ['system'] }));
const data = fs.mkdtempSync(path.join(os.tmpdir(), 'tab-system-data-'));
fs.writeFileSync(path.join(data, 'settings.json'), JSON.stringify({ version: 2,
    machine: { system: 'sys.img' }, disks: { include: ['*.img'], mounted: ['disks/sys.img'] } }));
const server = await startServer(dir, { images, data });
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
const PREF = '/executor/System Folder/Preferences/volfast prefs';
const SYS = '/executor/System Folder/System';
// The volume on the server (its manifest file: this test's data).
const manifestFile = () => {
    const dirv = path.join(data, 'volumes');
    const f = fs.existsSync(dirv) && fs.readdirSync(dirv).find((n) => n.startsWith('sys.img-'));
    return f ? path.join(dirv, f) : null;
};
const manifest = () => {
    const f = manifestFile();
    return f ? JSON.parse(fs.readFileSync(f)).manifest : null;
};
const files = () => Object.keys((manifest() || { files: {} }).files || {}).length;
// How the System Folder came this time: 'sideloaded' or 'saved'.
const how = (page) => {
    const seen = { system: null };
    page.on('worker', (w) => w.on('console', (m) => {
        const t = m.text();
        if (/^\[web\] System Folder from/.test(t))
            seen.system = 'sideloaded';
        else if (/^\[web\] the saved System Folder/.test(t))
            seen.system = 'saved';
        else if (/^\[web\] Executor's disk: .*recovered/.test(t))
            seen.recovered = t;
    }));
    return seen;
};
const finder = async (page) => {
    await firstFrame(page, 120000);
    return until(() => request(page, 'app').then((a) => a === 'Finder').catch(() => false), 120000, 250);
};

try {
    const page = await newPage(browser, { log: false });
    let seen = how(page);
    await page.goto(`${server.url}/`);
    check(await finder(page) && seen.system === 'sideloaded', `the first start sideloads the System (${seen.system})`);
    await request(page, 'write', [PREF, 'kept']);
    check(await until(() => manifest() && manifest().files[PREF] && manifest().files[SYS], 20000, 300),
          `the volume on the server, System and the preference in it (${files()} files)`);
    const full = files();

    // A reload, the page hidden (its 'save') while the volume comes down:
    // the bundle held back meanwhile.
    let release;
    const held = new Promise((r) => { release = r; });
    await page.context().route(/\/api\/sync\/volumes\/[^/]+\/bundle$/, async (route) => {
        await held;
        await route.continue();
    });
    seen = how(page);
    await page.reload();
    for (let i = 0; i < 10; i++) {
        await page.evaluate(() => {
            Object.defineProperty(document, 'visibilityState', { value: 'hidden', configurable: true });
            document.dispatchEvent(new Event('visibilitychange'));
        }).catch(() => {});
        await new Promise((r) => setTimeout(r, 300));
    }
    release();
    let partial = 0;
    check(await finder(page) && seen.system === 'saved', `the reload starts on the saved System Folder (${seen.system})`);
    check(files() >= full, `no save of part of the volume (${full} files, ${files()} now)`);
    check(await request(page, 'read', PREF) === 'kept', 'the preference is there');
    await page.context().unroute(/\/api\/sync\/volumes\/[^/]+\/bundle$/);

    // A bundle a quarter long, the page hidden as the rest come one by one.
    seen = how(page);
    await page.context().route(/\/api\/sync\/volumes\/[^/]+\/bundle$/, async (route) => {
        const res = await route.fetch();
        const body = await res.body();
        partial++;
        await route.fulfill({ status: 200, contentType: 'application/octet-stream', body: body.subarray(0, body.length >> 2) });
    });
    await page.context().route(/\/api\/sync\/objects\/[0-9a-f]{64}$/, async (route) => {
        await new Promise((r) => setTimeout(r, 20));
        await page.evaluate(() => {
            Object.defineProperty(document, 'visibilityState', { value: 'hidden', configurable: true });
            document.dispatchEvent(new Event('visibilitychange'));
        }).catch(() => {});
        await route.continue();
    });
    await page.reload();
    check(await finder(page) && seen.system === 'saved' && partial > 0,
          `a bundle cut short, the page hidden meanwhile: the saved System Folder (${seen.system})`);
    check(files() >= full, `still the whole volume on the server (${files()} files)`);
    await page.context().unroute(/\/api\/sync\/volumes\/[^/]+\/bundle$/);
    await page.context().unroute(/\/api\/sync\/objects\/[0-9a-f]{64}$/);

    // A volume saved without System's data fork (as the bug left them):
    // recovered, not sideloaded over.
    const context = page.context();
    await page.close();
    const f = manifestFile();
    const v = JSON.parse(fs.readFileSync(f));
    delete v.manifest.files[SYS];
    fs.writeFileSync(f, JSON.stringify(v));
    const again = await context.newPage();
    seen = how(again);
    await again.goto(`${server.url}/`);
    check(await finder(again) && seen.system === 'saved' && !!seen.recovered,
          `a volume without System: recovered, the saved System Folder (${seen.system}; ${seen.recovered})`);
    check(await request(again, 'read', PREF) === 'kept', 'the preference is still there');
    check(await until(() => manifest().files[SYS], 20000, 300), 'System back in the volume on the server');
} finally {
    await browser.close();
    server.stop();
    fs.rmSync(images, { recursive: true, force: true });
    fs.rmSync(data, { recursive: true, force: true });
}
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
