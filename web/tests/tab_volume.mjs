// Executor's own volume on tools/standalone (sync.js, volumes.go): saved
// in bundles (objects/missing, then POST objects/bundle, no PUT a file),
// restored from one request (POST volumes/KEY/bundle, gzip), every file
// as it was; again with every object from this browser's cache
// (ObjectCache) and none from the server, and a library disk's chunks
// from it (ChunkCache), read in compressed ranges; a cached object gone bad and
// the cache deleted: from the server, no error; a server without bundles,
// and a bundle cut short, restore it all the same through objects/SHA.
// Executor's own System.
//
//   node web/tests/tab_volume.mjs DIR
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { launch, newPage, firstFrame, request, until } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir] = process.argv.slice(2);
// A library disk (blank HFS: hfsutils' hformat), its chunks cached too
// (overlay.js: ChunkCache).
const images = fs.mkdtempSync(path.join(os.tmpdir(), 'tab-volume-'));
const disk = path.join(images, 'blank.img');
fs.writeFileSync(disk, Buffer.alloc(1440 * 1024));
try {
    execFileSync('hformat', ['-l', 'Blank', disk], { stdio: 'ignore', env: { ...process.env, HOME: images } });
} catch (e) {
    console.log(`SKIP: no hfsutils (${e.code || e.status})`);
    process.exit(77);
}
fs.writeFileSync(`${disk}.json`, JSON.stringify({ title: 'Blank', note: 'a blank disk', tags: [] }));
const server = await startServer(dir, { images });
if (!server) {
    console.log('SKIP: no go');
    process.exit(77);
}
const browser = await launch();
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};
const ready = async (page) => {
    await firstFrame(page, 60000);
    return until(() => request(page, 'app').then((a) => a === 'Browser'), 30000);
};
const kept = async (page) => {
    const s = await page.evaluate(() => fetch('api/sync').then((r) => r.json()));
    return (s.volumes.find((v) => v.key === 'own') || { files: 0 }).files;
};
// The sync requests a page makes (the Worker's included): "METHOD path".
const watch = (page) => {
    const seen = [];
    page.on('request', (r) => {
        const m = /\/api\/sync\/(.*)$/.exec(r.url());
        if (m)
            seen.push(`${r.method()} ${m[1].replace(/[0-9a-f]{64}/, 'SHA')}`);
    });
    return seen;
};
const count = (seen, what) => seen.filter((s) => s.startsWith(what)).length;
// The Worker's line on the restore: { cache, bundled, one } objects.
const restoreLine = (page) => {
    const line = {};
    page.on('worker', (w) => w.on('console', (m) => {
        const r = /\((\d+) objects from this browser's cache, (\d+) bundled, (\d+) one by one\)/.exec(m.text());
        if (r)
            Object.assign(line, { cache: +r[1], bundled: +r[2], one: +r[3] });
        if (/^\[web\] this browser's cache/.test(m.text()))
            line.said = m.text();
    }));
    return line;
};
// The cache gone (the Worker's connection blocks it until the reload).
const dropCache = (page) => page.evaluate(() => {
    indexedDB.deleteDatabase('executor-objects');
    indexedDB.deleteDatabase('executor-chunks');
});

// Files of every kind of size: empty, the same bytes twice, a few MB.
const FILES = 150;
const content = (i) => i % 50 === 7 ? '' : i % 50 === 8 ? 'the same' : i % 50 === 9 ? 'the same'
    : i === 0 ? 'big '.repeat(800000) : `file ${i} `.repeat(i * 37);
const writeAll = async (page, tag) => {
    for (let i = 0; i < FILES; i++)
        await request(page, 'write', [`/executor/vol-${tag}-${i}`, content(i)]);
};
const sameAll = async (page, tag) => {
    for (let i = 0; i < FILES; i++)
        if (await request(page, 'read', `/executor/vol-${tag}-${i}`) !== content(i))
            return `vol-${tag}-${i}`;
    return true;
};

try {
    const a = await newPage(browser, { log: false, tag: ' A' });
    let seen = watch(a);
    const reads = [];
    a.on('response', async (r) => {
        if (/\/disks\/blank\.img/.test(r.url()) && !/bytes=0-0/.test(await r.request().headerValue('range') || ''))
            reads.push(`${/[?&]bytes=/.test(r.url()) ? 'bytes' : 'range'} ${(await r.headerValue('content-encoding')) || '-'}`);
    });
    await a.goto(`${server.url}/?disk=disks/blank.img`);
    check(await ready(a), 'Executor runs');
    check(reads.length > 0 && reads.every((x) => x === 'bytes gzip'),
          `the disk read in compressed ranges (?bytes=, gzip: ${reads.join(', ')})`);
    const before = await until(async () => (await kept(a)) || 0, 20000, 300);
    seen.length = 0;
    await writeAll(a, 'a');
    check(await until(async () => (await kept(a)) >= before + FILES, 20000, 300),
          `${FILES} files more reach the server (${await kept(a)} kept)`);
    check(count(seen, 'POST objects/bundle') >= 1 && count(seen, 'PUT objects/SHA') === 0,
          `saved in bundles: ${count(seen, 'POST objects/bundle')} bundles, ${count(seen, 'PUT objects/SHA')} PUTs`);

    // A reload: one request for it all, every file as it was (the cache
    // dropped: a cold start).
    await dropCache(a);
    seen = watch(a);
    let line = restoreLine(a);
    await a.reload();
    check(await ready(a), 'Executor after a reload');
    const downs = seen.filter((s) => /^(GET|POST) (volumes|objects\/SHA)/.test(s));
    check(downs.length === 1 && downs[0] === 'POST volumes/own/bundle',
          `restored from one request (${downs.join(', ')})`);
    check(await sameAll(a, 'a') === true, `every file as it was (${JSON.stringify(line)})`);
    const cold = await request(a, 'stats');
    await new Promise((r) => setTimeout(r, 3000));          // (the chunks read written back: keep)

    // Warm: the server's manifest, every object from the cache.
    line = restoreLine(a);
    await a.reload();
    check(await ready(a), 'Executor after a reload, warm');
    check(line.cache > FILES && line.bundled === 0 && line.one === 0 && await sameAll(a, 'a') === true,
          `every object from the cache, none from the server (${JSON.stringify(line)})`);
    const warm = await request(a, 'stats');
    check(warm.cachedChunks > 0 && warm.remoteRequests < cold.remoteRequests,
          `the disk's chunks from the cache (${warm.cachedChunks}; Range requests ${cold.remoteRequests} -> ${warm.remoteRequests})`);

    // A cached object gone bad: that one from the server; nothing said to the page.
    const spoiled = await a.evaluate(() => new Promise((resolve) => {
        const r = indexedDB.open('executor-objects');
        r.onsuccess = () => {
            const tx = r.result.transaction('bytes', 'readwrite');
            const st = tx.objectStore('bytes');
            st.getAllKeys().onsuccess = (e) => {
                st.put(new Uint8Array([1, 2, 3]), e.target.result[0]);
                st.put(new Uint8Array([4]), e.target.result[1]);
            };
            tx.oncomplete = () => { r.result.close(); resolve(true); };
            tx.onerror = () => resolve(false);
        };
        r.onerror = () => resolve(false);
    }));
    line = restoreLine(a);
    await a.reload();
    check(spoiled && await ready(a), 'Executor after two cached objects were spoiled');
    check(await sameAll(a, 'a') === true && line.one === 2 && /not what they should be/.test(line.said || ''),
          `every file as it was, the two from the server (${JSON.stringify(line)})`);
    // The cache deleted: the network, no error.
    await dropCache(a);
    line = restoreLine(a);
    await a.reload();
    check(await ready(a) && await sameAll(a, 'a') === true && line.cache === 0 && line.bundled > FILES,
          `the cache deleted: all from the server (${JSON.stringify(line)})`);
    check(!/cache|disk/i.test(await a.locator('#status').textContent()),
          `nothing said on the page ("${await a.locator('#status').textContent()}")`);

    // A server without bundles: objects/SHA down, PUTs up.
    await a.context().route(/\/api\/sync\/(volumes\/[^/]+\/bundle|objects\/bundle)$/,
                  (route) => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"not found"}' }));
    await dropCache(a);
    seen = watch(a);
    await a.reload();
    check(await ready(a), 'Executor on a server without bundles');
    check(await sameAll(a, 'a') === true && count(seen, 'GET objects/SHA') > 100,
          `restored all the same, by objects/SHA (${count(seen, 'GET objects/SHA')} requests)`);
    const now = await kept(a);
    seen.length = 0;
    await request(a, 'write', ['/executor/vol-put', 'up by PUT']);
    check(await until(async () => (await kept(a)) > now, 20000, 300) && count(seen, 'PUT objects/SHA') >= 1,
          `saved by PUT there (${count(seen, 'PUT objects/SHA')})`);
    await a.context().unroute(/\/api\/sync\/(volumes\/[^/]+\/bundle|objects\/bundle)$/);

    // A bundle cut short (half its bytes): the rest by objects/SHA.
    await a.context().route(/\/api\/sync\/volumes\/[^/]+\/bundle$/, async (route) => {
        const res = await route.fetch();
        const body = await res.body();
        await route.fulfill({ status: 200, contentType: 'application/octet-stream',
                              body: body.subarray(0, body.length >> 1) });
    });
    await dropCache(a);
    seen = watch(a);
    await a.reload();
    check(await ready(a), 'Executor after a bundle cut short');
    check(await sameAll(a, 'a') === true && await request(a, 'read', '/executor/vol-put') === 'up by PUT'
          && count(seen, 'GET objects/SHA') > 0,
          `every file all the same (${count(seen, 'GET objects/SHA')} by objects/SHA)`);
} finally {
    await browser.close();
    server.stop();
    fs.rmSync(images, { recursive: true, force: true });
}
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
