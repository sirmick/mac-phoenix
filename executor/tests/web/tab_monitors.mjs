// The Monitors control panels left out of a System that has them
// (executor/core/res/extension-policy.txt's "-NAME" entries;
// docs/executor/DisplayModes.md, "Left out"): a full 7.6.1 System as the
// server's library disk, Executor's volume (kept on the server) has them in
// the "(Disabled)" folders and the sound ones where they were; after a
// reload, on the saved System Folder, still.
//
//   node executor/tests/web/tab_monitors.mjs DIR SYSTEM
//
// SYSTEM: a 7.6.1 image with Monitors, Monitors & Sound and the Monitor
// Control Strip modules (tests-7.6.1.img; only read). Skips (77) without it.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { launch, newPage, firstFrame, request, until } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir, system] = process.argv.slice(2);
if (!system || !fs.existsSync(system)) {
    console.log(`SKIP: no System image (${system || 'none given'})`);
    process.exit(77);
}
const tmp = process.env.TMPDIR || os.tmpdir();
const images = fs.mkdtempSync(path.join(tmp, 'tab-monitors-'));
fs.symlinkSync(path.resolve(system), path.join(images, 'sys.img'));
fs.writeFileSync(path.join(images, 'sys.img.json'), JSON.stringify({ title: 'The System', tags: ['system'] }));
const data = fs.mkdtempSync(path.join(tmp, 'tab-monitors-data-'));
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
const finder = async (page) => {
    await firstFrame(page, 120000);
    return until(() => request(page, 'app').then((a) => a === 'Finder').catch(() => false), 180000, 250);
};
// How the System Folder came: 'sideloaded' or 'saved' (main.js's lines).
const how = (page) => {
    const seen = { system: null };
    page.on('worker', (w) => w.on('console', (m) => {
        if (/^\[web\] System Folder from/.test(m.text()))
            seen.system = 'sideloaded';
        else if (/^\[web\] the saved System Folder/.test(m.text()))
            seen.system = 'saved';
    }));
    return seen;
};
const audit = async (page, when) => {
    const list = async (f) => (await request(page, 'list', `/executor/System Folder/${f}`)) || [];
    const [cp, cpOff, cs, csOff] = await Promise.all(['Control Panels', 'Control Panels (Disabled)',
        'Control Strip Modules', 'Control Strip (Disabled)'].map(list));
    for (const n of ['Monitors', 'Monitors & Sound'])
        check(!cp.includes(n) && cpOff.includes(n), `${when}: ${n} in Control Panels (Disabled)`);
    for (const n of ['Monitor Resolution', 'Monitor BitDepth'])
        check(!cs.includes(n) && csOff.includes(n), `${when}: ${n} in Control Strip (Disabled)`);
    check(cp.includes('Sound') && cs.includes('Sound Volume'),
          `${when}: Sound and Sound Volume kept (${cp.filter((n) => /Sound/.test(n))}; ${cs.filter((n) => /Sound/.test(n))})`);
};

try {
    const page = await newPage(browser, { log: !!process.env.TAB_LOG });
    let seen = how(page);
    await page.goto(`${server.url}/`);
    check(await finder(page) && seen.system === 'sideloaded', `the first start sideloads the System (${seen.system})`);
    await audit(page, 'first start');
    await page.waitForTimeout(3000);            // (the volume's save)
    seen = how(page);
    await page.reload();
    check(await finder(page) && seen.system === 'saved', `the reload starts on the saved System Folder (${seen.system})`);
    await audit(page, 'the saved volume');
} catch (e) {
    check(false, `${e.message}`);
} finally {
    await browser.close();
    server.stop();
}
process.exit(failed ? 1 : 0);
