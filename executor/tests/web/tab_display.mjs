// The tab's display diagnostics (main.js, docs/executor/DisplayModes.md):
// display questions answered from the session log. On Executor's own
// System, with ?diag:
//   - '[display] start' with the window, dpr, canvas, fit, the Mac's size
//     asked, the HiDPI scale, the cursor, the UA parsed;
//   - '[build]' with the server's version and the module's build id, the
//     status line's tooltip the same;
//   - Executor's '[display] mode ... (boot)', and '(request)' after a
//     resize asked for, then the page's '[display] mac size' line;
//   - a window resize: a '[display] resize' line;
//   - the first click after a change: '[display] click', the Mac's mouse
//     where the page mapped it;
//   - the overlay (?diag) showing the same.
//
//   node web/tests/tab_display.mjs DIR
//
// Needs playwright (harness.mjs).
import fs from 'node:fs';
import path from 'node:path';
import { open, firstFrame, request, until } from './harness.mjs';

const [dir] = process.argv.slice(2);
const { page, close, server } = await open(dir, { log: false, query: 'diag' });
// The session log as the server keeps it (log.js sends a batch a second):
// the '[display]' and '[build]' lines, page's and Worker's, without their
// time and source.
let lines = [];
function readLog() {
    const logs = path.join(server.data, 'logs');
    if (!fs.existsSync(logs))
        return lines = [];
    const files = fs.readdirSync(logs, { recursive: true }).filter((f) => f.endsWith('.log'));
    lines = files.flatMap((f) => fs.readFileSync(path.join(logs, f), 'utf8').split('\n'))
        .map((l) => l.replace(/^\S+ (page|worker) /, ''))
        .filter((l) => /^\[(display|build)\]/.test(l));
    return lines;
}
let ok = true;
const check = (good, what) => {
    console.log(`${good ? 'PASS' : 'FAIL'} ${what}`);
    ok &&= good;
};
const line = (re) => readLog().find((l) => re.test(l));
const last = (re) => readLog().findLast((l) => re.test(l));
const json = (l) => JSON.parse(l.slice(l.indexOf('{')));

try {
    await firstFrame(page, 60000);
    await until(() => line(/^\[display\] start/) && line(/^\[build\] server .*Executor (?!\?)/), 10000);
    const start = line(/^\[display\] start/);
    check(!!start, `a start line: ${start || readLog().map((l) => l.slice(0, 60)).join(" | ")}`);
    if (start) {
        const s = json(start);
        const want = ['browser', 'os', 'dpr', 'zoom', 'inner', 'outer', 'screen', 'canvas_css', 'canvas_device',
                      'fit', 'mac_asked', 'mac', 'hidpi', 'cursor_px', 'fit_window', 'fullscreen', 'ua'];
        check(want.every((k) => k in s), `its fields (${want.filter((k) => !(k in s)).join(' ') || 'all'})`);
        check(s.browser.startsWith('Chrome') && s.os === 'Linux' && s.inner === '800x640' && s.mac_asked === '640x480',
              `its values: ${s.browser}, ${s.os}, ${s.inner}, ${s.mac_asked}`);
    }
    const b = last(/^\[build\]/);
    check(/server \S+.*; Executor [^?]+$/.test(b || ''), `the builds: ${b}`);
    check(await page.locator('#status').getAttribute('title') === b.slice('[build] '.length), 'the status line\'s tooltip');
    check(!!line(/^\[display\] mode 0x0x0 -> 640x480x\d+ \(boot\)/), `Executor's boot mode: ${line(/\] mode/)}`);

    // The Mac's size changed by request: Executor's line, then the page's.
    await request(page, 'resize', '800x600');
    const macLine = () => readLog().find((l) => /^\[display\] \S*mac size/.test(l) && json(l).mac === '800x600');
    await until(macLine, 15000);
    check(!!line(/^\[display\] mode 640x480x\d+ -> 800x600x\d+ \(request\)/), `Executor's request mode: ${lines.filter((l) => /\] mode/.test(l)).join(' | ')}`);
    const mac = macLine();
    check(!!mac, `the page's: ${mac || lines.filter((l) => /mac size/.test(l)).join(' | ')}`);

    // The first click after it: logged with the Mac's own mouse.
    const box = await page.locator('#screen').boundingBox();
    await page.mouse.click(box.x + box.width * 0.5, box.y + box.height * 0.75);
    await until(() => line(/^\[display\] click/), 8000);
    const click = line(/^\[display\] click/);
    const m = /-> mac (\d+),(\d+); the Mac's mouse (\d+),(\d+)/.exec(click || '');
    check(m && m[1] === m[3] && m[2] === m[4], `a click: ${click}`);

    // The window resized.
    await page.setViewportSize({ width: 900, height: 700 });
    await until(() => line(/^\[display\] (\S*,)?resize/), 8000);
    const resize = line(/^\[display\] (\S*,)?resize/);
    check(resize && json(resize).inner === '900x700', `a resize: ${resize}`);

    const overlay = await page.locator('#diag').textContent().catch(() => '');
    check(/dpr: 1/.test(overlay) && /mac: 800x600/.test(overlay) && /executor: [^?]/.test(overlay),
          `the overlay: ${JSON.stringify(overlay.slice(0, 200))}`);
} finally {
    await close();
}
process.exit(ok ? 0 : 1);
