// The Mac's screen size changed live in the tab (docs/executor/DisplayModes.md):
// from Settings (machine.screen applies at once) and with "Fit the screen to
// the window" (display.fit_window) as the browser window changes, while the
// Finder and SimpleText run.
//
//   node web/tests/tab_dynres.mjs DIR SYSTEM
//
// SYSTEM: an image with a blessed System Folder and SimpleText
// (tests-7.6.1.img; only read). At 640x480, SimpleText launched; then:
//   - Settings 1024x768: the Mac's screen and the page's are 1024x768, the
//     pointer lands on the Mac pixel across the screen, SimpleText's File
//     menu opens from a click on its title;
//   - SimpleText's window dragged to the bottom right, Settings 640x480:
//     the window's title bar is back on the screen (the Display Manager
//     moved it), the menu and the pointer again;
//   - fit to the window, then the window made smaller: the Mac's screen is
//     the room the page has (a Mac pixel per CSS pixel), the pointer and
//     the menu again; the Finder still answers.
// Skips (77) without SYSTEM.
import fs from 'node:fs';
import path from 'node:path';
import { open, firstFrame, request, until, pickSystem } from './harness.mjs';

const [dir, system] = process.argv.slice(2);
if (!system || !fs.existsSync(system)) {
    console.log(`SKIP: no System image (${system || 'none given'})`);
    process.exit(77);
}
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};

const { page, close } = await open(dir, { log: !!process.env.TAB_LOG, viewport: { width: 1280, height: 1000 } });
try {
    await firstFrame(page);
    await page.setInputFiles('#add', system);
    await firstFrame(page, 120000);
    await pickSystem(page, path.basename(system));
    await firstFrame(page, 120000);
    if (!await until(() => request(page, 'app').then((a) => a === 'Finder'), 180000))
        throw new Error('the Finder is not up');
    await page.waitForTimeout(3000);

    // SimpleText, wherever the System image has it.
    const volumes = await request(page, 'volumes');
    let launched = false;
    for (const v of volumes) {
        for (const p of [`${v}:SimpleText`, `${v}:Applications:SimpleText`, `${v}:Applications (Mac OS 9):SimpleText`]) {
            if (await request(page, 'launch', p) === 0) {
                launched = true;
                break;
            }
        }
        if (launched)
            break;
    }
    check(launched && await until(() => request(page, 'app').then((a) => a === 'SimpleText'), 60000),
          `SimpleText runs (${volumes.join(', ')})`);
    await page.waitForTimeout(2000);

    // The page's box for the Mac's screen, and a Mac pixel's centre in it.
    const geometry = async () => {
        const mac = await request(page, 'mouse');
        const box = await page.locator('#screen').boundingBox();
        return { mac, box, at: (x, y) => [box.x + (x + 0.5) * box.width / mac.width,
                                          box.y + (y + 0.5) * box.height / mac.height] };
    };
    const sized = (w, h) => until(async () => {
        const mac = await request(page, 'mouse');
        const shown = await page.evaluate(() => window.executor.screen);
        return mac.width === w && mac.height === h && shown.width === w && shown.height === h;
    }, 30000);

    // The pointer on Mac pixels across the screen, and a menu title clicked.
    const works = async (tag) => {
        const { mac, box, at } = await geometry();
        const slack = box.width / mac.width >= 1 ? 0 : 1;
        const misses = [];
        for (const [x, y] of [[3, 3], [mac.width >> 1, mac.height >> 1], [mac.width - 4, mac.height - 4]]) {
            await page.mouse.move(...at(x, y));
            if (!await until(() => request(page, 'mouse').then((m) => Math.abs(m.x - x) <= slack
                                                                 && Math.abs(m.y - y) <= slack), 3000, 50)) {
                const m = await request(page, 'mouse');
                misses.push(`(${x},${y}) -> (${m.x},${m.y})`);
            }
        }
        check(!misses.length, `${tag}: the pointer lands on the Mac pixel ${misses.join(' ')}`);
        const menus = await request(page, 'menus');
        const file = menus.find((m) => m.title === 'File');
        if (!file) {
            check(false, `${tag}: a File menu (${JSON.stringify(menus.map((m) => m.title))})`);
            return;
        }
        await page.mouse.move(...at(file.left + 10, 10));
        await page.mouse.down();
        await page.waitForTimeout(1500);
        await page.mouse.move(...at(file.left + 10, mac.height - 20));    // off the menu: nothing chosen
        await page.mouse.up();
        check(await until(() => request(page, 'drawn').then((d) => d.some((s) => /Quit/.test(s))), 5000),
              `${tag}: a click on File's title opens it`);
        await page.locator('#screen').screenshot({ path: path.join(dir, `tab_dynres_${tag.replace(/\W+/g, '_')}.png`) });
    };

    // Settings: larger.
    await page.evaluate(() => Settings.set({ machine: { screen: '1024x768' } }));
    check(await sized(1024, 768), 'Settings 1024x768: the Mac\'s screen and the page\'s');
    check(await request(page, 'app') === 'SimpleText', '1024x768: SimpleText still in front');
    await works('1024x768');

    // SimpleText's window to the bottom right, then smaller.
    const doc = async () => (await request(page, 'windows')).find((w) => w.visible && w.kind === 8);
    let w = await doc();
    if (w) {
        const { at } = await geometry();
        const [top, left] = w.rect;
        await page.mouse.move(...at(left + 60, top + 8));
        await page.mouse.down();
        await page.mouse.move(...at(left + 300, top + 300), { steps: 8 });
        await page.mouse.move(...at(Math.min(980, left + 600), 700), { steps: 8 });
        await page.mouse.up();
        await page.waitForTimeout(1000);
        w = await doc();
    }
    check(w && w.rect[0] > 480, `SimpleText's window moved below 480 (${w && w.rect})`);
    await page.evaluate(() => Settings.set({ machine: { screen: '640x480' } }));
    check(await sized(640, 480), 'Settings 640x480: the Mac\'s screen and the page\'s');
    w = await doc();
    check(w && w.rect[0] >= 20 && w.rect[0] < 480 - 18 && w.rect[1] < 640 - 32,
          `640x480: SimpleText's title bar is on the screen (${w && w.rect})`);
    await works('640x480');

    // Fit to the window, then a smaller window.
    await page.evaluate(() => Settings.set({ display: { fit_window: true } }));
    let want = await page.evaluate(() => windowSize());
    check(await sized(...want), `fit: the Mac's screen is the window's room, ${want.join('x')}`);
    await page.setViewportSize({ width: 900, height: 760 });
    want = await page.evaluate(() => windowSize());
    check(await sized(...want), `fit, a smaller window: ${want.join('x')}`);
    await works(`fit ${want.join('x')}`);
    const box = await page.locator('#screen').boundingBox();
    check(Math.abs(box.width - want[0]) <= 1 && Math.abs(box.height - want[1]) <= 1,
          `fit: a Mac pixel per CSS pixel (${box.width}x${box.height})`);
    // Drawn past the old 640x480: the menu bar's row and SimpleText's empty
    // page are white, not black (the vdriver kept the first size's region:
    // white outside it went out transparent).
    const png = (await page.locator('#screen').screenshot()).toString('base64');
    const blackShare = (y, x0, x1) => page.evaluate(async ({ png, y, x0, x1 }) => {
        const img = new Image();
        img.src = `data:image/png;base64,${png}`;
        await img.decode();
        const c = document.createElement('canvas');
        c.width = img.width;
        c.height = img.height;
        const g = c.getContext('2d');
        g.drawImage(img, 0, 0);
        const d = g.getImageData(x0, y, x1 - x0, 1).data;
        let black = 0;
        for (let i = 0; i < d.length; i += 4)
            black += d[i] < 32 && d[i + 1] < 32 && d[i + 2] < 32;
        return black / (d.length / 4);
    }, { png, y, x0, x1 });
    const bar = await blackShare(2, 640, want[0] - 80);
    check(bar < 0.1, `fit: the menu bar past x=640 is drawn (black ${bar.toFixed(2)})`);
    w = await doc();
    if (w) {
        const [top, left, bottom, right] = w.rect;
        const y = Math.min(want[1] - 2, top + 40);
        const share = await blackShare(y, Math.max(0, left + 4), Math.min(want[0], right - 20));
        check(share < 0.2, `fit: SimpleText's page is drawn at y=${y} (black ${share.toFixed(2)})`);
    } else
        check(false, 'fit: SimpleText\'s window');
    check(await request(page, 'app') === 'SimpleText' && (await request(page, 'volumes')).length > 0,
          'fit: SimpleText and the Finder still answer');
} catch (e) {
    check(false, `${e.message}`);
} finally {
    await close();
}
process.exit(failed ? 1 : 0);
