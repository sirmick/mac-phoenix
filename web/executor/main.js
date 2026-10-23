// The page side: start the Worker, hand it the canvas, send it input.
// Query string: ?size=WxH (default 640x480).
'use strict';

const canvas = document.getElementById('screen');
const status = document.getElementById('status');
const params = new URLSearchParams(location.search);
const [width, height] = (params.get('size') || '640x480').split('x').map(Number);
canvas.width = width;
canvas.height = height;

const worker = new Worker('worker.js');
const offscreen = canvas.transferControlToOffscreen();
worker.postMessage({ type: 'start', canvas: offscreen, width, height }, [offscreen]);

let frames = 0;
worker.onmessage = (e) => {
    const m = e.data;
    if (m.type === 'frame') {
        if (frames++ === 0)
            status.textContent = 'Executor';
        window.executorFrames = frames;   // for tests
    } else if (m.type === 'log') {
        status.textContent = m.text;
    } else if (m.type === 'exit') {
        status.textContent = `Executor exited (${m.code})`;
    }
};

// ---- input: [kind, a, b] events (web_main.cpp) ----
function send(...events) {
    worker.postMessage({ type: 'input', events });
}

function guestPoint(e) {
    const r = canvas.getBoundingClientRect();
    const x = Math.floor((e.clientX - r.left) * canvas.width / r.width);
    const y = Math.floor((e.clientY - r.top) * canvas.height / r.height);
    return [Math.max(0, Math.min(canvas.width - 1, x)),
            Math.max(0, Math.min(canvas.height - 1, y))];
}

canvas.addEventListener('pointermove', (e) => send([1, ...guestPoint(e)]));
canvas.addEventListener('pointerdown', (e) => {
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

function onKey(down, e) {
    const code = macKeycodeForEvent(e);
    if (code === null)
        return;
    send([3, down ? 1 : 0, code]);
    e.preventDefault();
}
canvas.addEventListener('keydown', (e) => { if (!e.repeat) onKey(true, e); });
canvas.addEventListener('keyup', (e) => onKey(false, e));
