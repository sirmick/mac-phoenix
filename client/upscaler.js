/* Pixel-art-aware GPU upscaler for the guest framebuffer.
 *
 * Wraps a WebGL canvas that samples either an HTMLVideoElement (h264/vp9
 * via RTP) or a 2D canvas (PNG/WebP/HTTPStream) and runs a per-frame
 * fragment shader to upscale the result to 2× the source dimensions.
 *
 * Currently uses Scale2x (a.k.a. EPX / AdvanceMAME 2x) — a nearest-
 * neighbour-but-edge-aware pattern match that preserves hard pixel
 * edges on Mac UI and fills in diagonal stair-steps without blurring.
 * Roughly drop-in swappable with xBR / hqx / Anime4K shaders later;
 * the JS wiring (texture upload, RAF loop, attach/detach) doesn't care.
 *
 * Usage:
 *   const u = new Upscaler(canvas);
 *   u.setSource(videoOrCanvasElement);
 *   u.setSourceSize(w, h);     // source dimensions
 *   u.start();                 // begin RAF loop
 *   ...
 *   u.stop();
 */

(function (root) {
    'use strict';

    const VERTEX_SRC = `
        attribute vec2 a_pos;
        varying   vec2 v_uv;
        void main() {
            v_uv = (a_pos * 0.5) + 0.5;
            v_uv.y = 1.0 - v_uv.y;   // textures are upside-down
            gl_Position = vec4(a_pos, 0.0, 1.0);
        }
    `;

    /* Scale2x. For each output sub-pixel of a 2× tile, look at the 4
     * cardinal neighbours of the source pixel and pick the diagonal
     * neighbour iff the two corresponding cardinal pixels both match
     * AND the perpendicular pair doesn't — the canonical pattern that
     * smooths a diagonal stair while leaving solid blocks untouched. */
    const FRAGMENT_SRC = `
        precision highp float;
        uniform sampler2D u_tex;
        uniform vec2      u_texSize;
        varying vec2      v_uv;

        bool eq(vec3 a, vec3 b) {
            return all(lessThan(abs(a - b), vec3(0.04)));
        }

        vec3 fetch(vec2 ip, vec2 off) {
            return texture2D(u_tex, (ip + off + 0.5) / u_texSize).rgb;
        }

        void main() {
            vec2 px = v_uv * u_texSize;
            vec2 fp = fract(px);
            vec2 ip = floor(px);

            vec3 B = fetch(ip, vec2( 0, -1));
            vec3 D = fetch(ip, vec2(-1,  0));
            vec3 E = fetch(ip, vec2( 0,  0));
            vec3 F = fetch(ip, vec2( 1,  0));
            vec3 H = fetch(ip, vec2( 0,  1));

            vec3 result = E;
            if (fp.x < 0.5 && fp.y < 0.5) {
                if (eq(D, B) && !eq(B, F) && !eq(D, H)) result = D;
            } else if (fp.y < 0.5) {
                if (eq(B, F) && !eq(B, D) && !eq(F, H)) result = F;
            } else if (fp.x < 0.5) {
                if (eq(D, H) && !eq(D, B) && !eq(H, F)) result = D;
            } else {
                if (eq(H, F) && !eq(D, H) && !eq(B, F)) result = F;
            }
            gl_FragColor = vec4(result, 1.0);
        }
    `;

    function compileShader(gl, type, src) {
        const sh = gl.createShader(type);
        gl.shaderSource(sh, src);
        gl.compileShader(sh);
        if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
            const log = gl.getShaderInfoLog(sh);
            gl.deleteShader(sh);
            throw new Error('Shader compile failed: ' + log);
        }
        return sh;
    }

    function linkProgram(gl, vs, fs) {
        const prog = gl.createProgram();
        gl.attachShader(prog, vs);
        gl.attachShader(prog, fs);
        gl.linkProgram(prog);
        if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
            const log = gl.getProgramInfoLog(prog);
            gl.deleteProgram(prog);
            throw new Error('Program link failed: ' + log);
        }
        return prog;
    }

    class Upscaler {
        constructor(canvas) {
            this.canvas = canvas;
            this.gl = canvas.getContext('webgl', { premultipliedAlpha: false })
                   || canvas.getContext('experimental-webgl');
            if (!this.gl) throw new Error('WebGL unavailable');
            const gl = this.gl;

            const vs = compileShader(gl, gl.VERTEX_SHADER,   VERTEX_SRC);
            const fs = compileShader(gl, gl.FRAGMENT_SHADER, FRAGMENT_SRC);
            this.prog = linkProgram(gl, vs, fs);

            // Fullscreen quad (two triangles).
            this.vbo = gl.createBuffer();
            gl.bindBuffer(gl.ARRAY_BUFFER, this.vbo);
            gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
                -1, -1,  1, -1, -1,  1,
                -1,  1,  1, -1,  1,  1,
            ]), gl.STATIC_DRAW);

            this.tex = gl.createTexture();
            gl.bindTexture(gl.TEXTURE_2D, this.tex);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

            this.uTexSize = gl.getUniformLocation(this.prog, 'u_texSize');
            this.aPos     = gl.getAttribLocation(this.prog,  'a_pos');

            this.source = null;
            this.sourceW = 0;
            this.sourceH = 0;
            this.rafId = 0;
            this.running = false;
            this.boundLoop = () => this._loop();
        }

        setSource(elem) {
            this.source = elem;
        }

        // Source dimensions in source pixels. Output (canvas drawing
        // buffer) is sized 2× — Scale2x produces 2 output pixels per
        // axis per source pixel.
        setSourceSize(w, h) {
            if (!w || !h) return;
            if (this.sourceW === w && this.sourceH === h) return;
            this.sourceW = w;
            this.sourceH = h;
            this.canvas.width  = w * 2;
            this.canvas.height = h * 2;
        }

        start() {
            if (this.running) return;
            this.running = true;
            this._loop();
        }

        stop() {
            this.running = false;
            if (this.rafId) {
                cancelAnimationFrame(this.rafId);
                this.rafId = 0;
            }
        }

        _loop() {
            if (!this.running) return;
            this.rafId = requestAnimationFrame(this.boundLoop);
            this._render();
        }

        _render() {
            const gl = this.gl;
            const src = this.source;
            if (!src) return;
            // Source not ready (video buffering, canvas pre-first-frame).
            if (src.tagName === 'VIDEO' && src.readyState < 2) return;
            if (src.tagName === 'CANVAS' && (!src.width || !src.height)) return;

            // Auto-detect source dimension changes (resolution switch
            // mid-stream). Without this, texImage2D would re-upload at
            // the new dims but the shader's u_texSize and the canvas
            // drawing buffer would still be at the old dims — frames
            // come out stretched/distorted.
            const w = (src.tagName === 'VIDEO') ? src.videoWidth  : src.width;
            const h = (src.tagName === 'VIDEO') ? src.videoHeight : src.height;
            if (w && h && (w !== this.sourceW || h !== this.sourceH)) {
                this.setSourceSize(w, h);
            }
            if (!this.sourceW || !this.sourceH) return;

            gl.bindTexture(gl.TEXTURE_2D, this.tex);
            try {
                // texImage2D with HTMLVideoElement / HTMLCanvasElement is a
                // direct upload — zero-copy on most GPUs.
                gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA,
                              gl.UNSIGNED_BYTE, src);
            } catch (e) {
                // Some browsers throw if the source is in an
                // intermediate state; skip this frame and retry.
                return;
            }

            gl.viewport(0, 0, this.canvas.width, this.canvas.height);
            gl.useProgram(this.prog);
            gl.uniform2f(this.uTexSize, this.sourceW, this.sourceH);

            gl.bindBuffer(gl.ARRAY_BUFFER, this.vbo);
            gl.enableVertexAttribArray(this.aPos);
            gl.vertexAttribPointer(this.aPos, 2, gl.FLOAT, false, 0, 0);

            gl.drawArrays(gl.TRIANGLES, 0, 6);
        }
    }

    if (root) root.Upscaler = Upscaler;
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = { Upscaler };
    }
})(typeof window !== 'undefined' ? window : null);
