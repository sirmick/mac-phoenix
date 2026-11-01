// Two tabs on the same disks (overlay.js, worker.js): the second finds the
// first's Worker holding their OPFS files and runs on copies in memory,
// writable, with a notice, while the first, in use, keeps them; in the
// background, the first lets them go when the second starts again, and
// keeps on in memory itself.
//
//   node web/tests/tab_two_tabs.mjs DIR IMAGE
//
// IMAGE: an HFS image with a System Folder and a Finder (the tests'
// tests-7.5.5.img). Skips (77) without it.
import fs from 'node:fs';
import path from 'node:path';
import { launch, serve, firstFrame, request, until } from './harness.mjs';

const [dir, image] = process.argv.slice(2);
if (!image || !fs.existsSync(image)) {
    console.log(`SKIP: no System image (${image || 'none given'})`);
    process.exit(77);
}
const name = path.basename(image);
const server = serve(dir);
const url = `http://127.0.0.1:${server.address().port}/index.html`;
const browser = await launch();
const context = await browser.newContext({ viewport: { width: 800, height: 640 } });
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
// A tab and what its Worker printed.
async function tab(tag) {
    const page = await context.newPage();
    const lines = [];
    page.on('pageerror', (e) => console.log(`[page error ${tag}]`, e.stack || e.message));
    page.on('dialog', (d) => d.accept());
    page.on('worker', (w) => w.on('console', (m) => lines.push(m.text())));
    await page.goto(url);
    return { page, lines, saw: (re) => until(() => lines.some((l) => re.test(l)), 15000) };
}
const finder = (page) => until(() => request(page, 'app').then((a) => a === 'Finder'), 60000);
const title = (page) => page.evaluate((n) => [...document.querySelectorAll('#files span')]
    .find((s) => s.textContent.startsWith(n))?.title || '', name);

const one = await tab('1');
await firstFrame(one.page);
await one.page.setInputFiles('#add', image);
await firstFrame(one.page);
await one.page.selectOption('#system', name);
await firstFrame(one.page, 60000);
check(await finder(one.page), 'tab 1: the Finder is up');
check(!/another tab/.test(await title(one.page)), 'tab 1 has the disk');

const two = await tab('2');
await firstFrame(two.page, 60000);
check(await finder(two.page), 'tab 2: the Finder is up');
check(await one.saw(/Another tab wants/), 'tab 1, in use, was asked and keeps the disk');
check(await two.saw(/Another tab has .*: changes to it here last/), 'tab 2 says another tab has the disk');
check(/open in another tab/.test(await title(two.page)), "tab 2's disk is a copy in memory");
check(!two.lines.some((l) => /read-only/.test(l)), 'tab 2: no disk read-only');
const shots = process.env.SHOTS || dir;
check(/Another tab has/.test(await two.page.locator('#status').textContent()), "tab 2's page shows the notice");
await two.page.waitForTimeout(2000);
await two.page.screenshot({ path: path.join(shots, 'tab_two_tabs_copy.png') });

// Tab 1 in the background; tab 2 starts again and gets the disk.
await one.page.evaluate(() => worker.postMessage({ type: 'visibility', hidden: true }));
two.lines.length = 0;
await two.page.reload();
await firstFrame(two.page, 60000);
check(await finder(two.page), 'tab 2 again: the Finder is up');
check(await one.saw(/Another tab has .* now/), 'tab 1, in the background, let the disk go');
check(!/another tab/.test(await title(two.page)), 'tab 2 has the disk now');
check(await until(async () => /open in another tab/.test(await title(one.page)), 5000),
      "tab 1's disk is a copy in memory now");
check(await request(one.page, 'app') === 'Finder', 'tab 1 still runs');
await one.page.screenshot({ path: path.join(shots, 'tab_two_tabs_let_go.png') });

await browser.close();
server.close();
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
