// HFS+ in the tab (docs/executor/HFSPlus.md): the long-name fixture (an
// HFS+ volume made on Mac OS 9.0.4) uploaded as a disk image, mounted from
// the page's files (WORKERFS), and VolDump (tests/guest/hfsplus) run on it:
// its dump must equal the one 9.0.4 wrote, as natively
// (tests/test_hfsplus_volume.py, the same differences set aside).
//
//   node web/tests/tab_hfsplus.mjs DIR GUEST_DIR FIXTURE_GZ
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { open, firstFrame, request, until } from './harness.mjs';

const [dir, guest, fixture] = process.argv.slice(2);
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'tab-hfsplus-'));
const image = path.join(scratch, 'longnames.img');
fs.writeFileSync(image, zlib.gunzipSync(fs.readFileSync(fixture)));
const oracle = fs.readFileSync(path.join(guest, 'hfsplus/oracle-9.0.4/voldump-longnames.txt'), 'latin1');

/* tests/test_hfsplus_volume.py's normalize(): what depends on the machine */
function normalize(text) {
    const lines = text.replace(/\r/g, '\n').split('\n').filter((l) => l.length).map((l) => l
        .replace(/^(VOLUME .* attrib )([0-9A-F]{4})/, (m, a, b) => a + (parseInt(b, 16) & ~0x60).toString(16).toUpperCase().padStart(4, '0'))
        .replace(/^(FILE \S+ id \d+ par \d+ attrib )([0-9A-F]{2})/, (m, a, b) => a + (parseInt(b, 16) & ~0x8C).toString(16).toUpperCase().padStart(2, '0'))
        .replace(/ write (-?\d+)\/-?\d+/g, ' write $1'));
    return lines;
}

const { page, close } = await open(dir);
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};

await firstFrame(page);
await page.setInputFiles('#add', [image, path.join(guest, 'hfsplus/VolDump.bin')]);
await firstFrame(page);   // restarted with the uploads
check(await until(() => request(page, 'app').then((a) => a === 'Browser'), 20000), 'Browser up');

await request(page, 'write', ['/host/voldump.cfg', 'LongNames\r']);
check(await request(page, 'launch', 'executor:Uploads:VolDump.bin') === 0, 'launch VolDump');
const dump = await until(() => request(page, 'read', '/host/voldump.txt')
                                   .then((t) => t && /(^|[\r\n])---/.test(t) && t), 60000);
check(!!dump, 'VolDump wrote its dump');
if (dump) {
    const want = normalize(oracle), got = normalize(dump);
    for (let i = 0; i < want.length; i++)       // forks 9.0.4's Finder held open for writing
        if (want[i].includes(' rdwr -49') && got[i]) {
            want[i] = want[i].split(' rdwr ')[0];
            got[i] = got[i].split(' rdwr ')[0];
        }
    const diff = want.map((w, i) => [w, got[i]]).filter(([w, g]) => w !== g);
    for (const [w, g] of diff.slice(0, 10))
        console.log(`  9.0.4:    ${w}\n  the tab:  ${g}`);
    check(got.length === want.length && diff.length === 0,
          `the dump equals 9.0.4's (${want.length} lines, ${diff.length} differ)`);
}

await close();
fs.rmSync(scratch, { recursive: true, force: true });
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
