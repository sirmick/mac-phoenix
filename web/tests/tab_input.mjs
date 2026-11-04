// Clicks and the pointer in the tab, at each screen size and display scale:
// the owner's 8.6 at 1024x768 came up 640x480 (SetDepth's setMode(0, 0)
// reset the size), the page fitted and mapped clicks for 1024x768 and drew
// the pointer for it: clicks landed off target, the pointer small.
//
//   node web/tests/tab_input.mjs DIR SYSTEM [ppc]
//
// SYSTEM: an image with a blessed System Folder (8.6.img with "ppc", the
// page's PowerPC box; tests-7.6.1.img without; only read). For each cell
// (TAB_INPUT_CELLS, default "640x480@1 1024x768@1 640x480@2 1024x768@2":
// screen @ devicePixelRatio [/ window], the window 800x640 if not given,
// narrower than 1024; executor.web.input_86_laptop: the owner's 1280x1024
// in laptop windows, "1280x1024@2/1440x900 1280x1024@1/1280x800"):
//   - the Mac's screen is the size asked for, and the page's is the Mac's;
//   - the screen fits the window (shrunk when it is bigger) and fills what
//     it can: a Mac pixel a CSS pixel or more, or as large as the room
//     (1280x1024 at dpr 2 in 1440x900 had a device pixel each, a quarter
//     of the room, the pointer 8 CSS pixels), and the
//     pointer moved to a Mac pixel's centre on the page is at that pixel on
//     the Mac (web_main.cpp's 'mouse'; within one when shrunk), at points
//     across the screen;
//   - the pointer is 16 Mac pixels at the canvas's scale, never under 16
//     CSS pixels (cursor.js);
//   - a click on the Special menu's title opens it (Restart drawn), a
//     double-click on the startup disk's icon (the right edge) its window.
// Skips (77) without SYSTEM.
import fs from 'node:fs';
import path from 'node:path';
import { open, firstFrame, request, until, pickSystem } from './harness.mjs';

const [dir, system, arch] = process.argv.slice(2);
if (!system || !fs.existsSync(system)) {
    console.log(`SKIP: no System image (${system || 'none given'})`);
    process.exit(77);
}
const ppc = arch === 'ppc';
const cells = (process.env.TAB_INPUT_CELLS || '640x480@1 1024x768@1 640x480@2 1024x768@2')
    .split(/\s+/).filter(Boolean).map((c) => {
        const [size, rest = '1'] = c.split('@');
        const [dpr, win = '800x640'] = rest.split('/');
        const [width, height] = size.split('x').map(Number);
        const [vw, vh] = win.split('x').map(Number);
        return { size, width, height, dpr: Number(dpr), viewport: { width: vw, height: vh } };
    });
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};

for (const cell of cells) {
    const { viewport } = cell;
    const tag = `${path.basename(system)} ${cell.size} dpr ${cell.dpr} in ${viewport.width}x${viewport.height}`;
    const { page, close } = await open(dir, { log: !!process.env.TAB_LOG, viewport, dpr: cell.dpr,
                                              query: `size=${cell.size}${ppc ? '&ppc' : ''}` });
    try {
        await firstFrame(page);
        await page.setInputFiles('#add', system);
        await firstFrame(page, 120000);
        await pickSystem(page, path.basename(system));
        await firstFrame(page, 120000);
        if (!await until(() => request(page, 'app').then((a) => a === 'Finder'), 180000)) {
            check(false, `${tag}: the Finder is up`);
            continue;
        }
        await until(() => request(page, 'windows').then((ws) => ws.some((w) => w.title === 'Desktop')), 30000);
        await page.waitForTimeout(3000);

        const mac = await request(page, 'mouse');
        const shown = await page.evaluate(() => window.executor.screen);
        check(mac.width === cell.width && mac.height === cell.height,
              `${tag}: the Mac's screen is ${mac.width}x${mac.height}`);
        check(shown.width === mac.width && shown.height === mac.height,
              `${tag}: the page's is ${shown.width}x${shown.height}`);
        const W = mac.width, H = mac.height;

        // Where a Mac pixel's centre is on the page; null outside the window.
        const box = await page.locator('#screen').boundingBox();
        check(box.x >= 0 && box.y >= 0 && box.x + box.width <= viewport.width + 0.5
              && box.y + box.height <= viewport.height + 0.5,
              `${tag}: the screen fits the window (${JSON.stringify(box)})`);
        const [across, down] = await page.evaluate(() => room());
        const most = Math.min(1, across / W, down / H);
        // (within 5%: the status line's height changes after a fit)
        check(box.width / W >= most * 0.95,
              `${tag}: the screen fills the room it can (${(box.width / W).toFixed(2)} CSS pixels a Mac pixel, ${most.toFixed(2)} at least)`);
        const at = (x, y) => {
            const px = box.x + (x + 0.5) * box.width / W, py = box.y + (y + 0.5) * box.height / H;
            return px >= 0 && py >= 0 && px < viewport.width && py < viewport.height ? [px, py] : null;
        };
        const points = [[3, 3], [W >> 1, H >> 1], [W - 4, H - 4], [W - 4, 3], [3, H - 4],
                        [Math.min(W, Math.floor((viewport.width - box.x) * W / box.width)) - 4, H >> 2]];
        // Exact at a whole number of device pixels a Mac pixel; a screen
        // shrunk to fit a smaller window has more Mac pixels than page ones.
        const slack = box.width / W >= 1 ? 0 : 1;
        let misses = [];
        for (const [x, y] of points) {
            const p = at(x, y);
            if (!p)
                continue;
            await page.mouse.move(...p);
            const ok = await until(() => request(page, 'mouse')
                .then((m) => Math.abs(m.x - x) <= slack && Math.abs(m.y - y) <= slack), 3000, 50);
            if (!ok) {
                const m = await request(page, 'mouse');
                misses.push(`(${x},${y}) -> (${m.x},${m.y})`);
            }
        }
        check(!misses.length, `${tag}: the pointer lands on the Mac pixel${misses.length ? ' ' + misses.join(' ') : ''}`);

        // The pointer: 16 Mac pixels, each the canvas's device pixels, or
        // a CSS pixel's when the screen is shrunk.
        const want = 16 * Math.max(1, Math.round(cell.dpr), Math.round(box.width / W * cell.dpr));
        const got = await page.evaluate(() => new Promise((resolve) => {
            const m = /url\("?([^")]+)"?\)/.exec(document.getElementById('screen').style.cursor);
            if (!m)
                return resolve(0);
            const img = new Image();
            img.onload = () => resolve(img.naturalWidth);
            img.onerror = () => resolve(-1);
            img.src = m[1];
        }));
        check(got === want, `${tag}: the pointer is ${got} device pixels (${want}: 16 Mac pixels)`);

        // The Special menu's title.
        const menus = await request(page, 'menus');
        const special = menus.find((m) => m.title === 'Special');
        if (!special || !at(special.left + 12, 10))
            check(false, `${tag}: the Special menu (${JSON.stringify(menus.map((m) => [m.title, m.left]))})`);
        else {
            // (Executor answers no request while it tracks the menu: the
            // strings it drew are asked for after the button is up.)
            await page.mouse.move(...at(special.left + 12, 10));
            await page.mouse.down();
            await page.waitForTimeout(1500);
            await page.mouse.move(...at(special.left + 12, H >> 1));  // off the menu: nothing chosen
            await page.mouse.up();
            const open = await until(() => request(page, 'drawn').then((d) => d.some((s) => /Restart/.test(s))), 5000);
            check(open, `${tag}: a click on Special's title opens it`);
        }

        // The startup disk's icon, down the right edge (8.6's slots 64
        // apart from y 51; 7.x's drive icon the same), found by trying.
        let opened = null;
        for (const y of [51, 115, 51, 115]) {
            const p = at(W - 47, y);
            if (!p)
                break;
            await page.mouse.click(...p);
            await page.waitForTimeout(600);
            await page.mouse.dblclick(...p);
            opened = await until(() => request(page, 'windows').then((ws) => ws.find(
                (w) => w.title && !['Desktop', 'Host'].includes(w.title) && w.visible && w.kind !== 2)), 8000, 100);
            if (opened)
                break;
        }
        if (at(W - 47, 51))
            check(!!opened, `${tag}: a double-click on the disk icon opens its window${opened ? ` (${opened.title})` : ''}`);
        else
            console.log(`(${tag}: the icons are outside the window)`);
        await page.locator('#screen').screenshot({ path: path.join(dir, `tab_input_${ppc ? 'ppc' : '68k'}_${cell.size}_${cell.dpr}_${viewport.width}x${viewport.height}.png`) });
    } finally {
        await close();
    }
}
process.exit(failed ? 1 : 0);
