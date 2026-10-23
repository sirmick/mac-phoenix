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

export async function open(dir, { log = true } = {}) {
    const server = http.createServer((req, res) => {
        const f = path.join(dir, decodeURIComponent(new URL(req.url, 'http://x').pathname));
        fs.readFile(f, (err, data) => {
            if (err) { res.writeHead(404); res.end(); return; }
            res.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream' });
            res.end(data);
        });
    }).listen(0);
    const browser = await chromium.launch(process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM } : {});
    const page = await browser.newPage({ viewport: { width: 800, height: 640 } });
    page.on('pageerror', (e) => console.log('[page error]', e.message));
    if (log)
        page.on('worker', (w) => w.on('console', (m) => console.log('[worker]', m.text())));
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
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
