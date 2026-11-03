// The top bar's CPU meter (main.js CpuMeter): the emulator thread's busy
// share, from its waits (rsys/hostidle.h busy()), each half second.
//
//   node web/tests/tab_cpu.mjs DIR LIBRARY
//
// LIBRARY tools/web/make_library.py's output, with boot-7.6.1.img and
// games-68k.img (skips, 77, without them). Checks: the meter hidden while
// the tab loads; shown once the Mac is up, low at the idle Finder; high
// while a game runs (it doesn't wait for events); its % and tooltip;
// the memory beside it (the parts adding up, the Mac's RAM in the heap).
// CPU_APP=Name: the game (default SimEarth Color: its simulation runs on;
// Maelstrom's title screen waits). The top bar as it is: DIR/tab_cpu_bar.png.
import fs from 'node:fs';
import path from 'node:path';
import { launch, newPage, request as ask, until } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir, library] = process.argv.slice(2);
const boot = 'boot-7.6.1.img', disk = 'games-68k.img';
if (!library || ![boot, disk].every((f) => fs.existsSync(path.join(library, f)))) {
    console.log(`SKIP: no ${boot} and ${disk} in ${library || '(none given)'} (tools/web/make_library.py)`);
    process.exit(77);
}
const record = JSON.parse(fs.readFileSync(path.join(library, disk + '.json'), 'utf8'));
const game = record.launch.find((a) => a.name === (process.env.CPU_APP || 'SimEarth Color'));
const server = await startServer(dir, { images: library, include: [boot, disk] });
if (!server) {
    console.log('SKIP: no go');
    process.exit(77);
}
// A request, or null after ms (a game in a loop of its own answers late).
const request = (page, op, arg = '', ms = 15000) => Promise.race([
    ask(page, op, arg), new Promise((r) => setTimeout(() => r(null), ms))]);
const browser = await launch();
const page = await newPage(browser, { log: false });
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const meter = () => page.evaluate(() => ({
    hidden: document.getElementById('cpu').hidden,
    pct: document.getElementById('cpu-pct').textContent,
    mem: document.getElementById('mem').textContent,
    memTitle: document.getElementById('mem').title,
    title: document.getElementById('cpu').title,
    frames: window.executor.frames,
    cpu: window.executor.cpu,
}));
// The busy share over a few seconds: the mean of the half-second samples.
async function busyOver(ms) {
    await page.waitForTimeout(1000);                 // the last window's start passed
    const before = (await meter()).cpu.history.length;
    await page.waitForTimeout(ms);
    const h = (await meter()).cpu.history;
    const got = h.slice(-Math.max(1, Math.min(h.length, Math.round(ms / 500))));
    return got.reduce((a, b) => a + b, 0) / got.length;
}

try {
    await page.goto(`${server.url}/?disk=disks/${boot}&disk=disks/${disk}&system=${boot}`);
    // Loading: no meter until the first frame.
    let shownEarly = false;
    await until(async () => {
        const m = await meter();
        shownEarly ||= !m.hidden && m.frames === 0;
        return m.frames > 0;
    }, 180000, 200);
    check(!shownEarly, 'hidden while loading');
    check(await until(() => request(page, 'app').then((a) => a === 'Finder'), 120000), 'the Finder is up');
    check(await until(async () => !(await meter()).hidden, 10000), 'shown once the Mac is up');
    await page.waitForTimeout(5000);                  // the Finder settled
    const idle = await busyOver(4000);
    const m = await meter();
    console.log(`idle Finder: ${(idle * 100).toFixed(0)}% (shown ${m.pct}; history ${m.cpu.history.map((v) => Math.round(v * 100)).join(' ')})`);
    check(idle < 0.3, 'low at the idle Finder');
    check(/^\d+%$/.test(m.pct), `its % (${m.pct})`);
    check(/Mac CPU busy/.test(m.title), 'its tooltip');
    // Memory beside it: the parts add up, the Mac's RAM in the heap (MemTop:
    // most of the 128 MB on Apple's System), the library disks' pieces cached, the System
    // Folder in Executor's volume.
    const u = m.cpu.memory, mb = (n) => Math.round(n / (1 << 20));
    console.log(`memory: ${m.mem} (heap ${mb(u.heap)}, RAM ${mb(u.ram)}, caches ${mb(u.caches)}, changes ${mb(u.changes)}, volume ${mb(u.volume)} MB)`);
    check(/^\d+ MB$|^\d+\.\d GB$/.test(m.mem), `its memory (${m.mem})`);
    check(u.total === u.heap + u.caches + u.changes + u.volume, 'the parts add up');
    check(u.ram > 100 << 20 && u.ram <= 128 << 20 && u.heap > u.ram, "the Mac's RAM (MemTop, of 128 MB) inside the heap");
    check(u.caches > 0 && u.volume > 0, 'disk caches and the volume counted');
    check(/Mac's RAM/.test(m.memTitle) && /disk caches/.test(m.memTitle) && /Executor's volume/.test(m.memTitle)
          && /estimate/.test(m.memTitle), 'the memory tooltip: the parts, an estimate');
    // The browser's own figure where it measures (cross-origin isolated):
    // then in the tooltip too.
    const iso = await page.evaluate(() => window.crossOriginIsolated && !!performance.measureUserAgentSpecificMemory);
    if (iso) {
        await until(async () => (await meter()).cpu.measured > 0, 30000, 500);
        const w = await meter();
        console.log(`the browser's measurement: ${mb(w.cpu.measured)} MB`);
        check(w.cpu.measured > 0 && /browser's own measurement/.test(w.memTitle), "the browser's measurement in the tooltip");
    } else
        console.log("(no performance.measureUserAgentSpecificMemory here: not cross-origin isolated)");

    const err = await request(page, 'launch', game.app, 60000);
    check(err === 0 || err === '0', `${game.name} launched (${err})`);
    await until(() => request(page, 'app', '', 5000).then((a) => a === game.name || a === null), 60000, 500);
    await page.waitForTimeout(5000);
    const busy = await busyOver(4000);
    const g = await meter();
    console.log(`${game.name}: ${(busy * 100).toFixed(0)}% (history ${g.cpu.history.map((v) => Math.round(v * 100)).join(' ')})`);
    check(busy > 0.6, `high while ${game.name} runs`);
    await page.locator('#bar').screenshot({ path: path.join(dir, 'tab_cpu_bar.png') });
} finally {
    await browser.close();
    server.stop();
}
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
