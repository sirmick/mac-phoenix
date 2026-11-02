// Pairing with tools/standalone (pair.go): a browser that hasn't shown
// the key gets the enter-the-key page, wherever it goes, and nothing
// else (the API, the relay's WebSocket); a wrong key is refused; the key
// typed, or the printed address with #key=, pairs it and the key leaves
// the address (and the tab's log). Settings > Network lists the paired
// browsers; Revoke unpairs one; --rotate-key unpairs them all.
//
//   node web/tests/tab_pair.mjs DIR [SHOTS]
//
// DIR: the wasm build's page. SHOTS: a directory for screenshots (the
// enter-the-key page, the Network tab's list). Skips (77) without go.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { launch, newPage, firstFrame } from './harness.mjs';
import { startGoServer } from './server.mjs';

const [dir, shots] = process.argv.slice(2);
const scratch = fs.mkdtempSync(path.join(process.env.TMPDIR || os.tmpdir(), 'tab-pair-'));
fs.mkdirSync(path.join(scratch, 'images'));
if (shots)
    fs.mkdirSync(shots, { recursive: true });
let go = await startGoServer(dir, { images: path.join(scratch, 'images'), data: path.join(scratch, 'data') });
if (!go) {
    console.log('SKIP: no go');
    process.exit(77);
}
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const shot = (page, name) => shots && page.screenshot({ path: path.join(shots, name) });
const tabReady = (page) => page.waitForFunction(() => typeof Settings !== 'undefined' && Settings.where === 'server', null, { timeout: 30000 })
    .then(() => true, () => false);
const isPairPage = (page) => page.locator('#key').isVisible();
const status = (page, url) => page.evaluate((u) => fetch(u, { cache: 'no-store' }).then((r) => r.status), url);
const relayOpens = (page) => page.evaluate(() => new Promise((resolve) => {
    const ws = new WebSocket(new URL('relay', location.href).href.replace(/^http/, 'ws'));
    ws.onopen = () => { ws.close(); resolve(true); };
    ws.onerror = ws.onclose = () => resolve(false);
}));
const grep = (d, text) => fs.readdirSync(d, { recursive: true }).some((f) => {
    const p = path.join(d, f);
    return fs.statSync(p).isFile() && fs.readFileSync(p, 'latin1').includes(text);
});

const browser = await launch();
try {
    // ---- unpaired: the enter-the-key page, nothing else ----
    const a = await newPage(browser, { log: false, tag: ' A' });
    const res = await a.goto(`${go.url}/?size=800x600`);
    check(res.status() === 401 && await isPairPage(a), `unpaired: the enter-the-key page instead of the tab (${res.status()})`);
    for (const u of ['/index.html', '/main.js', '/api/me', '/api/settings', '/library.json', '/api/logs', '/api/paired'])
        check(await status(a, u) === 401, `unpaired: ${u} refused`);
    check(!(await relayOpens(a)), "unpaired: the relay's WebSocket refused");
    await shot(a, 'pair-page.png');
    await a.fill('#key', '0123-4567-89ab-cdef-0123-4567-89ab-cdef');
    await a.click('#go');
    await a.waitForFunction(() => document.getElementById('err').textContent);
    check((await a.textContent('#err')).includes('not the key'), `a wrong key: "${await a.textContent('#err')}"`);
    await shot(a, 'pair-wrong-key.png');

    // ---- the key typed (in fours, as printed) ----
    await a.fill('#key', go.key.toUpperCase().replace(/(.{4})(?!$)/g, '$1-'));
    await Promise.all([a.waitForNavigation(), a.click('#go')]);
    check(await tabReady(a), 'the key typed: paired, the tab (its settings from the server)');
    check(new URL(a.url()).search === '?size=800x600', `... at the address asked for (${a.url()})`);
    check(await status(a, '/api/me') === 200 && await relayOpens(a), '... the API and the relay open to it');

    // ---- the printed address: #key= pairs, then leaves the address ----
    const b = await newPage(browser, { log: false, tag: ' B' });
    await b.goto(go.pairURL);
    check(await tabReady(b), 'the printed address (#key=): paired, the tab');
    check(!b.url().includes(go.key) && !(await b.evaluate(() => location.hash)), `... the key out of the address (${b.url()})`);
    await b.waitForTimeout(2500);               // a log batch a second (log.js)
    const logs = path.join(go.data, 'logs');
    check(fs.existsSync(logs) && !grep(logs, go.key), "... and not in the tab's log on the server");
    check(!grep(go.data, `#key=${go.key}`), '... nor its address anywhere in the data directory');

    // ---- Settings > Network: the paired browsers, Revoke ----
    await a.goto(`${go.url}/`);
    await tabReady(a);
    await firstFrame(a, 60000).catch(() => {});
    await a.click('#settings-button');
    await a.click('nav button[data-category=network]');
    const rows = () => a.$$eval('.settings-paired li', (l) => l.map((x) => x.textContent));
    let list = await rows();
    check(list.length === 2 && list.filter((t) => t.includes('(this browser)')).length === 1
          && list.every((t) => t.includes('Headless Chrome on Linux') && t.includes('127.0.0.1')),
          `Settings > Network lists both, this one marked (${list.join(' | ')})`);
    await a.locator('[data-path="network.paired"]').scrollIntoViewIfNeeded();
    await shot(a, 'settings-paired.png');
    await a.locator('.settings-paired li', { hasNotText: '(this browser)' }).getByRole('button', { name: 'Revoke' }).click();
    await a.waitForFunction(() => document.querySelectorAll('.settings-paired li').length === 1);
    list = await rows();
    check(list.length === 1 && list[0].includes('(this browser)'), `Revoke: B gone from the list (${list.join(' | ')})`);
    check(await status(b, '/api/me') === 401 && !(await relayOpens(b)), 'B, revoked: refused');
    await b.reload();
    check(await isPairPage(b), 'B, reloaded: the enter-the-key page');
    await shot(a, 'settings-revoked.png');
    await a.keyboard.press('Escape');

    // ---- --rotate-key: a new key, every browser unpaired ----
    const { bin, data, key } = go;
    go.stop();
    go = await startGoServer(dir, { images: path.join(scratch, 'images'), data, bin, args: ['--rotate-key'] });
    check(go.key !== key && /^[0-9a-f]{32}$/.test(go.key), '--rotate-key: a new key');
    await a.goto(`${go.url}/`);
    check(await isPairPage(a), '... A unpaired');
    const old = await a.evaluate((k) => fetch('/api/pair', { method: 'POST', headers: { 'Content-Type': 'application/json' },
                                                            body: JSON.stringify({ key: k }) }).then((r) => r.status), key);
    check(old === 401, `... the old key refused (${old})`);
    await a.goto(go.pairURL);
    check(await tabReady(a), '... the new address pairs it');
    // A restart without it keeps them.
    go.stop();
    go = await startGoServer(dir, { images: path.join(scratch, 'images'), data, bin });
    await a.goto(`${go.url}/`);
    check(await tabReady(a), 'a restart: still paired, the same key');
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
