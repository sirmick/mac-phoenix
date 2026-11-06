// The page's watch (main.js): Executor answering no request is busy while
// it still reports its load, not "not answering" (8.6's Finder starting
// makes no event call for 15 s on a slower machine, and the owner's log
// said it had hung); the Worker gone is "not answering". Requests left
// unanswered by the page itself ('app' never resolves), on Executor's own
// System.
//
//   node executor/tests/web/tab_watch.mjs DIR
//
// Needs playwright (harness.mjs).
import { open, firstFrame, until } from './harness.mjs';

const [dir] = process.argv.slice(2);
const { page, close } = await open(dir, { log: false });
const watch = [];
page.on('console', (m) => /^\[watch\] Executor/.test(m.text()) && watch.push(m.text()));
let ok = true;
const check = (good, what) => {
    console.log(`${good ? 'PASS' : 'FAIL'} ${what}`);
    ok &&= good;
};

try {
    await firstFrame(page, 60000);
    await page.evaluate(() => {
        const request = window.executor.request;
        window.executor.request = (op, arg) => op === 'app' ? new Promise(() => {}) : request(op, arg);
    });
    // Three unanswered, 3 s apart: busy, the Worker still reporting its load.
    await until(() => watch.length > 0, 20000);
    check(watch.length === 1 && /busy: no event call/.test(watch[0]), `running: busy ${JSON.stringify(watch)}`);
    // The Worker gone: no load reported.
    await page.evaluate(() => worker.terminate());
    await until(() => watch.some((w) => /not answering/.test(w)), 20000);
    check(watch.some((w) => /not answering/.test(w)), `the Worker gone: not answering ${JSON.stringify(watch)}`);
} finally {
    await close();
}
process.exit(ok ? 0 : 1);
