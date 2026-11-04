// tools/bench/run.py's tab: Executor in headless Chromium (the wasm
// build's page on tools/standalone, web/tests/server.mjs), driven by one
// JSON command a line on stdin, one {"result": ...} line on stdout each.
//
//   node tools/bench/tab.mjs PAGE-DIR LIBRARY SYSTEM QUERY-JSON
//
// LIBRARY: boot-SYSTEM.img and utilities.img with their records; QUERY:
// the page's extra settings ({"ppc": 1, "depth": 8}). Commands:
//   ["ready", ms]            the Finder in front (after the first frame)
//   ["launch", path]         LaunchApplication's result
//   ["app"]                  the front application's name
//   ["screenshot"]           the canvas as a base64 PNG
//   ["mouse", x, y, down]    move there (Mac coordinates); down true/false: press/release
//   ["key", code]            a Mac key code (Return, digits, letters)
//   ["download", name, file] the disk image as changed, written to file
//   ["alive"]                the page not crashed, Executor not stopped
//   ["quit"]
import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const { launch, newPage, firstFrame, request, until } = await import(path.join(ROOT, 'web/tests/harness.mjs'));
const { startServer } = await import(path.join(ROOT, 'web/tests/server.mjs'));

const [dir, library, system, queryJson] = process.argv.slice(2);
const boot = `boot-${system}.img`;
const extra = JSON.parse(queryJson || '{}');
const say = (result) => process.stdout.write(JSON.stringify({ result }) + '\n');
const within = (p, ms) => Promise.race([p, new Promise((r) => setTimeout(() => r(null), ms))]);

const server = await startServer(dir, { images: library, include: [boot, 'utilities.img'] });
if (!server) {
    console.error('no go');
    process.exit(77);
}
const browser = await launch();
const page = await newPage(browser, { log: !!process.env.BENCH_LOG });
let crashed = false;
page.on('crash', () => { crashed = true; });
page.on('dialog', (d) => d.accept());
const query = new URLSearchParams([['disk', `disks/${boot}`], ['disk', 'disks/utilities.img'], ['system', boot],
                                   ...Object.entries(extra).map(([k, v]) => [k, String(v)])]);
await page.goto(`${server.url}/?${query}`);

const at = async (x, y) => {
    const b = await page.locator('#screen').boundingBox();
    return [b.x + x * b.width / 640, b.y + y * b.height / 480];
};
// Mac key codes -> the browser's (web/executor/keys.js maps them back)
const KEYS = { 36: 'Enter', 18: 'Digit1', 19: 'Digit2', 20: 'Digit3', 21: 'Digit4', 23: 'Digit5', 22: 'Digit6',
               11: 'KeyB', 14: 'KeyE', 45: 'KeyN', 8: 'KeyC', 4: 'KeyH', 53: 'Escape' };

const ops = {
    async ready(ms = 240000) {
        await firstFrame(page, ms);
        return !!await until(() => within(request(page, 'app'), 5000).then((a) => a === 'Finder'), ms, 500);
    },
    launch: (p) => within(request(page, 'launch', p), 60000),
    app: () => within(request(page, 'app'), 10000),
    screenshot: () => within(page.evaluate(async () => {
        const p = await executor.request('pixels');
        const c = new OffscreenCanvas(p.width, p.height);
        c.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(p.data), p.width, p.height), 0, 0);
        const bytes = new Uint8Array(await (await c.convertToBlob({ type: 'image/png' })).arrayBuffer());
        let bin = '';
        for (let i = 0; i < bytes.length; i += 0x8000)
            bin += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
        return btoa(bin);
    }), 15000),
    async mouse(x, y, down) {
        await page.mouse.move(...await at(x, y));
        if (down === true)
            await page.mouse.down();
        else if (down === false)
            await page.mouse.up();
        return true;
    },
    async key(code) {
        await page.locator('#screen').focus();
        await page.keyboard.press(KEYS[code]);
        return true;
    },
    async download(name, file) {
        const b64 = await within(page.evaluate(async (n) => {
            const blob = await executor.request('download', n);
            if (!blob)
                return null;
            const bytes = new Uint8Array(await blob.arrayBuffer());
            let bin = '';
            for (let i = 0; i < bytes.length; i += 0x8000)
                bin += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
            return btoa(bin);
        }, name), 120000);
        if (!b64)
            return false;
        fs.writeFileSync(file, Buffer.from(b64, 'base64'));
        return true;
    },
    alive: () => page.evaluate(() => !window.executor.stopped).then((ok) => ok && !crashed).catch(() => false),
    async quit() {
        await browser.close();
        server.stop();
        say(true);
        process.exit(0);
    },
};

for await (const line of readline.createInterface({ input: process.stdin })) {
    const [op, ...args] = JSON.parse(line);
    let result = null;
    try {
        result = await ops[op](...args);
    } catch (e) {
        console.error(`${op}: ${e.stack || e.message}`);
    }
    say(result);
}
await browser.close();
server.stop();
