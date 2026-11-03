// The Worker side: runs executor-web.js (web_main.cpp) and draws its
// frames on the page's OffscreenCanvas.
//
// The module loads as soon as the Worker exists (noInitialRun), while the
// page is still fetching disks; 'start' mounts them and calls main.
//
// Disks: a disk image on the server comes as a description, read as
// needed (overlay.js: RemoteImage); an uploaded one is mounted from the
// page's File without copying. Both are writable through a copy-on-write
// overlay in memory (overlay.js: OVERLAYFS at /uploads; the images
// themselves are never written). Anything else uploaded (an application in
// MacBinary) is copied into Executor's own volume, in "executor:Uploads".
//
// The server (tools/standalone) keeps it all (sync.js: Store): a server
// disk's overlay comes from it before Executor mounts the disk, and
// Executor's volume is restored from it; both are saved there about every
// second and when Executor flushes; what is left for when the
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
        // An eject that went (or had no volume to unmount): the image out too.
        const name = ejecting.get(id);
        ejecting.delete(id);
        if (name !== undefined && (result === 0 || result === -35))
            ejected(name, true);
        postMessage({ type: 'reply', id, result });
    },
    // A disk image's volumes gone (web_main.cpp: ejected here or by the Finder).
    onEjected(path) {
        ejected(path.replace(/^\/uploads\//, ''));
    },
    onExit(code) {
        console.log(`[web] Executor exited (${code})`);
        Log.flush();
        if (code)
            stopped(`exited (${code})`);
        postMessage({ type: 'exit', code });
    },
    // (web_main.cpp: Executor's fatal error alert is up in the guest)
    onFatal(message) {
        fatal = message;
        stopped(message);
    },
    onAbort(what) {
        console.error(`[web] Executor aborted: ${what}`);
        stopped(`aborted${what ? `: ${what}` : ''}`);
    },
};

// Executor stopped by an error (a fatal one of its own, an abort, the wasm
// trapping: an access out of bounds): the page says so, once, and stays.
// The reason: Executor's fatal message when there was one (an abort or a
// trap may follow it).
let fatal = '', stoppedSaid = false;
function stopped(what) {
    if (stoppedSaid)
        return;
    stoppedSaid = true;
    postMessage({ type: 'stopped', reason: fatal || what });
}
// (the module's errors only: the wasm trapping, or Emscripten's abort;
// Asyncify resumes it from timers and promises)
function moduleError(error) {
    if (error instanceof WebAssembly.RuntimeError || (typeof ABORT !== 'undefined' && ABORT))
        stopped(String(error && error.message || error));
}
self.addEventListener('error', (e) => moduleError(e.error || e.message));
self.addEventListener('unhandledrejection', (e) => moduleError(e.reason));

importScripts('log.js', 'overlay.js', 'sync.js', 'net.js');

// An image's overlay is for exactly this version of it: its content id
// when the server said it (ids: name -> id), else its name, size and time
// (main.js computes the same, for Revert).
let ids = {};
function imageKey(f) {
    return (ids[f.name] ? `${f.name}-${ids[f.name]}` : `${f.name}-${f.size}-${f.lastModified}`).replace(/[^\w.-]/g, '_');
}
let overlays = [];

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

// ---- Executor's own volume (/executor), kept by the server ----
// Per System choice (the page's volumeKey): restored into memory before
// main, saved with its manifest (every folder, every file's size, time
// and SHA-256; sync.js, Store).
const VOLUME = '/executor';
// Not kept: System 7.5's "Shutdown Check", which it makes at startup and
// deletes at Shut Down; a tab never shuts down, and restored it would
// bring up the improper-shutdown alert.
const NOT_KEPT = new Set([`${VOLUME}/Shutdown Check`, `${VOLUME}/%Shutdown Check`]);
let saved = { dirs: [], files: {} };

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

// ---- disks in and out while Executor runs (main.js: the Disks panel) ----
// The image's overlay opened, its changes fetched, then in /uploads;
// Executor mounts it at the running application's next event call
// ('mount': its OSErr is the reply). CDs read-only (mounted locked).
let cds = new Set();
const ejecting = new Map();         // request id -> image name
async function openImage(f) {
    const o = Overlay.open(f.name, f, imageKey(f));
    if (Store.enabled && o.file.remote)
        await Store.overlayDown(o);
    else
        o.syncOff = true;               // an upload: this visit's
    return o;
}

async function insert(id, { file, cd, ids: more }) {
    if (!IMAGE.test(file.name)) {
        // (an application restarts Executor: main.js)
        postMessage({ type: 'reply', id, result: { error: 'not a disk image' } });
        return;
    }
    Object.assign(ids, more || {});
    if (cd)
        cds.add(file.name);
    if (overlays.some((o) => o.name === file.name)) {           // in already
        postMessage({ type: 'reply', id, result: 0 });
        return;
    }
    let o;
    try {
        o = await openImage(file);
    } catch (e) {
        postMessage({ type: 'reply', id, result: { error: `${e.message || e}` } });
        return;
    }
    overlays.push(o);
    OVERLAYFS.add(o);
    reportImages();
    // A CD locked as --cdrom's are (ROMlib_locked_images).
    Module.requests.push([id, cds.has(file.name) ? 'mount-cd' : 'mount', `/uploads/${file.name}`]);
}

// Gone from the Mac: its changes to the server at once, out of /uploads;
// the page told. always: told even when it was never in (an eject of a
// disk with no volume).
async function ejected(name, always = false) {
    const o = overlays.find((x) => x.name === name);
    if (!o) {
        if (always)
            postMessage({ type: 'ejected', name });
        return;
    }
    overlays = overlays.filter((x) => x !== o);
    try {
        if (Store.enabled && !o.syncOff) {
            await Store.round;          // (one running may have it in hand)
            await Store.overlayUp(o);
        }
    } catch (e) {
        console.log(`[web] ${name}: saving its changes at eject: ${e}`);
    }
    OVERLAYFS.remove(name);
    console.log(`[web] ${name} ejected`);
    reportImages();
    postMessage({ type: 'ejected', name });
}

// Which images have changes (the page marks them, offers their download).
function reportImages() {
    postMessage({ type: 'images', images: overlays.map((o) => ({
        name: o.name, modified: o.modified, changed: o.map.size * OVERLAY_BLOCK, writable: !cds.has(o.name),
        onServer: Store.enabled && !o.syncOff })) });
}

// The overlays and the volume, to the server.
async function saveVolume() {
    if (!Store.enabled)
        return;
    if (overlays.length)
        reportImages();
    return Store.up(overlays, storeVolume);
}

// The volume as it is now, to the server (sync.js: Store); the files
// changed since the last round read and hashed.
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
// may still be saving the volume to the server, and restored from under
// it, this one would start on the state before). Its lock is held until
// the Worker ends; another tab keeps its own, so after a few seconds this
// one goes on regardless.
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
                  ...images.flatMap((f) => [CD.test(f.name) || (m.cds || []).includes(f.name) ? '--cdrom' : '--disk',
                                            `/uploads/${f.name}`])];
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
    }
    FS.mkdir('/host');              // Executor's "Host" volume
    cds = new Set([...(m.cds || []), ...images.map((f) => f.name).filter((n) => CD.test(n))]);
    overlays = [];
    for (const f of images)
        overlays.push(await openImage(f));
    // Mounted with none too: the Disks panel puts images in later.
    FS.mkdir('/uploads');
    FS.mount(OVERLAYFS, { overlays }, '/uploads');
    if (images.length)
        reportImages();
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
    }
}

self.onmessage = (e) => {
    const m = e.data;
    if (m.type === 'start') {
        start(m);
    } else if (m.type === 'save') {
        // The page is going away, or restarting (it waits for the reply).
        saveVolume(true).then(() => postMessage({ type: 'saved' }));
    } else if (m.type === 'input') {
        Module.input.push(...m.events);
    } else if (m.type === 'request') {
        // Files are the Worker's: answered at once, Executor running or not.
        if (m.op === 'insert') {
            insert(m.id, m.arg);
        } else if (m.op === 'eject') {
            ejecting.set(m.id, m.arg);
            Module.requests.push([m.id, 'eject', `/uploads/${m.arg}`]);
        } else if (m.op === 'download') {
            // The image as changed: the original's slices and the changed blocks.
            const o = overlays.find((x) => x.name === m.arg);
            postMessage({ type: 'reply', id: m.id, result: o ? o.blob() : null });
        } else if (m.op === 'stats') {
            const remote = overlays.filter((o) => o.image instanceof RemoteImage);
            postMessage({ type: 'reply', id: m.id, result: { ...Module.stats,
                remoteBytes: remote.reduce((n, o) => n + o.image.fetched, 0),
                remoteRequests: remote.reduce((n, o) => n + o.image.requests, 0),
                // blocks not on the server yet (or on their way there)
                unsynced: Store.enabled ? overlays.reduce((n, o) => n + o.pending.size, 0) + (Store.round ? 1 : 0) : 0 } });
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
