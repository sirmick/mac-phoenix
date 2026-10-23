// The Mac's cursor as the page's own (a CSS cursor), so the pointer moves
// at the browser's speed and is never part of the frames: the guest only
// says what it looks like. Shared by mac-phoenix's client (client.js, the
// Executor backend) and Executor's tab (web/executor; the wasm build
// copies this file there).
//
//   const cursor = new MacCursor(element, guestWidth);
//   cursor.set({ data, mask, hotX, hotY, visible });   // on every change
//
// data, mask: 16x16, 1 bit a pixel, rows MSB first (32 bytes each); data
// the black pixels, mask where the cursor is opaque. guestWidth: the guest
// screen's width in pixels (a function, or a number), to scale the cursor
// with the element. Pointer Lock (relative mouse) needs a page-drawn
// cursor instead; this is the absolute-mouse one.
'use strict';

class MacCursor {
    constructor(element, guestWidth) {
        this.element = element;
        this.guestWidth = typeof guestWidth === 'function' ? guestWidth : () => guestWidth;
        this.cursor = null;
        this.cache = new Map();             // image + scale -> CSS value
        element.style.cursor = 'default';   // until the guest sets one
        this.resize = new ResizeObserver(() => this.apply());
        this.resize.observe(element);
    }

    set(cursor) {
        this.cursor = cursor;
        this.apply();
    }

    // The frames hold the cursor again (a ROM machine): the element's
    // stylesheet cursor back.
    release() {
        this.cursor = null;
        this.element.style.cursor = '';
    }

    apply() {
        const c = this.cursor;
        if (!c)
            return;
        if (!c.visible) {
            this.element.style.cursor = 'none';
            return;
        }
        // Guest pixels per CSS pixel, and device pixels per CSS pixel.
        const w = this.element.getBoundingClientRect().width || this.guestWidth();
        const scale = w / this.guestWidth();
        const dpr = window.devicePixelRatio || 1;
        const px = Math.max(1, Math.round(scale * dpr));    // device pixels a guest pixel
        const key = `${px}:${c.hotX},${c.hotY}:${Array.from(c.data).join(',')}:${Array.from(c.mask).join(',')}`;
        let css = this.cache.get(key);
        if (!css) {
            const canvas = document.createElement('canvas');
            canvas.width = canvas.height = 16 * px;
            const ctx = canvas.getContext('2d');
            for (let y = 0; y < 16; y++) {
                const d = (c.data[2 * y] << 8) | c.data[2 * y + 1];
                const m = (c.mask[2 * y] << 8) | c.mask[2 * y + 1];
                for (let x = 0; x < 16; x++) {
                    const bit = 0x8000 >> x;
                    if (!(m & bit) && !(d & bit))
                        continue;
                    ctx.fillStyle = d & bit ? '#000' : '#fff';   // inverting pixels: black
                    ctx.fillRect(x * px, y * px, px, px);
                }
            }
            const url = canvas.toDataURL('image/png');
            // The hot spot is in CSS pixels; the image at px / dpr of them a
            // guest pixel.
            const hx = Math.round(c.hotX * px / dpr), hy = Math.round(c.hotY * px / dpr);
            css = dpr === 1 ? `url(${url}) ${hx} ${hy}, auto`
                            : `image-set(url(${url}) ${dpr}x) ${hx} ${hy}, auto`;
            if (this.cache.size > 64)
                this.cache.clear();
            this.cache.set(key, css);
        }
        this.element.style.cursor = css;
    }
}
