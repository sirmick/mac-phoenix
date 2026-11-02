// Shared by the tab tests: the wasm build's page on tools/standalone
// (server.mjs: built here with go; none: exit 77), headless Chromium on
// it. Needs the playwright package (NODE_PATH; none: exit 77) and
// CHROMIUM=PATH for a browser other than the one it expects.
import { createRequire } from 'node:module';
import { startServer } from './server.mjs';

let chromium;
try {
    ({ chromium } = createRequire(import.meta.url)('playwright'));
} catch {
    console.log('SKIP: no playwright package (set NODE_PATH)');
    process.exit(77);
}

// Headless Chromium (CHROMIUM=PATH, or playwright's own).
// CHROMIUM_ARGS: extra switches, space-separated (--enable-logging=stderr, say);
// args: the test's own.
export function launch({ args: own = [] } = {}) {
    const args = [...own, ...(process.env.CHROMIUM_ARGS ? process.env.CHROMIUM_ARGS.split(' ') : [])];
    return chromium.launch(process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM, args } : { args });
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
// the patterns offered), in a browser of its own at index.html?query: the
// page, close(), and the server ({ url, data, ... }: server.mjs).
export async function open(dir, { log = true, query = '', images, include } = {}) {
    const server = await startServer(dir, { images, include });
    if (!server) {
        console.log('SKIP: no go');
        process.exit(77);
    }
    const browser = await launch();
    const page = await browser.newPage({ viewport: { width: 800, height: 640 } });
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
