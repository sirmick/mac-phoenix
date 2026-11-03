// Executor's own volume on tools/standalone (sync.js, volumes.go): saved
// in bundles (objects/missing, then POST objects/bundle, no PUT a file),
// restored from one request (GET volumes/KEY/bundle, gzip), every file
// as it was; a server without bundles, and a bundle cut short, restore
// it all the same through objects/SHA. Executor's own System.
//
//   node web/tests/tab_volume.mjs DIR
import { launch, newPage, firstFrame, request, until } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir] = process.argv.slice(2);
const server = await startServer(dir);
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
    await a.goto(`${server.url}/`);
    check(await ready(a), 'Executor runs');
    const before = await until(async () => (await kept(a)) || 0, 20000, 300);
    seen.length = 0;
    await writeAll(a, 'a');
    check(await until(async () => (await kept(a)) >= before + FILES, 20000, 300),
          `${FILES} files more reach the server (${await kept(a)} kept)`);
    check(count(seen, 'POST objects/bundle') >= 1 && count(seen, 'PUT objects/SHA') === 0,
          `saved in bundles: ${count(seen, 'POST objects/bundle')} bundles, ${count(seen, 'PUT objects/SHA')} PUTs`);

    // A reload: one request for it all, every file as it was.
    seen = watch(a);
    await a.reload();
    check(await ready(a), 'Executor after a reload');
    check(count(seen, 'GET volumes/') === 1 && count(seen, 'GET volumes/own/bundle') === 1
          && count(seen, 'GET objects/SHA') === 0,
          `restored from one request (${seen.filter((s) => s.startsWith('GET v') || s.startsWith('GET o')).join(', ')})`);
    check(await sameAll(a, 'a') === true, 'every file as it was');

    // A server without bundles: objects/SHA down, PUTs up.
    await a.context().route(/\/api\/sync\/(volumes\/[^/]+\/bundle|objects\/bundle)$/,
                  (route) => route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"not found"}' }));
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
    seen = watch(a);
    await a.reload();
    check(await ready(a), 'Executor after a bundle cut short');
    check(await sameAll(a, 'a') === true && await request(a, 'read', '/executor/vol-put') === 'up by PUT'
          && count(seen, 'GET objects/SHA') > 0,
          `every file all the same (${count(seen, 'GET objects/SHA')} by objects/SHA)`);
} finally {
    await browser.close();
    server.stop();
}
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
