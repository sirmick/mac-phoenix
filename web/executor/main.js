// The page side: keep uploads in OPFS, start the Worker with them, hand it
// a canvas, send it input. Query string:
//   ?size=WxH        the screen (default 640x480)
//   ?scale=N         render at N times the screen, text from outlines
//                    (docs/executor/HiDPI.md; default: devicePixelRatio,
//                    EXECUTOR_SKIA builds only)
//   ?disk=URL        a disk image to fetch and mount (repeatable); kept in
//                    OPFS (disk-cache/) and revalidated on later visits
//                    with its ETag, so it downloads once
//   ?system=NAME     run on that image's System and Finder
//   ?lazy=1|0        read ?disk= images as they are needed, or download them
//                    whole (default: lazily from LAZY_MIN up, when the
//                    server takes Range requests and sends them uncompressed)
//   ?ppc             run fat applications' PowerPC code (the PowerPC box;
//                    PowerPC-only applications always run theirs)
// library.json beside the page (optional) offers more disks: [{ title,
// url, note }]; the ones picked are remembered (localStorage) and loaded
// like ?disk=.
// Served by tools/web/server, signed in (api/me): the overlays and
// Executor's own volume are synced with the account (sync.js).
// Outside a secure context (plain http to another host) there is no OPFS
// and no AudioWorklet: uploads, library disks (read as needed, held in
// memory) and changes to them and to Executor's volume last while
// Executor runs, sound is off; the page says so (#insecure).
//
// Automation: window.executor.request(op, arg) -> Promise. At the running
// application's next event call (web_main.cpp): 'app', 'windows', 'menus', 'launch'
// "Volume:Folder:App". At once, by the Worker: 'read' PATH (text or null),
// 'write' [PATH, TEXT], 'list' PATH (names or null); Executor's own volume
// is /executor, its "Host" volume /host.
// window.executor.frames counts frames.
'use strict';

const box = document.getElementById('screen-box');
document.getElementById('insecure').hidden = window.isSecureContext;
const status = document.getElementById('status');
const params = new URLSearchParams(location.search);
const [width, height] = (params.get('size') || '640x480').split('x').map(Number);
// Executor scales by whole numbers (fractional scales: HiDPI.md phase 6).
const scale = Math.max(1, Math.round(Number(params.get('scale')) || window.devicePixelRatio || 1));

let worker = null, canvas = null, cursor = null, nextId = 1;
// Bumped when a restart begins: messages from an older Worker (still
// drawing while it saves) no longer count.
let generation = 0;
const pending = new Map();
window.executor = {
    frames: 0,
    audioFrames: 0,     // 20 ms frames of sound received
    cursor: null,       // the guest's last cursor message
    request(op, arg = '') {
        if (!worker)
            return Promise.resolve({ error: 'not running' });   // between Workers
        const id = nextId++;
        return new Promise((resolve) => {
            pending.set(id, resolve);
            worker.postMessage({ type: 'request', id, op, arg });
        });
    },
};

// ---- uploads, kept in OPFS (or for this visit, where there is none) ----
const opfs = window.isSecureContext && navigator.storage && navigator.storage.getDirectory;
let sessionFiles = [];          // without OPFS; and ?disk= fetches

async function uploads() {
    return navigator.storage.getDirectory();
}

async function storedFiles() {
    const files = [...sessionFiles];
    if (opfs) {
        const dir = await uploads();
        for await (const [, handle] of dir.entries())
            if (handle.kind === 'file' && !files.some((f) => f.name === handle.name))
                files.push(await handle.getFile());
    }
    return files.sort((a, b) => a.name.localeCompare(b.name));
}

async function store(file) {
    if (!opfs) {
        sessionFiles = sessionFiles.filter((f) => f.name !== file.name).concat(file);
        return;
    }
    const dir = await uploads();
    const handle = await dir.getFileHandle(file.name, { create: true });
    const w = await handle.createWritable();
    await w.write(file);
    await w.close();
}

async function remove(name) {
    sessionFiles = sessionFiles.filter((f) => f.name !== name);
    if (opfs)
        await (await uploads()).removeEntry(name).catch(() => {});
}

// A large ?disk= is not downloaded first: the Worker reads it as Executor
// needs it (overlay.js: RemoteImage), and keeps what it read. Smaller ones,
// or compressed ones (the server's NAME.gz):
// ?disk=URL: downloaded once into OPFS (disk-cache/: the image and a .json
// with its URL and ETag), revalidated on later visits (304: the copy is
// current), mounted from there: disk-backed, never whole in memory. If the
// server can't be reached, the cached copy. Without OPFS: into memory, per
// visit.
async function diskCache() {
    return (await uploads()).getDirectoryHandle('disk-cache', { create: true });
}

async function cacheKey(url) {
    const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(url));
    return Array.from(new Uint8Array(digest).slice(0, 12), (b) => b.toString(16).padStart(2, '0')).join('');
}

async function readJson(dir, name) {
    try {
        return JSON.parse(await (await (await dir.getFileHandle(name)).getFile()).text());
    } catch {
        return null;
    }
}

const LAZY_MIN = 32 << 20;

// A disk's identity, for its overlay's key (overlay.js): the server's
// content id (X-Image-Id, tools/web/server), name -> id; and the key its
// overlay had before ids (name, size, download time), to move it from.
const imageIds = {}, legacyKeys = {};
const plainKey = (name, size, time) => `${name}-${size}-${time}`.replace(/[^\w.-]/g, '_');

async function fetchDisk(url) {
    const absolute = new URL(url, location.href).href;
    const name = decodeURIComponent(new URL(absolute).pathname.split('/').pop());
    const progress = (got, total) => {
        status.textContent = total ? `Loading ${name}… ${Math.floor(got * 100 / total)}%`
                                   : `Loading ${name}… ${(got / 1e6).toFixed(0)} MB`;
    };
    const dir = opfs ? await diskCache() : null;
    const key = dir ? await cacheKey(absolute) : null;
    const meta = dir ? await readJson(dir, `${key}.json`) : null;
    // Its OPFS time kept: without a content id, an image's overlay
    // (overlay.js) is for this version.
    const cached = async () => {
        const f = await (await dir.getFileHandle(`${key}.img`)).getFile();
        const m = await readJson(dir, `${key}.json`);
        if (m && m.id)
            imageIds[name] = m.id;
        legacyKeys[name] = plainKey(name, f.size, f.lastModified);
        return new File([f], name, { lastModified: f.lastModified });
    };

    // A Range request for its first byte: a 206 says the server (and any
    // proxy) gives ranges, and the size.
    const lazy = params.get('lazy');
    if (lazy !== '0') {
        const abort = new AbortController();
        let probe = null;
        try {
            probe = await fetch(absolute, { cache: 'no-store', headers: { Range: 'bytes=0-0' }, signal: abort.signal });
        } catch { }
        const range = probe && probe.status === 206 && /^bytes 0-0\/(\d+)$/.exec(probe.headers.get('Content-Range') || '');
        abort.abort();
        const size = range && Number(range[1]);
        if (size && (lazy === '1' || size >= LAZY_MIN)) {
            if (probe.headers.get('X-Image-Id'))
                imageIds[name] = probe.headers.get('X-Image-Id');
            return { name, size, lastModified: Date.parse(probe.headers.get('Last-Modified')) || 0,
                     remote: { url: absolute, etag: probe.headers.get('ETag'), key } };
        }
    }

    let res;
    try {
        // no-store: the browser's HTTP cache would keep a second copy.
        res = await fetch(absolute, { cache: 'no-store',
                                      headers: meta && meta.etag ? { 'If-None-Match': meta.etag } : {} });
    } catch (e) {
        if (meta)
            return cached();            // offline: what we have
        throw e;
    }
    if (res.status === 304 && meta)
        return cached();
    if (!res.ok)
        throw new Error(`${name}: ${res.status}`);
    // Compressed in transit, Content-Length is the compressed size;
    // tools/web/serve.py says the real one.
    const total = Number(res.headers.get('X-Decoded-Length'))
        || (res.headers.get('Content-Encoding') ? 0 : Number(res.headers.get('Content-Length')) || 0);
    const reader = res.body.getReader();
    let got = 0;
    if (!dir) {
        const chunks = [];
        for (let r; !(r = await reader.read()).done;) {
            chunks.push(r.value);
            progress(got += r.value.length, total);
        }
        if (res.headers.get('X-Image-Id'))
            imageIds[name] = res.headers.get('X-Image-Id');
        return new File(chunks, name);
    }
    // Into OPFS as it arrives; the file is replaced only when complete
    // (createWritable writes a swap file and commits at close).
    const out = await (await dir.getFileHandle(`${key}.img`, { create: true })).createWritable();
    try {
        for (let r; !(r = await reader.read()).done;) {
            await out.write(r.value);
            progress(got += r.value.length, total);
        }
        await out.close();
    } catch (e) {
        await out.abort();
        throw e;
    }
    const w = await (await dir.getFileHandle(`${key}.json`, { create: true })).createWritable();
    await w.write(JSON.stringify({ url: absolute, name, etag: res.headers.get('ETag'),
                                   lastModified: res.headers.get('Last-Modified'), size: got,
                                   id: res.headers.get('X-Image-Id') }));
    await w.close();
    return cached();
}

// ---- the library: disks the site offers (library.json), picked by the user
const libraryBox = document.getElementById('library-box');
const librarySelect = document.getElementById('library');
let library = [];
const picked = () => JSON.parse(localStorage.getItem('executor-library') || '[]');

async function loadLibrary() {
    try {
        const res = await fetch('library.json', { cache: 'no-store' });
        library = res.ok ? await res.json() : [];
    } catch {
        library = [];
    }
    showLibrary();
}

function showLibrary() {
    const have = new Set([...params.getAll('disk'), ...picked()].map((u) => new URL(u, location.href).href));
    const offer = library.filter((d) => !have.has(new URL(d.url, location.href).href));
    librarySelect.replaceChildren(new Option('Add a disk…', ''),
        ...offer.map((d) => {
            const o = new Option(d.title, d.url);
            o.title = d.note || '';
            return o;
        }));
    libraryBox.hidden = offer.length === 0;
}

librarySelect.addEventListener('change', async () => {
    const url = librarySelect.value;
    if (!url)
        return;
    localStorage.setItem('executor-library', JSON.stringify([...picked(), url]));
    showLibrary();
    generation++;                   // restarting, once it is downloaded
    window.executor.frames = 0;
    try {
        const file = await fetchDisk(url);
        sessionFiles = sessionFiles.filter((f) => f.name !== file.name).concat(file);
    } catch (e) {
        status.textContent = `Cannot load ${url}: ${e.message}`;
    }
    start();
});

// A library disk the user removes is forgotten too.
function forgetLibraryDisk(name) {
    const keep = picked().filter((u) => decodeURIComponent(new URL(u, location.href).pathname.split('/').pop()) !== name);
    localStorage.setItem('executor-library', JSON.stringify(keep));
    showLibrary();
}

async function fetchDisks() {
    loadLibrary();
    for (const url of [...params.getAll('disk'), ...picked()]) {
        try {
            const file = await fetchDisk(url);
            sessionFiles = sessionFiles.filter((f) => f.name !== file.name).concat(file);
        } catch (e) {
            status.textContent = `Cannot load ${url}: ${e.message}`;
        }
    }
}

// Disk images are writable through an overlay in OPFS (overlay.js), per
// exact version of the image (the Worker computes the same key).
function imageKey(f) {
    return imageIds[f.name] ? `${f.name}-${imageIds[f.name]}`.replace(/[^\w.-]/g, '_')
                            : plainKey(f.name, f.size, f.lastModified);
}
let imageState = {};                // name -> { modified, writable }, from the Worker

function link(text, title, onclick) {
    const a = document.createElement('a');
    a.textContent = text;
    a.title = title;
    a.onclick = onclick;
    return a;
}

async function showFiles(files) {
    const list = document.getElementById('files');
    list.replaceChildren(...files.map((f) => {
        const s = document.createElement('span');
        const st = imageState[f.name];
        s.textContent = f.name + (st && st.modified ? ' •' : '');
        if (st)
            s.title = st.inMemory ? (st.modified ? 'changed (until Executor restarts: not a secure page; the original is untouched)'
                                                 : 'writable (changes last until Executor restarts: not a secure page)')
                    : st.modified ? 'changed (kept in this browser; the original is untouched)'
                    : st.writable ? 'writable' : 'read-only (open in another tab?)';
        if (IMAGE.test(f.name)) {
            s.append(link('⤓', 'Download the image as it is now', () => download(f.name)));
            if (st && st.modified)
                s.append(link('↺', 'Revert: forget the changes to this image', () => revert(f)));
        }
        s.append(link('✕', 'Remove', async () => {
            await remove(f.name);
            forgetLibraryDisk(f.name);
            start();
        }));
        return s;
    }));
}

async function download(name) {
    const blob = await window.executor.request('download', name);
    if (!(blob instanceof Blob))
        return;
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 60000);
}

// The Worker goes without saving (its overlay handles with it), then the
// overlay is deleted and Executor starts again on the original.
async function revert(f) {
    if (!confirm(`Forget all changes to ${f.name}?`))
        return;
    generation++;
    if (worker)
        worker.terminate();
    worker = null;
    delete imageState[f.name];
    if (!opfs) {
        start();                    // its changes were the Worker's
        return;
    }
    const overlays = await (await uploads()).getDirectoryHandle('overlays', { create: true });
    for (let i = 0; i < 20; i++) {                  // until the old Worker lets go
        try {
            await overlays.removeEntry(imageKey(f), { recursive: true });
            break;
        } catch (e) {
            if (e.name === 'NotFoundError')
                break;
            await new Promise((r) => setTimeout(r, 100));
        }
    }
    // Signed in: the account forgets them too, at the start (sync.js), or
    // at the next one it reaches the server.
    if (account)
        await (await overlays.getDirectoryHandle(imageKey(f), { create: true }))
            .getFileHandle('revert', { create: true });
    start();
}

document.getElementById('add').addEventListener('change', async (e) => {
    generation++;                   // restarting (once the files are stored)
    window.executor.frames = 0;
    for (const f of e.target.files)
        await store(f);
    e.target.value = '';
    start();
});
document.getElementById('restart').addEventListener('click', () => start());

// Executor's own volume is kept per System choice (worker.js, volumes/KEY):
// Executor's own, or a disk image's by name and size.
function volumeKey(files) {
    const sys = files.find((f) => f.name === systemSelect.value);
    return sys ? `${sys.name}-${sys.size}`.replace(/[^\w.-]/g, '_') : 'own';
}

document.getElementById('reset').addEventListener('click', async () => {
    if (!confirm("Forget everything saved on Executor's disk for this System?"))
        return;
    if (worker)
        worker.terminate();
    worker = null;
    if (opfs) {
        const volumes = await (await uploads()).getDirectoryHandle('volumes', { create: true });
        const key = volumeKey(await storedFiles());
        await volumes.removeEntry(key, { recursive: true }).catch(() => {});
        // The same for Reset as for Revert.
        if (account) {
            const w = await (await (await volumes.getDirectoryHandle(key, { create: true }))
                .getFileHandle('sync.json', { create: true })).createWritable();
            await w.write(JSON.stringify({ reset: true }));
            await w.close();
        }
    }
    start();
});

// Leaving or hiding the page: the Worker saves what changed now.
document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'hidden' && worker)
        worker.postMessage({ type: 'save' });
});

// The System to run on: Executor's own (''), or a disk image's (its name),
// remembered across visits.
const IMAGE = /\.(img|image|dsk|hfs|iso|toast|cdr)$/i;
const systemSelect = document.getElementById('system');
systemSelect.addEventListener('change', () => {
    localStorage.setItem('executor-system', systemSelect.value);
    start();
});

// Fat applications' PowerPC code (mac-phoenix's --executor-ppc), remembered.
const ppcBox = document.getElementById('ppc');
ppcBox.checked = params.has('ppc') || localStorage.getItem('executor-ppc') === '1';
ppcBox.addEventListener('change', () => {
    localStorage.setItem('executor-ppc', ppcBox.checked ? '1' : '0');
    start();
});

function showSystems(files) {
    const want = params.get('system') || localStorage.getItem('executor-system') || '';
    systemSelect.replaceChildren(new Option("Executor's own", ''),
        ...files.filter((f) => IMAGE.test(f.name)).map((f) => new Option(f.name, f.name)));
    systemSelect.value = [...systemSelect.options].some((o) => o.value === want) ? want : '';
}

// ---- the account (tools/web/server): { name, owner }, or null ----
let account = null;
const accountKnown = (async () => {
    if (!opfs)
        return;
    try {
        const res = await fetch('api/me', { cache: 'no-store' });
        account = res.ok ? await res.json() : null;
    } catch { }
    const a = document.getElementById('account');
    if (account) {
        a.textContent = account.name;
        a.hidden = false;
    }
})();

// Sync (sync.js) found changes here and on the account: which to keep.
function conflictText(m) {
    const why = m.why === 'before'
        ? `${m.what} has changes made in this browser before you signed in, and your account has its own.`
        : `${m.what} was changed in another browser too, since this one last synced.`;
    return `${why}\n\nOK: keep this browser's (it replaces the account's).\n`
        + "Cancel: take the account's (this browser's changes are dropped).";
}

// ---- the Worker ----
// What changed on Executor's disk, saved before the Worker goes (at most
// a few seconds).
async function saveAndStop() {
    if (!worker)
        return;
    const w = worker;               // answers requests until it stops
    await new Promise((resolve) => {
        const timer = setTimeout(resolve, 3000);
        w.addEventListener('message', (e) => {
            if (e.data.type === 'saved') {
                clearTimeout(timer);
                resolve();
            }
        });
        w.postMessage({ type: 'save' });
    });
    w.terminate();
    if (worker === w)
        worker = null;
}

async function start() {
    const mine = ++generation;
    window.executor.frames = 0;     // "running" means the new Worker's frames
    await saveAndStop();
    if (mine !== generation)
        return;                     // another restart began meanwhile
    for (const resolve of pending.values())
        resolve({ error: 'restarted' });
    pending.clear();
    window.executor.frames = 0;
    status.textContent = 'Loading Executor…';

    // The Worker loads the module while the disks still download.
    worker = new Worker('worker.js');
    await disksFetched;
    await accountKnown;
    const files = await storedFiles();
    showFiles(files);
    showSystems(files);

    // A canvas hands its control over once: a fresh one per start.
    canvas = document.createElement('canvas');
    canvas.id = 'screen';
    canvas.width = width;
    canvas.height = height;
    canvas.tabIndex = 0;
    box.replaceChildren(canvas);
    fitCanvas();
    attachInput(canvas);
    cursor = new MacCursor(canvas, () => width);

    const offscreen = canvas.transferControlToOffscreen();
    worker.postMessage({ type: 'start', canvas: offscreen, width, height, scale, files,
                         system: systemSelect.value, ppc: ppcBox.checked,
                         volumeKey: volumeKey(files), sync: !!account, relay: !!account,
                         ids: imageIds, legacyKeys },
                       [offscreen]);
    worker.onmessage = (e) => {
        if (mine !== generation)
            return;
        const m = e.data;
        if (m.type === 'frame') {
            if (window.executor.frames++ === 0)
                status.textContent = 'Executor';
        } else if (m.type === 'images') {
            const before = JSON.stringify(imageState);
            for (const i of m.images)
                imageState[i.name] = { modified: i.modified, writable: i.writable, inMemory: i.inMemory };
            if (JSON.stringify(imageState) !== before)
                storedFiles().then(showFiles);
        } else if (m.type === 'cursor') {
            window.executor.cursor = m;
            cursor.set(m);
        } else if (m.type === 'audio') {
            window.executor.audioFrames++;
            if (audio)
                audio.port.postMessage(m.samples, [m.samples.buffer]);
        } else if (m.type === 'reply') {
            const resolve = pending.get(m.id);
            pending.delete(m.id);
            if (resolve)
                resolve(m.result);
        } else if (m.type === 'notice') {
            status.textContent = m.text;
        } else if (m.type === 'conflict') {
            worker.postMessage({ type: 'resolve', id: m.id, keep: confirm(conflictText(m)) ? 'mine' : 'account' });
        } else if (m.type === 'restart') {
            start();
        } else if (m.type === 'exit') {
            status.textContent = `Executor exited (${m.code})`;
        }
    };
}

// ---- sound: an AudioWorklet (audio.js) fed 48 kHz stereo from the Worker.
// Browsers start an AudioContext only after a click or key press.
let audio = null;
async function startAudio() {
    if (audio || !window.isSecureContext || typeof AudioWorkletNode === 'undefined')
        return;     // AudioWorklet: secure contexts only
    const ctx = new AudioContext({ sampleRate: 48000 });
    await ctx.audioWorklet.addModule('audio.js');
    audio = new AudioWorkletNode(ctx, 'executor-audio', { outputChannelCount: [2] });
    audio.connect(ctx.destination);
    await ctx.resume();
}

// ---- the canvas's size: the largest whole number of device pixels a guest
// pixel that fits the window, so every pixel is the same size (a
// fractional scale makes some one device pixel and some two).
function fitCanvas() {
    if (!canvas)
        return;
    const dpr = window.devicePixelRatio || 1;
    const bar = document.getElementById('bar').offsetHeight + document.getElementById('status').offsetHeight
        + document.getElementById('insecure').offsetHeight + 32;
    const fit = Math.min(window.innerWidth * dpr / width, (window.innerHeight - bar) * dpr / height);
    const scale = Math.max(1, Math.floor(fit));     // device pixels a guest pixel
    canvas.style.width = `${width * scale / dpr}px`;
    canvas.style.height = `${height * scale / dpr}px`;
}
window.addEventListener('resize', fitCanvas);
// The device-pixel ratio changes with the browser's zoom and between screens.
(function watchDpr() {
    matchMedia(`(resolution: ${window.devicePixelRatio}dppx)`)
        .addEventListener('change', () => { fitCanvas(); watchDpr(); }, { once: true });
})();

// ---- input: [kind, a, b] events (web_main.cpp) ----
function send(...events) {
    worker.postMessage({ type: 'input', events });
}

function attachInput(canvas) {
    const guestPoint = (e) => {
        const r = canvas.getBoundingClientRect();
        // The guest's 1x size: after transferControlToOffscreen the element's
        // width follows the Worker's (scaled) canvas.
        const x = Math.floor((e.clientX - r.left) * width / r.width);
        const y = Math.floor((e.clientY - r.top) * height / r.height);
        return [Math.max(0, Math.min(width - 1, x)),
                Math.max(0, Math.min(height - 1, y))];
    };
    canvas.addEventListener('pointermove', (e) => send([1, ...guestPoint(e)]));
    canvas.addEventListener('pointerdown', (e) => {
        startAudio();
        canvas.focus();
        canvas.setPointerCapture(e.pointerId);
        send([1, ...guestPoint(e)], [2, 1, 0]);
        e.preventDefault();
    });
    canvas.addEventListener('pointerup', (e) => {
        send([1, ...guestPoint(e)], [2, 0, 0]);
        e.preventDefault();
    });
    canvas.addEventListener('contextmenu', (e) => e.preventDefault());
    const onKey = (down, e) => {
        const code = macKeycodeForEvent(e);
        if (code === null)
            return;
        send([3, down ? 1 : 0, code]);
        e.preventDefault();
    };
    canvas.addEventListener('keydown', (e) => { startAudio(); if (!e.repeat) onKey(true, e); });
    canvas.addEventListener('keyup', (e) => onKey(false, e));
}

const disksFetched = fetchDisks();
start();
