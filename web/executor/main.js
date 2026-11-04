// The page side: start the Worker with the disks and uploads, hand it a
// canvas, send it input. The settings (settings.js: the Settings dialog,
// kept by the server or in this browser) say how; the query string
// overrides some of them for the visit:
//   ?size=WxH        the screen (default 640x480)
//   ?scale=N         render at N times the screen, text from outlines
//                    (docs/executor/HiDPI.md; default: devicePixelRatio,
//                    EXECUTOR_SKIA builds only)
//   ?disk=URL        a disk image to mount (repeatable): the server's, read
//                    by Range as Executor needs it
//   ?system=NAME     run on that image's System and Finder
//   ?lazy=0          download ?disk= images whole first (default: read as
//                    needed, when the server takes Range requests)
//   ?ppc             run fat applications' PowerPC code (the PowerPC box;
//                    PowerPC-only applications always run theirs)
//   ?logtraps=LIST   Executor logs Toolbox calls (as --executor-logtraps):
//                    LIST the names (wildcards, -NAME leaves out; 1: all);
//                    &nesting=N calls nested N deep (default 6); ?debug=OPTS Executor's
//                    --executor-debug options. Everything printed goes to the
//                    server too (log.js; tools/standalone keeps it per session)
//   ?ppcbreak=SPEC   PowerPC breakpoints (as --executor-ppc-break; several
//                    separated by ';'; docs/executor/PPCTrace.md)
// library.json beside the page (optional) offers more disks: [{ title,
// url, note, cover }]; the ones picked are remembered (the disks.mounted
// setting) and loaded like ?disk=. The Disks panel (disks.js; docs/executor/Disks.md)
// shows the System, the disks in the Mac and the library, and puts disks in
// and takes them out while Executor runs.
// Served by tools/standalone (api/me's mode; served by anything else the
// page says it needs it): the server is the only store: disks read by
// Range as needed, their changes and Executor's volume kept on the server
// (sync.js: Store), the last of them in a beacon when the page goes.
// Until Executor's first frame, loading.js shows the start's steps over the
// screen (the module's download from the Worker, 'progress').
// Uploads are this visit's: disk images held in memory, other files copied
// onto Executor's volume (kept with it). One tab is in control
// (api/session); the others show its screen until they take control or it
// goes. Outside a secure context (plain http to another host) there is no
// AudioWorklet: no sound.
//
// Automation: window.executor.request(op, arg) -> Promise. At the running
// application's next event call (web_main.cpp): 'app', 'windows', 'menus',
// 'drawn' (the strings drawn lately, oldest first), 'launch' "Volume:Folder:App". At once, by the Worker: 'read' PATH (text or null),
// 'write' [PATH, TEXT], 'list' PATH (names or null); Executor's own volume
// is /executor, its "Host" volume /host.
// window.executor.frames counts frames.
'use strict';

const box = document.getElementById('screen-box');
const status = document.getElementById('status');
const params = new URLSearchParams(location.search);
addEventListener('pagehide', storeLast);      // before log.js's: the beacons' room is shared
Log.start(Log.newId(), 'page');
// The screen and the scale Executor draws at, from the settings at each start.
let width = 640, height = 480, scale = 1;
function machine() {
    [width, height] = Settings.get('display.fit_window') ? windowSize()
        : Settings.get('machine.screen').split('x').map(Number);
    // Executor scales by whole numbers (fractional scales: HiDPI.md phase 6).
    scale = Math.max(1, Math.round(Settings.get('display.scale') || window.devicePixelRatio || 1));
}

let worker = null, canvas = null, cursor = null, nextId = 1;
// Bumped when a restart begins: messages from an older Worker (still
// drawing while it saves) no longer count.
let generation = 0;
const pending = new Map();
window.executor = {
    frames: 0,
    audioFrames: 0,     // 20 ms frames of sound received
    cursor: null,       // the guest's last cursor message
    stopped: null,      // why Executor stopped, when an error stopped it
    additions: {},      // disk -> its System Folder additions copied in (copied, kept, restart, live)
    cpu: null,          // the CPU meter: { busy (0..1, the last half second), history, memory (worker.js memoryUse), measured (the browser's figure, 0: none) }
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

// ---- the CPU meter in the top bar: the emulator thread's busy share
// (the time it isn't waiting for events: hostidle::busy), each half second
// from the Worker; a sparkline of the last 30 and the latest as a %. Beside
// it Executor's memory, an estimate (worker.js memoryUse), its parts in the
// tooltip. Hidden until the first frame and when Executor stops.
const MB = (n) => (n >= 1 << 30 ? `${(n / (1 << 30)).toFixed(1)} GB` : `${Math.round(n / (1 << 20))} MB`);
function memoryTip(m) {
    return [`Executor's memory: about ${MB(m.total)}`,
            `• the module's heap: ${MB(m.heap)} (WebAssembly memory: Executor, its caches, and the Mac's RAM, ${MB(m.ram)})`,
            `• disk caches: ${MB(m.caches)} (library disks' pieces read from the server)`,
            `• disk changes: ${MB(m.changes)} (what the Mac wrote to its disks, kept here)`,
            `• Executor's volume: ${MB(m.volume)} (its System Folder and files, in memory; estimated from the files' sizes, counted every 5 s)`,
            'Not counted: the browser\'s own (the page, its scripts, uploaded files), so the total is an estimate.',
            ...(measured ? [`The browser's own measurement of this tab: ${MB(measured)}`
                            + ` (performance.measureUserAgentSpecificMemory, ${Math.round((Date.now() - measuredAt) / 1000)} s ago)`] : [])]
        .join('\n');
}
// The browser's measurement, where it has one (cross-origin isolated
// pages, Chromium): slow (it may wait for a garbage collection), so every
// 10 s at most, one at a time.
let measured = 0, measuredAt = 0, measuring = false;
function measure() {
    if (measuring || Date.now() - measuredAt < 10000 || !window.crossOriginIsolated
        || !performance.measureUserAgentSpecificMemory)
        return;
    measuring = true;
    performance.measureUserAgentSpecificMemory()
        .then((r) => { measured = r.bytes; measuredAt = Date.now(); })
        .catch(() => { measuredAt = Date.now(); })
        .finally(() => { measuring = false; });
}
const CpuMeter = (() => {
    const el = document.getElementById('cpu');
    const pct = document.getElementById('cpu-pct');
    const mem = document.getElementById('mem');
    const ctx = el.querySelector('canvas').getContext('2d');
    const N = 30, history = [];
    function draw() {
        const { width: w, height: h } = ctx.canvas;
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = '#333';
        ctx.fillRect(0, 0, w, h);
        const step = w / N;
        history.forEach((v, i) => {
            ctx.fillStyle = v > 0.9 ? '#e96' : '#7af';
            const bh = Math.max(1, Math.round(v * h));
            ctx.fillRect(Math.floor((N - history.length + i) * step), h - bh, Math.ceil(step), bh);
        });
    }
    return {
        add(busy, memory) {
            history.push(busy);
            if (history.length > N)
                history.shift();
            pct.textContent = `${Math.round(busy * 100)}%`;
            if (memory) {
                measure();
                mem.textContent = MB(memory.total);
                mem.title = memoryTip(memory);
            }
            window.executor.cpu = { busy, history: [...history], memory, measured };
            el.hidden = false;
            draw();
        },
        reset() {
            history.length = 0;
            window.executor.cpu = null;
            el.hidden = true;
        },
    };
})();

// ---- the disks: uploads and ?disk= fetches, for this visit ----
let sessionFiles = [];

async function storedFiles() {
    return [...sessionFiles].sort((a, b) => a.name.localeCompare(b.name));
}

async function store(file) {
    sessionFiles = sessionFiles.filter((f) => f.name !== file.name).concat(file);
}

async function remove(name) {
    sessionFiles = sessionFiles.filter((f) => f.name !== name);
}

// A ?disk= is not downloaded first: the Worker reads it as Executor needs
// it (overlay.js: RemoteImage). Where the server gives no ranges (or
// ?lazy=0): downloaded into memory.
// A disk's identity, for its overlay's key (overlay.js): the server's
// content id (X-Image-Id, tools/standalone), name -> id.
const imageIds = {};
const plainKey = (name, size, time) => `${name}-${size}-${time}`.replace(/[^\w.-]/g, '_');

// starting: the progress to the loading panel's Disks step (else the status).
// Each disk image fetched: the url it came from, by its file's name (a
// library System disk put in and the panel's choice of it, by url).
const fetchedFrom = new Map();
async function fetchDisk(url, { starting = false } = {}) {
    const absolute = new URL(url, location.href).href;
    const name = decodeURIComponent(new URL(absolute).pathname.split('/').pop());
    fetchedFrom.set(name, absolute);
    const progress = (got, total) => {
        if (starting)
            return Loading.step('disks', { got, total });
        status.textContent = total ? `Loading ${name}… ${Math.floor(got * 100 / total)}%`
                                   : `Loading ${name}… ${(got / 1e6).toFixed(0)} MB`;
    };
    if (starting)
        Loading.step('disks', { detail: `${name}…` });
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
        if (size) {
            if (probe.headers.get('X-Image-Id'))
                imageIds[name] = probe.headers.get('X-Image-Id');
            return { name, size, lastModified: Date.parse(probe.headers.get('Last-Modified')) || 0,
                     remote: { url: absolute, etag: probe.headers.get('ETag'),
                               gzip: /\bgzip\b/.test(probe.headers.get('X-Range-Encodings') || '') } };
        }
    }

    // no-store: the browser's HTTP cache would keep a second copy.
    const res = await fetch(absolute, { cache: 'no-store' });
    if (!res.ok)
        throw new Error(`${name}: ${res.status}`);
    // Compressed in transit, Content-Length is the compressed size;
    // tools/standalone says the real one (X-Decoded-Length).
    const total = Number(res.headers.get('X-Decoded-Length'))
        || (res.headers.get('Content-Encoding') ? 0 : Number(res.headers.get('Content-Length')) || 0);
    const reader = res.body.getReader();
    let got = 0;
    const chunks = [];
    for (let r; !(r = await reader.read()).done;) {
        chunks.push(r.value);
        progress(got += r.value.length, total);
    }
    if (res.headers.get('X-Image-Id'))
        imageIds[name] = res.headers.get('X-Image-Id');
    return new File(chunks, name);
}

const IMAGE = /\.(img|image|dsk|hfs|iso|toast|cdr)$/i;
const systemSelect = document.createElement('select');
systemSelect.id = 'system';

// ---- the library: disks the site offers (library.json), picked by the user
// (the disks.mounted setting: the Disks panel, or the Settings dialog's
// list). The page follows the setting while Executor runs: a disk added
// goes into the Mac at once, one taken off comes out.
let library = [];
const picked = () => Settings.get('disks.mounted') || [];
Settings.provide('library', () => library);
const href = (u) => new URL(u, location.href).href;
const fileName = (url) => decodeURIComponent(new URL(url, location.href).pathname.split('/').pop());

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
    showSystems(shownFiles);        // (and the panel)
}

function addLibraryDisk(url) {
    if (url && !picked().some((u) => href(u) === href(url)))
        return Settings.set({ disks: { mounted: [...picked(), url] } });
}

// A library disk taken out is forgotten too.
function forgetLibraryDisk(name) {
    const keep = picked().filter((u) => fileName(u) !== name);
    if (keep.length !== picked().length)
        return Settings.set({ disks: { mounted: keep } });
}

async function fetchDisks() {
    await modeKnown;
    await Settings.ready;
    loadLibrary();
    const urls = [...params.getAll('disk'), ...picked()];
    let failed = 0;
    for (const url of urls) {
        try {
            const file = await fetchDisk(url, { starting: true });
            sessionFiles = sessionFiles.filter((f) => f.name !== file.name).concat(file);
        } catch (e) {
            failed++;
            status.textContent = `Cannot load ${url}: ${e.message}`;
        }
    }
    // (read by Range: as Executor needs them, not now)
    Loading.step('disks', { state: failed ? 'error' : 'done',
                            detail: !urls.length ? 'none' : failed ? `${failed} of ${urls.length} not loaded`
                                  : urls.length === 1 ? '1 disk' : `${urls.length} disks` });
}

// Disk images are writable through an overlay (overlay.js) kept by the
// server, per exact version of the image (the Worker computes the same key).
function imageKey(f) {
    return imageIds[f.name] ? `${f.name}-${imageIds[f.name]}`.replace(/[^\w.-]/g, '_')
                            : plainKey(f.name, f.size, f.lastModified);
}
let imageState = {};                // name -> { modified, changed, writable, onServer }, from the Worker

// ---- disks in and out of the running Mac ----
// Executor running: a disk goes in at once (the Worker opens it, Executor
// mounts it as a floppy put in: web_main.cpp 'mount'); not yet, at the start.
const live = () => !!worker && window.executor.frames > 0;
const inserting = new Set();        // names on their way in
const reverting = new Set();        // taken out to be put back
const ejectWaits = new Map();       // name -> resolve, for 'ejected'
const FBSY = -47, NSV = -35;
// CDs are read-only (Executor mounts them locked).
const isCD = (name) => /\.(iso|toast|cdr)$/i.test(name)
    || library.some((d) => fileName(d.url) === name && (d.tags || []).includes('cd'));

// A disk's System Folder additions (web_main.cpp) go in only on a System
// its record lists: the library System's own ("systems" of a boot disk is
// its version). An 8.6 disk's libraries in 7.6.1's System Folder, or a
// disk or System not from the library: none.
function additionsFor(name) {
    const rec = (n) => library.find((d) => fileName(d.url) === n);
    const sys = rec(systemSelect.value), disk = rec(name);
    const version = sys && (sys.systems || []).length === 1 && sys.systems[0];
    return !!(version && disk && (disk.systems || []).includes(version));
}

async function putIn(file) {
    if (!live())
        return start();
    inserting.add(file.name);
    showDisks();
    const r = await window.executor.request('insert', { file, cd: isCD(file.name), ids: { ...imageIds },
                                                         additions: additionsFor(file.name) });
    inserting.delete(file.name);
    if (r !== 0)
        status.textContent = `${file.name}: not mounted (${r === NSV ? 'no Mac volume on it' : r === -1 ? 'cannot open it'
                                                           : (r && r.error) || r})`;
    showFiles(await storedFiles());
}

// Out of the Mac: 0 when it is (the Worker has saved its changes and let it
// go: 'ejected'), else the OSErr (fBsyErr: in use).
function takeOut(name) {
    return new Promise((resolve) => {
        ejectWaits.set(name, resolve);
        window.executor.request('eject', name).then((r) => {
            if (r !== 0 && r !== NSV) {
                ejectWaits.delete(name);
                resolve(r && r.error ? -1 : r);
            }
        });
    });
}

// Ejected, here or in the Finder: out of the panel. A library disk (an
// upload too: it is in the library) is forgotten from the setting, still
// in the library; one held for this visit only is dropped.
async function ejected(name) {
    delete imageState[name];
    const wait = ejectWaits.get(name);
    ejectWaits.delete(name);
    if (wait)
        wait(0);
    if (reverting.has(name) || name === systemSelect.value)
        return;
    shownFiles = shownFiles.filter((f) => f.name !== name);    // (before the setting changes: followPicked)
    await remove(name);
    await forgetLibraryDisk(name);
    showFiles(await storedFiles());
}

async function eject(name) {
    if (!IMAGE.test(name) || !live()) {
        // An application, or Executor not running: off the list (an
        // application stays in "executor:Uploads" until Executor restarts).
        await ejected(name);
        return;
    }
    const r = await takeOut(name);
    if (r === FBSY)
        status.textContent = `${name} is in use: quit what has files open on it, then eject it`;
    else if (r !== 0)
        status.textContent = `${name}: not ejected (${r})`;
}

// Followed while Executor runs: library disks added (here, in the Settings
// dialog, by an upload) go in; ones taken off come out.
async function followPicked(urls) {
    if (!live())
        return showDisks();
    const want = new Set(urls.map(href));
    for (const url of urls) {
        const name = fileName(url);
        if (shownFiles.some((f) => f.name === name) || inserting.has(name))
            continue;
        inserting.add(name);
        showDisks();
        try {
            const file = await fetchDisk(url);
            sessionFiles = sessionFiles.filter((f) => f.name !== file.name).concat(file);
            inserting.delete(name);
            await putIn(file);
        } catch (e) {
            inserting.delete(name);
            status.textContent = `Cannot load ${url}: ${e.message}`;
            showDisks();
        }
    }
    for (const d of library)
        if (!want.has(href(d.url)) && shownFiles.some((f) => f.name === fileName(d.url)) && !ejectWaits.has(fileName(d.url))
            && fileName(d.url) !== systemSelect.value && !params.getAll('disk').some((u) => href(u) === href(d.url)))
            takeOut(fileName(d.url));
}
Settings.on('disks.mounted', (v) => followPicked(v || []));

// ---- the Disks panel (disks.js): what is in the Mac, from the files and
// the Worker's word on each image, its title and cover from the library.
let shownFiles = [];

function showFiles(files) {
    shownFiles = files;
    showSystems(files);             // (and the panel)
}

function showDisks() {
    const byName = new Map(library.map((d) => [fileName(d.url), d]));
    const kind = (name, size) => !IMAGE.test(name) ? 'app' : isCD(name) ? 'cd'
        : (size || 0) <= 1440 * 1024 ? 'floppy' : 'hd';
    const mounted = shownFiles.filter((f) => f.name !== systemSelect.value).map((f) => {
        const d = byName.get(f.name) || {};
        return { name: f.name, title: d.title || f.name, cover: d.cover, kind: kind(f.name, f.size),
                 state: imageState[f.name], inserting: inserting.has(f.name) };
    });
    for (const name of inserting)
        if (!mounted.some((m) => m.name === name)) {
            const d = byName.get(name) || {};
            mounted.push({ name, title: d.title || name, cover: d.cover, kind: kind(name, d.size), inserting: true });
        }
    const sel = systemSelect.selectedOptions[0];
    const sys = shownFiles.find((f) => f.name === systemSelect.value);
    Disks.update({ system: { value: systemSelect.value, label: sel ? sel.textContent : "Executor's own",
                             disk: sys && { name: sys.name, kind: 'hd', state: imageState[sys.name] } },
                   mounted, library, mountedUrls: [...params.getAll('disk'), ...picked()] });
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

// Revert: the disk out of the Mac, the server forgets its changes, back in
// as it came. In use (or the System's): the Worker goes without saving and
// Executor starts again on the original.
async function revert(f) {
    if (!confirm(`Forget all changes to ${f.name}? The disk goes back to how it came.`))
        return;
    if (live() && f.name !== systemSelect.value) {
        reverting.add(f.name);
        const r = await takeOut(f.name);
        if (r === 0) {
            await fetch(`api/sync/overlays/${imageKey(f)}`, { method: 'DELETE', cache: 'no-store' }).catch(() => {});
            reverting.delete(f.name);
            delete imageState[f.name];
            await putIn(f);
            return;
        }
        reverting.delete(f.name);
        if (r === FBSY && !confirm(`${f.name} is in use. Restart Executor to revert it?`))
            return;
    }
    generation++;
    if (worker)
        worker.terminate();
    worker = null;
    delete imageState[f.name];
    await fetch(`api/sync/overlays/${imageKey(f)}`, { method: 'DELETE', cache: 'no-store' }).catch(() => {});
    start();
}

// A disk image the user adds goes into the server's library
// (tools/standalone uploads.go) and is mounted from there, so it and its
// changes are kept; resolves to its library entry. Rejects with .status
// (409: there is one of that name, { replace: true } replaces it).
async function uploadImage(file, { replace = false } = {}) {
    status.textContent = `Uploading ${file.name}…`;
    const res = await fetch(`api/uploads/${encodeURIComponent(file.name)}${replace ? '?replace=1' : ''}`,
                            { method: 'POST', body: file, cache: 'no-store' });
    if (!res.ok) {
        const e = new Error((await res.json().catch(() => ({}))).error || `${res.status}`);
        e.status = res.status;
        throw e;
    }
    const entry = await res.json();
    await loadLibrary();
    await Settings.set({ disks: { mounted: [...picked().filter((u) => u !== entry.url), entry.url] } });
    showLibrary();
    const disk = await fetchDisk(entry.url);
    sessionFiles = sessionFiles.filter((f) => f.name !== disk.name).concat(disk);
    return entry;
}

// Added by the panel's button or dropped: disk images uploaded into the
// library and put in (while Executor runs: followPicked, at once); an
// application (into "executor:Uploads") restarts Executor, whose volume
// doesn't see a file put there under it.
async function addFiles(files) {
    const restart = !live() || files.some((f) => !IMAGE.test(f.name));
    if (restart) {
        generation++;               // restarting (once the files are stored)
        window.executor.frames = 0;
    }
    for (const f of files) {
        if (!IMAGE.test(f.name)) {
            await store(f);
            continue;
        }
        try {
            await uploadImage(f).catch((err) => {
                if (err.status === 409 && confirm(`Replace the uploaded ${f.name} (and its changes)?`))
                    return uploadImage(f, { replace: true });
                throw err;
            });
        } catch (err) {
            status.textContent = `${f.name} isn't kept on the server (${err.message}): for this visit only`;
            await store(f);
            if (!restart)
                await putIn(f);
        }
    }
    if (restart)
        return start();
    showFiles(await storedFiles());
}
document.getElementById('add').addEventListener('change', async (e) => {
    const files = [...e.target.files];
    e.target.value = '';
    addFiles(files);
});

// One's upload deleted from the server's library (tools/standalone: the
// image, its record and its overlay).
async function removeUpload(d) {
    if (!confirm(`Delete ${d.title} from the server? The image and its changes are gone.`))
        return;
    const res = await fetch(`api/uploads/${encodeURIComponent(fileName(d.url))}`, { method: 'DELETE', cache: 'no-store' })
        .catch(() => null);
    if (!res || !res.ok)
        status.textContent = `${d.title}: not deleted (${res ? res.status : 'the server can\'t be reached'})`;
    await loadLibrary();
}

Disks.init({
    add: addFiles,
    insert: (d) => addLibraryDisk(d.url),
    removeUpload,
    eject,
    revert(name) {
        const f = shownFiles.find((x) => x.name === name);
        if (f)
            revert(f);
    },
    download,
    reset,
    systemSelect,
});
document.getElementById('restart').addEventListener('click', () => start());

// Executor's own volume is kept per System choice (worker.js, volumes/KEY):
// Executor's own, or a disk image's by name and size.
function volumeKey(files) {
    const sys = files.find((f) => f.name === systemSelect.value);
    return sys ? `${sys.name}-${sys.size}`.replace(/[^\w.-]/g, '_') : 'own';
}

// Screenshots, to the server beside this session's log (/api/shot): the
// canvas's own pixels (the display scale) and Executor's 1x frame (when
// Executor still answers), with a note. The button asks for the note;
// errors take one by themselves (autoShot), a few a session at most.
function pngOf(p) {
    const c = document.createElement('canvas');
    c.width = p.width;
    c.height = p.height;
    c.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(p.data), p.width, p.height), 0, 0);
    return new Promise((resolve) => c.toBlob(resolve, 'image/png'));
}

async function postShot(png, note, tag = '') {
    try {
        const res = await fetch(`/api/shot?session=${Log.session}&note=${encodeURIComponent(note)}${tag ? '&tag=' + tag : ''}`, {
            method: 'POST', body: png, credentials: 'same-origin', headers: { 'Content-Type': 'image/png' } });
        return res.ok ? (await res.json()).name : `not sent (${res.status})`;
    } catch {
        return 'not sent (offline)';
    }
}

const within = (promise, ms) => Promise.race([promise, new Promise((r) => setTimeout(() => r(null), ms))]);

async function sendShots(note) {
    if (!worker)
        return 'no screen';
    const scaled = await within(window.executor.request('pixels'), 3000);
    if (!scaled || !scaled.width)
        return 'no screen';
    const name = await postShot(await pngOf(scaled), note);
    if (scale !== 1) {
        const base = await within(window.executor.request('screen1x'), 3000);   // Executor gone: none
        if (base && base.width)
            await postShot(await pngOf(base), `1x of ${name}`, '1x');
    }
    console.log(`[web] screenshot ${name}${note ? ': ' + note : ''}`);
    return name;
}

document.getElementById('shot').addEventListener('click', async () => {
    const note = prompt('A note to go with the screenshot (optional):', '');
    if (note === null)
        return;
    const name = await sendShots(note);
    status.textContent = /^\d/.test(name) ? `Screenshot sent (${name})` : `Screenshot ${name}`;
});

let autoShots = 0, lastAutoShot = 0;
function autoShot(why) {
    if (autoShots >= 8 || performance.now() - lastAutoShot < 20000)
        return;
    autoShots++;
    lastAutoShot = performance.now();
    sendShots(`auto: ${why}`.slice(0, 400));
}

// Watching from the server: which application is in front (when it
// changes), and Executor no longer answering (3 requests in a row: hung,
// or the module died).
let lastApp = null, unanswered = 0;
setInterval(async () => {
    if (!worker || document.hidden || !window.executor.frames)
        return;
    const app = await within(window.executor.request('app'), 2500);
    if (app === null) {
        if (++unanswered === 3) {
            console.log('[watch] Executor not answering');
            autoShot('Executor not answering');
        }
        return;
    }
    if (unanswered >= 3)
        console.log('[watch] Executor answering again');
    unanswered = 0;
    if (app !== lastApp) {
        console.log(`[watch] front: ${app || '(none)'}`);
        lastApp = app;
    }
}, 3000);

async function reset() {
    if (!confirm("Forget everything saved on Executor's disk for this System?"))
        return;
    if (worker)
        worker.terminate();
    worker = null;
    await fetch(`api/sync/volumes/${volumeKey(await storedFiles())}`, { method: 'DELETE', cache: 'no-store' }).catch(() => {});
    start();
}

// Leaving or hiding the page: the Worker saves what changed now.
document.addEventListener('visibilitychange', () => {
    if (!worker)
        return;
    if (document.visibilityState === 'hidden')
        worker.postMessage({ type: 'save' });
});

// The System to run on: Executor's own (''), a disk image's (its name), or
// a library System disk's (its url: put in, then that), remembered across
// visits (machine.system: the image's name). Chosen in the Disks panel
// (disks.js) or Settings > Machine; the library System disk the last
// choice put in comes out of disks.mounted when the choice changes.
const systemDisks = () => library.filter((d) => (d.tags || []).includes('system'));
const isSystemDisk = (u) => systemDisks().some((d) => href(d.url) === href(u));
// The library System disk a file NAME came from (its url), if it did.
const systemUrlOf = (name) => {
    const u = name && fetchedFrom.get(name);
    return u && isSystemDisk(u) ? u : undefined;
};
let lastSystem, lastSystemUrl;      // the System as last chosen: its name, its library disk's url
async function pickSystem(value) {
    // a library System disk: by its url, or by the name of the one in already
    const url = value.includes('/') ? value : systemUrlOf(value);
    const d = url ? systemDisks().find((x) => href(x.url) === href(url)) : null;
    const prev = lastSystemUrl || systemUrlOf(lastSystem === undefined ? Settings.get('machine.system') : lastSystem);
    let mounted = picked();
    if (prev && !(d && href(prev) === href(d.url)))
        mounted = mounted.filter((u) => href(u) !== href(prev));
    // (one in already, as a disk of its own, stays in after: not ours)
    const ours = d && !mounted.some((u) => href(u) === href(d.url));
    if (ours)
        mounted = [...mounted, d.url];
    lastSystem = d ? fileName(d.url) : value;
    lastSystemUrl = ours ? d.url : (d && prev && href(prev) === href(d.url) ? prev : undefined);
    await Settings.set({ machine: { system: lastSystem }, disks: { mounted } });
    return d;
}
systemSelect.addEventListener('change', async () => {
    generation++;
    await saveAndStop();            // (not live: the disk waits for the start)
    const d = await pickSystem(systemSelect.value).catch(() => null);
    if (d) {
        try {
            const file = await fetchDisk(d.url);
            sessionFiles = sessionFiles.filter((f) => f.name !== file.name).concat(file);
        } catch (e) {
            status.textContent = `Cannot load ${d.url}: ${e.message}`;
        }
    }
    start();
});
// The Settings dialog's System choices: the panel's. A library System disk
// not in yet by its url (chosen there, it goes in: below).
Settings.provide('systems', () => [
    ...shownFiles.filter((f) => IMAGE.test(f.name) && !isCD(f.name))
        .map((f) => ({ value: f.name, label: (library.find((d) => fileName(d.url) === f.name) || {}).title || f.name })),
    ...systemDisks().filter((d) => !shownFiles.some((f) => f.name === fileName(d.url)))
        .map((d) => (picked().some((u) => href(u) === href(d.url))
            ? { value: fileName(d.url), label: d.title }                   // (in, on its way)
            : { value: d.url, label: `${d.title} (from the library)` })),
]);
// One source of truth: the machine.system setting. The panel's choice
// writes it (pickSystem); one from elsewhere (the Settings dialog) goes
// through pickSystem too (a library disk by url: put in, the name kept;
// the last System's library disk out), and shows in the panel.
Settings.on('machine.system', (v) => {
    if (lastSystem === undefined && !(v || '').includes('/'))
        lastSystem = v || '';           // (the saved one, as the page loads)
    else if (v !== lastSystem)
        pickSystem(v || '').catch(() => {});
    showSystems(shownFiles);
});

// Fat applications' PowerPC code (mac-phoenix's --executor-ppc), remembered.
const ppcBox = document.getElementById('ppc');
Settings.on('machine.ppc', (v) => { ppcBox.checked = v; });
ppcBox.addEventListener('change', () => {
    Settings.set({ machine: { ppc: ppcBox.checked } }).finally(start);
});

function showSystems(files) {
    const want = Settings.get('machine.system');
    const title = (name) => (library.find((d) => fileName(d.url) === name) || {}).title;
    const offered = library.filter((d) => (d.tags || []).includes('system') && !files.some((f) => f.name === fileName(d.url)));
    systemSelect.replaceChildren(new Option("Executor's own", ''),
        ...files.filter((f) => IMAGE.test(f.name) && !isCD(f.name)).map((f) => new Option(title(f.name) || f.name, f.name)),
        ...offered.map((d) => new Option(`${d.title} (from the library)`, d.url)));
    const opt = [...systemSelect.options].find((o) => o.value === want || (o.value.includes('/') && fileName(o.value) === want));
    systemSelect.value = opt ? opt.value : '';
    showDisks();
    Settings.choicesChanged();      // (the Settings dialog's list, if open)
}

// ---- the server: tools/standalone (api/me's mode), the tab's store ----
let standalone = false;
const modeKnown = (async () => {
    let me = null, code = 0;
    try {
        const res = await fetch('api/me', { cache: 'no-store' });
        code = res.status;
        me = res.ok ? await res.json() : null;
    } catch { }
    standalone = !!me && me.mode === 'standalone';
    if (code === 401 || code === 403)
        lostClaim(code);
    else if (!standalone)
        status.textContent = "Executor's tab needs its server: tools/standalone (docs/executor/Standalone.md)";
})();

// ---- standalone: one tab in control (tools/standalone's session.go) ----
// This tab says it is there every BEAT ms while it watches; in control,
// its Worker does (its timers aren't held back in a background tab) and
// says what the server answered ('session'). Taken over: saved, stopped,
// let go, watching. The beacons: what the Worker hasn't stored yet.
const BEAT = 2000, HANDOVER = 6000;
let control = false, watching = null, storeBeacons = [];
const viewer = document.getElementById('viewer');

async function beat(take = false) {
    try {
        const res = await fetch(`api/session?id=${Log.session}${take ? '&take=1' : ''}`,
                                { method: 'POST', cache: 'no-store' });
        if (res.status === 401 || res.status === 403)
            lostClaim(res.status);
        return res.ok ? await res.json() : null;
    } catch {
        return null;
    }
}

// tools/standalone (claim.go): the first browser to load the tab has the
// server until it stops. Refused now (401: it restarted; 403: and another
// browser has it), this tab's cookie opens nothing: a reload claims it
// again. Said once, and kept over other notices.
let claimLost = false;
function lostClaim(code) {
    if (claimLost)
        return;
    claimLost = true;
    const reload = document.createElement('button');
    reload.id = 'reload';
    reload.textContent = 'Reload';
    reload.onclick = () => location.reload();
    Loading.end();
    status.replaceChildren(code === 403 ? 'The server has restarted and another browser has it now. '
                                        : 'The server has restarted: reload this page to go on. ', reload);
    status.classList.add('lost');
}

// In control (s: the server's answer): after the tab it was taken from
// let go (it saves first), Executor starts.
async function takeControl(s) {
    control = true;
    clearTimeout(watching);
    watching = null;
    viewer.hidden = true;
    for (const end = Date.now() + HANDOVER; s && s.handing && Date.now() < end;) {
        await new Promise((r) => setTimeout(r, 300));
        s = await beat();
    }
    start();
}

// Another tab is in control: its screen, a PNG a second, until this one
// takes control (the button) or gets it (the other one gone).
async function watch() {
    const s = await beat();
    if (s && s.control)
        return takeControl(s);
    viewer.hidden = false;
    Loading.end();
    if (!claimLost)
        status.textContent = s ? 'Executor is open in another tab: watching it here' : 'The server can\'t be reached';
    let img = box.querySelector('img');
    if (!img) {
        img = document.createElement('img');
        img.id = 'screen';
        box.replaceChildren(img);
    }
    img.style.width = `${width}px`;
    const next = new Image();
    next.onload = () => { img.src = next.src; };
    next.src = `api/session/screen?t=${Date.now()}`;
    watching = setTimeout(watch, BEAT);
}

document.getElementById('take').addEventListener('click', async () => {
    const s = await beat(true);
    if (s && s.control)
        takeControl(s);
});

// Executor stopped by an error (worker.js 'stopped': a fatal error of its
// own, an abort, the wasm trapping): said here, with Restart, and the page
// goes on (what the disks changed is kept as before).
function executorStopped(reason) {
    window.executor.stopped = reason;
    CpuMeter.reset();
    console.log(`[web] Executor stopped: ${reason}`);
    autoShot(`stopped: ${reason}`);
    Loading.end();
    if (claimLost)
        return;
    const again = document.createElement('button');
    again.id = 'restart-stopped';
    again.textContent = 'Restart';
    again.onclick = () => start();
    status.replaceChildren(`Executor stopped: ${reason} `, again);
    status.classList.add('lost');
    status.setAttribute('role', 'alert');
}

async function lostControl() {
    control = false;
    generation++;
    await saveAndStop();
    navigator.sendBeacon(`api/session/release?id=${Log.session}`);
    watch();
}

function storeLast() {
    if (!standalone || !control)
        return;
    for (const b of storeBeacons)
        navigator.sendBeacon(b.url, b.body);
    navigator.sendBeacon(`api/session/release?id=${Log.session}`);
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

// The Settings dialog's "Restart Executor": what changed, saved, and the
// page again (the library's disks too).
Settings.restart = async () => {
    generation++;
    await saveAndStop();
    location.reload();
};

async function start() {
    await Settings.ready;
    machine();
    await modeKnown;
    if (!standalone)
        return;                     // (modeKnown said why)
    if (!control)
        return watching || watch();
    const mine = ++generation;
    window.executor.frames = 0;     // "running" means the new Worker's frames
    await saveAndStop();
    if (mine !== generation)
        return;                     // another restart began meanwhile
    for (const resolve of pending.values())
        resolve({ error: 'restarted' });
    pending.clear();
    window.executor.frames = 0;
    window.executor.stopped = null;
    if (!claimLost) {
        status.classList.remove('lost');
        status.removeAttribute('role');
    }
    status.textContent = '';
    CpuMeter.reset();
    Loading.begin();
    box.replaceChildren(Loading.el);

    // The Worker loads the module while the disks still download; it
    // says how far it has got from the start ('progress': loading.js).
    worker = new Worker('worker.js');
    worker.addEventListener('message', (e) => {
        if (e.data.type === 'progress' && mine === generation)
            Loading.step(e.data.step, e.data);
    });
    worker.addEventListener('error', (e) => {
        console.error(`worker: ${e.message} at ${e.filename}:${e.lineno}`);
        autoShot(e.message);
    });
    await disksFetched;
    const files = await storedFiles();
    showFiles(files);

    // A canvas hands its control over once: a fresh one per start.
    window.executor.screen = { width, height };
    canvas = document.createElement('canvas');
    canvas.id = 'screen';
    canvas.width = width;
    canvas.height = height;
    canvas.tabIndex = 0;
    box.replaceChildren(canvas, Loading.el);       // (over the screen until its first frame)
    fitCanvas();
    attachInput(canvas);
    cursor = new MacCursor(canvas, () => width);

    const offscreen = canvas.transferControlToOffscreen();
    worker.postMessage({ type: 'start', canvas: offscreen, width, height, scale, files,
                         system: systemSelect.value, ppc: ppcBox.checked,
                         volumeKey: volumeKey(files), relay: standalone,
                         standalone,
                         ids: imageIds, cds: files.map((f) => f.name).filter(isCD), log: Log.session,
                         additions: files.map((f) => f.name).filter(additionsFor),
                         ram: Settings.get('machine.ram_mb'), depth: Settings.get('machine.depth'),
                         logtraps: Settings.get('advanced.logtraps'),
                         nesting: String(Settings.get('advanced.logtraps_nesting')),
                         debug: Settings.get('advanced.debug'), ppcbreak: Settings.get('advanced.ppc_break') },
                       [offscreen]);
    Settings.started();
    worker.onmessage = (e) => {
        if (mine !== generation)
            return;
        const m = e.data;
        if (m.type === 'frame') {
            // (a notice from the Worker before it stays)
            if (window.executor.frames++ === 0) {
                Loading.end();
                if (!status.textContent)
                    status.textContent = 'Executor';
            }
        } else if (m.type === 'cpu') {
            if (window.executor.frames > 0 && !window.executor.stopped)
                CpuMeter.add(m.busy, m.memory);
        } else if (m.type === 'images') {
            const before = JSON.stringify(imageState);
            for (const i of m.images)
                imageState[i.name] = { modified: i.modified, changed: i.changed, writable: i.writable, onServer: i.onServer };
            if (JSON.stringify(imageState) !== before)
                storedFiles().then(showFiles);
        } else if (m.type === 'screen') {
            // The Mac's screen isn't what the settings asked for (an
            // application changed it): fit and map clicks with the real one.
            console.log(`[web] the Mac's screen: ${m.width}x${m.height}`);
            width = m.width;
            height = m.height;
            window.executor.screen = { width, height };
            fitCanvas();
            cursor.apply();
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
            if (!claimLost && !window.executor.stopped)
                status.textContent = m.text;
        } else if (m.type === 'additions') {
            // What a disk's applications put in the System Folder, copied
            // in (web_main.cpp); put in while running, some of it (fonts,
            // startup code) is in use only after a restart.
            window.executor.additions[m.name] = m;
            if (m.live && m.restart.length && !claimLost && !window.executor.stopped)
                status.textContent = `${m.name}: ${m.copied} files added to the System Folder; `
                    + `restart to use ${m.restart.join(', ')}`;
        } else if (m.type === 'ejected') {
            ejected(m.name);
        } else if (m.type === 'restart') {
            start();
        } else if (m.type === 'beacons') {
            storeBeacons = m.items;
        } else if (m.type === 'claim') {
            lostClaim(m.status);
        } else if (m.type === 'session') {
            if (!m.s.control && control)
                lostControl();
        } else if (m.type === 'error') {
            autoShot(m.text);
        } else if (m.type === 'stopped') {
            executorStopped(m.reason);
        } else if (m.type === 'exit') {
            if (!window.executor.stopped)
                status.textContent = `Executor exited (${m.code})`;
        }
    };
}

// ---- sound: an AudioWorklet (audio.js) fed 48 kHz stereo from the Worker.
// Browsers start an AudioContext only after a click or key press. The
// volume (the sound settings) applies at once.
let audio = null, gain = null;
const loudness = () => (Settings.get('sound.enabled') ? Settings.get('sound.volume') / 100 : 0);
async function startAudio() {
    if (audio || !window.isSecureContext || typeof AudioWorkletNode === 'undefined' || !Settings.get('sound.enabled'))
        return;     // AudioWorklet: secure contexts only
    const ctx = new AudioContext({ sampleRate: 48000 });
    await ctx.audioWorklet.addModule('audio.js');
    audio = new AudioWorkletNode(ctx, 'executor-audio', { outputChannelCount: [2] });
    gain = new GainNode(ctx, { gain: loudness() });
    audio.connect(gain).connect(ctx.destination);
    await ctx.resume();
}
Settings.on('sound.volume', () => gain && (gain.gain.value = loudness()));
Settings.on('sound.enabled', () => gain && (gain.gain.value = loudness()));

// ---- the canvas's size: the largest whole number of device pixels a guest
// pixel that fits the window, so every pixel is the same size (a
// fractional scale makes some one device pixel and some two); below one,
// what fits.
// The room the screen has in the window, CSS pixels: below the bar and the
// status line, beside the Disks panel when it is open.
function room() {
    const bar = document.fullscreenElement ? 0 : document.getElementById('bar').offsetHeight
        + document.getElementById('status').offsetHeight + 32;
    const across = window.innerWidth - (parseFloat(getComputedStyle(document.body).paddingRight) || 0);
    return [across, window.innerHeight - bar];
}

function fitCanvas() {
    if (!canvas)
        return;
    const dpr = window.devicePixelRatio || 1;
    const [across, down] = room();
    const fit = Math.min(across * dpr / width, down * dpr / height);
    // device pixels a guest pixel; in a window smaller than the screen, the
    // screen made smaller to fit (a cropped one left the menu bar and an
    // edge out of reach: 1024x768 in an 800-pixel window at dpr 1). Never
    // below a CSS pixel when that fits, nor smaller than the room when it
    // doesn't: whole device pixels alone drew 1280x1024 at dpr 2 in a
    // 1440x900 window a device pixel each, a quarter of the room.
    const scale = Math.max(Math.floor(fit), Math.min(fit, dpr));
    canvas.style.width = `${width * scale / dpr}px`;
    canvas.style.height = `${height * scale / dpr}px`;
}
window.addEventListener('resize', fitCanvas);

// ---- the Mac's screen size, live (docs/executor/DisplayModes.md): Executor
// changes it as the Display Manager does, and the page follows its
// 'screen' message (canvas, clicks, pointer).
// Fit the screen to the window: a Mac pixel for each CSS pixel of the room
// there is, even sizes, from 512x342 to the largest Executor keeps a
// framebuffer for (1920x1200 in the browser).
function windowSize() {
    const [across, down] = room();
    const even = (v) => Math.floor(v / 2) * 2;
    return [Math.min(1920, Math.max(512, even(across))), Math.min(1200, Math.max(342, even(down)))];
}

function resizeMac(w, h) {
    if (!worker || (w === width && h === height))
        return;
    console.log(`[web] screen size asked: ${w}x${h}`);
    window.executor.request('resize', `${w}x${h}`);
}

// The window's size settles before the Mac follows: one change for a drag.
let followTimer = null;
function followWindow() {
    clearTimeout(followTimer);
    followTimer = setTimeout(() => {
        if (Settings.get('display.fit_window'))
            resizeMac(...windowSize());
    }, 400);
}
window.addEventListener('resize', followWindow);
document.addEventListener('fullscreenchange', followWindow);
Settings.on('display.fit_window', (on) => {
    if (on)
        followWindow();
    else
        resizeMac(...Settings.get('machine.screen').split('x').map(Number));
});
Settings.on('machine.screen', (v) => {
    if (!Settings.get('display.fit_window'))
        resizeMac(...v.split('x').map(Number));
});
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
    // The right button: a click, or a Control-click (input.right_click).
    let controlClick = false;
    canvas.addEventListener('pointerdown', (e) => {
        startAudio();
        canvas.focus();
        canvas.setPointerCapture(e.pointerId);
        const at = guestPoint(e);
        controlClick = e.button === 2 && Settings.get('input.right_click') === 'control';
        console.log(`[in] down ${at}${controlClick ? ' control' : ''}`);
        send(...(controlClick ? [[3, 1, 0x36]] : []), [1, ...at], [2, 1, 0]);
        e.preventDefault();
    });
    canvas.addEventListener('pointerup', (e) => {
        const at = guestPoint(e);
        console.log(`[in] up ${at}`);
        send([1, ...at], [2, 0, 0], ...(controlClick ? [[3, 0, 0x36]] : []));
        controlClick = false;
        e.preventDefault();
    });
    canvas.addEventListener('contextmenu', (e) => e.preventDefault());
    // The wheel and trackpad: lines (a notch's 100 pixels about 3, a page
    // 15: Executor clicks a page part from 15 on) to Executor, which
    // clicks the front window's scroll bar under the pointer
    // (osevent/wheel.cpp; input.wheel_scroll). A trackpad's fractions add up.
    const wheelLines = [0, 0];
    canvas.addEventListener('wheel', (e) => {
        if (!Settings.get('input.wheel_scroll'))
            return;
        e.preventDefault();
        const unit = e.deltaMode === 1 ? 1 : e.deltaMode === 2 ? 15 : 1 / 33;
        wheelLines[0] += e.deltaX * unit;
        wheelLines[1] += e.deltaY * unit;
        const [h, v] = wheelLines.map(Math.trunc);
        if (!h && !v)
            return;
        wheelLines[0] -= h;
        wheelLines[1] -= v;
        send([1, ...guestPoint(e)], [4, h, v]);
    }, { passive: false });
    const onKey = (down, e) => {
        const code = macKeycodeForEvent(e);
        if (code === null)
            return;
        if (down)
            console.log(`[in] key ${['ctrlKey', 'altKey', 'metaKey', 'shiftKey'].filter((m) => e[m]).map((m) => m.slice(0, -3) + '+').join('')}${e.code} (${code})`);
        send([3, down ? 1 : 0, code]);
        e.preventDefault();
    };
    canvas.addEventListener('keydown', (e) => { startAudio(); if (!e.repeat) onKey(true, e); });
    canvas.addEventListener('keyup', (e) => onKey(false, e));
}

const disksFetched = fetchDisks();
start();
