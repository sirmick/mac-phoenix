// W3's check (docs/executor/DIRECTION.md): Playwright drives uploaded
// applications in the tab.
//
//   node web/tests/tab_apps.mjs DIR GUEST_DIR
//
// DIR is the wasm build's executor-web output, GUEST_DIR tests/guest. Both
// applications are uploaded through the page's file input (kept in OPFS),
// launched from the Browser with the 'launch' request (a process switch),
// and report through Executor's Host volume:
//   PPCHello         PowerPC code (mame-ppc's C back-end), its PASS lines
//   MacPhoenixSuite  its keyboard section: the test types "hello 123" and
//                    Return on the canvas and reads what the guest got
import path from 'node:path';
import { open, firstFrame, request, until } from './harness.mjs';

const [dir, guest] = process.argv.slice(2);
const { page, close } = await open(dir);
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};

await firstFrame(page);
await page.setInputFiles('#add', [path.join(guest, 'ppc-hello/PPCHello.bin'),
                                  path.join(guest, 'suite/MacPhoenixSuite.bin')]);
await firstFrame(page);   // restarted with the uploads
check(await until(() => request(page, 'app').then((a) => a === 'Browser'), 20000), 'Browser up');

// PPCHello: PowerPC in the tab.
check(await request(page, 'launch', 'executor:Uploads:PPCHello.bin') === 0, 'launch PPCHello');
const ppc = await until(() => request(page, 'read', '/host/ppc_hello.txt')
                                  .then((t) => t && t.includes('---') && t), 30000);
console.log(JSON.stringify(ppc));
check(ppc && !/FAIL/.test(ppc) && (ppc.match(/PASS/g) || []).length >= 7, 'PPCHello reports all PASS');

// MacPhoenixSuite's keyboard section, typed on the canvas.
await request(page, 'write', ['/host/suite.cfg', 'suite_results.txt\rkeyboard\r'  /* Mac lines: MPW C's fgets ends at CR */]);
check(await request(page, 'launch', 'executor:Uploads:MacPhoenixSuite.bin') === 0, 'launch MacPhoenixSuite');
check(await until(() => request(page, 'read', '/host/test_keyboard_ready.txt'), 30000), 'suite ready for keys');
check(await until(() => request(page, 'windows').then((w) => w.some((x) => x.title === 'Keyboard test')), 10000),
      'its "Keyboard test" window is up');
await page.locator('#screen').click({ position: { x: 200, y: 100 } });
await page.keyboard.type('hello 123', { delay: 60 });
await page.keyboard.press('Enter');
const recv = await until(() => request(page, 'read', '/host/test_keyboard_recv.txt'), 20000);
const want = [...'hello 123'].map((c) => c.charCodeAt(0)).join(',');
console.log(`received: ${recv && recv.trim()}  wanted: ${want}`);
check(recv && recv.trim() === want, 'the guest got the keys typed in the tab');
const results = await until(() => request(page, 'read', '/host/suite_results.txt')
                                       .then((t) => t && t.includes('---') && t), 20000);
console.log(JSON.stringify(results));
check(results && /PASS keyboard_recv/.test(results), 'the suite reports keyboard_recv PASS');

// Sound: the suite's audio section (SysBeep, played through the Sound
// Manager) reaches the page as audio frames.
const before = await page.evaluate(() => executor.audioFrames);
await request(page, 'write', ['/host/suite.cfg', 'audio_results.txt\raudio\r']);
check(await request(page, 'launch', 'executor:Uploads:MacPhoenixSuite.bin') === 0, 'launch MacPhoenixSuite (audio)');
const audio = await until(() => request(page, 'read', '/host/audio_results.txt')
                                    .then((t) => t && t.includes('---') && t), 30000);
console.log(JSON.stringify(audio));
const frames = await page.evaluate(() => executor.audioFrames) - before;
console.log(`audio frames: ${frames}`);
check(audio && /PASS audio_sysbeep/.test(audio) && frames > 10, 'SysBeep plays in the tab');

await page.locator('#screen').screenshot({ path: path.join(dir, 'tab_apps.png') });
await close();
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
