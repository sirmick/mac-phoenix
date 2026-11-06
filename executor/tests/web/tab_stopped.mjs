// Executor stopped by an error, in the tab (M1.md #13): the page says so in
// its status line, with a Restart that brings it back, and stays alive. A
// fatal error of Executor's own (web_main.cpp's test-fatal request: a
// gui_fatal), then the wasm trapping (test-trap: a load out of bounds,
// which chrome-headless-shell turns into the renderer's SIGSEGV; harness.mjs
// runs a full Chromium).
//
//   node web/tests/tab_stopped.mjs DIR
//
// Needs playwright (harness.mjs).
import { open, firstFrame, until } from './harness.mjs';

const [dir] = process.argv.slice(2);
const { page, close } = await open(dir);
let crashed = false;
page.on('crash', () => { crashed = true; });
let ok = true;
const fail = (what) => { console.log(`FAIL: ${what}`); ok = false; };

// One stop: asked for by op, said in the page, then Restart.
async function stop(op, expect) {
    await firstFrame(page, 60000);
    page.evaluate(([op]) => { executor.request(op, 'tab_stopped'); }, [op]);
    const reason = await until(() => !crashed && page.evaluate(() => window.executor.stopped), 20000);
    if (crashed)
        return fail(`${op}: the renderer crashed`);
    if (!reason)
        return fail(`${op}: the page never said Executor stopped`);
    const status = await page.locator('#status').innerText();
    console.log(`${op}: ${status}`);
    if (!/Executor stopped/.test(status) || !expect.test(status))
        fail(`${op}: the status says ${JSON.stringify(status)}`);
    if (!(await page.locator('#restart-stopped').isVisible()))
        return fail(`${op}: no Restart`);
    await page.locator('#restart-stopped').click();
    await until(() => page.evaluate(() => window.executor.frames > 0 && !window.executor.stopped), 60000);
    if (await page.evaluate(() => window.executor.stopped || !window.executor.frames))
        fail(`${op}: Restart didn't bring Executor back`);
    else if (/Executor stopped/.test(await page.locator('#status').innerText()))
        fail(`${op}: the status still says it stopped`);
}

await stop('test-fatal', /asked for by the page \(tab_stopped\)/);
await stop('test-trap', /out of bounds/);
await close();
console.log(ok ? 'PASS' : 'FAIL');
process.exit(ok ? 0 : 1);
