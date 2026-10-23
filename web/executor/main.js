// The page side: keep uploads in OPFS, start the Worker with them, hand it
// a canvas, send it input. Query string:
//   ?size=WxH        the screen (default 640x480)
//   ?disk=URL        a disk image to fetch and mount for this visit (not
//                    kept; repeatable)
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

// ?disk=URL: fetched once per visit, with progress in the status line.
async function fetchDisks() {
    for (const url of params.getAll('disk')) {
        const name = decodeURIComponent(new URL(url, location.href).pathname.split('/').pop());
        const res = await fetch(url);
        if (!res.ok) {
            status.textContent = `Cannot fetch ${name} (${res.status})`;
            continue;
        }
        const total = Number(res.headers.get('Content-Length')) || 0;
        const reader = res.body.getReader();
        const chunks = [];
        let got = 0;
        for (;;) {
            const { done, value } = await reader.read();
            if (done)
                break;
            chunks.push(value);
            got += value.length;
            status.textContent = total ? `Loading ${name}… ${Math.floor(got * 100 / total)}%`
                                       : `Loading ${name}… ${(got / 1e6).toFixed(0)} MB`;
        }
        sessionFiles = sessionFiles.filter((f) => f.name !== name)
            .concat(new File(chunks, name));
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
    attachInput(canvas);
    cursor = new MacCursor(canvas, () => canvas.width);

    worker = new Worker('worker.js');
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

fetchDisks().then(start);
