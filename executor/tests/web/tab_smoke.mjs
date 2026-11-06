// W2's check (docs/executor/DIRECTION.md): Executor's built-in Browser in a
// headless Chromium tab. Serves DIR (the wasm build's executor-web output),
// loads the page, waits for frames, screenshots the canvas, checks the
// guest's cursor became the canvas's CSS cursor, then presses on the File
// menu and checks the screen changed (input reaches the guest while it
// tracks the menu).
//
//   node web/tests/tab_smoke.mjs DIR OUT.png [SECONDS]
//
// Needs playwright (harness.mjs).
import { open } from './harness.mjs';

const [dir, out, secs = '60'] = process.argv.slice(2);
const { page, close } = await open(dir);

const start = Date.now();
const deadline = start + Number(secs) * 1000;
let frames = 0;
while (Date.now() < deadline && !(frames = await page.evaluate(() => window.executor ? window.executor.frames : 0)))
    await page.waitForTimeout(100);
console.log(`first frame after ${((Date.now() - start) / 1000).toFixed(1)} s`);
await page.waitForTimeout(3000);
const before = await page.locator('#screen').screenshot({ path: out });

// The cursor is the page's: a CSS cursor made from the guest's (cursor.js),
// never in the frames.
const css = await page.evaluate(() => document.getElementById('screen').style.cursor);
const guestCursor = await page.evaluate(() => window.executor.cursor);
console.log(`cursor: ${guestCursor ? `hot spot ${guestCursor.hotX},${guestCursor.hotY}` : 'none'}; CSS ${css.slice(0, 40)}…`);
const cursorOk = !!guestCursor && /^(url|image-set)\(/.test(css);
if (!cursorOk)
    console.log('FAIL: no CSS cursor from the guest');

// Input: open the File menu (the menu bar's second title), screenshot it
// held open, let go.
const box = await page.locator('#screen').boundingBox();
const at = (x, y) => [box.x + x * box.width / 640, box.y + y * box.height / 480];
await page.mouse.move(...at(50, 9));
await page.mouse.down();
await page.waitForTimeout(1500);
const menu = await page.locator('#screen').screenshot({ path: out.replace(/\.png$/, '-menu.png') });
await page.mouse.up();
await page.waitForTimeout(500);
frames = await page.evaluate(() => window.executor ? window.executor.frames : 0);
console.log(`frames: ${frames}, screenshots: ${out}, ${out.replace(/\.png$/, '-menu.png')}`);
await close();
// Frames came, and the press opened the menu (the screen changed).
const ok = frames > 0 && !before.equals(menu) && cursorOk;
if (!ok)
    console.log(frames ? 'FAIL: the click changed nothing' : 'FAIL: no frames');
process.exit(ok ? 0 : 1);
