// W2's check (docs/executor/DIRECTION.md): Executor's built-in Browser in a
// headless Chromium tab. Serves DIR (the wasm build's executor-web output),
// loads the page, waits for frames, screenshots the canvas, then presses
// on the File menu and checks the screen changed (input reaches the guest
// while it tracks the menu).
//
//   node web/tests/tab_smoke.mjs DIR OUT.png [SECONDS]
//
// Needs the playwright package (NODE_PATH) and its Chromium.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

// Playwright from NODE_PATH (require honours it); none: skip (77).
let chromium;
try {
    ({ chromium } = createRequire(import.meta.url)('playwright'));
} catch {
    console.log('SKIP: no playwright package (set NODE_PATH)');
    process.exit(77);
}

const [dir, out, secs = '60'] = process.argv.slice(2);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const server = http.createServer((req, res) => {
    const f = path.join(dir, decodeURIComponent(new URL(req.url, 'http://x').pathname));
    fs.readFile(f, (err, data) => {
        if (err) { res.writeHead(404); res.end(); return; }
        res.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream' });
        res.end(data);
    });
}).listen(0);
const port = server.address().port;

// CHROMIUM=PATH: a browser other than the one this playwright expects.
const browser = await chromium.launch(process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM } : {});
const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
page.on('console', (m) => console.log('[page]', m.text()));
page.on('pageerror', (e) => console.log('[page error]', e.message));
page.on('worker', (w) => w.on('console', (m) => console.log('[worker]', m.text())));
await page.goto(`http://127.0.0.1:${port}/index.html`);

const start = Date.now();
const deadline = start + Number(secs) * 1000;
let frames = 0;
while (Date.now() < deadline && !(frames = await page.evaluate(() => window.executorFrames || 0)))
    await page.waitForTimeout(100);
console.log(`first frame after ${((Date.now() - start) / 1000).toFixed(1)} s`);
await page.waitForTimeout(3000);
const before = await page.locator('#screen').screenshot({ path: out });

// Input: open the File menu (the menu bar's second title), screenshot it
// held open, let go.
const box = await page.locator('#screen').boundingBox();
const at = (x, y) => [box.x + x * box.width / 640, box.y + y * box.height / 480];
await page.mouse.move(...at(50, 9));
await page.mouse.down();
await page.waitForTimeout(1500);
const menu = await page.locator('#screen').screenshot({ path: out.replace(/\.png$/, '-menu.png') });
await page.mouse.up();
await page.waitForTimeout(500);
frames = await page.evaluate(() => window.executorFrames || 0);
console.log(`frames: ${frames}, screenshots: ${out}, ${out.replace(/\.png$/, '-menu.png')}`);
await browser.close();
server.close();
// Frames came, and the press opened the menu (the screen changed).
const ok = frames > 0 && !before.equals(menu);
if (!ok)
    console.log(frames ? 'FAIL: the click changed nothing' : 'FAIL: no frames');
process.exit(ok ? 0 : 1);
