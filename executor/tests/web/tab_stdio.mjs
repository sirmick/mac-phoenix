// A file an application writes across WaitNextEvent, in the tab on Mac OS
// 8.6 (the library's boot-8.6.img, PowerPC): StdioWait (tests/guest/stdiowait)
// writes a line, waits three seconds in WaitNextEvent, writes fifty more and
// DONE, closes the file and quits; the image downloaded from the page has
// all of it. Launched as soon as the Finder is up, as MacPhoenixBench is
// (tools/bench/run.py): the Finder, given time in the wait, loaded
// libraries that import StdCLib, Executor's StdCLib started afresh under
// the application, and the file kept only START.
//
//   node executor/tests/web/tab_stdio.mjs DIR LIBRARY
//
// LIBRARY: tools/web/make_library.py's (boot-8.6.img). Skips (77) without it,
// or without hfsutils.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { open, firstFrame, request, until } from './harness.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const APP = path.join(ROOT, 'tests/guest/stdiowait/StdioWait.bin');
const [dir, library] = process.argv.slice(2);
const boot = 'boot-8.6.img';
if (!library || !fs.existsSync(path.join(library, boot))) {
    console.log(`SKIP: no ${boot} in ${library || '(no library given)'}`);
    process.exit(77);
}
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'tab-stdio-'));
const env = { ...process.env, HOME: scratch };         // hfsutils' current volume
const hfs = (...args) => execFileSync(args[0], args.slice(1), { env, stdio: ['ignore', 'pipe', 'ignore'] });
try {
    execFileSync('hformat', ['--version'], { stdio: 'ignore' });
} catch (e) {
    if (e.code === 'ENOENT') {
        console.log('SKIP: no hfsutils');
        process.exit(77);
    }
}

// The library: the System's disk, and a fresh one with StdioWait.
const lib = path.join(scratch, 'library');
fs.mkdirSync(lib);
for (const f of [boot, `${boot}.json`])
    fs.symlinkSync(path.join(path.resolve(library), f), path.join(lib, f));
const disk = 'stdio.img';
fs.writeFileSync(path.join(lib, disk), Buffer.alloc(4 * 1024 * 1024));
hfs('hformat', '-l', 'Stdio', path.join(lib, disk));
hfs('hmount', path.join(lib, disk));
hfs('hcopy', '-m', APP, ':StdioWait');
hfs('humount');
fs.writeFileSync(path.join(lib, `${disk}.json`), JSON.stringify({ title: 'Stdio', label: 'stdio' }));

const query = new URLSearchParams([['disk', `disks/${boot}`], ['disk', `disks/${disk}`], ['system', boot],
                                   ['ppc', '1'], ['depth', '8']]);
const { page, close } = await open(dir, { log: !!process.env.TAB_LOG, images: lib, include: [boot, disk],
                                          query: query.toString() });
page.on('dialog', (d) => d.accept());
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    if (!ok)
        failed++;
};
const within = (p, ms) => Promise.race([p, new Promise((r) => setTimeout(() => r(null), ms))]);
const app = () => within(request(page, 'app'), 5000);

try {
    await firstFrame(page, 240000);
    check(!!await until(async () => await app() === 'Finder', 240000, 500), 'the Finder is up');
    check(await within(request(page, 'launch', 'Stdio:StdioWait'), 60000) === 0, 'StdioWait launched');
    check(!!await until(async () => await app() === 'StdioWait', 20000, 250), 'StdioWait in front');
    // Gone: the Finder in front three looks running.
    let looks = 0;
    await until(async () => (looks = await app() === 'Finder' ? looks + 1 : 0) >= 3, 60000, 1000);
    check(looks >= 3, 'StdioWait quit');

    const blob = await within(page.evaluate(async (n) => {
        const b = await executor.request('download', n);
        if (!b)
            return null;
        const bytes = new Uint8Array(await b.arrayBuffer());
        let bin = '';
        for (let i = 0; i < bytes.length; i += 0x8000)
            bin += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
        return btoa(bin);
    }, disk), 60000);
    check(!!blob, 'the disk downloaded');
    let text = '';
    if (blob) {
        const out = path.join(scratch, 'out.img');
        fs.writeFileSync(out, Buffer.from(blob, 'base64'));
        const txt = path.join(scratch, 'stdiowait.txt');
        try {
            hfs('hmount', out);
            hfs('hcopy', '-r', ':stdiowait.txt', txt);
            hfs('humount');
            text = fs.readFileSync(txt, 'latin1');
        } catch (e) {
            console.log(`hfsutils: ${e.message}`);
        }
    }
    const lines = text.split(/\r|\n/).filter(Boolean);
    console.log(`stdiowait.txt: ${text.length} bytes, ${lines.length} lines, last ${JSON.stringify(lines.at(-1) || '')}`);
    check(lines[0] === 'START', 'the line before the wait is in the file');
    check(lines.length === 52 && lines[50] === 'LINE 50' && lines[51] === 'DONE',
          'the lines after the wait and DONE are in the file');
} catch (e) {
    console.log(`FAIL ${e.stack || e.message}`);
    failed++;
} finally {
    await close();
    fs.rmSync(scratch, { recursive: true, force: true });
}
console.log(failed ? `${failed} failed` : 'all passed');
process.exit(failed ? 1 : 0);
