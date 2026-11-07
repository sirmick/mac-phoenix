// executor/server: a large library disk mounts before it has downloaded.
// The tab reads it by Range requests as Executor needs it (overlay.js:
// RemoteImage); the Finder runs on its System having read a fraction of
// it, and again after a reload (the server is the store: nothing kept in
// the browser, so a fraction again).
//
//   node executor/tests/web/tab_server_lazy.mjs DIR IMAGE
//
// IMAGE: an HFS image with a System Folder and a Finder, and its record
// (IMAGE.json; tests-7.5.5.img). Skips (77) without them.
import fs from 'node:fs';
import path from 'node:path';
import { launch, newPage, firstFrame, request, until } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir, image] = process.argv.slice(2);
if (!image || !fs.existsSync(image) || !fs.existsSync(`${image}.json`)) {
    console.log(`SKIP: no System image with a record (${image || 'none given'})`);
    process.exit(77);
}
const name = path.basename(image);
const size = fs.statSync(image).size;
const server = await startServer(dir, { images: path.dirname(image), include: [name] });
if (!server) {
    console.log('SKIP: no go');
    process.exit(77);
}
const browser = await launch();
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const finder = (page) => until(() => request(page, 'app').then((a) => a === 'Finder'), 90000);
const mb = (n) => `${(n / 1e6).toFixed(1)} MB`;

try {
    const page = await newPage(browser, { log: false });
    await page.goto(`${server.url}/?disk=disks/${encodeURIComponent(name)}&system=${encodeURIComponent(name)}`);
    const lib = await page.evaluate(() => fetch('library.json').then((r) => r.json()));
    check(lib.length === 1 && lib[0].url === `disks/${encodeURIComponent(name)}`, 'the library has the image, from its record');
    await firstFrame(page, 60000);
    check(await finder(page), `the Finder on ${name}'s System`);
    const first = await request(page, 'stats');
    console.log(`  read ${mb(first.remoteBytes)} of ${mb(size)} in ${first.remoteRequests} requests`);
    check(first.remoteBytes > 0 && first.remoteBytes < size / 4, 'only a fraction of the image downloaded');

    await page.reload();
    await firstFrame(page, 60000);
    check(await finder(page), 'the Finder after a reload');
    const again = await request(page, 'stats');
    console.log(`  read ${mb(again.remoteBytes)} in ${again.remoteRequests} requests`);
    check(again.remoteBytes > 0 && again.remoteBytes < size / 4, '... again a fraction of it');
} finally {
    await browser.close();
    server.stop();
}
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
