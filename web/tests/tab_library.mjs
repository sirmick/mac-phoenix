// The library's details (web/executor/disks.js): what is on a library disk,
// from its record's "items" (tools/web/make_library.py), on
// tools/standalone. A disk's "▸ N items" opens a row under it with each
// item (its group's name above its items, name with description and kind,
// version, arch, size) and the disk's note, without putting the disk in the
// Mac; again closes it. The filter finds a disk by an item on it. A CD's
// items are its top-level folders with their sizes.
//
//   node web/tests/tab_library.mjs DIR LIBRARY [DISK] [SHOTS]
//
// DIR the wasm build's page, LIBRARY make_library.py's output, DISK a disk
// in it with items (default development.img: groups, descriptions);
// SHOTS a directory for a screenshot of the panel. Skips (77) without the
// library, or with one made before "items".
import fs from 'node:fs';
import path from 'node:path';
import { launch, firstFrame, until } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir, library, diskName = 'development.img', shots] = process.argv.slice(2);
const recordOf = (name) => {
    const f = path.join(library || '', `${name}.json`);
    return fs.existsSync(f) ? JSON.parse(fs.readFileSync(f, 'utf8')) : null;
};
const disk = recordOf(diskName);
if (!disk || !Array.isArray(disk.items) || !disk.items.length) {
    console.log(`SKIP: no ${diskName} with items in ${library} (tools/web/make_library.py)`);
    process.exit(77);
}
const cdName = fs.readdirSync(library).find((f) => f.endsWith('.iso') && (recordOf(f) || {}).items);

const server = await startServer(dir, { images: library, include: ['*.img', '*.iso'] });
if (!server) {
    console.log('SKIP: no go to build tools/standalone');
    process.exit(77);
}
const browser = await launch();
const context = await browser.newContext({ viewport: { width: 1400, height: 900 } });
const page = await context.newPage();
page.on('pageerror', (e) => console.log('[page error]', e.stack || e.message));
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const rowSel = (name) => `#disks-library .lib-row[data-url="disks/${encodeURIComponent(name)}"]`;
const detailsSel = (name) => `#disks-library .lib-details[data-url="disks/${encodeURIComponent(name)}"]`;

try {
    await page.goto(server.url);
    await firstFrame(page, 90000);
    await page.click('#disks-button');
    const toggle = page.locator(`${rowSel(diskName)} button.contents`);
    check(await toggle.count() === 1 && (await toggle.textContent()).includes(`${disk.items.length} item`),
          `${diskName}'s row offers its ${disk.items.length} items`);
    check(!(await page.locator(detailsSel(diskName)).count()), 'closed to begin with');

    await toggle.click();
    const details = page.locator(detailsSel(diskName));
    check(await until(() => details.count().then((n) => n === 1), 5000), '▸ opens the row under it');
    const names = await details.locator('.item-title').allTextContents();
    check(JSON.stringify(names) === JSON.stringify(disk.items.map((it) => it.name)),
          `every item, in the record's order (${names.length})`);
    const groups = [...new Set(disk.items.map((it) => it.group).filter(Boolean))];
    check(JSON.stringify(await details.locator('.lib-group').allTextContents()) === JSON.stringify(groups),
          `the groups' names above their items (${groups.join(', ')})`);
    const described = disk.items.find((it) => it.description);
    if (described)
        check((await details.textContent()).includes(described.description), `a description (${described.name})`);
    const versioned = disk.items.find((it) => it.version && it.arch);
    if (versioned) {
        const r = details.locator('.lib-item').nth(disk.items.indexOf(versioned));
        check(await r.locator('.item-version').textContent() === versioned.version
              && await r.locator('.item-arch').getAttribute('title') === versioned.arch
              && await r.getAttribute('data-kind') === versioned.kind,
              `version, kind and arch (${versioned.name}: ${versioned.version}, ${versioned.kind}, ${versioned.arch})`);
    }
    if (disk.note)
        check(await details.locator('.lib-note').textContent() === disk.note, "the disk's note");
    check(!(await page.locator(`${rowSel(diskName)}.mounted`).count())
          && !(await page.locator('#disks-mounted .disk').count()), 'opening it puts nothing in the Mac');
    if (shots) {
        fs.mkdirSync(shots, { recursive: true });
        await page.locator('#disks-panel').screenshot({ path: path.join(shots, 'library-details.png') });
    }
    await toggle.click();
    check(await until(() => page.locator(detailsSel(diskName)).count().then((n) => n === 0), 5000), 'again closes it');

    // The filter: a disk by an item on it
    const item = disk.items.find((it) => it.description) || disk.items[0];
    await page.fill('#disks-library .lib-filter input', item.name);
    const shown = await page.$$eval('#disks-library .lib-row', (rows) => rows.map((r) => r.dataset.url));
    check(shown.includes(`disks/${encodeURIComponent(diskName)}`) && shown.length < (await page.evaluate(() => library.length)),
          `the filter finds it by an item on it ("${item.name}": ${shown.length} disks)`);
    await page.fill('#disks-library .lib-filter input', '');

    if (cdName) {
        const cd = recordOf(cdName);
        await page.locator(`${rowSel(cdName)} button.contents`).click();
        const cdDetails = page.locator(detailsSel(cdName));
        check(await until(() => cdDetails.count().then((n) => n === 1), 5000), `a CD's row opens (${cdName})`);
        const folder = cd.items.find((it) => it.kind === 'folder');
        if (folder) {
            const r = cdDetails.locator('.lib-item').nth(cd.items.indexOf(folder));
            check(await r.getAttribute('data-kind') === 'folder' && (await r.locator('.item-size').textContent()) !== ''
                  && (await r.locator('.item-about').textContent()).includes('folder'),
                  `its top-level folders with their sizes (${folder.name})`);
        }
    }
} finally {
    await browser.close();
    await server.stop();
}
console.log(failed ? `${failed} failed` : 'all passed');
process.exit(failed ? 1 : 0);
