// Shared by the tab tests: the wasm build's page on tools/standalone
// (server.mjs: built here with go; none: exit 77), headless Chromium on
// it (TAB_BROWSER=firefox: playwright's Firefox; none installed: exit 77).
// Needs the playwright package (NODE_PATH; none: exit 77) and
// CHROMIUM=PATH for a browser other than the one it expects.
import fs from 'node:fs';
import os from 'node:os';
import { createRequire } from 'node:module';
import { startServer } from './server.mjs';

let chromium, firefoxType;
try {
    ({ chromium, firefox: firefoxType } = createRequire(import.meta.url)('playwright'));
} catch {
    console.log('SKIP: no playwright package (set NODE_PATH)');
    process.exit(77);
}

// Headless Chromium (CHROMIUM=PATH, or playwright's own).
// CHROMIUM_ARGS: extra switches, space-separated (--enable-logging=stderr, say);
// args: the test's own.
// Not chrome-headless-shell: it turns the wasm trapping (an access out of
// bounds) into the renderer's SIGSEGV, "Target crashed", where Chromium
// throws a RuntimeError the tab reports (M1.md #4). A CHROMIUM that is
// playwright's shell runs the full Chromium of its revision when that is
// installed; TAB_HEADLESS_SHELL=1 keeps the shell.
function browser() {
    const shell = process.env.CHROMIUM;
    if (process.env.TAB_HEADLESS_SHELL)
        return shell ? { executablePath: shell } : {};
    if (!shell)
        return { channel: 'chromium' };
    const full = shell.replace(/chromium_headless_shell-(\d+)\/chrome-headless-shell-linux64\/chrome-headless-shell$/,
                               'chromium-$1/chrome-linux64/chrome');
    return { executablePath: full !== shell && fs.existsSync(full) ? full : shell };
}
// Firefox (the owner's, on Windows at 1.25 and 1.5): headless, it ignores
// deviceScaleFactor; its devicePixelRatio is layout.css.devPixelsPerPx,
// a browser's own (dpr).
export const firefox = process.env.TAB_BROWSER === 'firefox';
// (Chromium's singleton socket goes in its TMPDIR, at most 107 bytes of
// path: a build tree's is too long; the profile stays in ours.)
export async function launch({ args: own = [], dpr = 1 } = {}) {
    if (firefox) {
        try {
            return await firefoxType.launch({ firefoxUserPrefs: { 'layout.css.devPixelsPerPx': String(dpr) } });
        } catch (e) {
            console.log(`SKIP: no Firefox (${e.message.split('\n')[0]})`);
            process.exit(77);
        }
    }
    const args = [...own, ...(process.env.CHROMIUM_ARGS ? process.env.CHROMIUM_ARGS.split(' ') : [])];
    const tmp = os.tmpdir().length < 60 ? os.tmpdir() : '/tmp';
    return chromium.launch({ ...browser(), args, env: { ...process.env, TMPDIR: tmp } });
}

// A page of browser (a context of its own: another browser, to the
// server), its errors and the Worker's console shown; scale: its
// devicePixelRatio.
export async function newPage(browser, { log = true, tag = '', scale = 1 } = {}) {
    const context = await browser.newContext({ viewport: { width: 800, height: 640 }, deviceScaleFactor: scale });
    const page = await context.newPage();
    page.on('pageerror', (e) => console.log(`[page error${tag}]`, e.stack || e.message));
    if (log)
        page.on('worker', (w) => w.on('console', (m) => console.log(`[worker${tag}]`, m.text())));
    return page;
}

// The tab from dir on tools/standalone (images: its library; include:
// the patterns offered), in a browser of its own at index.html?query
// (viewport and dpr: the window's size and devicePixelRatio): the page,
// close(), and the server ({ url, data, ... }: server.mjs).
export async function open(dir, { log = true, query = '', images, include,
                                  viewport = { width: 800, height: 640 }, dpr = 1 } = {}) {
    const server = await startServer(dir, { images, include });
    if (!server) {
        console.log('SKIP: no go');
        process.exit(77);
    }
    const browser = await launch({ dpr });
    const page = await browser.newPage({ viewport, ...(firefox ? {} : { deviceScaleFactor: dpr }) });
    page.on('pageerror', (e) => console.log('[page error]', e.stack || e.message));
    page.on('crash', () => console.log('[page crashed]'));
    if (log)
        page.on('worker', (w) => w.on('console', (m) => console.log('[worker]', m.text())));
    await page.goto(`${server.url}/index.html${query ? `?${query}` : ''}`);
    const close = async () => { await browser.close(); server.stop(); };
    return { page, close, server };
}

// Until the Worker has drawn a frame (after a start or restart).
export async function firstFrame(page, ms = 30000) {
    await page.waitForFunction(() => window.executor.frames > 0, null, { timeout: ms });
}

export const request = (page, op, arg) => page.evaluate(([op, arg]) => executor.request(op, arg), [op, arg]);

// Polls fn until it returns something truthy, or ms pass.
export async function until(fn, ms, every = 250) {
    const end = Date.now() + ms;
    for (;;) {
        const v = await fn();
        if (v || Date.now() > end)
            return v;
        await new Promise((r) => setTimeout(r, every));
    }
}

// The Disks panel (web/executor/disks.js): open, closed (it moves the
// screen aside), the System picked in it.
// (through Disks: a small viewport can leave the panel's buttons outside
// it; web/tests/tab_disks.mjs clicks the real ones)
export async function openDisks(page) {
    await page.evaluate(() => Disks.open());
}
export async function closeDisks(page) {
    await page.evaluate(() => Disks.close());
}
export async function pickSystem(page, value) {
    await openDisks(page);
    await page.selectOption('#system', value);
    await closeDisks(page);
}
// A disk's button in the panel (revert, download, eject; the System's
// disk too), clicked with the panel open, then closed.
export async function diskButton(page, name, button) {
    await openDisks(page);
    await page.locator(`#disks-panel [data-name="${name}"] button.${button}`).evaluate((b) => b.click());
    await closeDisks(page);
}
// What the panel says of a disk: its badges and explanation.
export async function diskState(page, name) {
    await openDisks(page);
    const row = page.locator(`#disks-panel [data-name="${name}"]`);
    const text = (await row.count()) ? (await row.first().locator('.file, .explain').allTextContents()).join(' ') : '';
    await closeDisks(page);
    return text;
}
