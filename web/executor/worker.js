// The Worker side: runs executor-web.js (web_main.cpp) and draws its
// frames on the page's OffscreenCanvas.
//
// Uploads: disk images are mounted from the page's Files without copying
// (WORKERFS, /uploads) and handed to Executor read-only; anything else (an
// application in MacBinary) is copied into Executor's own volume, in
// "executor:Uploads".
'use strict';

let canvas = null, ctx = null, image = null;
const IMAGE = /\.(img|image|dsk|hfs|iso|toast|cdr)$/i;

self.Module = {
    arguments: [],
    input: [],          // [kind, a, b] events; web_main.cpp takes them
    requests: [],       // [id, op, arg]; answered at the next event call
    print: (t) => console.log(t),
    printErr: (t) => console.log(t),
    onFrame(rgba, width, height) {
        if (canvas.width !== width || canvas.height !== height) {
            canvas.width = width;
            canvas.height = height;
            image = null;
        }
        if (!image)
            image = new ImageData(width, height);
        image.data.set(rgba);
        ctx.putImageData(image, 0, 0);
        postMessage({ type: 'frame' });
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

self.onmessage = (e) => {
    const m = e.data;
    if (m.type === 'start') {
        canvas = m.canvas;
        ctx = canvas.getContext('2d');
        const images = m.files.filter((f) => IMAGE.test(f.name));
        const others = m.files.filter((f) => !IMAGE.test(f.name));
        Module.arguments = ['--size', `${m.width}x${m.height}`,
                            ...images.flatMap((f) => ['--disk', `/uploads/${f.name}`])];
        if (m.system && images.some((f) => f.name === m.system))
            Module.arguments.push('--system', `/uploads/${m.system}`);
        Module.preRun = [() => {
            FS.mkdir('/host');              // Executor's "Host" volume
            if (images.length) {
                FS.mkdir('/uploads');
                FS.mount(WORKERFS, { files: images }, '/uploads');
            }
            if (others.length) {
                FS.mkdirTree('/executor/Uploads');
                const reader = new FileReaderSync();
                for (const f of others)
                    FS.writeFile(`/executor/Uploads/${f.name}`,
                                 new Uint8Array(reader.readAsArrayBuffer(f)));
            }
        }];
        importScripts('executor-web.js');
    } else if (m.type === 'input') {
        Module.input.push(...m.events);
    } else if (m.type === 'request') {
        // Files are the Worker's: answered at once, Executor running or not.
        if (m.op === 'read') {
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
