// The Worker side: runs executor-web.js (web_main.cpp) and draws its
// frames on the page's OffscreenCanvas.
//
// The module loads as soon as the Worker exists (noInitialRun), while the
// page is still fetching disks; 'start' mounts them and calls main.
//
// Uploads: disk images are mounted from the page's Files without copying,
// writable through a copy-on-write overlay in OPFS (overlay.js: OVERLAYFS
// at /uploads; the images themselves are never written); anything else (an
// application in MacBinary) is copied into Executor's own volume, in
// "executor:Uploads". A disk image on the server (main.js: a large one)
// comes as a description, read as needed (overlay.js: RemoteImage).
//
// Signed in to tools/web/server: the overlays and the volume are synced
// with the account (sync.js), down before they are mounted, up after
// saves, and the network goes through its relay (net.js).
//
// Standalone (the server says so; main.js): no OPFS, the server keeps
// it all (sync.js: Store): disks read by Range as needed, the overlays in
// memory and Executor's volume restored from the server and saved there
// about every second and when Executor flushes; what is left for when the
// page goes, in a beacon the page holds ('beacons'). The page in control
// says it is there (sessionBeat); while another tab watches, the screen
// goes to the server for it, a PNG a second.
'use strict';

let canvas = null, ctx = null;
const IMAGE = /\.(img|image|dsk|hfs|iso|toast|cdr)$/i;
const CD = /\.(iso|toast|cdr)$/i;

self.Module = {
    // Drawing: frames, pixels drawn, ms in the front end (web_main.cpp)
    // and here; the page's 'stats' request.
    stats: { frames: 0, pixels: 0, hostMs: 0, drawMs: 0 },
    arguments: [],
    input: [],          // [kind, a, b] events; web_main.cpp takes them
    requests: [],       // [id, op, arg]; answered at the next event call
    print: (t) => console.log(t),
    printErr: (t) => console.log(t),
    // What changed: rects [top, left, bottom, right]..., their RGBA pixels
    // one after another (web_main.cpp).
    onRects(width, height, rects, rgba) {
        const t0 = performance.now();
        if (canvas.width !== width || canvas.height !== height) {
            canvas.width = width;
            canvas.height = height;
        }
        let at = 0, pixels = 0;
        for (let i = 0; i < rects.length; i += 4) {
            const [top, left, bottom, right] = rects.subarray(i, i + 4);
            const w = right - left, h = bottom - top, n = w * h * 4;
            const image = new ImageData(new Uint8ClampedArray(rgba.subarray(at, at + n)), w, h);
            ctx.putImageData(image, left, top);
            at += n;
            pixels += w * h;
        }
        Module.stats.frames++;
        Module.stats.pixels += pixels;
        Module.stats.drawMs += performance.now() - t0;
        postMessage({ type: 'frame' });
    },
    onCursor(data, mask, hotX, hotY, visible) {
        postMessage({ type: 'cursor', data, mask, hotX, hotY, visible });
    },
    // 48 kHz big-endian 16-bit stereo -> interleaved floats for the page.
    onAudio(bytes) {
        const n = bytes.length >> 1;
        const out = new Float32Array(n);
        for (let i = 0; i < n; i++) {
            const v = (bytes[2 * i] << 8) | bytes[2 * i + 1];
            out[i] = (v > 32767 ? v - 65536 : v) / 32768;
        }
        postMessage({ type: 'audio', samples: out }, [out.buffer]);
    },
    onReply(id, result) {
        postMessage({ type: 'reply', id, result });
    },
    onExit(code) {
        stopped = true;
        console.log(`[web] Executor exited (${code})`);
        Log.flush();
        postMessage({ type: 'exit', code });
    },
    onAbort(what) {
        stopped = true;
        console.error(`[web] Executor aborted: ${what}`);
    },
};

importScripts('log.js', 'overlay.js', 'sync.js', 'net.js');

// An image's overlay is for exactly this version of it: its content id
// when the server said it (ids: name -> id), else its name, size and time
// (main.js computes the same, for Revert).
let ids = {};
function imageKey(f) {
    return (ids[f.name] ? `${f.name}-${ids[f.name]}` : `${f.name}-${f.size}-${f.lastModified}`).replace(/[^\w.-]/g, '_');
}
let overlays = [];

// Disks open in another tab (its Worker has their OPFS files: overlay.js):
// asked for on 'executor-disks', { type: 'release', keys }. A holder whose
// Executor has stopped, or whose page is in the background, lets them go
// (keeping on with them in memory) and says 'released'; one in use says
// 'held', and the asker keeps its copies in memory.
let stopped = false;                // Executor exited, aborted or trapped
let hidden = false;                 // the page in the background (main.js)
const disks = typeof BroadcastChannel === 'function' ? new BroadcastChannel('executor-disks') : null;
const DISKS_WAIT = 1500;            // ms for the holder's answer
self.addEventListener('error', () => { stopped = true; });
self.addEventListener('unhandledrejection', () => { stopped = true; });

if (disks)
    disks.addEventListener('message', (e) => {
        const m = e.data;
        if (m.type !== 'release')
            return;
        const mine = overlays.filter((o) => m.keys.includes(o.key) && o.writable && !o.inMemory);
        if (!mine.length)
            return;
        const keys = mine.map((o) => o.key);
        if (stopped || hidden) {
            for (const o of mine)
                o.toMemory();
            reportImages();
            notice(`Another tab has ${mine.map((o) => o.name).join(', ')} now: changes here last until this tab closes`);
            disks.postMessage({ type: 'released', keys });
        } else {
            notice(`Another tab wants ${mine.map((o) => o.name).join(', ')}: leave this tab in the background, or close it, to hand them over`);
            disks.postMessage({ type: 'held', keys });
        }
    });

// The other tab's answers for keys; resolves when every key has one, or
// after DISKS_WAIT.
function askForDisks(keys) {
    if (!disks)
        return Promise.resolve();
    return new Promise((resolve) => {
        const left = new Set(keys);
        const done = () => {
            disks.removeEventListener('message', answer);
            resolve();
        };
        const answer = (e) => {
            if (e.data.type !== 'released' && e.data.type !== 'held')
                return;
            for (const k of e.data.keys)
                left.delete(k);
            if (!left.size)
                done();
        };
        disks.addEventListener('message', answer);
        disks.postMessage({ type: 'release', keys });
        setTimeout(done, DISKS_WAIT);
    });
}

Module.noInitialRun = true;
const runtimeReady = new Promise((resolve) => { Module.onRuntimeInitialized = resolve; });

// The module, from Cache Storage when it is current: revalidated with its
// ETag (a 304 costs nothing), downloaded again only when it changed. Not
// left to the browser's HTTP cache, which a proxy's headers can defeat.
const WASM = 'executor-web.wasm';
async function wasmResponse() {
    const url = new URL(WASM, location.href).href;
    let cache = null, cached = null;
    try {
        cache = await caches.open('executor');
        cached = await cache.match(url);
    } catch { }                                  // no Cache Storage: no cache
    const etag = cached && cached.headers.get('ETag');
    let res;
    try {
        res = await fetch(url, { cache: 'no-store', credentials: 'same-origin',
                                 headers: etag ? { 'If-None-Match': etag } : {} });
    } catch (e) {
        if (cached)
            return cached;                       // offline: what we have
        throw e;
    }
    if (res.status === 304 && cached)
        return cached;
    if (cache && res.ok && res.headers.get('ETag'))
        await cache.put(url, res.clone());
    return res;
}
Module.instantiateWasm = (imports, receive) => {
    wasmResponse()
        .then((res) => WebAssembly.instantiateStreaming(res, imports))
        .then((r) => receive(r.instance, r.module))
        .catch((e) => console.log(`[web] cannot load ${WASM}: ${e}`));
    return {};                                   // asynchronous
};
importScripts('executor-web.js');

// ---- Executor's own volume (/executor), kept in OPFS ----
// volumes/KEY/ (KEY: the System choice) holds a manifest.json (every
// folder, and every file's blob, size and time) and the blobs. Restored
// into memory before main; changes saved every couple of seconds while
// Executor runs (and when the page is hidden): new blobs first, the
// manifest last, the old blobs deleted after, so a tab closed mid-save
// leaves the previous state whole.
const VOLUME = '/executor';
// Not kept: uploads (kept as uploads already), and System 7.5's "Shutdown
// Check", which it makes at startup and deletes at Shut Down; a tab never
// shuts down, and restored it would bring up the improper-shutdown alert.
const NOT_KEPT = new Set([`${VOLUME}/Uploads`, `${VOLUME}/Shutdown Check`, `${VOLUME}/%Shutdown Check`]);
let volumeDir = null, volumeKey = '', saved = { dirs: [], files: {} }, nextBlob = 1, saving = false;

async function openVolume(key) {
    volumeKey = key;
    try {
        const root = await navigator.storage.getDirectory();
        const volumes = await root.getDirectoryHandle('volumes', { create: true });
        volumeDir = await volumes.getDirectoryHandle(key, { create: true });
    } catch {
        volumeDir = null;                        // no OPFS: not kept
    }
}

async function restoreVolume() {
    if (!volumeDir)
        return 0;
    try {
        const text = await (await (await volumeDir.getFileHandle('manifest.json')).getFile()).text();
        saved = JSON.parse(text);
    } catch {
        return 0;                                // nothing saved yet
    }
    for (const d of saved.dirs)
        FS.mkdirTree(d);
    let n = 0;
    for (const [path, f] of Object.entries(saved.files)) {
        const data = new Uint8Array(await (await (await volumeDir.getFileHandle(f.blob)).getFile()).arrayBuffer());
        FS.writeFile(path, data);
        FS.utime(path, f.mtime, f.mtime);
        nextBlob = Math.max(nextBlob, Number(f.blob.slice(1)) + 1);
        n++;
    }
    return n;
}

function scanVolume() {
    const dirs = [], files = {};
    const walk = (dir) => {
        dirs.push(dir);
        for (const name of FS.readdir(dir)) {
            if (name === '.' || name === '..')
                continue;
            const path = `${dir}/${name}`;
            if (NOT_KEPT.has(path))
                continue;
            const st = FS.stat(path);
            if (FS.isDir(st.mode))
                walk(path);
            else if (FS.isFile(st.mode))
                files[path] = { size: st.size, mtime: st.mtime.getTime() };
        }
    };
    try { walk(VOLUME); } catch { }
    return { dirs, files };
}

// Which images have changes (the page marks them, offers their download).
function reportImages() {
    postMessage({ type: 'images', images: overlays.map((o) => ({
        name: o.name, modified: o.modified, writable: o.writable, inMemory: o.inMemory, borrowed: o.borrowed,
        onServer: Store.enabled && !o.syncOff })) });
}

// now: the page is going away, sync without waiting for the next round.
async function saveVolume(now = false) {
    if (Store.enabled) {
        if (overlays.length)
            reportImages();
        return Store.up(overlays, storeVolume);
    }
    await saveLocally();
    await Sync.up(overlays, volumeDir, volumeKey, saved, now);
}

async function saveLocally() {
    for (const o of overlays) {
        try { o.save(); } catch (e) { console.log(`[web] saving ${o.name}'s changes: ${e}`); }
    }
    if (overlays.length)
        reportImages();
    if (!volumeDir || saving)
        return;
    saving = true;
    try {
        const now = scanVolume();
        const files = {}, stale = [];
        let changed = now.dirs.length !== saved.dirs.length;
        for (const [path, f] of Object.entries(now.files)) {
            const old = saved.files[path];
            if (old && old.size === f.size && old.mtime === f.mtime) {
                files[path] = old;
                continue;
            }
            const blob = `b${nextBlob++}`;
            const data = FS.readFile(path);
            const w = await (await volumeDir.getFileHandle(blob, { create: true })).createWritable();
            await w.write(data);
            await w.close();
            files[path] = { ...f, blob, sha: Sync.enabled ? await Sync.sha256(data) : undefined };
            if (old)
                stale.push(old.blob);
            changed = true;
        }
        for (const [path, old] of Object.entries(saved.files))
            if (!(path in now.files)) {
                stale.push(old.blob);
                changed = true;
            }
        if (!changed)
            return;
        const manifest = { dirs: now.dirs, files };
        const w = await (await volumeDir.getFileHandle('manifest.json', { create: true })).createWritable();
        await w.write(JSON.stringify(manifest));
        await w.close();
        saved = manifest;
        for (const blob of stale)
            await volumeDir.removeEntry(blob).catch(() => {});
    } catch (e) {
        console.log(`[web] saving the volume: ${e}`);
    } finally {
        saving = false;
    }
}

// Standalone: the volume as it is now, to the server (sync.js: Store); the
// files changed since the last round read and hashed.
async function storeVolume() {
    const now = scanVolume();
    const files = {}, bytes = new Map();
    for (const [path, f] of Object.entries(now.files)) {
        const old = saved.files[path];
        if (old && old.size === f.size && old.mtime === f.mtime && old.sha) {
            files[path] = old;
            continue;
        }
        const data = FS.readFile(path);
        const sha = await Sync.sha256(data);
        bytes.set(sha, data);
        files[path] = { ...f, sha };
    }
    saved = { dirs: now.dirs, files };
    await Store.volumeUp(saved, bytes);
}

// Standalone: this tab is there, and in control (main.js; the Worker's
// timers run on in a background tab); who watches.
async function sessionBeat() {
    try {
        const res = await fetch(`api/session?id=${session}`, { method: 'POST', cache: 'no-store' });
        if (res.status === 401 || res.status === 403)
            postMessage({ type: 'claim', status: res.status });     // (main.js: reload)
        if (!res.ok)
            return;
        const s = await res.json();
        viewers = s.viewers;
        postMessage({ type: 'session', s });
    } catch { }
}

// Standalone: the page's beacon for if it goes now (sync.js: Store), when
// something changed; and the screen for whoever watches.
let beaconWrites = -1, viewers = 0, shotFrames = -1, shooting = false, session = '';
function storeBeacons() {
    const writes = overlays.reduce((n, o) => n + o.writes + o.sync.version, 0) + Store.flying.size;
    if (writes === beaconWrites)
        return;
    beaconWrites = writes;
    const items = Store.beacons(overlays);
    postMessage({ type: 'beacons', items }, items.map((i) => i.body.buffer));
}

async function sendScreen() {
    if (!viewers || shooting || !canvas || Module.stats.frames === shotFrames)
        return;
    shooting = true;
    shotFrames = Module.stats.frames;
    try {
        const png = await canvas.convertToBlob({ type: 'image/png' });
        await fetch(`api/session/screen?id=${session}`, { method: 'PUT', body: png, cache: 'no-store',
                                                          headers: { 'Content-Type': 'image/png' } });
    } catch { }
    shooting = false;
}

// The previous Worker of this origin's tab gone (a reload, a restart: it
// may still be saving the volume or holding the overlays' files, and
// restored from under it, a blob it removes is missing, an overlay opens
// read-only). Its lock is held until the Worker ends; another tab keeps
// its own, so after a few seconds this one goes on regardless.
function previousGone() {
    if (!navigator.locks)
        return Promise.resolve();
    return new Promise((resolve) => {
        navigator.locks.request('executor-tab', { signal: AbortSignal.timeout(5000) }, () => {
            resolve();
            return new Promise(() => {});        // held while this Worker runs
        }).catch(() => resolve());
    });
}

async function start(m) {
    if (m.log) {
        Log.start(m.log, 'worker');
        Log.onError = (text) => postMessage({ type: 'error', text });
    }
    await previousGone();
    canvas = m.canvas;
    ctx = canvas.getContext('2d');
    const images = m.files.filter((f) => IMAGE.test(f.name));
    const others = m.files.filter((f) => !IMAGE.test(f.name));
    // CDs are mounted locked, as a Mac's are (their overlay stays empty).
    const args = ['--size', `${m.width}x${m.height}`,
                  ...images.flatMap((f) => [CD.test(f.name) ? '--cdrom' : '--disk', `/uploads/${f.name}`])];
    if (m.scale && m.scale !== 1)
        args.push('--scale', `${m.scale}`);
    if (m.system && images.some((f) => f.name === m.system))
        args.push('--system', `/uploads/${m.system}`);
    if (m.ppc)
        args.push('--ppc');
    if (m.ram)
        args.push('--ram', `${m.ram}`);
    if (m.depth)
        args.push('--depth', `${m.depth}`);
    if (m.logtraps) {
        args.push('--logtraps');
        if (m.logtraps !== '1')
            args.push('--logtraps-filter', m.logtraps);
        // Applications' calls come nested in the Toolbox's callbacks (the Finder's are all)
        args.push('--logtraps-nesting', m.nesting || '6');
    }
    if (m.debug)
        args.push('--debug', m.debug);
    if (m.ppcbreak)
        for (const spec of m.ppcbreak.split(';').filter(Boolean))
            args.push('--ppc-break', spec);
    await runtimeReady;
    Sync.enabled = !!m.sync && !m.standalone;
    Store.enabled = !!m.standalone;
    session = m.log || '';
    if (Store.enabled)
        setInterval(sessionBeat, 2000);
    Module.net.enable(!!m.relay);
    ids = m.ids || {};
    if (m.volumeKey && Store.enabled) {
        try {
            // A connection that drops (the server restarted under the
            // download: "Content-Length ... exceeds response Body") is tried
            // again a few times; an HTTP error is not.
            let kept;
            for (let attempt = 1; ; attempt++) {
                try {
                    kept = await Store.volumeDown(m.volumeKey);
                    break;
                } catch (e) {
                    if (!(e instanceof TypeError) || attempt === 4)
                        throw e;
                    console.log(`[web] Executor's disk: ${e.message || e}; trying again`);
                    await new Promise((r) => setTimeout(r, 1500 * attempt));
                }
            }
            if (kept) {
                saved = kept;
                console.log(`[web] restored ${Object.keys(kept.files).length} files of the volume from the server`);
            }
        } catch (e) {
            notice(`Executor's disk: the server can't be reached (${e.message || e}): changes to it are not kept`);
            Store.volumeOff = true;
        }
    } else if (m.volumeKey) {
        await openVolume(m.volumeKey);
        await Sync.volumeDown(volumeDir, m.volumeKey);
        const n = await restoreVolume();
        if (n)
            console.log(`[web] restored ${n} files of the saved volume`);
    }
    FS.mkdir('/host');              // Executor's "Host" volume
    if (images.length) {
        let root = null;
        if (!Store.enabled)
            try { root = await navigator.storage.getDirectory(); } catch { }
        const open = (f, image) => Overlay.open(root, f.name, f, imageKey(f), (m.legacyKeys || {})[f.name], image);
        overlays = await Promise.all(images.map((f) => open(f)));
        // Another tab's: asked for, then opened again (a Worker gone
        // meanwhile lets go without answering), before Executor mounts them.
        const borrowed = overlays.filter((o) => o.borrowed);
        if (borrowed.length) {
            await askForDisks(borrowed.map((o) => o.key));
            overlays = await Promise.all(overlays.map((o, i) => o.borrowed ? open(images[i], o.image) : o));
            const still = overlays.filter((o) => o.borrowed).map((o) => o.name);
            if (still.length)
                notice(`Another tab has ${still.join(', ')}: changes to ${still.length > 1 ? 'them' : 'it'} here last until this tab closes`);
        }
        for (const o of overlays)
            await (Store.enabled ? Store.overlayDown(o) : Sync.overlayDown(o));
        FS.mkdir('/uploads');
        FS.mount(OVERLAYFS, { overlays }, '/uploads');
        reportImages();
    }
    if (others.length) {
        FS.mkdirTree(`${VOLUME}/Uploads`);
        const reader = new FileReaderSync();
        for (const f of others)
            FS.writeFile(`/executor/Uploads/${f.name}`,
                         new Uint8Array(reader.readAsArrayBuffer(f)));
    }
    Module.callMain(args);
    // Runs while Executor yields to the browser (about every frame).
    if (Store.enabled) {
        // A flush: the beacon as it is now (before the page hears of
        // anything after it), and the changes up at once.
        Overlay.onFlush = () => {
            storeBeacons();
            setTimeout(saveVolume);
        };
        setInterval(saveVolume, STORE_EVERY);
        setInterval(storeBeacons, 250);
        setInterval(sendScreen, 1000);
    } else {
        setInterval(saveVolume, 2000);
    }
}

self.onmessage = (e) => {
    const m = e.data;
    if (m.type === 'start') {
        start(m);
    } else if (m.type === 'save') {
        // The page is going away, or restarting (it waits for the reply).
        saveVolume(true).then(() => postMessage({ type: 'saved' }));
    } else if (m.type === 'resolve') {
        Sync.resolve(m.id, m.keep);
    } else if (m.type === 'visibility') {
        hidden = m.hidden;
    } else if (m.type === 'input') {
        Module.input.push(...m.events);
    } else if (m.type === 'request') {
        // Files are the Worker's: answered at once, Executor running or not.
        if (m.op === 'download') {
            // The image as changed: the original's slices and the changed blocks.
            const o = overlays.find((x) => x.name === m.arg);
            if (o)
                o.save();
            postMessage({ type: 'reply', id: m.id, result: o ? o.blob() : null });
        } else if (m.op === 'stats') {
            const remote = overlays.filter((o) => o.image instanceof RemoteImage);
            postMessage({ type: 'reply', id: m.id, result: { ...Module.stats,
                remoteBytes: remote.reduce((n, o) => n + o.image.fetched, 0),
                remoteRequests: remote.reduce((n, o) => n + o.image.requests, 0),
                // blocks not on the account yet (or on their way there)
                unsynced: overlays.reduce((n, o) => n + o.pending.size, 0) + (Sync.busy || Store.round ? 1 : 0) } });
        } else if (m.op === 'pixels') {
            // The canvas's own pixels (tests: comparing scales needs more
            // than a screenshot's possible browser-side resampling).
            const img = ctx.getImageData(0, 0, canvas.width, canvas.height);
            postMessage({ type: 'reply', id: m.id,
                         result: { width: canvas.width, height: canvas.height, data: img.data } },
                        [img.data.buffer]);
        } else if (m.op === 'list') {
            let names = null;
            try { names = FS.readdir(m.arg).filter((n) => n !== '.' && n !== '..'); } catch { }
            postMessage({ type: 'reply', id: m.id, result: names });
        } else if (m.op === 'read') {
            let text = null;
            try { text = FS.readFile(m.arg, { encoding: 'utf8' }); } catch { }
            postMessage({ type: 'reply', id: m.id, result: text });
        } else if (m.op === 'write') {
            const [path, text] = m.arg;
            FS.writeFile(path, text);
            postMessage({ type: 'reply', id: m.id, result: true });
        } else {
            Module.requests.push([m.id, m.op, m.arg]);
        }
    }
};
