// The claim with tools/standalone (claim.go): the first browser to load
// the tab has the server; its reloads and other tabs keep working, its
// cookie out of the page's reach (HttpOnly). Another browser gets the
// in-use page and nothing else (the API, writes, the relay's WebSocket).
// A restart of the server frees it: the running tab says to reload, and
// the reload claims it again; if another browser was first, that one has
// it and the old tab says so.
//
//   node web/tests/tab_claim.mjs DIR [SHOTS]
//
// DIR: the wasm build's page. SHOTS: a directory for screenshots (the
// in-use page, the tab's reload notice). Skips (77) without go.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { launch, newPage, firstFrame } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir, shots] = process.argv.slice(2);
const scratch = fs.mkdtempSync(path.join(process.env.TMPDIR || os.tmpdir(), 'tab-claim-'));
fs.mkdirSync(path.join(scratch, 'images'));
if (shots)
    fs.mkdirSync(shots, { recursive: true });
const images = path.join(scratch, 'images'), data = path.join(scratch, 'data');
let go = await startServer(dir, { images, data });
if (!go) {
    console.log('SKIP: no go');
    process.exit(77);
}
const { bin } = go;
const port = new URL(go.url).port;
// A restart on the same address (as a user's would be).
async function restart() {
    const exited = new Promise((r) => go.child.once('exit', r));
    go.stop();
    await exited;
    go = await startServer(dir, { images, data, bin, args: ['--port', port] });
}
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const shot = (page, name) => shots && page.screenshot({ path: path.join(shots, name) });
const tabReady = (page) => page.waitForFunction(() => typeof Settings !== 'undefined' && Settings.where === 'server', null, { timeout: 30000 })
    .then(() => true, () => false);
const inUse = (page) => page.locator('#in-use').isVisible();
const status = (page, url, method = 'GET') => page.evaluate(([u, m]) => fetch(u, {
    method: m, cache: 'no-store', headers: { 'Content-Type': 'application/json' }, body: m === 'GET' ? undefined : '{}',
}).then((r) => r.status), [url, method]);
const relayOpens = (page) => page.evaluate(() => new Promise((resolve) => {
    const ws = new WebSocket(new URL('relay', location.href).href.replace(/^http/, 'ws'));
    ws.onopen = () => { ws.close(); resolve(true); };
    ws.onerror = ws.onclose = () => resolve(false);
}));
const reloadNotice = (page, text) => page.waitForFunction((t) => {
    const s = document.getElementById('status');
    return s.classList.contains('lost') && s.textContent.includes(t) && document.getElementById('reload');
}, text, { timeout: 20000 }).then(() => true, () => false);

const browser = await launch();
try {
    // ---- the first browser claims it ----
    const a = await newPage(browser, { log: false, tag: ' A' });
    const res = await a.goto(`${go.url}/?size=640x480`);
    check(res.status() === 200 && await tabReady(a), `the first load: the tab (${res.status()})`);
    const cookies = await a.context().cookies();
    const claim = cookies.find((c) => c.name === `executor_claim_${port}`);
    check(claim && claim.httpOnly && claim.sameSite === 'Strict', `... its cookie, HttpOnly, SameSite=Strict (${cookies.map((c) => c.name)})`);
    check(!(await a.evaluate(() => document.cookie)).includes('executor_claim'), "... out of the page's reach");
    check(await status(a, '/api/me') === 200 && await relayOpens(a), '... the API and the relay open to it');
    await firstFrame(a, 60000).catch(() => {});
    await a.reload();
    check(await tabReady(a), 'a reload: still the tab');
    const a2 = await a.context().newPage();
    await a2.goto(`${go.url}/`);
    check(await tabReady(a2), 'another tab of the same browser: the tab');
    await a2.close();

    // ---- another browser: the in-use page, nothing else ----
    const b = await newPage(browser, { log: false, tag: ' B' });
    const rb = await b.goto(`${go.url}/?size=640x480`);
    check(rb.status() === 403 && await inUse(b), `another browser: the in-use page (${rb.status()})`);
    await shot(b, 'claim-in-use.png');
    for (const u of ['/index.html', '/main.js', '/api/me', '/api/settings', '/library.json', '/api/logs', '/api/sync'])
        check(await status(b, u) === 403, `... ${u} refused`);
    check(await status(b, '/api/settings', 'POST') === 403, '... a write refused');
    check(!(await relayOpens(b)), "... the relay's WebSocket refused");
    await b.reload();
    check(await inUse(b), '... reloaded: still in use');

    // ---- a restart frees it: the tab says to reload; that claims it ----
    await restart();
    check(await reloadNotice(a, 'reload this page'), 'restarted: the running tab says to reload');
    await shot(a, 'claim-reload.png');
    await Promise.all([a.waitForNavigation(), a.click('#reload')]);
    check(await tabReady(a), '... Reload: the tab again, claimed');
    check(await status(a, '/api/me') === 200, '... the API open to it');
    await b.reload();
    check(await inUse(b), '... B still refused');

    // ---- restarted, B first: B has it; A's tab says so ----
    await firstFrame(a, 60000).catch(() => {});
    await restart();
    await b.goto(`${go.url}/?size=640x480`);
    check(await tabReady(b), 'restarted, B loads first: B has it');
    check(await reloadNotice(a, 'another browser has it'), "... A's tab says another browser has it");
    await shot(a, 'claim-taken.png');
    await a.reload();
    check(await inUse(a), '... A reloaded: the in-use page');
} catch (e) {
    console.log(`FAIL ${e.stack || e}`);
    failed++;
} finally {
    await browser.close();
    go.stop();
    fs.rmSync(scratch, { recursive: true, force: true });
}
console.log(failed ? `${failed} FAILED` : 'all passed');
process.exit(failed ? 1 : 0);
