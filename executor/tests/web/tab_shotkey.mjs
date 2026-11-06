// Ctrl+Alt+S (executor/web/main.js; docs/executor/Settings.md): a screenshot
// to the server's session log that works with the mouse button down.
//
//   node executor/tests/web/tab_shotkey.mjs DIR IMAGE
//
// IMAGE: a System image (tests-7.6.1.img; only read). On executor/server at
// dpr 2 (the canvas's pixels and Executor's 1x frame both sent):
//   - the button up: the note asked for first, the shot with it in the log;
//   - the button held on the Special menu's title (the menu tracked): the
//     shot at once, both PNGs in the log with the menu open in them (white
//     under the title, where the first had the desktop), no dialog while
//     the button is down; the note asked for after it comes up, a line in
//     the log;
//   - the keys never reach the Mac (no "[in] key ... KeyS").
// Skips (77) without IMAGE.
import fs from 'node:fs';
import path from 'node:path';
import { open, firstFrame, request, until, pickSystem } from './harness.mjs';

const [dir, image] = process.argv.slice(2);
if (!image || !fs.existsSync(image)) {
    console.log(`SKIP: no System image (${image || 'none given'})`);
    process.exit(77);
}
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};

const { page, close, server } = await open(dir, { log: !!process.env.TAB_LOG, dpr: 2 });
const consoleLines = [];
page.on('console', (m) => consoleLines.push(m.text()));
const dialogs = [];
const answers = ['before', 'menu open'];
page.on('dialog', (d) => {
    dialogs.push({ message: d.message(), at: Date.now() });
    d.accept(answers.shift() || '');
});
try {
    await firstFrame(page);
    await page.setInputFiles('#add', image);
    await firstFrame(page, 120000);
    await pickSystem(page, path.basename(image));
    await firstFrame(page, 120000);
    check(await until(() => request(page, 'app').then((a) => a === 'Finder'), 180000), 'the Finder is up');
    await until(() => request(page, 'windows').then((ws) => ws.some((w) => w.title === 'Desktop')), 30000);
    await page.waitForTimeout(2000);

    const session = await page.evaluate(() => Log.session);
    const day = new Date().toISOString().slice(0, 10);
    const logFile = path.join(server.data, 'logs', day, `${session}.log`);
    const shots = () => (fs.existsSync(logFile) ? fs.readFileSync(logFile, 'utf8') : '')
        .split('\n').filter((l) => l.startsWith('# screenshot '));
    const png = (line) => path.join(server.data, 'logs', line.slice('# screenshot '.length).split(':')[0]);
    const shortcut = async () => {
        await page.keyboard.down('Control');
        await page.keyboard.down('Alt');
        await page.keyboard.press('KeyS');
        await page.keyboard.up('Alt');
        await page.keyboard.up('Control');
    };
    // The share of white pixels, and of pixels differing from OTHER, in a
    // rectangle (1x coordinates) of a PNG, whatever its scale.
    const measure = (file, [l, t, r, b], other) => page.evaluate(async ([a, o, rect]) => {
        const load = async (b64) => {
            const bmp = await createImageBitmap(await (await fetch(`data:image/png;base64,${b64}`)).blob());
            const c = new OffscreenCanvas(bmp.width, bmp.height);
            c.getContext('2d').drawImage(bmp, 0, 0);
            return c.getContext('2d').getImageData(0, 0, bmp.width, bmp.height);
        };
        const A = await load(a), B = o && await load(o);
        const k = A.width / 640, [l, t, r, b] = rect.map((v) => Math.round(v * k));
        let white = 0, differ = 0, n = 0;
        for (let y = t; y < b; y++)
            for (let x = l; x < r; x++) {
                const i = (y * A.width + x) * 4;
                n++;
                white += A.data[i] > 240 && A.data[i + 1] > 240 && A.data[i + 2] > 240;
                if (B)
                    differ += A.data[i] !== B.data[i] || A.data[i + 1] !== B.data[i + 1] || A.data[i + 2] !== B.data[i + 2];
            }
        return { white: white / n, differ: differ / n, width: A.width };
    }, [fs.readFileSync(file).toString('base64'), other && fs.readFileSync(other).toString('base64'), [l, t, r, b]]);

    // The button up: the note first.
    await page.click('#screen', { position: { x: 300, y: 300 } });     // (focus; the desktop)
    await page.waitForTimeout(500);
    await shortcut();
    check(await until(() => shots().some((l) => /: before$/.test(l)), 10000), 'button up: the note asked for, the shot with it in the log');
    check(dialogs.length === 1, `button up: one dialog (${dialogs.length})`);
    const before = shots().filter((l) => !/-1x\.png/.test(l)).pop();

    // The button held on Special's title.
    const special = (await request(page, 'menus')).find((m) => m.title === 'Special');
    const r = await page.locator('#screen').boundingBox();
    const at = (x, y) => [r.x + (x + 0.5) * r.width / 640, r.y + (y + 0.5) * r.height / 480];
    const n0 = shots().length;
    await page.mouse.move(...at(special.left + 12, 10));
    await page.mouse.down();
    await page.waitForTimeout(1500);
    await shortcut();
    const got = await until(() => shots().length >= n0 + 2, 15000);
    check(got, `button down: two PNGs in the log (${shots().slice(n0).join(' | ')})`);
    check(dialogs.length === 1, `button down: no dialog while the button is down (${dialogs.length})`);
    const held = shots().slice(n0);
    const scaledLine = held.find((l) => !/-1x\.png/.test(l)), oneX = held.find((l) => /-1x\.png/.test(l));
    check(!!scaledLine && /mouse button down/.test(scaledLine), `button down: the note says so (${scaledLine})`);
    const box = [special.left + 6, 24, special.left + 70, 70];          // under the title: the menu's items
    if (scaledLine && oneX) {
        const m2 = await measure(png(scaledLine), box, png(before));
        const m1 = await measure(png(oneX), box);
        check(m2.width === 1280 && m2.white > 0.6 && m2.differ > 0.3,
              `button down: the 2x PNG has the menu open (${JSON.stringify(m2)})`);
        check(m1.width === 640 && m1.white > 0.6, `button down: the 1x PNG has the menu open (${JSON.stringify(m1)})`);
    }
    await page.mouse.up();
    check(await until(() => dialogs.length === 2, 5000) && /taken with the button down/.test(dialogs[1].message),
          `button up again: the note asked for (${dialogs[1] && dialogs[1].message})`);
    check(await until(() => fs.readFileSync(logFile, 'utf8').includes('note: menu open'), 10000),
          'the note afterwards: a line in the log');
    check(!consoleLines.some((l) => /^\[in\] key .*KeyS/.test(l)), 'the keys never reached the Mac');
    check(await page.evaluate(() => /Ctrl\+Alt\+S/.test(document.getElementById('shot').title)), 'the Screenshot button\'s tooltip says Ctrl+Alt+S');
    await page.keyboard.press('Control+Alt+KeyD');
    check(await until(() => page.evaluate(() => /Ctrl\+Alt\+S/.test((document.getElementById('diag') || {}).textContent || '')), 3000),
          'the ?diag overlay\'s help says Ctrl+Alt+S');
    for (const l of held)
        fs.copyFileSync(png(l), path.join(process.env.TMPDIR || dir, `tab_shotkey_${/-1x/.test(l) ? '1x' : '2x'}.png`));
} catch (e) {
    check(false, `${e.message}`);
} finally {
    await close();
}
process.exit(failed ? 1 : 0);
