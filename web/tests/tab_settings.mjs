// The Settings dialog (web/executor/settings.js), its settings kept in
// tools/standalone's settings.json: a setting changed in the dialog is
// kept across a reload, an invalid one is refused there (and by the
// server's API); the Command key preset applies at once; the
// Executor-restart notice shows; ?size= is the visit's only. In the page,
// the schema's cases (settings_cases.json) as the server checks them.
// Screenshots of every tab (all categories).
//
//   node web/tests/tab_settings.mjs DIR [SHOTS]
//
// DIR: the wasm build's page. SHOTS: a directory for the screenshots.
// The Go server is built here (go; without it, skips: 77).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { launch, newPage, firstFrame } from './harness.mjs';
import { startServer } from './server.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const [dir, shots] = process.argv.slice(2);
const cases = JSON.parse(fs.readFileSync(path.join(ROOT, 'web/tests/settings_cases.json'), 'utf8'));
const scratch = fs.mkdtempSync(path.join(process.env.TMPDIR || os.tmpdir(), 'tab-settings-'));
if (shots)
    fs.mkdirSync(shots, { recursive: true });

let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const ready = (page) => page.evaluate(() => Settings.ready.then(() => Settings.where));
const get = (page, p) => page.evaluate((p) => Settings.get(p), p);

// The dialog: change Memory, refuse a bad one, pick Ctrl as Command; then
// a reload keeps them.
async function exercise(page, url, what, { network }) {
    await page.goto(url);
    const where = await ready(page);
    check(where === 'server', `${what}: kept by the server (${where})`);
    await page.click('#settings-button');
    await page.waitForSelector('dialog#settings[open]');
    const tabs = await page.$$eval('dialog#settings nav button', (b) => b.map((x) => x.dataset.category));
    check(tabs.includes('network') === network, `${what}: the Network tab ${network ? 'is' : 'is not'} there (${tabs.join(' ')})`);

    await page.click('nav button[data-category=machine]');
    await page.fill('#setting-machine-ram_mb', '96');
    await page.press('#setting-machine-ram_mb', 'Tab');
    await page.waitForFunction(() => Settings.get('machine.ram_mb') === 96);
    check(await page.isVisible('#settings-pending'), `${what}: Memory changed, the bar says restart Executor`);
    check((await page.textContent('.settings-notices')).includes('Memory'), `${what}: ... and the dialog, naming it`);
    await page.fill('#setting-machine-ram_mb', '9999');
    await page.press('#setting-machine-ram_mb', 'Tab');
    await page.waitForFunction(() => document.querySelector('[data-path="machine.ram_mb"] .settings-error').textContent);
    const err = await page.textContent('[data-path="machine.ram_mb"] .settings-error');
    check(/at most 512/.test(err) && await get(page, 'machine.ram_mb') === 96, `${what}: 9999 MB refused ("${err}"), 96 kept`);
    await page.fill('#setting-machine-screen', '640 by 480');
    await page.press('#setting-machine-screen', 'Tab');
    await page.waitForFunction(() => document.querySelector('[data-path="machine.screen"] .settings-error').textContent);
    check(await get(page, 'machine.screen') === '640x480', `${what}: a screen of "640 by 480" refused`);

    await page.click('nav button[data-category=input]');
    await page.selectOption('#setting-input-command_key', '"ctrl"');
    await page.waitForFunction(() => Settings.get('input.command_key') === 'ctrl');
    check(await page.evaluate(() => macKeycodeForEvent({ code: 'ControlLeft' }) === 0x37
                                 && macKeycodeForEvent({ code: 'MetaLeft' }) === 0x36),
          `${what}: Command on Ctrl applies at once (Ctrl -> Command, Meta -> Control)`);
    await page.click('nav button[data-category=sound]');
    await page.$eval('#setting-sound-volume', (e) => { e.value = 35; e.dispatchEvent(new Event('change')); });
    await page.waitForFunction(() => Settings.get('sound.volume') === 35);

    await page.reload();
    await ready(page);
    check(await get(page, 'machine.ram_mb') === 96 && await get(page, 'input.command_key') === 'ctrl'
          && await get(page, 'sound.volume') === 35, `${what}: kept across a reload`);
    check(await page.evaluate(() => macKeycodeForEvent({ code: 'ControlLeft' })) === 0x37, `${what}: ... and applied`);
    await page.click('#settings-button');
    check(await page.inputValue('#setting-machine-ram_mb') === '96', `${what}: the dialog shows it`);
    // Reset the category: its defaults again.
    await page.click('nav button[data-category=input]');
    await page.click('dialog#settings .settings-reset');
    await page.waitForFunction(() => Settings.get('input.command_key') === 'meta');
    check(true, `${what}: Reset Input to defaults`);
    await page.keyboard.press('Escape');
}

const browser = await launch();
const pages = [];
const page = async () => {
    const p = await newPage(browser, { log: false });
    pages.push(p);
    return p;
};
try {
    // ---- tools/standalone: settings.json ----
    fs.mkdirSync(path.join(scratch, 'images'));
    const go = await startServer(dir, { images: path.join(scratch, 'images') });
    if (!go) {
        console.log('SKIP: no go');
        process.exit(77);
    } else {
        const p = await page();
        await exercise(p, `${go.url}/`, 'tools/standalone', { network: true });
        // The schema's cases, as settings.js checks them.
        const bad = await p.evaluate((cases) => {
            const out = [];
            const at = (d, q) => q.split('.').reduce((v, k) => (v == null ? v : v[k]), d);
            for (const c of cases.patches) {
                const start = c.start ? Settings.patched(Settings.defaults(), c.start) : Settings.defaults();
                try {
                    const d = Settings.patched(start, c.patch);
                    if (c.error)
                        out.push(`${JSON.stringify(c.patch)}: accepted`);
                    else
                        for (const [q, want] of Object.entries(c.expect))
                            if (JSON.stringify(at(d, q)) !== JSON.stringify(want))
                                out.push(`${JSON.stringify(c.patch)}: ${q} = ${JSON.stringify(at(d, q))}`);
                } catch (e) {
                    if (!c.error || e.path !== c.error)
                        out.push(`${JSON.stringify(c.patch)}: ${e.message}`);
                }
            }
            return out;
        }, cases);
        check(bad.length === 0, `settings.js: the ${cases.patches.length} cases of settings_cases.json${bad.length ? ': ' + bad.join('; ') : ''}`);
        // ?size= is this visit's, not saved.
        await p.goto(`${go.url}/index.html?size=800x600`);
        await ready(p);
        check(await get(p, 'machine.screen') === '800x600'
              && await p.evaluate(() => Settings.doc.machine.screen) === '640x480', '?size= applies for the visit, unsaved');
        await p.goto(`${go.url}/`);
        await ready(p);
        const file = JSON.parse(fs.readFileSync(go.settingsFile, 'utf8'));
        check(file.version === 2 && file.machine.ram_mb === 96 && file.sound.volume === 35,
              `tools/standalone: in ${go.settingsFile} (version ${file.version})`);
        const res = await p.evaluate(() => fetch('api/settings', { method: 'POST', headers: { 'Content-Type': 'application/json' },
                                                                   body: '{"display":{"scale":9}}' })
            .then(async (r) => [r.status, (await r.json()).path]));
        check(res[0] === 400 && res[1] === 'display.scale', `tools/standalone: the API refuses a scale of 9 (${res})`);
        // A server setting: applies at the next start, says so.
        await p.click('#settings-button');
        await p.click('nav button[data-category=network]');
        await p.fill('#setting-network-port', '8123');
        await p.press('#setting-network-port', 'Tab');
        await p.waitForFunction(() => document.querySelector('.settings-notice.server'));
        check((await p.textContent('.settings-notice.server')).includes('Port'), 'tools/standalone: the port, at the next start');
        await p.keyboard.press('Escape');
        // A library disk, mounted from the Disks tab (a blank HFS one: hformat).
        const blank = path.join(scratch, 'images', 'blank.img');
        fs.writeFileSync(blank, Buffer.alloc(800 * 1024));
        let hfs = true;
        try {
            execFileSync('hformat', ['-l', 'Blank', blank], { stdio: 'ignore', env: { ...process.env, HOME: scratch } });
        } catch {
            hfs = false;
            fs.rmSync(blank);
            console.log('SKIP the library disk: no hfsutils');
        }
        if (hfs) {
            fs.writeFileSync(`${blank}.json`, JSON.stringify({ title: 'Blank disk', note: 'empty', tags: [] }));
            await p.reload();
            await ready(p);
            await p.waitForFunction(() => Disks.state.library.length > 0);
            await p.click('#settings-button');
            await p.click('nav button[data-category=disks]');
            await p.selectOption('.settings-disks select', 'disks/blank.img');
            await p.waitForFunction(() => Settings.get('disks.mounted').length === 1);
            check((await p.textContent('.settings-disks ol')).includes('Blank disk'),
                  'tools/standalone: a library disk mounted from the Disks tab');
            await p.keyboard.press('Escape');
        }
        if (shots) {
            // Every tab, over Executor running.
            await p.reload();
            await ready(p);
            await firstFrame(p, 60000).catch(() => {});
            await p.waitForTimeout(1500);
            await p.click('#settings-button');
            for (const c of await p.$$eval('dialog#settings nav button', (b) => b.map((x) => x.dataset.category))) {
                await p.click(`nav button[data-category=${c}]`);
                await p.screenshot({ path: path.join(shots, `settings-${c}.png`) });
            }
            await p.keyboard.press('Escape');
        }
        go.stop();
    }

} catch (e) {
    console.log(`FAIL ${e.stack || e}`);
    failed++;
} finally {
    await browser.close();
    fs.rmSync(scratch, { recursive: true, force: true });
}
console.log(failed ? `${failed} failed` : 'all passed');
process.exit(failed ? 1 : 0);
