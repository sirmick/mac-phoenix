// The page side: keep uploads in OPFS, start the Worker with them, hand it
// a canvas, send it input. The settings (settings.js: the Settings dialog,
// kept by the server or in this browser) say how; the query string
// overrides some of them for the visit:
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
//   ?logtraps=LIST   Executor logs Toolbox calls (as --executor-logtraps):
//                    LIST the names (wildcards, -NAME leaves out; 1: all);
//                    &nesting=N calls nested N deep (default 6); ?debug=OPTS Executor's
//                    --executor-debug options. Everything printed goes to the
//                    server too (log.js; tools/web/server keeps it per session)
//   ?ppcbreak=SPEC   PowerPC breakpoints (as --executor-ppc-break; several
//                    separated by ';'; docs/executor/PPCTrace.md)
// library.json beside the page (optional) offers more disks: [{ title,
// url, note, cover }]; the ones picked are remembered (the disks.mounted
// setting) and loaded like ?disk=. Disks with a cover (CDs) are offered by it too.
// Served by tools/web/server, signed in (api/me): the overlays and
// Executor's own volume are synced with the account (sync.js).
// Standalone (api/me's mode; the server on one's own machine): the server
// is the only store, no OPFS: disks read by Range as needed, their changes
// and Executor's volume kept on the server (sync.js: Store), the last of
// them in a beacon when the page goes. One tab is in control (api/session);
// the others show its screen until they take control or it goes.
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
addEventListener('pagehide', storeLast);      // before log.js's: the beacons' room is shared
Log.start(Log.newId(), 'page');
// The screen and the scale Executor draws at, from the settings at each start.
let width = 640, height = 480, scale = 1;
function machine() {
    [width, height] = Settings.get('machine.screen').split('x').map(Number);
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
let opfs = window.isSecureContext && navigator.storage && navigator.storage.getDirectory;
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
        if (size && (lazy === '1' || standalone || size >= LAZY_MIN)) {
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
let library = [];
const picked = () => Settings.get('disks.mounted') || [];
Settings.provide('library', () => library);

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
    showDisks();
}

async function addLibraryDisk(url) {
    if (!url || picked().some((u) => new URL(u, location.href).href === new URL(url, location.href).href))
        return;
    await Settings.set({ disks: { mounted: [...picked(), url] } });
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
}

// A library disk the user removes is forgotten too.
function forgetLibraryDisk(name) {
    const keep = picked().filter((u) => decodeURIComponent(new URL(u, location.href).pathname.split('/').pop()) !== name);
    Settings.set({ disks: { mounted: keep } }).then(showLibrary);
}

async function fetchDisks() {
    await modeKnown;
    await Settings.ready;
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

// The Disks panel (disks.js): what is in the Mac, from the files and the
// Worker's word on each image, its title and cover from the library.
let shownFiles = [];
const fileName = (url) => decodeURIComponent(new URL(url, location.href).pathname.split('/').pop());

function showFiles(files) {
    shownFiles = files;
    showDisks();
}

function showDisks() {
    const byName = new Map(library.map((d) => [fileName(d.url), d]));
    const mounted = shownFiles.filter((f) => f.name !== systemSelect.value).map((f) => {
        const d = byName.get(f.name) || {};
        const kind = !IMAGE.test(f.name) ? 'app'
            : /\.(iso|toast|cdr)$/i.test(f.name) || (d.tags || []).includes('cd') ? 'cd'
            : (f.size || 0) <= 1440 * 1024 ? 'floppy' : 'hd';
        return { name: f.name, title: d.title || f.name, cover: d.cover, kind, state: imageState[f.name] };
    });
    // The library's System disks too, not yet here: picking one inserts it.
    const systems = [...systemSelect.options].map((o) => ({ value: o.value, label: o.textContent }));
    for (const d of library)
        if ((d.tags || []).includes('system') && !shownFiles.some((f) => f.name === fileName(d.url)))
            systems.push({ value: d.url, label: `${d.title} (from the library)` });
    Disks.update({ system: systemSelect.value, systems, mounted, library,
                   mountedUrls: [...params.getAll('disk'), ...picked()] });
}

Disks.init({
    add: addFiles,
    insert: (d) => addLibraryDisk(d.url),
    async eject(name) {
        await remove(name);
        forgetLibraryDisk(name);
        start();
    },
    revert: (name) => {
        const f = shownFiles.find((x) => x.name === name);
        if (f)
            revert(f);
    },
    download,
    async system(value) {
        const d = library.find((x) => x.url === value);
        if (d) {
            await Settings.set({ machine: { system: fileName(d.url) } });
            return addLibraryDisk(d.url);
        }
        systemSelect.value = value;
        systemSelect.dispatchEvent(new Event('change'));
    },
    reset,
});

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
    if (standalone) {
        await fetch(`api/sync/overlays/${imageKey(f)}`, { method: 'DELETE', cache: 'no-store' }).catch(() => {});
        start();
        return;
    }
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

async function addFiles(files) {
    generation++;                   // restarting (once the files are stored)
    window.executor.frames = 0;
    for (const f of files)
        await store(f);
    start();
}
document.getElementById('add').addEventListener('change', async (e) => {
    const files = [...e.target.files];
    e.target.value = '';
    addFiles(files);
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
    if (standalone)
        await fetch(`api/sync/volumes/${volumeKey(await storedFiles())}`, { method: 'DELETE', cache: 'no-store' }).catch(() => {});
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
}

// Leaving or hiding the page: the Worker saves what changed now.
// Hidden, it hands its disks to another tab that asks (worker.js).
document.addEventListener('visibilitychange', () => {
    if (!worker)
        return;
    if (document.visibilityState === 'hidden')
        worker.postMessage({ type: 'save' });
    worker.postMessage({ type: 'visibility', hidden: document.visibilityState === 'hidden' });
});

// The System to run on: Executor's own (''), or a disk image's (its name),
// remembered across visits.
const IMAGE = /\.(img|image|dsk|hfs|iso|toast|cdr)$/i;
const systemSelect = document.getElementById('system');
systemSelect.addEventListener('change', () => {
    Settings.set({ machine: { system: systemSelect.value } }).finally(start);
});
Settings.provide('systems', () => [...systemSelect.options].map((o) => o.value).filter(Boolean));

// Fat applications' PowerPC code (mac-phoenix's --executor-ppc), remembered.
const ppcBox = document.getElementById('ppc');
Settings.on('machine.ppc', (v) => { ppcBox.checked = v; });
ppcBox.addEventListener('change', () => {
    Settings.set({ machine: { ppc: ppcBox.checked } }).finally(start);
});

function showSystems(files) {
    const want = Settings.get('machine.system');
    systemSelect.replaceChildren(new Option("Executor's own", ''),
        ...files.filter((f) => IMAGE.test(f.name)).map((f) => new Option(f.name, f.name)));
    systemSelect.value = [...systemSelect.options].some((o) => o.value === want) ? want : '';
    showDisks();
}

// ---- the server (tools/web/server): the account, { name, owner }, or
// null; standalone, no account and no OPFS ----
let account = null, standalone = false;
const modeKnown = (async () => {
    let me = null;
    try {
        const res = await fetch('api/me', { cache: 'no-store' });
        me = res.ok ? await res.json() : null;
    } catch { }
    standalone = !!me && me.mode === 'standalone';
    if (standalone) {
        opfs = false;
        document.getElementById('insecure').hidden = true;
    }
    account = opfs ? me : null;
    const a = document.getElementById('account');
    if (account) {
        a.textContent = account.name;
        a.hidden = false;
    }
})();

// ---- standalone: one tab in control (tools/web/server/session.py) ----
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
        return res.ok ? await res.json() : null;
    } catch {
        return null;
    }
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
    if (standalone && !control)
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
    status.textContent = 'Loading Executor…';

    // The Worker loads the module while the disks still download.
    worker = new Worker('worker.js');
    worker.addEventListener('error', (e) => {
        console.error(`worker: ${e.message} at ${e.filename}:${e.lineno}`);
        autoShot(e.message);
    });
    await disksFetched;
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
                         volumeKey: volumeKey(files), sync: !!account, relay: !!account || standalone,
                         standalone,
                         ids: imageIds, legacyKeys, log: Log.session,
                         ram: Settings.get('machine.ram_mb'), depth: Settings.get('machine.depth'),
                         logtraps: Settings.get('advanced.logtraps'),
                         nesting: String(Settings.get('advanced.logtraps_nesting')),
                         debug: Settings.get('advanced.debug'), ppcbreak: Settings.get('advanced.ppc_break') },
                       [offscreen]);
    Settings.started();
    worker.postMessage({ type: 'visibility', hidden: document.visibilityState === 'hidden' });
    worker.onmessage = (e) => {
        if (mine !== generation)
            return;
        const m = e.data;
        if (m.type === 'frame') {
            // (a notice from the Worker before it stays)
            if (window.executor.frames++ === 0 && status.textContent === 'Loading Executor…')
                status.textContent = 'Executor';
        } else if (m.type === 'images') {
            const before = JSON.stringify(imageState);
            for (const i of m.images)
                imageState[i.name] = { modified: i.modified, changed: i.changed, writable: i.writable, inMemory: i.inMemory,
                                       borrowed: i.borrowed, onServer: i.onServer };
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
        } else if (m.type === 'beacons') {
            storeBeacons = m.items;
        } else if (m.type === 'session') {
            if (!m.s.control && control)
                lostControl();
        } else if (m.type === 'error') {
            autoShot(m.text);
        } else if (m.type === 'exit') {
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
// fractional scale makes some one device pixel and some two).
function fitCanvas() {
    if (!canvas)
        return;
    const dpr = window.devicePixelRatio || 1;
    const bar = document.fullscreenElement ? 0 : document.getElementById('bar').offsetHeight
        + document.getElementById('status').offsetHeight + document.getElementById('insecure').offsetHeight + 32;
    // (the Disks panel open: what it leaves)
    const across = window.innerWidth - (parseFloat(getComputedStyle(document.body).paddingRight) || 0);
    const fit = Math.min(across * dpr / width, (window.innerHeight - bar) * dpr / height);
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
