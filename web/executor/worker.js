// The Worker side: runs executor-web.js (web_main.cpp) and draws its
// frames on the page's OffscreenCanvas.
//
// The module loads as soon as the Worker exists (noInitialRun), while the
// page is still fetching disks; 'start' mounts them and calls main.
//
// Uploads: disk images are mounted from the page's Files without copying
// (WORKERFS, /uploads) and handed to Executor read-only; anything else (an
// application in MacBinary) is copied into Executor's own volume, in
// "executor:Uploads".
'use strict';

let canvas = null, ctx = null;
const IMAGE = /\.(img|image|dsk|hfs|iso|toast|cdr)$/i;

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
        postMessage({ type: 'exit', code });
    },
};

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
let volumeDir = null, saved = { dirs: [], files: {} }, nextBlob = 1, saving = false;

async function openVolume(key) {
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

async function saveVolume() {
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
            const w = await (await volumeDir.getFileHandle(blob, { create: true })).createWritable();
            await w.write(FS.readFile(path));
            await w.close();
            files[path] = { ...f, blob };
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

async function start(m) {
    canvas = m.canvas;
    ctx = canvas.getContext('2d');
    const images = m.files.filter((f) => IMAGE.test(f.name));
    const others = m.files.filter((f) => !IMAGE.test(f.name));
    const args = ['--size', `${m.width}x${m.height}`,
                  ...images.flatMap((f) => ['--disk', `/uploads/${f.name}`])];
    if (m.system && images.some((f) => f.name === m.system))
        args.push('--system', `/uploads/${m.system}`);
    await runtimeReady;
    if (m.volumeKey) {
        await openVolume(m.volumeKey);
        const n = await restoreVolume();
        if (n)
            console.log(`[web] restored ${n} files of the saved volume`);
    }
    FS.mkdir('/host');              // Executor's "Host" volume
    if (images.length) {
        FS.mkdir('/uploads');
        FS.mount(WORKERFS, { files: images }, '/uploads');
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
    setInterval(saveVolume, 2000);
}

self.onmessage = (e) => {
    const m = e.data;
    if (m.type === 'start') {
        start(m);
    } else if (m.type === 'save') {
        // The page is going away, or restarting (it waits for the reply).
        saveVolume().then(() => postMessage({ type: 'saved' }));
    } else if (m.type === 'input') {
        Module.input.push(...m.events);
    } else if (m.type === 'request') {
        // Files are the Worker's: answered at once, Executor running or not.
        if (m.op === 'stats') {
            postMessage({ type: 'reply', id: m.id, result: { ...Module.stats } });
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
