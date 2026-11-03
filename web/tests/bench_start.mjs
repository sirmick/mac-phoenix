// Where the tab's start goes on tools/standalone (not a ctest: a
// measurement, docs/executor/Standalone.md "Start-up costs"): a copy of a
// server's data directory (its settings: the System, the disks mounted;
// its saved volumes), the tab loaded RUNS times, the Worker's milestones
// timed from the navigation: the module, Executor's disk restored (the
// Worker's "[web] restored" line), the disks' changes, main, the Finder.
//
//   node web/tests/bench_start.mjs SITE DATA IMAGES [RUNS] [--slow]
//
// SITE: the page (tools/web/make_site.sh's output); DATA: copied to
// scratch each run (TMPDIR), never written; IMAGES: the library (seen
// read-only, server.mjs). --slow: 50 Mbit/s with 20 ms of latency
// (Chromium's network emulation, the Worker's requests included).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { launch, newPage, request, until } from './harness.mjs';
import { startServer } from './server.mjs';

const args = process.argv.slice(2);
const slow = args.includes('--slow');
const [site, data, images, runs = '3'] = args.filter((a) => !a.startsWith('--'));
const browser = await launch();
const rows = [];
for (let run = 0; run < Number(runs); run++) {
    const scratch = fs.mkdtempSync(path.join(process.env.TMPDIR || os.tmpdir(), 'bench-start-'));
    const copy = path.join(scratch, 'data');
    execFileSync('cp', ['-r', data, copy]);
    fs.rmSync(path.join(copy, 'logs'), { recursive: true, force: true });
    const server = await startServer(site, { images, data: copy });
    const page = await newPage(browser, { log: !!process.env.BENCH_LOG });
    if (process.env.BENCH_LOG)
        page.on('console', (m) => console.log('[page]', m.text()));
    if (process.env.BENCH_LOG)
        page.on('response', (r) => console.log('[response]', r.status(), r.url()));
    if (slow) {
        const cdp = await page.context().newCDPSession(page);
        await cdp.send('Network.emulateNetworkConditions', {
            offline: false, latency: 20, downloadThroughput: 50e6 / 8, uploadThroughput: 20e6 / 8 });
    }
    const marks = {};
    let t0 = 0;
    const mark = (name) => { if (!(name in marks)) marks[name] = Date.now() - t0; };
    const trace = [];
    page.on('worker', (w) => w.on('console', (m) => {
        const t = m.text();
        trace.push(`${Date.now() - t0} ${t.slice(0, 160)}`);
        if (/^\[web\] restored/.test(t)) {
            mark('volume');
            marks.volumeLine = t;
        } else if (/^\[web\] Executor's disk: \d+ files/.test(t))
            marks.detail = t.replace(/^\[web\] Executor's disk: /, '');
        else if (/^\[web\] (the saved System Folder|System Folder from|no System Folder)/.test(t))
            mark('system');
    }));
    page.on('requestfinished', (r) => {
        const tm = r.timing();
        trace.push(`${Date.now() - t0} [request] ${r.method()} ${r.url().replace(server.url, '')} `
                   + `(${Math.round(tm.responseEnd)} ms)`);
        if (/api\/sync\/volumes\//.test(r.url()))
            marks.key = r.url().replace(/.*volumes\//, '');
        if (/executor-web\.wasm/.test(r.url()))
            mark('module');
    });
    t0 = Date.now();
    await page.goto(`${server.url}/`);
    await page.waitForFunction(() => window.executor && window.executor.frames > 0, null, { timeout: 300000 });
    mark('frame');
    const finder = await until(() => request(page, 'app').then((a) => a === 'Finder').catch(() => false), 300000, 100);
    mark(finder ? 'finder' : 'noFinder');
    const stats = await request(page, 'stats').catch(() => ({}));
    rows.push({ run, ...marks, remoteRequests: stats.remoteRequests });
    console.log(JSON.stringify(rows.at(-1)));
    if (process.env.BENCH_TRACE)
        fs.writeFileSync(`${process.env.BENCH_TRACE}.${run}`, trace.join('\n') + '\n');
    await page.context().close();
    server.stop();
    fs.rmSync(scratch, { recursive: true, force: true, maxRetries: 5 });
}
await browser.close();
