// The Settings dialog (web/executor/settings.js), its settings kept in
// tools/standalone's settings.json: changes in the dialog are a draft,
// kept by Save (across a reload), forgotten by Cancel and Escape; an
// invalid one is refused there (and by the server's API); the screen's
// sizes and Custom…; the help is a tooltip the control is described by,
// shown on focus; the Command key preset applies once saved; the
// Executor-restart notice shows; ?size= is the visit's only. The loading
// panel (loading.js): the module's download in bytes, every step done, gone
// at the first frame. In the page, the schema's cases (settings_cases.json)
// as the server checks them. The System: the Disks panel's choice and
// Settings > Machine's one setting, each following the other, the dialog's
// list from the disks and the library. Screenshots of every tab.
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

const openDialog = async (page) => {
    await page.click('#settings-button');
    await page.waitForSelector('dialog#settings[open]');
};
const save = async (page) => {
    await page.click('dialog#settings .settings-save');
    await page.waitForSelector('dialog#settings:not([open])', { state: 'attached', timeout: 10000 }).catch(async (e) => {
        throw new Error(`Save left the dialog open: "${await page.textContent('.settings-unsaved')}" `
                        + `${await page.$$eval('.settings-row.invalid', (r) => r.map((x) => x.dataset.path + ': ' + x.querySelector('.settings-error').textContent))}`);
    });
};

// The dialog: change Memory, refuse a bad one, pick Ctrl as Command, Save;
// then a reload keeps them. Cancel and Escape forget what they leave.
async function exercise(page, url, what, { network }) {
    await page.goto(url);
    const where = await ready(page);
    check(where === 'server', `${what}: kept by the server (${where})`);
    // The loading panel (the first load: no module cached): the module's
    // bytes, every step done, gone at the first frame.
    await firstFrame(page, 60000).catch(() => {});
    const log = await page.evaluate(() => Loading.log);
    const last = (k) => log.filter((l) => l.startsWith(`${k} `)).at(-1) || '';
    check(log.some((l) => /^module active .*MB of \d+ MB \(\d+%\)/.test(l)), `${what}: loading: the module's download in bytes and percent`);
    check(['module', 'disks', 'saved'].every((k) => last(k).startsWith(`${k} done`)) && /^start active/.test(last('start')),
          `${what}: loading: every step done (${['module', 'disks', 'saved', 'start'].map(last).join('; ')})`);
    check(await page.isHidden('#loading'), `${what}: loading: gone at the first frame`);
    await openDialog(page);
    const tabs = await page.$$eval('dialog#settings nav button', (b) => b.map((x) => x.dataset.category));
    check(tabs.includes('network') === network, `${what}: the Network tab ${network ? 'is' : 'is not'} there (${tabs.join(' ')})`);
    check(await page.isDisabled('dialog#settings .settings-save'), `${what}: nothing changed, Save disabled`);

    await page.click('nav button[data-category=machine]');
    // The help: a tooltip, the control's description, shown on focus.
    const help = await page.$eval('#setting-machine-ram_mb', (e) => {
        const h = document.getElementById(e.getAttribute('aria-describedby'));
        return h && { role: h.getAttribute('role'), text: h.textContent, shown: getComputedStyle(h).visibility };
    });
    check(help && help.role === 'tooltip' && /Auto: 32 MB/.test(help.text) && help.shown === 'hidden',
          `${what}: Memory's help is its tooltip, hidden (${JSON.stringify(help)})`);
    await page.focus('#setting-machine-ram_mb-auto');
    check(await page.$eval('#setting-machine-ram_mb-auto', (e) => e.getAttribute('aria-describedby')) === 'setting-machine-ram_mb-help'
          && await page.waitForSelector('#setting-machine-ram_mb-help', { state: 'visible', timeout: 2000 }).then(() => true, () => false),
          `${what}: ... shown with the keyboard's focus`);

    // Memory: Auto, the choices disabled showing what it picks; unticked,
    // that pick chosen, then 128.
    const shown = () => page.$eval('#setting-machine-ram_mb', (e) => [e.disabled, e.selectedOptions[0].textContent]);
    const auto0 = await shown();
    check(await page.isChecked('#setting-machine-ram_mb-auto') && auto0[0] && auto0[1] === '32 MB (Auto)',
          `${what}: Memory on Auto, showing its pick (${auto0})`);
    check(!/0 =/.test(await page.textContent('dialog#settings')), `${what}: no "0 =" in the dialog`);
    await page.uncheck('#setting-machine-ram_mb-auto');
    const manual = await shown();
    check(!manual[0] && manual[1] === '32 MB', `${what}: Auto unticked: 32 MB, chosen (${manual})`);
    await page.selectOption('#setting-machine-ram_mb', '128');
    await page.waitForFunction(() => document.querySelector('[data-path="machine.ram_mb"]').classList.contains('changed'));
    check(await get(page, 'machine.ram_mb') === 0 && !await page.isDisabled('dialog#settings .settings-save')
          && /Executor restarts to apply Memory/.test(await page.textContent('.settings-unsaved')),
          `${what}: Memory changed, not saved yet; the footer says Executor restarts for it`);

    // The screen: a size from the list, Custom… with a bad width, then a good one.
    const sizes = await page.$$eval('#setting-machine-screen option', (o) => o.map((x) => x.value));
    check(sizes.includes('512x342') && sizes.includes('1152x870') && sizes.at(-1) === 'custom'
          && await page.inputValue('#setting-machine-screen') === '640x480', `${what}: the screen sizes (${sizes.length}), Custom… last`);
    await page.selectOption('#setting-machine-screen', 'custom');
    check(await page.isVisible('#setting-machine-screen-w'), `${what}: Custom… shows width and height`);
    await page.fill('#setting-machine-screen-w', '64');
    await page.press('#setting-machine-screen-w', 'Tab');
    await page.waitForFunction(() => document.querySelector('[data-path="machine.screen"] .settings-error').textContent);
    const err = await page.textContent('[data-path="machine.screen"] .settings-error');
    await page.click('dialog#settings .settings-save');
    check(/100 to 9999/.test(err) && await page.isVisible('dialog#settings[open]') && await get(page, 'machine.screen') === '640x480',
          `${what}: a screen 64 wide refused ("${err}"), Save holds the dialog open`);
    await page.fill('#setting-machine-screen-w', '700');
    await page.press('#setting-machine-screen-w', 'Tab');
    await page.waitForFunction(() => !document.querySelector('[data-path="machine.screen"]').classList.contains('invalid'));

    await page.click('nav button[data-category=input]');
    await page.selectOption('#setting-input-command_key', '"ctrl"');
    await page.click('nav button[data-category=sound]');
    await page.$eval('#setting-sound-volume', (e) => { e.value = 35; e.dispatchEvent(new Event('change')); });
    await save(page);
    check(await get(page, 'machine.ram_mb') === 128 && await get(page, 'machine.screen') === '700x480'
          && await get(page, 'sound.volume') === 35, `${what}: Save keeps them`);
    check(await page.evaluate(() => macKeycodeForEvent({ code: 'ControlLeft' }) === 0x37
                                 && macKeycodeForEvent({ code: 'MetaLeft' }) === 0x36),
          `${what}: Command on Ctrl applies once saved (Ctrl -> Command, Meta -> Control)`);
    check(await page.isVisible('#settings-pending'), `${what}: the bar says restart Executor`);
    await openDialog(page);
    check((await page.textContent('.settings-notices')).includes('Memory'), `${what}: ... and the dialog, naming it`);

    // Cancel and Escape: forgotten.
    await page.click('nav button[data-category=sound]');
    await page.$eval('#setting-sound-volume', (e) => { e.value = 10; e.dispatchEvent(new Event('change')); });
    await page.click('dialog#settings .settings-cancel');
    await openDialog(page);
    await page.click('nav button[data-category=machine]');
    await page.selectOption('#setting-machine-screen', '1024x768');
    await page.keyboard.press('Escape');
    await page.waitForSelector('dialog#settings:not([open])', { state: 'attached' });
    await openDialog(page);
    await page.click('nav button[data-category=sound]');
    check(await get(page, 'sound.volume') === 35 && await get(page, 'machine.screen') === '700x480'
          && await page.inputValue('#setting-sound-volume') === '35', `${what}: Cancel and Escape forget the changes`);
    await page.keyboard.press('Escape');
    // Back to 640x480 (the screenshots, the Executor run below).
    await openDialog(page);
    await page.click('nav button[data-category=machine]');
    await page.selectOption('#setting-machine-screen', '640x480');
    await save(page);

    await page.reload();
    await ready(page);
    check(await get(page, 'machine.ram_mb') === 128 && await get(page, 'input.command_key') === 'ctrl'
          && await get(page, 'sound.volume') === 35, `${what}: kept across a reload`);
    check(await page.evaluate(() => macKeycodeForEvent({ code: 'ControlLeft' })) === 0x37, `${what}: ... and applied`);
    await openDialog(page);
    check(await page.inputValue('#setting-machine-ram_mb') === '128' && !await page.isChecked('#setting-machine-ram_mb-auto'),
          `${what}: the dialog shows it`);
    // Reset the category: its defaults again, once saved.
    await page.click('nav button[data-category=input]');
    await page.click('dialog#settings .settings-reset');
    check(await get(page, 'input.command_key') === 'ctrl', `${what}: Reset Input: not before Save`);
    await save(page);
    check(await get(page, 'input.command_key') === 'meta', `${what}: Reset Input to defaults, saved`);
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
        check(file.version === 2 && file.machine.ram_mb === 128 && file.sound.volume === 35,
              `tools/standalone: in ${go.settingsFile} (version ${file.version})`);
        const res = await p.evaluate(() => fetch('api/settings', { method: 'POST', headers: { 'Content-Type': 'application/json' },
                                                                   body: '{"display":{"scale":9}}' })
            .then(async (r) => [r.status, (await r.json()).path]));
        check(res[0] === 400 && res[1] === 'display.scale', `tools/standalone: the API refuses a scale of 9 (${res})`);
        // A server setting: applies at the next start, says so.
        await openDialog(p);
        await p.click('nav button[data-category=network]');
        await p.fill('#setting-network-port', '8123');
        await p.press('#setting-network-port', 'Tab');
        check(/server applies Port/.test(await p.textContent('.settings-unsaved')), 'tools/standalone: the port: Save says at the next start');
        await save(p);
        await p.waitForFunction(() => document.querySelector('.settings-notice.server'));
        check((await p.textContent('.settings-notice.server')).includes('Port'), 'tools/standalone: the port, at the next start');
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
            await openDialog(p);
            await p.click('nav button[data-category=disks]');
            await p.selectOption('.settings-disks select', 'disks/blank.img');
            check((await p.textContent('.settings-disks ol')).includes('Blank disk'),
                  'tools/standalone: a library disk in the Disks tab\'s list');
            await save(p);
            await p.waitForFunction(() => Settings.get('disks.mounted').length === 1);
            check(true, 'tools/standalone: ... mounted, saved');

            // The System: one setting (machine.system) behind the Disks
            // panel's choice and Settings > Machine's, each following the
            // other; the dialog's list made from the disks and the library
            // each time it opens, and while it is open.
            for (const n of ['sys-a', 'sys-b']) {
                fs.copyFileSync(blank, path.join(scratch, 'images', `${n}.img`));
                fs.writeFileSync(path.join(scratch, 'images', `${n}.img.json`),
                                 JSON.stringify({ title: `System ${n.at(-1).toUpperCase()}`, note: '', tags: ['system'] }));
            }
            await p.reload();
            await ready(p);
            await p.waitForFunction(() => Disks.state.library.length >= 3);
            const machine = async () => {
                await openDialog(p);
                await p.click('nav button[data-category=machine]');
                return p.$eval('#setting-machine-system', (s) => ({ value: s.value, options: [...s.options].map((o) => [o.value, o.textContent]) }));
            };
            let m = await machine();
            check(m.options.some(([v, t]) => v.endsWith('/sys-a.img') && t === 'System A (from the library)')
                  && m.options.some(([v]) => v.endsWith('/sys-b.img')), `tools/standalone: Settings offers the library's Systems (${JSON.stringify(m.options)})`);
            await p.keyboard.press('Escape');
            // The panel's choice, then the dialog opened again
            const panelPick = (n) => p.evaluate((n) => {
                const s = systemSelect;     // (main.js; in the Disks panel when it is open)
                s.value = [...s.options].find((o) => o.value === n || o.value.endsWith(`/${n}`)).value;
                s.dispatchEvent(new Event('change'));
            }, n);
            const systemsIn = () => p.evaluate(() => Settings.get('disks.mounted').map((u) => u.split('/').pop()).filter((n) => /^sys-/.test(n)));
            await panelPick('sys-a.img');
            await p.waitForFunction(() => Settings.get('machine.system') === 'sys-a.img');
            m = await machine();
            const label = (m.options.find(([v]) => v === m.value) || [])[1] || '';
            check(m.value === 'sys-a.img' && !/not here/.test(label),
                  `tools/standalone: the panel's System shown in Settings when it opens (${m.value}: ${label})`);
            // ... and while it is open: a change from the page reaches it
            await p.keyboard.press('Escape');
            m = await machine();
            await p.evaluate(() => Settings.set({ machine: { system: 'blank.img' } }));
            check(await p.$eval('#setting-machine-system', (s) => s.value) === 'blank.img',
                  'tools/standalone: the open dialog follows a change of the System');
            await p.waitForFunction(() => !Settings.get('disks.mounted').some((u) => u.endsWith('sys-a.img')));
            check(true, "tools/standalone: ... the last System's library disk out of disks.mounted");
            // The dialog's choice (a library System disk, by its url), saved:
            // the panel follows, the disk put in
            const bUrl = (await p.$$eval('#setting-machine-system option', (o) => o.map((x) => x.value))).find((v) => v.endsWith('/sys-b.img'));
            await p.selectOption('#setting-machine-system', bUrl);
            await save(p);
            await p.waitForFunction(() => Settings.get('machine.system') === 'sys-b.img');
            const panel = await p.evaluate(() => systemSelect.value);
            check(/sys-b\.img$/.test(panel), `tools/standalone: Settings' System shown in the Disks panel (${panel})`);
            await p.waitForFunction(() => Settings.get('disks.mounted').some((u) => u.endsWith('sys-b.img')));
            check(true, "tools/standalone: ... the library's System disk put in (disks.mounted)");
            // a -> b -> a in the panel: only the System's disk stays in, each time
            const after = [];
            for (const n of ['sys-a.img', 'sys-b.img', 'sys-a.img']) {
                await panelPick(n);
                await p.waitForFunction((n) => Settings.get('machine.system') === n, n);
                await p.waitForTimeout(300);
                after.push((await systemsIn()).join('+'));
            }
            check(after.join(' ') === 'sys-a.img sys-b.img sys-a.img',
                  `tools/standalone: System a -> b -> a: only the System's library disk in disks.mounted (${after.join(', ')})`);
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
