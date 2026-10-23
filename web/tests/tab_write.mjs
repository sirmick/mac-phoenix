// Writable disk images in the tab (overlay.js): a folder the Finder makes
// on an uploaded image survives a reload, is in the image downloaded from
// the page (read back with hfsutils), and Revert takes it away; the
// uploaded image itself is never written.
//
//   node web/tests/tab_write.mjs DIR IMAGE
//
// IMAGE: an HFS image with a System Folder and a Finder (the tests'
// tests-7.5.5.img). Skips (77) without it, or without hfsutils' hls.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { open, firstFrame, request, until } from './harness.mjs';

const [dir, image] = process.argv.slice(2);
if (!image || !fs.existsSync(image)) {
    console.log(`SKIP: no System image (${image || 'none given'})`);
    process.exit(77);
}
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'tab-write-'));
function hls(img) {
    // hfsutils keeps its current volume in HOME: a private one.
    const env = { ...process.env, HOME: scratch };
    execFileSync('hmount', [img], { env, stdio: 'ignore' });
    const names = execFileSync('hls', ['-1a', ':'], { env }).toString('latin1').split('\n');
    execFileSync('humount', [], { env, stdio: 'ignore' });
    return names;
}
try {
    execFileSync('hls', ['--version'], { stdio: 'ignore' });
} catch (e) {
    if (e.code === 'ENOENT') {
        console.log('SKIP: no hfsutils');
        process.exit(77);
    }
}
const before = fs.statSync(image).mtimeMs;

const { page, close } = await open(dir, { log: false });
page.on('dialog', (d) => d.accept());
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const finder = () => until(() => request(page, 'app').then((a) => a === 'Finder'), 60000);
async function openDisk() {
    const box = await page.locator('#screen').boundingBox();
    const at = (x, y) => [box.x + x * box.width / 640, box.y + y * box.height / 480];
    await page.waitForTimeout(2000);
    for (const y of [155, 103, 207]) {
        await page.mouse.dblclick(...at(600, y));
        const w = await until(() => request(page, 'windows')
            .then((ws) => ws.find((w) => w.title && !['Desktop', 'executor', 'Host'].includes(w.title))), 6000);
        if (w)
            return w;
    }
    return null;
}
async function download(to) {
    const dl = page.waitForEvent('download');
    await page.locator('#files a[title^="Download"]').first().click();
    fs.copyFileSync(await (await dl).path(), to);
}

await firstFrame(page);
await page.setInputFiles('#add', image);
await firstFrame(page);
await page.selectOption('#system', path.basename(image));
await firstFrame(page, 60000);
check(await finder(), 'the Finder is up');
const disk = await openDisk();
check(!!disk, "the image's disk window");
await page.keyboard.press('Meta+n');                   // New Folder there
await page.waitForTimeout(1200);
await page.keyboard.press('Enter');
await page.waitForTimeout(3500);                       // the overlay's index saved

await page.reload();
await firstFrame(page, 60000);
check(await finder(), 'the Finder after a reload');
await download(path.join(scratch, 'changed.img'));
check(hls(path.join(scratch, 'changed.img')).includes('untitled folder'),
      'the downloaded image has the new folder');

await page.locator('#files a[title^="Revert"]').first().click();
await firstFrame(page, 60000);
check(await finder(), 'the Finder after Revert');
await download(path.join(scratch, 'reverted.img'));
check(!hls(path.join(scratch, 'reverted.img')).includes('untitled folder'), 'Revert took it away');
check(fs.statSync(image).mtimeMs === before, 'the uploaded image itself was never written');

await close();
fs.rmSync(scratch, { recursive: true, force: true });
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
