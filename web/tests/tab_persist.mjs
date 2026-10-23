// Executor's own volume is kept in OPFS (worker.js): a file written on it
// survives a reload; Reset forgets it. Executor's own System, so no Apple
// software is needed.
//
//   node web/tests/tab_persist.mjs DIR
import { open, firstFrame, request, until } from './harness.mjs';

const [dir] = process.argv.slice(2);
const { page, close } = await open(dir, { log: false });
page.on('dialog', (d) => d.accept());
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};

await firstFrame(page);
await until(() => request(page, 'app').then((a) => a === 'Browser'), 20000);
await request(page, 'write', ['/executor/note.txt', 'kept across reloads']);
await page.waitForTimeout(3000);                       // saved every 2 s

await page.reload();
await firstFrame(page);
await until(() => request(page, 'app').then((a) => a === 'Browser'), 20000);
check(await request(page, 'read', '/executor/note.txt') === 'kept across reloads',
      'a file on Executor\'s volume survives a reload');
check(!(await request(page, 'list', '/executor')).includes('Shutdown Check'),
      'no Shutdown Check carried over');

await page.click('#reset');
await firstFrame(page);
await until(() => request(page, 'app').then((a) => a === 'Browser'), 20000);
check(await request(page, 'read', '/executor/note.txt') === null, 'Reset forgets it');

await close();
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
