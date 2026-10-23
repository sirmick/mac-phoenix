// The Worker side: runs executor-web.js (web_main.cpp) and draws its
// frames on the page's OffscreenCanvas.
'use strict';

let canvas = null, ctx = null, image = null;

self.Module = {
    arguments: [],
    input: [],          // [kind, a, b] events; web_main.cpp takes them
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
    onExit(code) {
        postMessage({ type: 'exit', code });
    },
};

self.onmessage = (e) => {
    const m = e.data;
    if (m.type === 'start') {
        canvas = m.canvas;
        ctx = canvas.getContext('2d');
        Module.arguments = ['--size', `${m.width}x${m.height}`];
        importScripts('executor-web.js');
    } else if (m.type === 'input') {
        Module.input.push(...m.events);
    }
};
