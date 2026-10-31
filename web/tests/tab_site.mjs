// The assembled site (docs/executor/WebSite.md): the applications on a
// library disk, opened in the tab on one of the site's systems and used a
// moment, as a visitor would: signed in to tools/web/server, its boot disk
// and the application disk from the library (read lazily), each
// application launched in turn (a reload between them) and checked:
//
//   front    it is the current application (CurApName)
//   drawn    a window of its own (none at launch: File > New, Cmd-N), or
//            (full screen) most of the screen redrawn
//   menu     its menu bar has menus, and pressing on the second one draws it
//            (a modal dialog at launch is cancelled first, Cmd-.; games: the
//            menus are there, full-screen ones hide the bar)
//   alive    still in front after that, and no alert in front whose text
//            says something went wrong (an error, "requires", -7004, ...)
// An application that crashes the tab fails; the next gets a new tab.
//
//   node web/tests/tab_site.mjs DIR LIBRARY SYSTEM DISK
//
// DIR the wasm build's page, LIBRARY tools/web/make_library.py's output,
// SYSTEM 7.6.1 or 8.6 (8.6: PowerPC ticked), DISK a library disk there.
// The applications are the disk record's "launch" entries for SYSTEM;
// docs/executor/SCOREBOARD.md says which run yet (the e761 or e86 column:
// "runs"; "partial", either; else expected to fail), so a cell turns green
// as fixes land and the scoreboard is brought up to date: an expected
// failure prints XFAIL, one that works XPASS (the scoreboard is behind);
// only an application the scoreboard says runs fails the cell.
// SITE_APPS=Name,Name: only those. SITE_RESULTS=FILE: appends one JSON
// line an application. SITE_LOG=1: the Worker's console. The canvas as
// it is after each application's checks (and the Finder's, first) goes
// to SITE_SHOTS (default ~/storage/tmp/site/shots) as SYSTEM-NAME.png;
// SITE_DPR=2: the page at devicePixelRatio 2 (the tab draws at 2x), shots
// SYSTEM-NAME@2x.png.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { launch, newPage, firstFrame, request as ask, until } from './harness.mjs';
import { startServer, signIn, OWNER_PASSWORD } from './server.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const [dir, library, system, diskName] = process.argv.slice(2);
const COLUMN = { '7.6.1': 'e761', '8.6': 'e86' }[system];
const records = {};
for (const f of fs.existsSync(library || '') ? fs.readdirSync(library) : [])
    if (f.endsWith('.img.json'))
        records[f.slice(0, -'.json'.length)] = JSON.parse(fs.readFileSync(path.join(library, f), 'utf8'));
const boot = Object.keys(records).find((n) => records[n].system === system);
const disk = records[diskName];
if (!COLUMN || !boot || !disk || !fs.existsSync(path.join(library, diskName))) {
    console.log(`SKIP: no ${system} boot disk or ${diskName} in ${library} (tools/web/make_library.py)`);
    process.exit(77);
}
const only = process.env.SITE_APPS ? process.env.SITE_APPS.split(',') : null;
const apps = disk.launch.filter((a) => a.systems.includes(system) && (!only || only.includes(a.name)));
const games = disk.tags.includes('games');

// A request, or null after ms: answered at the application's next event
// call, so an application stuck (or crashed) answers nothing.
const request = (page, op, arg, ms = 15000) => Promise.race([
    ask(page, op, arg), new Promise((r) => setTimeout(() => r(null), ms))]);

// SCOREBOARD.md: SYSTEM's section, the row of the application's source
// disk and path, the column's verdict.
function verdict(app) {
    const text = fs.readFileSync(path.join(ROOT, 'docs/executor/SCOREBOARD.md'), 'utf8');
    const section = text.split(/^## /m).find((s) => s.startsWith(`${system}\n`));
    if (!section)
        return null;
    const rows = section.split('\n').filter((l) => l.startsWith('|')).map((l) => l.split('|').slice(1, -1).map((c) => c.trim()));
    const head = rows.find((r) => r[0] === 'Disk');
    const row = rows.find((r) => r[0] === app.source.replace(/\.img$/, '') && r[1] === app.source_app);
    return head && row ? row[head.indexOf(COLUMN)] : null;
}

// The screen, coarsely: grey levels of every 4th pixel of every 4th row
// (or a box of it), worked out in the page; null when the Worker doesn't
// answer (Executor not yielding).
const sample = (page, box = [0, 0, 640, 480]) => Promise.race([page.evaluate(async ([l, t, r, b]) => {
    const p = await executor.request('pixels');
    const sx = p.width / 640, sy = p.height / 480, out = [];
    for (let y = t; y < b; y += 4)
        for (let x = l; x < r; x += 4) {
            const i = (Math.floor(y * sy) * p.width + Math.floor(x * sx)) * 4;
            out.push((p.data[i] + p.data[i + 1] + p.data[i + 2]) / 3 | 0);
        }
    return out;
}, box), new Promise((r) => setTimeout(() => r(null), 15000))]);
// The canvas's own pixels as a PNG (at the page's scale), in the shots
// directory.
const SHOTS = process.env.SITE_SHOTS || path.join(os.homedir(), 'storage/tmp/site/shots');
const DPR = Number(process.env.SITE_DPR) || 1;
async function shot(name) {
    const png = await Promise.race([page.evaluate(async () => {
        const p = await executor.request('pixels');
        const c = new OffscreenCanvas(p.width, p.height);
        c.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(p.data), p.width, p.height), 0, 0);
        const bytes = new Uint8Array(await (await c.convertToBlob({ type: 'image/png' })).arrayBuffer());
        let bin = '';
        for (let i = 0; i < bytes.length; i += 0x8000)
            bin += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
        return btoa(bin);
    }), new Promise((r) => setTimeout(() => r(null), 15000))]).catch(() => null);
    if (!png)
        return;
    fs.mkdirSync(SHOTS, { recursive: true });
    const at = DPR === 1 ? '' : `@${DPR}x`;              // runs at other scales keep theirs
    fs.writeFileSync(path.join(SHOTS, `${system}-${name.replace(/[^\w.-]+/g, '_')}${at}.png`), Buffer.from(png, 'base64'));
}
const changed = (a, b) => !a || !b ? 0 : a.reduce((n, v, i) => n + (Math.abs(v - b[i]) > 8), 0) / a.length;

const server = await startServer(dir, { images: library, include: [boot, diskName] });
const browser = await launch();
const query = `?disk=disks/${encodeURIComponent(boot)}&disk=disks/${encodeURIComponent(diskName)}`
    + `&system=${encodeURIComponent(boot)}${system === '8.6' ? '&ppc=1' : ''}`;
// A tab signed in on the System with the disk; a new one (a browser
// context of its own) when an application crashed the last.
let page = null, crashed = false;
async function openTab() {
    if (page)
        await page.context().close().catch(() => {});
    page = await newPage(browser, { log: !!process.env.SITE_LOG, scale: DPR });
    page.on('dialog', (d) => d.accept());
    page.on('crash', () => { crashed = true; });
    crashed = false;
    await signIn(page, server.url, 'owner', OWNER_PASSWORD, { query });
}
const finder = async () => {
    await firstFrame(page, 120000);
    return until(() => request(page, 'app').then((a) => a === 'Finder'), 180000, 500);
};
const at = async (x, y) => {
    const b = await page.locator('#screen').boundingBox();
    return [b.x + x * b.width / 640, b.y + y * b.height / 480];
};

// An alert saying something went wrong (windows' dialog text).
const DIALOG_KIND = 2;
const ERROR = /error|sorry|requires?\b|required|cannot|can't|could not|couldn't|unable|not enough|out of memory|newer version|insert the|damaged|modified by|-\d{3,}/i;

const own = async () => ((await request(page, 'windows')) || []).filter((w) => w.visible && w.title !== 'Desktop');
const key = async (combo) => {
    await page.locator('#screen').focus();
    await page.keyboard.press(combo);
};

// Presses on the second menu's title; whether the menu is drawn.
async function pressMenu() {
    const menus = await request(page, 'menus') || [];
    if (menus.length < 2)
        return false;
    const m = menus[1], right = menus[2] ? menus[2].left : m.left + 60;
    const box = [m.left, 22, Math.min(m.left + 160, 640), 100];
    const before = await sample(page, box);
    await page.mouse.move(...await at(m.left + (right - m.left) / 2, 10));
    await page.mouse.down();
    const drawn = !!await until(async () => changed(before, await sample(page, box)) > 0.2, 3000, 300);
    await page.mouse.move(...await at(630, 10));         // off every item: nothing chosen
    await page.mouse.up();
    await page.waitForTimeout(500);
    return drawn;
}

async function tryApp(app) {
    const got = {};
    const err = await request(page, 'launch', app.app, 60000);
    got.launch = err === '0' || err === 0;
    if (!got.launch)
        return { ...got, why: `LaunchApplication: ${err}` };
    got.front = !!await until(() => request(page, 'app').then((a) => a === app.name), 90000, 500);
    if (!got.front)
        return { ...got, why: `not in front (${await request(page, 'app')})` };
    await page.waitForTimeout(4000);                     // its first windows, splash screens
    got.drawn = (await own()).length > 0 || changed(await sample(page), finderScreen) > 0.5;
    const menus = await request(page, 'menus') || [];
    got.menus = menus.map((m) => m.title);
    if (games) {
        got.menu = menus.length > 0;
    } else {
        // A modal dialog at launch (Standard File, a splash) leaves the
        // menus disabled: Cmd-. cancels it, then the menu again.
        got.menu = await pressMenu();
        if (!got.menu) {
            await key('Meta+Period');
            await page.waitForTimeout(1500);
            got.menu = await pressMenu();
        }
        // No window of its own yet: File > New, as a user would.
        if (!(await own()).length) {
            await key('Meta+KeyN');
            got.drawn = !!await until(async () => (await own()).length > 0, 8000, 500) || got.drawn;
        }
    }
    // Still up: in front (or too busy to answer: a full-screen game), and
    // not showing an error.
    const now = await request(page, 'app');
    got.alive = now === null || now === app.name;
    const front = ((await request(page, 'windows')) || []).find((w) => w.visible);
    got.front = front ? { title: front.title, kind: front.kind, rect: front.rect } : null;
    // its text as drawn: ParamText's strings for ^0..^3 (or beside it,
    // for an application that draws them itself)
    const params = front && front.params || [];
    const said = front && front.kind === DIALOG_KIND
        ? (/\^[0-3]/.test(front.text) ? front.text.replace(/\^([0-3])/g, (_, i) => params[i])
                                      : [front.text, ...params].filter(Boolean).join('\n')) : '';
    got.alert = ERROR.test(said) ? said.replace(/\n+/g, ' ') : '';
    const ok = got.drawn && got.menu && got.alive && !got.alert;
    return { ...got, ok, why: ok ? '' : [!got.drawn && 'nothing drawn', !got.menu && 'no menu',
                                        !got.alive && `then gone (${now} in front)`,
                                        got.alert && `alert "${got.alert}"`].filter(Boolean).join(', ') };
}

let finderScreen = null, unexpected = 0;
const table = [];
try {
    await openTab();
    const up = await finder();
    await page.waitForTimeout(3000);                     // the desktop drawn
    await shot('Finder');
    if (!up) {
        console.log(`FAIL the Finder on ${boot}`);
        process.exitCode = 1;
    } else {
        for (const [n, app] of apps.entries()) {
            if (n) {
                if (crashed)
                    await openTab();
                else
                    await page.reload();
                if (!await finder()) {
                    console.log(`FAIL the Finder again before ${app.name}`);
                    unexpected++;
                    break;
                }
            }
            await page.waitForTimeout(2000);
            finderScreen = await sample(page);
            const v = verdict(app);
            // partial: up, but something drawn or done wrong: either
            const expect = v === 'runs' || v === null ? 'pass' : v === 'partial' ? 'either' : 'fail';
            const t0 = Date.now();
            const r = await tryApp(app).catch((e) => ({ ok: false, why: e.message }));
            const ok = !!r.ok;
            const mark = ok ? (expect === 'fail' ? 'XPASS' : 'PASS') : (expect === 'pass' ? 'FAIL' : 'XFAIL');
            unexpected += mark === 'FAIL';
            const line = `${mark} ${system} ${app.name}${r.why ? `: ${r.why}` : ''} (scoreboard: ${v || 'no row'})`;
            console.log(line);
            table.push({ system, disk: diskName, app: app.name, result: mark, scoreboard: v, why: r.why || '',
                         menus: r.menus || [], front: r.front || null, seconds: Math.round((Date.now() - t0) / 1000) });
            if (!crashed)
                await shot(app.name);
        }
    }
} finally {
    await browser.close();
    server.stop();
}
if (process.env.SITE_RESULTS)
    fs.appendFileSync(process.env.SITE_RESULTS, table.map((r) => JSON.stringify(r) + '\n').join(''));
console.log(unexpected ? `${unexpected} application(s) the scoreboard says run failed` : 'as the scoreboard says (or better)');
process.exit(unexpected || process.exitCode ? 1 : 0);
