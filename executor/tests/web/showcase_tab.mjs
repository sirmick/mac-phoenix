// The showcase (executor/SHOWCASE.md) in the tab: the standalone server with
// a library of Apple's System 7.5.3 image and a disk of applications; each
// application launched at 2x, a screenshot of the tab each.
//
//   node executor/tests/web/showcase_tab.mjs DIR IMAGES SHOTS SYSTEM APP...
//
// DIR the wasm build's executor-web output, IMAGES the library, SYSTEM a
// library image's name ('' for Executor's own), APP "name|Mac path".
import fs from 'node:fs';
import path from 'node:path';
import { launch, newPage, firstFrame, request, until } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir, images, shots, system, ...apps] = process.argv.slice(2);
fs.mkdirSync(shots, { recursive: true });
const server = await startServer(dir, { images });
if (!server) {
    console.log('SKIP: no go');
    process.exit(77);
}
const browser = await launch({ dpr: 2 });
let failed = 0;
const page = await newPage(browser, { log: false, scale: 2 });   /* one page: a second would only watch */
for (const app of apps) {
    const [name, mac] = app.split('|');
    const sys = system ? `&disk=disks/${encodeURIComponent(system)}&system=${encodeURIComponent(system)}` : '';
    await page.goto(`${server.url}/?disk=disks/showcase.img${sys}&scale=2&size=800x600`);
    await firstFrame(page, 120000);
    const front = system ? 'Finder' : 'Browser';
    const up = await until(() => request(page, 'app').then((a) => a === front), 90000);
    const r = up ? await request(page, 'launch', mac) : 'no ' + front;
    const ok = await until(() => request(page, 'app').then((a) => a && a !== front && a), 30000);
    await page.waitForTimeout(6000);
    await page.locator('#screen').screenshot({ path: path.join(shots, `${name}.png`) });
    console.log(`${ok ? 'PASS' : 'FAIL'} ${name}: launch ${r}, front ${ok}`);
    failed += !ok;
}
await browser.close();
server.stop && await server.stop();
process.exit(failed ? 1 : 0);
