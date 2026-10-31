// Shared by the tab tests: serve the wasm build's page, start headless
// Chromium on it. Needs the playwright package (NODE_PATH; none: exit 77)
// and CHROMIUM=PATH for a browser other than the one it expects.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

let chromium;
try {
    ({ chromium } = createRequire(import.meta.url)('playwright'));
} catch {
    console.log('SKIP: no playwright package (set NODE_PATH)');
    process.exit(77);
}

const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };

// Headless Chromium (CHROMIUM=PATH, or playwright's own).
// CHROMIUM_ARGS: extra switches, space-separated (--enable-logging=stderr, say).
export function launch() {
    const args = process.env.CHROMIUM_ARGS ? process.env.CHROMIUM_ARGS.split(' ') : [];
    return chromium.launch(process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM, args } : { args });
}

// A page of browser (a context of its own: its own OPFS), its errors and
// the Worker's console shown; scale: its devicePixelRatio.
export async function newPage(browser, { log = true, tag = '', scale = 1 } = {}) {
    const context = await browser.newContext({ viewport: { width: 800, height: 640 }, deviceScaleFactor: scale });
    const page = await context.newPage();
    page.on('pageerror', (e) => console.log(`[page error${tag}]`, e.stack || e.message));
    if (log)
        page.on('worker', (w) => w.on('console', (m) => console.log(`[worker${tag}]`, m.text())));
    return page;
}

export async function open(dir, { log = true, query = '' } = {}) {
    const server = http.createServer((req, res) => {
        const f = path.join(dir, decodeURIComponent(new URL(req.url, 'http://x').pathname));
        fs.readFile(f, (err, data) => {
            if (err) { res.writeHead(404); res.end(); return; }
            res.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream' });
            res.end(data);
        });
    }).listen(0);
    const browser = await launch();
    const page = await browser.newPage({ viewport: { width: 800, height: 640 } });
    page.on('pageerror', (e) => console.log('[page error]', e.stack || e.message));
    page.on('crash', () => console.log('[page crashed]'));
    if (log)
        page.on('worker', (w) => w.on('console', (m) => console.log('[worker]', m.text())));
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html${query ? `?${query}` : ''}`);
    const close = async () => { await browser.close(); server.close(); };
    return { page, close };
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
