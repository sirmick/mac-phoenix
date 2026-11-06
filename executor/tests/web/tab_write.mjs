// Writable disk images in the tab (overlay.js): a folder the Finder makes
// on a library disk survives a reload (its changes kept by the server:
// sync.js, Store), is in the image downloaded from the page (read back
// with hfsutils), and Revert takes it away; the image itself is never
// written. The new folder's window scroll (frScroll, set with the rest of
// its Finder info) is the Finder's (-8,-16), as on the Quadra (7.6.1):
// Executor's PBSetCatInfo wrote a folder's dates there, and every library
// disk's window opened scrolled far from its icons.
//
//   node web/tests/tab_write.mjs DIR IMAGE
//
// IMAGE: an HFS image with a System Folder and a Finder (the tests'
// tests-7.5.5.img). Skips (77) without it, or without hfsutils' hls.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { open, firstFrame, request, until, pickSystem, diskButton } from './harness.mjs';

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
// A folder's frScroll, by tools/diskaudit's HFS reader.
const audit = path.join(path.dirname(new URL(import.meta.url).pathname), '..', '..', 'tools', 'diskaudit');
const folderScroll = (img, folder) => execFileSync('python3', ['-c',
    'import sys; sys.path.insert(0, sys.argv[1]); from disk_audit import Volume\n'
    + 'print([n.scroll for n in Volume(sys.argv[2]).nodes.values() if n.is_dir and n.name == sys.argv[3]])',
    audit, img, folder]).toString().trim();
try {
    execFileSync('hls', ['--version'], { stdio: 'ignore' });
} catch (e) {
    if (e.code === 'ENOENT') {
        console.log('SKIP: no hfsutils');
        process.exit(77);
    }
}
const before = fs.statSync(image).mtimeMs;
const name = path.basename(image);
const lib = path.join(scratch, 'library');
fs.mkdirSync(lib);
fs.symlinkSync(path.resolve(image), path.join(lib, name));
fs.writeFileSync(path.join(lib, `${name}.json`), JSON.stringify({ title: name }));

const { page, close } = await open(dir, { log: false, images: lib, include: [name],
                                          query: `disk=disks/${encodeURIComponent(name)}&system=${encodeURIComponent(name)}&size=640x480` });
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
    await page.waitForTimeout(6000);                   // the Finder's startup over: clicks land
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
    await diskButton(page, path.basename(image), 'download');
    fs.copyFileSync(await (await dl).path(), to);
}

await firstFrame(page, 60000);
check(await finder(), 'the Finder is up');
const disk = await openDisk();
check(!!disk, "the image's disk window");
await page.keyboard.press('Meta+n');                   // New Folder there
await page.waitForTimeout(1200);
await page.keyboard.press('Enter');
await page.waitForTimeout(3500);                       // the changes on the server

await page.reload();
await firstFrame(page, 60000);
check(await finder(), 'the Finder after a reload');
await download(path.join(scratch, 'changed.img'));
check(hls(path.join(scratch, 'changed.img')).includes('untitled folder'),
      'the downloaded image has the new folder');
{
    const scroll = folderScroll(path.join(scratch, 'changed.img'), 'untitled folder');
    check(scroll === '[(-8, -16)]', `the new folder's window scroll is the Finder's (-8, -16) (${scroll})`);
}

await diskButton(page, path.basename(image), 'revert');
await firstFrame(page, 60000);
check(await finder(), 'the Finder after Revert');
await download(path.join(scratch, 'reverted.img'));
check(!hls(path.join(scratch, 'reverted.img')).includes('untitled folder'), 'Revert took it away');
check(fs.statSync(image).mtimeMs === before, 'the image itself was never written');

await close();
fs.rmSync(scratch, { recursive: true, force: true });
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
