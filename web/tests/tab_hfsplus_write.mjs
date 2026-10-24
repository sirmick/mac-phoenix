// HFS+ written in the tab (docs/executor/HFSPlus.md): the blank volume with
// long-named files (blank-long-64m.img, the fixture) uploaded -- writable,
// through the page's overlay -- and VolWrite (tests/guest/hfsplus) run on
// it, then VolDump: both as Mac OS 9.0.4 wrote them for the same run, as
// natively (tests/test_hfsplus_write.py; the same differences set aside,
// and the application volwrite.py copies in, which the tab doesn't). With
// docker (the macphoenix-hfsprogs image): the image downloaded from the
// page passes fsck.hfsplus.
//
//   node web/tests/tab_hfsplus_write.mjs DIR GUEST_DIR FIXTURE_GZ
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { execFileSync } from 'node:child_process';
import { open, firstFrame, request, until } from './harness.mjs';

const [dir, guest, fixture] = process.argv.slice(2);
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'tab-hfsplus-write-'));
const image = path.join(scratch, 'Blank.img');
fs.writeFileSync(image, zlib.gunzipSync(fs.readFileSync(fixture)));
const oracle = (f) => fs.readFileSync(path.join(guest, 'hfsplus/oracle-9.0.4', f), 'latin1');

const lines = (t) => t.replace(/\r/g, '\n').split('\n').filter((l) => l.length);
/* test_hfsplus_write.py's normalize_log() */
const normalizeLog = (t) => lines(t).filter((l) => !l.startsWith('COPY ')).map((l) =>
    (l.startsWith('VOLUME') && l.includes('attrib') ? l.replace(/ (attrib|nmfls|frblk|filcnt|dircnt) \S+/g, '') : l)
        .replace(/(DIRCREATE Folder -> 0) id \d+/, '$1'));
/* ... and normalize_dump(): the Finder's own items (and the copied VolDump) out */
const hex = (s) => Buffer.from(s, 'latin1').toString('hex').toUpperCase();
const SKIP = new Set(['Desktop DB', 'Desktop DF', 'Desktop Folder', 'Trash', 'TheVolumeSettingsFolder',
                      'DesktopPrinters DB', 'Temporary Items', 'VolDump'].map(hex));
const UNDEFINED = new Set([hex('Moved file') + ' data', hex('Resource file') + ' rsrc']);
function normalizeDump(t) {
    const out = [];
    let skip = false, name = '';
    for (let l of lines(t)) {
        const m = l.match(/^(FILE|DIR ) ([0-9A-F]+|-) /);
        if (m) {
            name = m[2];
            skip = SKIP.has(name);
        } else if (!l.startsWith('  '))
            skip = false;
        const f = l.match(/^ {2}(data|rsrc) open/);
        if (f && UNDEFINED.has(name + ' ' + f[1]))
            l = l.replace(/ adler [0-9A-F]+/, ' adler -');
        if (skip || l.startsWith('FOLDER '))
            continue;
        if (l.startsWith('VOLUME'))
            l = l.replace(/ (attrib|nmfls|frblk|filcnt|dircnt|nxtcnid|cr|md) \S+/g, '');
        if (m && name === hex('Long'))               // natively it also holds the copied VolDump
            l = l.replace(/ valence \d+/, ' valence -');
        out.push(l.replace(/\b(id|par) \d+/g, '$1 N').replace(/ (cr|md) \d+/g, ' $1 T'));
    }
    return out;
}

const { page, close } = await open(dir);
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
function compare(what, want, got) {
    const diff = want.map((w, i) => [w, got[i]]).filter(([w, g]) => w !== g);
    for (const [w, g] of diff.slice(0, 8))
        console.log(`  9.0.4:    ${w}\n  the tab:  ${g}`);
    check(got.length === want.length && diff.length === 0,
          `${what} as 9.0.4's (${want.length} lines, ${diff.length} differ, ${got.length - want.length} more)`);
}
async function run(app, cfg, out) {
    await request(page, 'write', [`/host/${cfg}`, 'Blank\r']);
    check(await request(page, 'launch', `executor:Uploads:${app}.bin`) === 0, `launch ${app}`);
    return until(() => request(page, 'read', `/host/${out}`).then((t) => t && /(^|[\r\n])---/.test(t) && t), 120000);
}

await firstFrame(page);
await page.setInputFiles('#add', [image, path.join(guest, 'hfsplus/VolWrite.bin'), path.join(guest, 'hfsplus/VolDump.bin')]);
await firstFrame(page);
check(await until(() => request(page, 'app').then((a) => a === 'Browser'), 20000), 'Browser up');

const log = await run('VolWrite', 'volwrite.cfg', 'volwrite.txt');
check(!!log, 'VolWrite wrote its log');
if (log)
    compare('VolWrite', normalizeLog(oracle('volwrite.txt')), normalizeLog(log));
await page.waitForTimeout(1500);
const dump = await run('VolDump', 'voldump.cfg', 'voldump.txt');
check(!!dump, 'VolDump wrote its dump');
if (dump)
    compare('VolDump after', normalizeDump(oracle('voldump-written.txt')), normalizeDump(dump));

let docker = true;
try {
    execFileSync('docker', ['image', 'inspect', 'macphoenix-hfsprogs'], { stdio: 'ignore' });
} catch {
    docker = false;
}
if (docker) {
    await page.waitForTimeout(3500);                   // the overlay's index saved
    const dl = page.waitForEvent('download');
    await page.locator('#files a[title^="Download"]').first().click();
    const changed = path.join(scratch, 'changed.img');
    fs.copyFileSync(await (await dl).path(), changed);
    let ok = true, out = '';
    try {
        out = execFileSync('docker', ['run', '--rm', '-v', `${scratch}:/w`, 'macphoenix-hfsprogs', 'fsck.hfsplus',
                                      '-n', '-f', '/w/changed.img']).toString();
    } catch (e) {
        ok = false;
        out = String(e.stdout || e);
    }
    check(ok, 'fsck.hfsplus finds the volume the tab wrote OK' + (ok ? '' : `: ${out.slice(-300)}`));
} else
    console.log('(no fsck: docker or the macphoenix-hfsprogs image missing)');

await close();
fs.rmSync(scratch, { recursive: true, force: true });
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
