// The page side: keep uploads in OPFS, start the Worker with them, hand it
// a canvas, send it input. Query string:
//   ?size=WxH        the screen (default 640x480)
//   ?disk=URL        a disk image to fetch and mount (repeatable); kept in
//                    OPFS (disk-cache/) and revalidated on later visits
//                    with its ETag, so it downloads once
//   ?system=NAME     run on that image's System and Finder
// Outside a secure context (plain http to another host) there is no OPFS
// and no AudioWorklet: uploads last the visit, sound is off.
//
// Automation: window.executor.request(op, arg) -> Promise. At the running
// application's next event call (web_main.cpp): 'app', 'windows', 'launch'
// "Volume:Folder:App". At once, by the Worker: 'read' PATH (text or null),
// 'write' [PATH, TEXT]; Executor's "Host" volume is /host.
// window.executor.frames counts frames.
'use strict';

const box = document.getElementById('screen-box');
const status = document.getElementById('status');
const params = new URLSearchParams(location.search);
const [width, height] = (params.get('size') || '640x480').split('x').map(Number);

let worker = null, canvas = null, cursor = null, nextId = 1;
const pending = new Map();
window.executor = {
    frames: 0,
    audioFrames: 0,     // 20 ms frames of sound received
    cursor: null,       // the guest's last cursor message
    request(op, arg = '') {
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
    const cached = async () => new File([await (await dir.getFileHandle(`${key}.img`)).getFile()], name);

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
                                   lastModified: res.headers.get('Last-Modified'), size: got }));
    await w.close();
    return cached();
}

async function fetchDisks() {
    for (const url of params.getAll('disk')) {
        try {
            const file = await fetchDisk(url);
            sessionFiles = sessionFiles.filter((f) => f.name !== file.name).concat(file);
        } catch (e) {
            status.textContent = `Cannot load ${url}: ${e.message}`;
        }
    }
}

async function showFiles(files) {
    const list = document.getElementById('files');
    list.replaceChildren(...files.map((f) => {
        const s = document.createElement('span');
        s.textContent = f.name;
        const x = document.createElement('a');
        x.textContent = '✕';
        x.title = 'Remove';
        x.onclick = async () => {
            await remove(f.name);
            start();
        };
        s.append(x);
        return s;
    }));
}

document.getElementById('add').addEventListener('change', async (e) => {
    for (const f of e.target.files)
        await store(f);
    e.target.value = '';
    start();
});
document.getElementById('restart').addEventListener('click', () => start());

// The System to run on: Executor's own (''), or a disk image's (its name),
// remembered across visits.
const IMAGE = /\.(img|image|dsk|hfs|iso|toast|cdr)$/i;
const systemSelect = document.getElementById('system');
systemSelect.addEventListener('change', () => {
    localStorage.setItem('executor-system', systemSelect.value);
    start();
});

function showSystems(files) {
    const want = params.get('system') || localStorage.getItem('executor-system') || '';
    systemSelect.replaceChildren(new Option("Executor's own", ''),
        ...files.filter((f) => IMAGE.test(f.name)).map((f) => new Option(f.name, f.name)));
    systemSelect.value = [...systemSelect.options].some((o) => o.value === want) ? want : '';
}

// ---- the Worker ----
async function start() {
    if (worker)
        worker.terminate();
    for (const resolve of pending.values())
        resolve({ error: 'restarted' });
    pending.clear();
    window.executor.frames = 0;
    status.textContent = 'Loading Executor…';

    // The Worker loads the module while the disks still download.
    worker = new Worker('worker.js');
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
    cursor = new MacCursor(canvas, () => canvas.width);

    const offscreen = canvas.transferControlToOffscreen();
    worker.postMessage({ type: 'start', canvas: offscreen, width, height, files,
                         system: systemSelect.value }, [offscreen]);
    worker.onmessage = (e) => {
        const m = e.data;
        if (m.type === 'frame') {
            if (window.executor.frames++ === 0)
                status.textContent = 'Executor';
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
    const bar = document.getElementById('bar').offsetHeight + document.getElementById('status').offsetHeight + 32;
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
        const x = Math.floor((e.clientX - r.left) * canvas.width / r.width);
        const y = Math.floor((e.clientY - r.top) * canvas.height / r.height);
        return [Math.max(0, Math.min(canvas.width - 1, x)),
                Math.max(0, Math.min(canvas.height - 1, y))];
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
