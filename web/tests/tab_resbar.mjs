// The bar's screen size (web/executor/main.js ScreenBar; docs/executor/Settings.md):
// the same settings as the dialog (machine.screen, display.fit_window),
// applied live, the Mac's real size shown, the two in step both ways.
//
//   node web/tests/tab_resbar.mjs DIR
//
// On Executor's own System (no image needed), on tools/standalone:
//   - at start: 640 × 480 chosen, the tooltip says so;
//   - 1024 × 768 from the bar: the setting, the Mac's screen and the page's,
//     the dialog's list opened afterwards;
//   - the dialog's 800 × 600 saved: the bar follows; the bar changed while
//     the dialog is open: its unsaved draft follows;
//   - Fit to window: the setting, the Mac's screen the room, the size in
//     the option; a smaller window: followed; the dialog's box ticked;
//   - a size the list hasn't (asked of Executor, not by the settings, as an
//     application would): shown as its own option, the setting kept;
//   - 640 × 480 from the bar: the fit off, the size back.
import { open, firstFrame, request, until } from './harness.mjs';

const [dir] = process.argv.slice(2);
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};

const { page, close } = await open(dir, { log: !!process.env.TAB_LOG, viewport: { width: 1280, height: 1000 } });
try {
    await firstFrame(page, 120000);
    const get = (p) => page.evaluate((p) => Settings.get(p), p);
    const bar = () => page.evaluate(() => {
        const s = document.getElementById('res');
        return { value: s.value, text: s.selectedOptions[0] && s.selectedOptions[0].text, title: s.title,
                 disabled: s.disabled };
    });
    const sized = (w, h) => until(async () => {
        const mac = await request(page, 'mouse');
        const shown = await page.evaluate(() => window.executor.screen || { width: 640, height: 480 });
        return mac.width === w && mac.height === h && shown.width === w && shown.height === h;
    }, 30000);
    const barIs = (value, text) => until(async () => {
        const b = await bar();
        return b.value === value && b.text.includes(text) && b;
    }, 10000);

    let b = await barIs('640x480', '640 × 480');
    check(b && /The Mac's screen: 640 × 480/.test(b.title) && !b.disabled,
          `start: 640 × 480 in the bar (${JSON.stringify(await bar())})`);
    check(await page.isVisible('#res'), 'the control is in the bar');

    // From the bar: larger.
    await page.selectOption('#res', '1024x768');
    check(await until(async () => await get('machine.screen') === '1024x768', 10000), 'bar 1024 × 768: the setting');
    check(await sized(1024, 768), 'bar 1024 × 768: the Mac\'s screen and the page\'s');
    check(!!await barIs('1024x768', '1024 × 768'), 'bar 1024 × 768: shown');
    await page.evaluate(() => Settings.open('machine'));
    await page.waitForSelector('dialog#settings[open]');
    const dialogSize = () => page.$eval('[data-path="machine.screen"] select', (s) => s.value);
    check(await dialogSize() === '1024x768', 'the dialog opened afterwards has 1024x768');

    // From the dialog: saved, the bar follows.
    await page.selectOption('[data-path="machine.screen"] select', '800x600');
    check((await bar()).value === '1024x768', 'the dialog\'s unsaved change leaves the bar');
    await page.click('dialog#settings .settings-save');
    await page.waitForSelector('dialog#settings:not([open])', { state: 'attached', timeout: 10000 });
    check(await sized(800, 600), 'dialog 800x600 saved: the Mac\'s screen');
    check(!!await barIs('800x600', '800 × 600'), 'dialog 800x600 saved: the bar');

    // The bar while the dialog is open: the draft follows.
    await page.evaluate(() => Settings.open('machine'));
    await page.waitForSelector('dialog#settings[open]');
    await page.evaluate(() => {            // (the dialog is modal: the bar by script)
        const s = document.getElementById('res');
        s.value = '832x624';
        s.dispatchEvent(new Event('change'));
    });
    check(await until(async () => await dialogSize() === '832x624', 10000), 'bar 832 × 624 with the dialog open: its list follows');
    check(await sized(832, 624), 'bar 832 × 624: the Mac\'s screen');
    await page.click('dialog#settings .settings-cancel');

    // Fit to window.
    await page.selectOption('#res', 'fit');
    check(await until(() => get('display.fit_window'), 10000), 'bar Fit to window: the setting');
    let want = await page.evaluate(() => windowSize());
    check(await sized(...want), `fit: the Mac's screen is the room, ${want.join('x')}`);
    check(!!await barIs('fit', `${want[0]} × ${want[1]}`), `fit: the option says ${want.join(' × ')}`);
    await page.setViewportSize({ width: 1000, height: 800 });
    want = await page.evaluate(() => windowSize());
    check(await sized(...want), `fit, a smaller window: ${want.join('x')}`);
    check(!!await barIs('fit', `${want[0]} × ${want[1]}`), `fit, a smaller window: the option says ${want.join(' × ')}`);
    await page.evaluate(() => Settings.open('display'));
    await page.waitForSelector('dialog#settings[open]');
    check(await page.$eval('[data-path="display.fit_window"] input', (i) => i.checked), 'fit: the dialog\'s box is ticked');
    // ... and unticked there, saved: the bar back to the size.
    await page.click('[data-path="display.fit_window"] input');
    await page.click('dialog#settings .settings-save');
    await page.waitForSelector('dialog#settings:not([open])', { state: 'attached', timeout: 10000 });
    check(await sized(832, 624), 'the dialog\'s fit off: the Mac back to 832x624');
    check(!!await barIs('832x624', '832 × 624'), 'the dialog\'s fit off: the bar');

    // A size not in the list, not from the settings (as an application's
    // DMSetDisplayMode): shown as it is, the setting kept.
    await request(page, 'resize', '700x500');
    check(await sized(700, 500), 'Executor asked 700x500: the Mac\'s screen');
    b = await barIs('now', '700 × 500');
    check(b && /asked for 832 × 624/.test(b.title), `700x500: its own option, the tooltip says what was asked (${b && b.title.split('\n')[0]})`);
    check(await get('machine.screen') === '832x624', '700x500: the setting kept');

    // Back to 640 × 480 from the bar.
    await page.selectOption('#res', '640x480');
    check(await sized(640, 480), 'bar 640 × 480: the Mac\'s screen');
    check(!!await barIs('640x480', '640 × 480') && !await get('display.fit_window')
          && await get('machine.screen') === '640x480', 'bar 640 × 480: the settings');
    await page.locator('#bar').screenshot({ path: `${process.env.TMPDIR || dir}/tab_resbar_bar.png` }).catch(() => {});
} catch (e) {
    check(false, `${e.message}`);
} finally {
    await close();
}
process.exit(failed ? 1 : 0);
