// The Executor backend's cursor reaches mac-phoenix's own UI as the page's
// cursor (client/cursor.js): opens http://127.0.0.1:PORT/ in headless
// Chromium and waits for the 'cursor_image' message to have made the
// display elements' CSS cursor. Skips (77) without playwright (NODE_PATH);
// CHROMIUM=PATH for another browser.
import { createRequire } from 'node:module';

let chromium;
try {
    ({ chromium } = createRequire(import.meta.url)('playwright'));
} catch {
    console.log('SKIP: no playwright package (set NODE_PATH)');
    process.exit(77);
}
const [port] = process.argv.slice(2);
const browser = await chromium.launch(process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM } : {});
const page = await browser.newPage({ viewport: { width: 1200, height: 900 } });
page.on('pageerror', (e) => console.log('[page error]', e.message));
await page.goto(`http://127.0.0.1:${port}/`);
let msg = null;
for (let i = 0; i < 60 && !msg; i++) {
    await page.waitForTimeout(500);
    msg = await page.evaluate(() => window.macCursor);
}
const css = await page.evaluate(() => [document.getElementById('display').style.cursor,
                                       document.getElementById('display-canvas').style.cursor]);
console.log('cursor_image:', msg ? `hot spot ${msg.hotX},${msg.hotY}, visible ${msg.visible}` : 'none');
const ok = !!msg && css.every((c) => /^(url|image-set)\(/.test(c));
console.log(ok ? 'PASS the guest cursor is the page cursor' : `FAIL ${JSON.stringify(css)}`);
await browser.close();
process.exit(ok ? 0 : 1);
