/**
 * Basilisk II WebRTC Client (libdatachannel backend)
 *
 * Full-featured client with debugging, stats tracking, and connection monitoring.
 */

// ============================================================================
// Constants
// ============================================================================
const CONSTANTS = {
    // Debug & Logging
    MAX_LOG_ENTRIES: 500,

    // Timing & Intervals (milliseconds)
    MS_PER_SECOND: 1000,
    STATS_UPDATE_INTERVAL_MS: 1000,
    LATENCY_LOG_INTERVAL_MS: 3000,
    DETAILED_STATS_INTERVAL_MS: 3000,
    STATUS_POLL_INTERVAL_MS: 125,
    TRACK_READY_CHECK_INTERVAL_MS: 2000,
    FRAME_DETECTION_INTERVAL_MS: 1000,

    // Reconnection
    MAX_RECONNECT_ATTEMPTS: 10,
    BASE_RECONNECT_DELAY_MS: 1000,
    MAX_RECONNECT_DELAY_MS: 30000,
    MAX_DECODE_LATENCY_MS: 1000,

    // Frame Protocol Sizes (bytes)
    PNG_HEADER_SIZE: 45,
    MIN_PNG_SIZE_WITH_HEADER: 53,
    H264_METADATA_SIZE: 5,

    // Audio Capture
    AUDIO_CAPTURE_DURATION_SEC: 10,
    AUDIO_BUFFER_SIZE: 4096,
    AUDIO_CHANNELS: 2,

    // Canvas & Drawing
    FRAME_DETECTION_CANVAS_SIZE: 10,
    CURSOR_ARROW_HEIGHT: 20,

    // Conversion factors
    BITS_PER_BYTE: 8,
    BITS_TO_KILOBITS: 1000,
};

// Global debug configuration (fetched from server)
const debugConfig = {
    debug_connection: false,   // WebRTC/ICE/signaling logs
    debug_mode_switch: false,  // Mode/resolution/color depth changes
    debug_perf: false          // Performance stats
};

// Store UI config from server
let serverUIConfig = {
    webcodec: 'vp9',
    mousemode: 'absolute',
    resolution: '800x600'
};

/**
 * Load configuration embedded in HTML by server (eliminates race conditions)
 * Server injects config as JSON in <script id="server-config"> tag
 */
function loadEmbeddedConfig() {
    const configScript = document.getElementById('server-config');
    if (!configScript) {
        return null;
    }

    try {
        const configText = configScript.textContent.trim();

        // Check if server has replaced placeholders (if not, we'll see {{PLACEHOLDER}})
        if (configText.includes('{{')) {
            logger.info('Embedded config contains unreplaced placeholders, will use fetch fallback');
            return null;
        }

        const config = JSON.parse(configText);
        logger.info('Loaded embedded config from HTML', config);
        return config;
    } catch (e) {
        logger.error('Failed to parse embedded config', { error: e.message });
        return null;
    }
}

/**
 * Fetch configuration from server
 * Uses embedded config (injected by C++ server) if available, falls back to API fetch
 */
async function fetchConfig() {
    // STRATEGY 1: Try embedded config first (synchronous, no race condition)
    const embeddedConfig = loadEmbeddedConfig();
    if (embeddedConfig) {
        // Apply to debug config
        Object.assign(debugConfig, embeddedConfig);

        // Apply to UI config
        if (embeddedConfig.webcodec) serverUIConfig.webcodec = embeddedConfig.webcodec;
        if (embeddedConfig.mousemode) serverUIConfig.mousemode = embeddedConfig.mousemode;
        if (embeddedConfig.resolution) serverUIConfig.resolution = embeddedConfig.resolution;

        logger.info('[Browser] Using embedded config (no fetch needed)', serverUIConfig);

        // Apply codec availability from embedded config (removes unavailable codecs from dropdown)
        if (embeddedConfig.codecs) {
            applyCodecAvailability(embeddedConfig.codecs);
        }

        // Note: UI elements (select dropdowns, resolution) are already correct
        // because server pre-rendered them with selected attributes
        return;
    }

    // STRATEGY 2: Fallback to fetch if embedded config not available
    // (backwards compatibility, development with static HTML)
    try {
        const response = await fetch('/api/config');
        const config = await response.json();
        Object.assign(debugConfig, config);
        logger.info('[Browser] Fetched config from API (fallback)', config);

        // Store UI config from server (supports both old and new JSON format)
        const webcodec = config.webcodec || config.codec;
        const resolution = config.resolution || config.screen;
        if (webcodec) serverUIConfig.webcodec = webcodec;
        if (config.mousemode) serverUIConfig.mousemode = config.mousemode;
        if (resolution) serverUIConfig.resolution = resolution;

        // Set UI dropdowns to match server config (only needed when using fetch)
        const codecSelect = document.getElementById('codec-select');
        if (codecSelect && webcodec) {
            codecSelect.value = webcodec;
        }

        const mouseSelect = document.getElementById('mouse-mode-select');
        if (mouseSelect && config.mousemode) {
            mouseSelect.value = config.mousemode;
        }

        // Set initial resolution display (only needed when using fetch)
        const headerResEl = document.getElementById('header-resolution');
        if (headerResEl && resolution) {
            headerResEl.textContent = resolution;
        }

        logger.info('[Browser] UI config loaded from fetch', serverUIConfig);
    } catch (e) {
        logger.warn('[Browser] Failed to fetch config, using defaults', { error: e.message });
    }
}

// Debug logging system - sends to server and local debug panel
class DebugLogger {
    constructor() {
        this.logElement = null;
        this.maxEntries = CONSTANTS.MAX_LOG_ENTRIES;
        this.sendToServer = true;  // Send important logs to server
    }

    init() {
        this.logElement = document.getElementById('debug-log');
    }

    log(level, message, data = null) {
        const timestamp = new Date().toISOString().split('T')[1].slice(0, 12);
        const logLine = data ? `${message}: ${JSON.stringify(data)}` : message;

        // Console output with [Browser] prefix
        const consoleFn = level === 'error' ? console.error :
                         level === 'warn' ? console.warn : console.log;
        consoleFn(`[Browser] ${level}: ${logLine}`);

        // UI output
        if (this.logElement) {
            const entry = document.createElement('div');
            entry.className = `log-entry ${level}`;
            entry.innerHTML = `<span class="timestamp">${timestamp}</span>${this.escapeHtml(logLine)}`;
            this.logElement.appendChild(entry);

            // Trim old entries
            while (this.logElement.children.length > this.maxEntries) {
                this.logElement.removeChild(this.logElement.firstChild);
            }

            // Auto-scroll
            this.logElement.scrollTop = this.logElement.scrollHeight;
        }

        // Send to server (errors, warnings, and key info messages)
        if (this.sendToServer && (level === 'error' || level === 'warn' || level === 'info')) {
            this.sendToServerAsync(level, message, data);
        }
    }

    async sendToServerAsync(level, message, data) {
        try {
            await fetch('/api/log', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    level,
                    message,
                    data: data ? JSON.stringify(data) : ''
                })
            });
        } catch (e) {
            // Silently ignore - don't create infinite loops
        }
    }

    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }

    info(msg, data) { this.log('info', msg, data); }
    warn(msg, data) { this.log('warn', msg, data); }
    error(msg, data) { this.log('error', msg, data); }
    debug(msg, data) { this.log('debug', msg, data); }

    clear() {
        if (this.logElement) {
            this.logElement.innerHTML = '';
        }
    }
}

const logger = new DebugLogger();

// Error reporting to server
function reportErrorToServer(error, type = 'error') {
    try {
        const errorData = {
            message: error.message || String(error),
            stack: error.stack || '',
            url: error.filename || window.location.href,
            line: error.lineno || '',
            col: error.colno || '',
            type: type,
            timestamp: new Date().toISOString(),
            userAgent: navigator.userAgent
        };

        // Send to server via beacon (works even during page unload)
        const blob = new Blob([JSON.stringify(errorData)], { type: 'application/json' });
        navigator.sendBeacon('/api/error', blob);
    } catch (e) {
        // Fail silently - don't want error reporting to cause more errors
        console.error('Failed to report error to server:', e);
    }
}

// Global error handler for uncaught exceptions
window.addEventListener('error', (event) => {
    reportErrorToServer({
        message: event.message,
        filename: event.filename,
        lineno: event.lineno,
        colno: event.colno,
        stack: event.error ? event.error.stack : ''
    }, 'UncaughtException');
});

// Global handler for unhandled promise rejections
window.addEventListener('unhandledrejection', (event) => {
    reportErrorToServer({
        message: `Unhandled Promise Rejection: ${event.reason}`,
        stack: event.reason && event.reason.stack ? event.reason.stack : String(event.reason)
    }, 'UnhandledPromiseRejection');
});

// Connection step tracking
class ConnectionSteps {
    constructor() {
        this.steps = ['ws', 'offer', 'ice', 'track', 'frames'];
        this.currentStep = -1;
    }

    reset() {
        this.currentStep = -1;
        this.steps.forEach(step => {
            const el = document.getElementById(`step-${step}`);
            if (el) {
                el.className = 'step';
                el.querySelector('.step-icon').textContent = this.steps.indexOf(step) + 1;
            }
        });
    }

    setActive(stepName) {
        const idx = this.steps.indexOf(stepName);
        if (idx === -1) return;

        this.steps.forEach((step, i) => {
            const el = document.getElementById(`step-${step}`);
            if (!el) return;

            if (i < idx) {
                el.className = 'step done';
                el.querySelector('.step-icon').innerHTML = '&#10003;';
            } else if (i === idx) {
                el.className = 'step active';
                el.querySelector('.step-icon').innerHTML = '<div class="step-spinner"></div>';
            } else {
                el.className = 'step';
                el.querySelector('.step-icon').textContent = i + 1;
            }
        });

        this.currentStep = idx;
    }

    setDone(stepName) {
        const el = document.getElementById(`step-${stepName}`);
        if (el) {
            el.className = 'step done';
            el.querySelector('.step-icon').innerHTML = '&#10003;';
        }
    }

    setError(stepName) {
        const el = document.getElementById(`step-${stepName}`);
        if (el) {
            el.className = 'step error';
            el.querySelector('.step-icon').innerHTML = '&#10007;';
        }
    }
}

const connectionSteps = new ConnectionSteps();


/*
 * Video Decoder Abstraction
 *
 * Allows switching between different decoding strategies:
 * - H.264 via WebRTC video track (native browser decoding)
 * - AV1 via WebRTC video track (modern codec, best for dithered content)
 * - PNG via DataChannel (good for dithered 1-bit content, supports dirty rects)
 */

const CodecType = {
    H264: 'h264',           // WebRTC video track with H.264
    AV1: 'av1',             // WebRTC video track with AV1
    VP9: 'vp9',             // WebRTC video track with VP9
    PNG: 'png',             // PNG over DataChannel
    WEBP: 'webp',           // WebP over DataChannel (faster encoding than PNG)
    HTTP_STREAM: 'httpstream' // PNG over plain HTTP chunked (proxy-friendly, no WebRTC)
};

// Base class for video decoders
class VideoDecoder {
    constructor(displayElement) {
        this.display = displayElement;
        this.onFrame = null;  // Callback when frame is decoded
        this.frameCount = 0;
        this.lastFrameTime = 0;
    }

    // Get codec type
    get type() { throw new Error('Not implemented'); }

    // Get codec name for display
    get name() { throw new Error('Not implemented'); }

    // Initialize the decoder
    init() { throw new Error('Not implemented'); }

    // Cleanup resources
    cleanup() { throw new Error('Not implemented'); }

    // Handle incoming data (from track or datachannel)
    handleData(data) { throw new Error('Not implemented'); }

    // Get stats
    getStats() {
        return {
            frameCount: this.frameCount,
            fps: this.calculateFps()
        };
    }

    calculateFps() {
        const now = performance.now();
        if (this.lastFrameTime === 0) {
            this.lastFrameTime = now;
            return 0;
        }
        const elapsed = now - this.lastFrameTime;
        return elapsed > 0 ? Math.round(CONSTANTS.MS_PER_SECOND / elapsed) : 0;
    }
}

// Unified WebRTC video decoder for H.264, AV1, and VP9
// All three codecs use the same mechanism - native browser WebRTC decoding
class WebRTCVideoDecoder extends VideoDecoder {
    constructor(videoElement, codecType) {
        super(videoElement);
        this.videoElement = videoElement;
        this.codecType = codecType;
    }

    get type() {
        return this.codecType;
    }

    get name() {
        const names = {
            [CodecType.H264]: 'H.264 (WebRTC)',
            [CodecType.AV1]: 'AV1 (WebRTC)',
            [CodecType.VP9]: 'VP9 (WebRTC)'
        };
        return names[this.codecType] || 'Unknown';
    }

    init() {
        logger.info(`${this.name} initialized`);
        return true;
    }

    cleanup() {
        if (this.videoElement) {
            this.videoElement.srcObject = null;
        }
        this.lastDecodeTime = 0;
        this.lastDecodeFrames = 0;
        this.avgDecodeLatency = 0;
        this.avgRtt = 0;
    }

    // Called by updateStats() with values from RTCStats
    updateRtpLatency(totalDecodeTime, framesDecoded, rttSeconds) {
        // Average decode time per frame (totalDecodeTime is cumulative seconds)
        if (framesDecoded > this.lastDecodeFrames) {
            const deltaTime = totalDecodeTime - this.lastDecodeTime;
            const deltaFrames = framesDecoded - this.lastDecodeFrames;
            if (deltaFrames > 0 && deltaTime >= 0) {
                this.avgDecodeLatency = (deltaTime / deltaFrames) * 1000; // ms
            }
        }
        this.lastDecodeTime = totalDecodeTime;
        this.lastDecodeFrames = framesDecoded;

        if (rttSeconds > 0) {
            this.avgRtt = rttSeconds * 1000; // ms
        }
    }

    getAverageLatency() { return this.avgDecodeLatency || 0; }
    getAverageRtt() { return this.avgRtt || 0; }

    // Frames come through the WebRTC video track, not handleData()
    // The track is set up by the WebRTC connection directly
    async attachTrack(stream) {
        this.videoElement.srcObject = stream;
        try {
            await this.videoElement.play();
        } catch (e) {
            logger.warn('Video play() failed', { error: e.message });
        }
    }

    handleData(data) {
        // Video data is handled by the browser's native WebRTC stack
        // This method is not used for WebRTC video codecs
        logger.warn(`${this.name}.handleData called - this should not happen`);
    }
}

// Backwards compatibility aliases
class H264Decoder extends WebRTCVideoDecoder {
    constructor(videoElement) {
        super(videoElement, CodecType.H264);
    }
}

class AV1Decoder extends WebRTCVideoDecoder {
    constructor(videoElement) {
        super(videoElement, CodecType.AV1);
    }
}

class VP9Decoder extends WebRTCVideoDecoder {
    constructor(videoElement) {
        super(videoElement, CodecType.VP9);
    }
}

// PNG decoder using canvas rendering
// Expects frames with 8-byte timestamp header for latency measurement
class PNGDecoder extends VideoDecoder {
    constructor(canvasElement) {
        super(canvasElement);
        this.canvas = canvasElement;
        this.ctx = null;
        this.pendingBlob = null;

        // Video latency tracking - store totals for averaging
        // Note: We can only measure browser-side latencies (network + decode)
        // because server and browser clocks are not synchronized
        this.decodeLatencyTotal = 0;
        this.latencySamples = 0;
        this.lastLatencyLog = 0;
        this.lastAverageLatency = 0;  // Last calculated average for stats panel

        // Track frame receive times to measure frame intervals
        this.lastFrameReceiveTime = 0;

    }

    get type() { return CodecType.PNG; }
    get name() { return 'PNG (DataChannel)'; }

    init() {
        this.ctx = this.canvas.getContext('2d');
        if (!this.ctx) {
            logger.error('Failed to get canvas 2D context');
            return false;
        }
        // Reset latency tracking
        this.decodeLatencyTotal = 0;
        this.latencySamples = 0;
        this.lastFrameReceiveTime = 0;
        logger.info('PNGDecoder initialized');
        return true;
    }

    cleanup() {
        this.ctx = null;
    }

    // Get average video latency in ms
    getAverageLatency() {
        return this.lastAverageLatency;
    }

    // Handle PNG data from DataChannel
    // Frame format: [8-byte t1_frame_ready] [4-byte x] [4-byte y] [4-byte width] [4-byte height]
    //               [4-byte frame_width] [4-byte frame_height] [8-byte t4_send_time]
    //               [5-byte cursor data] [PNG data]
    async handleData(data) {
        if (!this.ctx) return;

        const t5_receive = Date.now();  // T5: Browser receive time
        let pngData = data;
        let t1_frame_ready = 0, t4_send = 0;
        let rectX = 0, rectY = 0, rectWidth = 0, rectHeight = 0;
        let frameWidth = 0, frameHeight = 0;
        let cursorX = 0, cursorY = 0, cursorVisible = 0;  // Declare at function scope

        // Parse metadata header if present (ArrayBuffer with at least header + PNG signature)
        if (data instanceof ArrayBuffer && data.byteLength > CONSTANTS.MIN_PNG_SIZE_WITH_HEADER) {
            const view = new DataView(data);

            // Read T1: 8-byte emulator frame ready time (ms since Unix epoch)
            let lo = view.getUint32(0, true);
            let hi = view.getUint32(4, true);
            t1_frame_ready = lo + hi * 0x100000000;

            // Read 4-byte dirty rect coordinates (all little-endian uint32)
            rectX = view.getUint32(8, true);
            rectY = view.getUint32(12, true);
            rectWidth = view.getUint32(16, true);
            rectHeight = view.getUint32(20, true);

            // Read 4-byte full frame resolution
            frameWidth = view.getUint32(24, true);
            frameHeight = view.getUint32(28, true);

            // Read T4: 8-byte server send time (ms since Unix epoch)
            lo = view.getUint32(32, true);
            hi = view.getUint32(36, true);
            t4_send = lo + hi * 0x100000000;

            // Read cursor position (5 bytes: x, y, visible)
            cursorX = view.getUint16(40, true);
            cursorY = view.getUint16(42, true);
            cursorVisible = view.getUint8(44);

            // Update cursor state
            this.currentCursorX = cursorX;
            this.currentCursorY = cursorY;
            this.cursorVisible = (cursorVisible !== 0);

            // PNG data starts after header (40 base + 5 cursor)
            pngData = data.slice(CONSTANTS.PNG_HEADER_SIZE);
        }

        // Create blob from PNG data
        const blob = pngData instanceof Blob ? pngData : new Blob([pngData], { type: 'image/png' });

        try {
            const bitmap = await createImageBitmap(blob);
            // Cleanup may have run during the await (codec stepping, reconnect,
            // or canvas swap). If so, drop the frame quietly — drawing into a
            // null ctx or a transferred canvas throws InvalidStateError, which
            // would otherwise fire logger.error → /api/log on every frame.
            if (!this.ctx || !this.canvas || !this.canvas.isConnected) {
                if (bitmap.close) bitmap.close();
                return;
            }
            const t6_draw = performance.now();  // Draw complete time (use performance.now for accuracy)

            // Calculate decode latency (receive to draw)
            // We use performance.now() for both timestamps to avoid clock skew
            const t5_receive_perf = performance.now() - (Date.now() - t5_receive);
            const decodeLatency = t6_draw - t5_receive_perf;

            // Track decode latency
            if (decodeLatency >= 0 && decodeLatency < CONSTANTS.MAX_DECODE_LATENCY_MS) {
                this.decodeLatencyTotal += decodeLatency;
                this.latencySamples++;
            }

            // Resize canvas based on explicit frame dimensions from server
            // This ensures canvas is the correct size even when receiving dirty rects
            if (frameWidth > 0 && frameHeight > 0) {
                if (this.canvas.width !== frameWidth || this.canvas.height !== frameHeight) {
                    this.canvas.width = frameWidth;
                    this.canvas.height = frameHeight;
                    if (debugConfig.debug_mode_switch) {
                        logger.info('Canvas resized to', frameWidth, 'x', frameHeight);
                    }
                }
                // Update screen dimensions for absolute mouse mode (if onFrame callback exists)
                if (this.onFrame && this.onFrame.updateScreenSize) {
                    this.onFrame.updateScreenSize(frameWidth, frameHeight);
                }
            }

            // Draw bitmap at dirty rect position
            // For full frames: rectX=0, rectY=0, bitmap size = canvas size
            // For dirty rects: rectX, rectY specify where to draw the smaller bitmap
            this.ctx.drawImage(bitmap, rectX, rectY);
            this.frameCount++;
            this.lastFrameTime = performance.now();

            if (this.onFrame) {
                this.onFrame(this.frameCount, { cursorX, cursorY, cursorVisible, frameWidth, frameHeight });
            }

            // Log latency stats periodically
            const now = performance.now();
            if (now - this.lastLatencyLog > CONSTANTS.LATENCY_LOG_INTERVAL_MS && this.latencySamples > 0) {
                const avgDecode = this.decodeLatencyTotal / this.latencySamples;
                this.lastAverageLatency = avgDecode;  // Save for stats panel

                // Log decode latency (brief, on same line as other stats)
                if (debugConfig.debug_perf) {
                    logger.info(`Decode latency: ${avgDecode.toFixed(1)}ms (${this.latencySamples} samples)`);
                }

                // Reset for next interval
                this.decodeLatencyTotal = 0;
                this.latencySamples = 0;
                this.lastLatencyLog = now;
            }
        } catch (e) {
            // Throttle so a stuck decoder can't fire a fetch per frame at 30 fps.
            const now = performance.now();
            this._lastDecodeErrAt = this._lastDecodeErrAt || 0;
            this._decodeErrCount = (this._decodeErrCount || 0) + 1;
            if (now - this._lastDecodeErrAt > 2000) {
                logger.error('Failed to decode PNG', { error: e.message, suppressed: this._decodeErrCount - 1 });
                this._lastDecodeErrAt = now;
                this._decodeErrCount = 0;
            }
        }
    }

}

/**
 * Render a frame with metadata header onto a canvas context.
 * Shared by PNGDecoder (DataChannel) and HTTPStreamDecoder (fetch streaming).
 *
 * @param {ArrayBuffer} data - Frame data: [45-byte header][PNG image data]
 *   (or raw PNG without header if data is small)
 * @param {CanvasRenderingContext2D} ctx - Canvas 2D context to draw on
 * @param {HTMLCanvasElement} canvas - Canvas element (for resizing)
 * @param {object} state - Mutable state object for latency tracking:
 *   { decodeLatencyTotal, latencySamples, lastLatencyLog, lastAverageLatency, frameCount, lastFrameTime }
 * @param {function|null} onFrame - Callback: onFrame(frameCount, metadata)
 * @returns {Promise<void>}
 */
async function renderFrameToCanvas(data, ctx, canvas, state, onFrame) {
    if (!ctx) {
        logger.error('renderFrameToCanvas: ctx is null');
        return;
    }

    if (state.frameCount < 3) {
        logger.info('renderFrameToCanvas called', {
            dataSize: data ? data.byteLength : 0,
            canvasW: canvas.width,
            canvasH: canvas.height,
            frameCount: state.frameCount
        });
    }

    const t5_receive = Date.now();
    let pngData = data;
    let t1_frame_ready = 0;
    let rectX = 0, rectY = 0, rectWidth = 0, rectHeight = 0;
    let frameWidth = 0, frameHeight = 0;
    let cursorX = 0, cursorY = 0, cursorVisible = 0;

    // Parse metadata header if present
    if (data instanceof ArrayBuffer && data.byteLength > CONSTANTS.MIN_PNG_SIZE_WITH_HEADER) {
        const view = new DataView(data);

        let lo = view.getUint32(0, true);
        let hi = view.getUint32(4, true);
        t1_frame_ready = lo + hi * 0x100000000;

        rectX = view.getUint32(8, true);
        rectY = view.getUint32(12, true);
        rectWidth = view.getUint32(16, true);
        rectHeight = view.getUint32(20, true);

        frameWidth = view.getUint32(24, true);
        frameHeight = view.getUint32(28, true);

        lo = view.getUint32(32, true);
        hi = view.getUint32(36, true);
        // t4_send = lo + hi * 0x100000000;

        cursorX = view.getUint16(40, true);
        cursorY = view.getUint16(42, true);
        cursorVisible = view.getUint8(44);

        pngData = data.slice(CONSTANTS.PNG_HEADER_SIZE);
    }

    const blob = pngData instanceof Blob ? pngData : new Blob([pngData], { type: 'image/png' });

    try {
        const bitmap = await createImageBitmap(blob);
        // Same teardown-race guard as PNGDecoder.handleData (see comment there).
        if (!ctx || !canvas || !canvas.isConnected) {
            if (bitmap.close) bitmap.close();
            return;
        }
        const t6_draw = performance.now();

        const t5_receive_perf = performance.now() - (Date.now() - t5_receive);
        const decodeLatency = t6_draw - t5_receive_perf;

        if (decodeLatency >= 0 && decodeLatency < CONSTANTS.MAX_DECODE_LATENCY_MS) {
            state.decodeLatencyTotal += decodeLatency;
            state.latencySamples++;
        }

        if (frameWidth > 0 && frameHeight > 0) {
            if (canvas.width !== frameWidth || canvas.height !== frameHeight) {
                canvas.width = frameWidth;
                canvas.height = frameHeight;
            }
        }

        ctx.drawImage(bitmap, rectX, rectY);
        state.frameCount++;
        state.lastFrameTime = performance.now();

        if (onFrame) {
            onFrame(state.frameCount, { cursorX, cursorY, cursorVisible, frameWidth, frameHeight });
        }

        const now = performance.now();
        if (now - state.lastLatencyLog > CONSTANTS.LATENCY_LOG_INTERVAL_MS && state.latencySamples > 0) {
            const avgDecode = state.decodeLatencyTotal / state.latencySamples;
            state.lastAverageLatency = avgDecode;
            if (debugConfig.debug_perf) {
                logger.info(`Decode latency: ${avgDecode.toFixed(1)}ms (${state.latencySamples} samples)`);
            }
            state.decodeLatencyTotal = 0;
            state.latencySamples = 0;
            state.lastLatencyLog = now;
        }
    } catch (e) {
        // Throttle so a stuck decoder can't fire a fetch per frame at 30 fps.
        const now = performance.now();
        state._lastDecodeErrAt = state._lastDecodeErrAt || 0;
        state._decodeErrCount = (state._decodeErrCount || 0) + 1;
        if (now - state._lastDecodeErrAt > 2000) {
            logger.error('Failed to decode frame', { error: e.message, suppressed: state._decodeErrCount - 1 });
            state._lastDecodeErrAt = now;
            state._decodeErrCount = 0;
        }
    }
}

/**
 * HTTP Stream Decoder — proxy-friendly video over plain HTTP chunked transfer.
 *
 * Uses fetch() + ReadableStream to receive length-prefixed frames from
 * GET /api/stream, then renders dirty rects onto a canvas.
 * No WebRTC or WebSocket required.
 *
 * Wire format per frame:
 *   [4-byte total_length (LE uint32)] [45-byte header] [PNG image data]
 */
class HTTPStreamDecoder extends VideoDecoder {
    constructor(canvasElement) {
        super(canvasElement);
        this.canvas = canvasElement;
        this.ctx = null;
        this.abortController = null;
        this.buffer = new Uint8Array(0);

        // Latency tracking state (shared with renderFrameToCanvas)
        this.state = {
            decodeLatencyTotal: 0,
            latencySamples: 0,
            lastLatencyLog: 0,
            lastAverageLatency: 0,
            frameCount: 0,
            lastFrameTime: 0
        };

        // Stats for the stats display
        this.bytesReceived = 0;
        this.reconnectDelay = 1000;
    }

    get type() { return CodecType.HTTP_STREAM; }
    get name() { return 'HTTP Stream'; }

    init() {
        this.ctx = this.canvas.getContext('2d');
        if (!this.ctx) {
            logger.error('HTTPStreamDecoder: Failed to get canvas 2D context');
            return false;
        }
        this.abortController = new AbortController();
        this.startStreaming();
        logger.info('HTTPStreamDecoder initialized');
        return true;
    }

    cleanup() {
        if (this.abortController) {
            this.abortController.abort();
            this.abortController = null;
        }
        this.ctx = null;
        this.buffer = new Uint8Array(0);
    }

    // Reset session ID so server sends a full frame (used on emulator restart)
    resetSession() {
        this._resetSid = true;
    }

    getAverageLatency() { return this.state.lastAverageLatency; }

    getStats() {
        return {
            frameCount: this.state.frameCount,
            fps: this.calculateFps()
        };
    }

    async startStreaming() {
        const baseUrl = getApiUrl('frame');
        logger.info('HTTPStreamDecoder: starting long-poll loop', { url: baseUrl });

        let errorCount = 0;
        let sid = null;  // Session ID for dirty rect tracking

        while (this.abortController && !this.abortController.signal.aborted) {
            try {
                // Reset session on emulator restart (forces full frame)
                if (this._resetSid) {
                    sid = null;
                    this._resetSid = false;
                    logger.info('HTTPStreamDecoder: session reset (new emulator launch)');
                }
                const url = sid ? `${baseUrl}?sid=${sid}` : baseUrl;
                const response = await fetch(url, {
                    signal: this.abortController.signal
                });

                // Capture session ID from server
                const newSid = response.headers.get('X-Frame-Sid');
                if (newSid) sid = newSid;

                if (response.status === 204) {
                    // No new frame — wait and retry
                    await new Promise(r => setTimeout(r, 33));
                    continue;
                }

                if (!response.ok) {
                    throw new Error(`HTTP ${response.status}`);
                }

                const data = await response.arrayBuffer();
                this.bytesReceived += data.byteLength;
                errorCount = 0;

                this.state.frameCount++;
                if (this.state.frameCount <= 3 || this.state.frameCount % 100 === 0) {
                    logger.info(`HTTPStreamDecoder: frame #${this.state.frameCount}`, {
                        size: data.byteLength,
                        seq: response.headers.get('X-Frame-Seq'),
                        sid
                    });
                }

                await renderFrameToCanvas(
                    data,
                    this.ctx,
                    this.canvas,
                    this.state,
                    this.onFrame
                );

            } catch (e) {
                if (e.name === 'AbortError') return;
                errorCount++;
                const delay = Math.min(1000 * Math.pow(2, errorCount - 1), 10000);
                if (errorCount <= 3) {
                    logger.warn('HTTPStreamDecoder: fetch error', { error: e.message, retry: delay });
                }
                await new Promise(r => setTimeout(r, delay));
            }
        }

        logger.info('HTTPStreamDecoder: poll loop ended');
    }
}


// Factory to create the right decoder based on codec type
function createDecoder(codecType, element) {
    switch (codecType) {
        case CodecType.H264:
            return new H264Decoder(element);
        case CodecType.AV1:
            return new AV1Decoder(element);
        case CodecType.VP9:
            return new VP9Decoder(element);
        case CodecType.PNG:
        case CodecType.WEBP:
            return new PNGDecoder(element);
        case CodecType.HTTP_STREAM:
            return new HTTPStreamDecoder(element);
        default:
            logger.error('Unknown codec type', { codecType });
            return null;
    }
}

// Helper: Convert codec string to CodecType enum
function parseCodecString(codecStr) {
    switch (codecStr) {
        case 'h264': return CodecType.H264;
        case 'av1': return CodecType.AV1;
        case 'vp9': return CodecType.VP9;
        case 'png': return CodecType.PNG;
        case 'webp': return CodecType.WEBP;
        case 'httpstream': return CodecType.HTTP_STREAM;
        default:
            logger.warn('Unknown codec string, defaulting to PNG', { codec: codecStr });
            return CodecType.PNG;
    }
}

// Helper: Convert CodecType enum back to its server-side string id.
function codecTypeToString(codecType) {
    switch (codecType) {
        case CodecType.H264: return 'h264';
        case CodecType.AV1: return 'av1';
        case CodecType.VP9: return 'vp9';
        case CodecType.PNG: return 'png';
        case CodecType.WEBP: return 'webp';
        case CodecType.HTTP_STREAM: return 'httpstream';
        default: return null;
    }
}

// Update the active codec indicator in the header
function updateCodecIndicator(codecType) {
    const el = document.getElementById('codec-active');
    if (el) {
        const label = getCodecLabel(codecType);
        const transport = codecType === CodecType.HTTP_STREAM ? '/HTTP' :
            (codecType === CodecType.H264 || codecType === CodecType.VP9) ? '/RTP' : '/DC';
        el.textContent = `[${label}${transport}]`;
    }
}

// Helper: Get display label for codec
function getCodecLabel(codecType) {
    switch (codecType) {
        case CodecType.H264: return 'H.264';
        case CodecType.AV1: return 'AV1';
        case CodecType.VP9: return 'VP9';
        case CodecType.PNG: return 'PNG';
        case CodecType.WEBP: return 'WEBP';
        case CodecType.HTTP_STREAM: return 'HTTP';
        default: return 'Unknown';
    }
}


// W3C KeyboardEvent.code → classic Mac ADB virtual keycode.
//
// Classic Mac ADB scancodes (what BasiliskII / SheepShaver / our adb.cpp
// expect) — NOT modern macOS HIToolbox kVK_* values. The big differences:
//   - Arrow keys live at 0x3B–0x3E (not 0x7B–0x7E).
//   - Modifiers don't split L/R: Control=0x36, Command=0x37, Shift=0x38,
//     CapsLock=0x39, Option=0x3A. Both ControlLeft and ControlRight map
//     to 0x36, etc.
// We key off `event.code` (physical key position) so non-QWERTY layouts
// — Dvorak, AZERTY, Colemak — produce the right Mac key. Keying off
// `event.keyCode` (the previous behavior) was layout-dependent and got
// every non-QWERTY user wrong characters.
const EVENT_CODE_TO_MAC = Object.freeze({
    // Letters
    KeyA: 0x00, KeyB: 0x0B, KeyC: 0x08, KeyD: 0x02, KeyE: 0x0E,
    KeyF: 0x03, KeyG: 0x05, KeyH: 0x04, KeyI: 0x22, KeyJ: 0x26,
    KeyK: 0x28, KeyL: 0x25, KeyM: 0x2E, KeyN: 0x2D, KeyO: 0x1F,
    KeyP: 0x23, KeyQ: 0x0C, KeyR: 0x0F, KeyS: 0x01, KeyT: 0x11,
    KeyU: 0x20, KeyV: 0x09, KeyW: 0x0D, KeyX: 0x07, KeyY: 0x10,
    KeyZ: 0x06,
    // Digits
    Digit0: 0x1D, Digit1: 0x12, Digit2: 0x13, Digit3: 0x14, Digit4: 0x15,
    Digit5: 0x17, Digit6: 0x16, Digit7: 0x1A, Digit8: 0x1C, Digit9: 0x19,
    // Punctuation
    Backquote: 0x32, Minus: 0x1B, Equal: 0x18,
    BracketLeft: 0x21, BracketRight: 0x1E, Backslash: 0x2A,
    Semicolon: 0x29, Quote: 0x27, Comma: 0x2B, Period: 0x2F, Slash: 0x2C,
    IntlBackslash: 0x0A, IntlYen: 0x5D, IntlRo: 0x5E,
    // Whitespace & control
    Space: 0x31, Tab: 0x30, Enter: 0x24, Escape: 0x35,
    Backspace: 0x33, Delete: 0x75, CapsLock: 0x39,
    // Shift is the only modifier in this static table — it's never user-
    // remapped (Mac Shift IS the only sensible target). Ctrl / Alt / Meta
    // are owned by `modifierOverride` (built from keyboardConfig) so the
    // user can change them in Settings → Keyboard at runtime.
    ShiftLeft: 0x38, ShiftRight: 0x38,
    // Editing & navigation
    Insert: 0x72,                                     // Mac Help
    Home: 0x73, End: 0x77, PageUp: 0x74, PageDown: 0x79,
    ArrowLeft: 0x3B, ArrowRight: 0x3C,
    ArrowDown: 0x3D, ArrowUp: 0x3E,
    // Function row (full Apple Extended Keyboard II range)
    F1: 0x7A, F2: 0x78, F3: 0x63, F4: 0x76, F5: 0x60,
    F6: 0x61, F7: 0x62, F8: 0x64, F9: 0x65, F10: 0x6D,
    F11: 0x67, F12: 0x6F, F13: 0x69, F14: 0x6B, F15: 0x71,
    // PC keys positionally aligned with Mac F13/F14/F15
    PrintScreen: 0x69, ScrollLock: 0x6B, Pause: 0x71,
    // Numpad
    NumLock: 0x47,                                    // Mac Clear
    NumpadDivide: 0x4B, NumpadMultiply: 0x43,
    NumpadSubtract: 0x4E, NumpadAdd: 0x45,
    NumpadEnter: 0x4C, NumpadDecimal: 0x41, NumpadEqual: 0x51,
    Numpad0: 0x52, Numpad1: 0x53, Numpad2: 0x54, Numpad3: 0x55,
    Numpad4: 0x56, Numpad5: 0x57, Numpad6: 0x58, Numpad7: 0x59,
    Numpad8: 0x5B, Numpad9: 0x5C,
});

// User-remappable modifier overrides. Keys are KeyboardEvent.code values
// for Ctrl/Alt/Meta L+R; values are classic Mac ADB scancodes. Built from
// `keyboardConfig` whenever it changes (initial defaults + /api/config
// load + Settings save). Always consulted before EVENT_CODE_TO_MAC so
// the user's remap wins.
const MAC_MOD_CODE = {
    command: 0x37, control: 0x36, option: 0x3A, shift: 0x38,
    off: null,            // produces no Mac event — browser keeps the key
};
const MAC_MOD_SYMBOL     = { command: '⌘', control: '⌃', option: '⌥', shift: '⇧' };
const MAC_MOD_CHIP_CLASS = { command: 'mod-cmd', control: 'mod-control',
                             option:  'mod-option', shift: 'mod-shift' };

// Platform-appropriate default modifier mapping. On Mac the user's ⌘
// reports as MetaLeft/Right and they expect ⌘-C to land as Mac
// Command-C — identity mapping. On PC users have habituated to Ctrl-C
// as the primary shortcut, so PC Ctrl→⌘ with PC Win→⌃ keeping Mac
// Control reachable. Detection prefers the modern Client-Hints API
// (Chromium-only, accurate); falls back to navigator.platform string
// matching (deprecated but still reliable across all current browsers).
function isMacPlatform() {
    try {
        if (navigator.userAgentData?.platform) {
            return /macOS|iOS/i.test(navigator.userAgentData.platform);
        }
    } catch (_) { /* fall through */ }
    return /Mac|iPhone|iPad|iPod/i.test(navigator.platform || navigator.userAgent || '');
}
function defaultKeyboardModMap() {
    return isMacPlatform()
        ? { ctrl: 'control', alt: 'option', meta: 'command', fn: 'off' }   // identity
        : { ctrl: 'command', alt: 'option', meta: 'control', fn: 'off' };  // PC habit
}

let keyboardConfig = { ...defaultKeyboardModMap(), release_on_blur: true };
let modifierOverride = {};

function rebuildModifierOverride() {
    modifierOverride = {};
    const apply = (cfgKey, codes) => {
        const target = MAC_MOD_CODE[keyboardConfig[cfgKey]];
        if (target == null) return;          // 'off' or unknown → leave unset
        for (const c of codes) modifierOverride[c] = target;
    };
    apply('ctrl', ['ControlLeft', 'ControlRight']);
    apply('alt',  ['AltLeft', 'AltRight']);
    apply('meta', ['MetaLeft', 'MetaRight', 'OSLeft', 'OSRight']);
    // Fn rarely fires from browser (most laptops handle it in firmware
    // before the OS sees it), but the W3C code is defined and some
    // keyboards emit it — wire it up so users can opt in.
    apply('fn',   ['Fn', 'FnLock']);
}

// Re-render the held-mods chip so its labels reflect the live remap. Each
// segment shows "{PC name}→{Mac symbol}"; segments lit (.has-* on parent)
// when their Mac modifier scancode is in _heldKeys.
function renderHeldModsChip() {
    const chip = document.getElementById('held-mods');
    if (!chip) return;
    const segments = ['<span class="mod mod-shift" title="Shift = ⇧">⇧</span>'];
    const pcMappings = [
        { pc: 'Ctrl', cfgKey: 'ctrl' },
        { pc: 'Alt',  cfgKey: 'alt'  },
        { pc: 'Win',  cfgKey: 'meta' },
        { pc: 'Fn',   cfgKey: 'fn'   },
    ];
    for (const {pc, cfgKey} of pcMappings) {
        const target = keyboardConfig[cfgKey];
        const sym    = MAC_MOD_SYMBOL[target];
        const cls    = MAC_MOD_CHIP_CLASS[target];
        if (!sym || !cls) continue;          // 'off' / unknown → omit
        segments.push(
            `<span class="mod ${cls}" title="${pc} → Mac ${target}">` +
            `${pc}<span class="arrow">→</span>${sym}</span>`
        );
    }
    chip.innerHTML = segments.join('');
}

// Refresh the "Platform default" option label in each Keyboard dropdown
// so it shows the resolved target, e.g. "Platform default (⌘ Command)".
// The value attribute stays "" — picking it persists the empty-string
// sentinel so the next load re-resolves on whatever platform the user
// is on. Called once at init; platform doesn't change mid-session.
function relabelKeyboardDefaults() {
    const def = defaultKeyboardModMap();
    const fmt = (target) => {
        if (target === 'off') return 'Platform default (off)';
        const sym  = MAC_MOD_SYMBOL[target] || '';
        const name = target.charAt(0).toUpperCase() + target.slice(1);
        return `Platform default (${sym} ${name})`;
    };
    const relabel = (selectId, key) => {
        const sel = document.getElementById(selectId);
        const opt = sel?.querySelector('option[value=""]');
        if (opt) opt.textContent = fmt(def[key]);
    };
    relabel('cfg-kb-ctrl', 'ctrl');
    relabel('cfg-kb-alt',  'alt');
    relabel('cfg-kb-meta', 'meta');
    relabel('cfg-kb-fn',   'fn');
}

function applyKeyboardConfig(cfg) {
    if (cfg) {
        // Empty / missing fields fall back to the platform default. Server
        // stores "" as the "no preference saved" sentinel — fresh installs
        // land here, and the JS picks Mac vs PC defaults from navigator.
        // Explicit user picks (any non-empty Mac modifier name) win.
        const def = defaultKeyboardModMap();
        keyboardConfig = {
            ctrl: cfg.ctrl || def.ctrl,
            alt:  cfg.alt  || def.alt,
            meta: cfg.meta || def.meta,
            fn:   cfg.fn   || def.fn,
            release_on_blur: cfg.release_on_blur ?? true,
        };
    }
    rebuildModifierOverride();
    renderHeldModsChip();
}

// Apply the bundled defaults at module load so a key pressed before any
// /api/config fetch still gets a sensible mapping.
rebuildModifierOverride();

// Fallback for events where `event.code` is empty/missing — older Safari
// IME path, on-screen keyboards, synthesized events. Keys are KeyboardEvent.key
// values; values are Mac ADB scancodes. Limited to keys we can map without
// knowing the physical position.
const EVENT_KEY_TO_MAC = Object.freeze({
    Enter: 0x24, Tab: 0x30, ' ': 0x31, Escape: 0x35, Backspace: 0x33,
    Delete: 0x75, CapsLock: 0x39,
    Shift: 0x38, Control: 0x36, Alt: 0x3A, Meta: 0x37,
    ArrowLeft: 0x3B, ArrowRight: 0x3C, ArrowDown: 0x3D, ArrowUp: 0x3E,
    Home: 0x73, End: 0x77, PageUp: 0x74, PageDown: 0x79,
    Insert: 0x72, Help: 0x72, Clear: 0x47,
    F1: 0x7A, F2: 0x78, F3: 0x63, F4: 0x76, F5: 0x60, F6: 0x61, F7: 0x62,
    F8: 0x64, F9: 0x65, F10: 0x6D, F11: 0x67, F12: 0x6F,
    F13: 0x69, F14: 0x6B, F15: 0x71,
});

// Resolve a KeyboardEvent to a Mac ADB scancode (0x00–0x7F), or null if
// we can't map it. Order: user-configurable modifier override (Ctrl/Alt/
// Meta) → static EVENT_CODE_TO_MAC table (everything else, plus Shift) →
// EVENT_KEY_TO_MAC fallback for events with empty `code` (IME paths).
function macKeycodeForEvent(e) {
    if (e.code) {
        if (Object.prototype.hasOwnProperty.call(modifierOverride, e.code)) {
            return modifierOverride[e.code];
        }
        if (Object.prototype.hasOwnProperty.call(EVENT_CODE_TO_MAC, e.code)) {
            return EVENT_CODE_TO_MAC[e.code];
        }
    }
    if (e.key && Object.prototype.hasOwnProperty.call(EVENT_KEY_TO_MAC, e.key)) {
        return EVENT_KEY_TO_MAC[e.key];
    }
    return null;
}


// Main WebRTC Client
class BasiliskWebRTC {
    constructor(videoElement, canvasElement = null) {
        this.video = videoElement;
        this.canvas = canvasElement;
        this.ws = null;
        this.pc = null;
        this.videoTrack = null;
        this.connected = false;
        this.wsUrl = null;
        this.audioCapturing = false;  // Flag for synchronized audio capture

        // Codec/decoder management
        this.codecType = null;  // Will be set by server
        this.decoder = null;
        this.isReconnecting = false;  // Suppress auto-reconnect during deliberate reconnect

        // Mouse mode ('absolute' or 'relative')
        this.mouseMode = 'relative';  // Default to relative (matches UI and emulator)
        this.currentScreenWidth = 0;  // Mac screen dimensions (from server)
        this.currentScreenHeight = 0;

        // Cursor overlay (for absolute mode)
        this.cursorOverlay = document.getElementById('cursor-overlay');
        this.cursorCtx = this.cursorOverlay ? this.cursorOverlay.getContext('2d') : null;
        this.currentCursorX = 0;
        this.currentCursorY = 0;
        this.cursorVisible = false;

        // Stats tracking
        this.stats = {
            fps: 0,
            bitrate: 0,
            framesDecoded: 0,
            packetsLost: 0,
            jitter: 0,
            codec: 'h264'
        };
        this.lastStatsTime = performance.now();
        this.lastBytesReceived = 0;
        this.lastFramesDecoded = 0;

        // Cached resolution to avoid unnecessary DOM updates
        this.cachedWidth = 0;
        this.cachedHeight = 0;

        // Cached mouse scaling for absolute mode (avoid getBoundingClientRect on every move)
        this.cachedMouseRect = null;
        this.cachedMouseScaleX = 1;
        this.cachedMouseScaleY = 1;

        // PNG/DataChannel stats
        this.pngStats = {
            framesReceived: 0,
            bytesReceived: 0,
            lastFrameTime: 0,
            avgFrameSize: 0
        };
        this.lastPngFrameCount = 0;
        this.lastPngBytesReceived = 0;

        // Reconnection
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = CONSTANTS.MAX_RECONNECT_ATTEMPTS;
        this.reconnectTimer = null;

        // Frame detection for black screen debugging
        this.firstFrameReceived = false;
        this.frameCheckInterval = null;

        // WebRTC → HTTP stream auto-fallback
        this.webrtcFallbackTimer = null;
        this.httpStreamFallbackSec = 5;  // Seconds before falling back

    }

    // Set codec type before connecting
    setCodec(codecType) {
        if (this.connected) {
            logger.warn('Cannot change codec while connected');
            return false;
        }
        this.codecType = codecType;
        this.stats.codec = codecType;
        logger.info('Codec set', { codec: codecType });
        return true;
    }

    // Initialize decoder based on codec type
    initDecoder() {
        logger.info('initDecoder called', { codecType: this.codecType });
        if (!this.codecType) {
            logger.warn('Cannot initialize decoder - codec not yet set by server');
            return false;
        }

        if (this.decoder) {
            this.decoder.cleanup();
        }

        // H.264 and VP9 use video element (RTP); everything else uses canvas
        const usesVideoElement = (this.codecType === CodecType.H264 ||
                                   this.codecType === CodecType.VP9);
        const element = usesVideoElement ? this.video : this.canvas;
        if (!element) {
            logger.error('No display element for codec', { codec: this.codecType });
            return false;
        }

        this.decoder = createDecoder(this.codecType, element);
        if (!this.decoder) {
            return false;
        }

        // Set up frame callback for canvas-based decoders to update screen dimensions
        const usesCanvas = (this.codecType === CodecType.PNG || this.codecType === CodecType.WEBP ||
                            this.codecType === CodecType.HTTP_STREAM);
        if (usesCanvas) {
            this.decoder.onFrame = (frameCount, metadata) => {
                if (metadata && metadata.frameWidth && metadata.frameHeight) {
                    this.currentScreenWidth = metadata.frameWidth;
                    this.currentScreenHeight = metadata.frameHeight;
                    this.cachedMouseRect = null;
                }

                // For HTTP stream: mark as connected on first frame
                if (this.codecType === CodecType.HTTP_STREAM && !this.firstFrameReceived) {
                    this.firstFrameReceived = true;
                    this.connected = true;
                    this.updateStatus('Connected', 'connected');
                    this.hideOverlay();
                    this.updateConnectionUI(true);
                    const displayContainer = document.getElementById('display-container');
                    if (displayContainer) displayContainer.classList.remove('disconnected');
                    connectionSteps.setDone('frames');
                    logger.info('HTTP stream: first frame received');
                }
            };
        }

        // Show/hide appropriate element and set initial size
        if (this.video) {
            this.video.style.display = usesVideoElement ? 'block' : 'none';
            if (usesVideoElement) {
                this.video.width = this.currentScreenWidth || 640;
                this.video.height = this.currentScreenHeight || 480;
            }
        }
        if (this.canvas) this.canvas.style.display = !usesVideoElement ? 'block' : 'none';

        // Clear canvas to avoid stale pixels from previous session on reconnect
        if (!usesVideoElement && this.canvas) {
            const ctx = this.canvas.getContext('2d');
            if (ctx) ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
        }

        return this.decoder.init();
    }

    connect(wsUrl) {
        this.wsUrl = wsUrl;
        this.reconnectAttempts = 0;
        this._connect();
    }

    // Start HTTP stream mode (no WebRTC, no WebSocket)
    _connectHTTPStream() {
        this.cleanup();
        connectionSteps.reset();

        this.codecType = CodecType.HTTP_STREAM;
        this.stats.codec = 'httpstream';
        updateCodecIndicator(this.codecType);

        const codecSelect = document.getElementById('codec-select');
        if (codecSelect) {
            codecSelect.value = 'httpstream';
            codecSelect.disabled = false;
        }

        this.updateStatus('Connecting...', 'connecting');
        connectionSteps.setActive('frames');
        this.updateOverlayStatus('Connecting via HTTP stream...');

        if (!this.initDecoder()) {
            logger.error('Failed to initialize HTTP stream decoder');
            this.updateStatus('Decoder init failed', 'error');
            return;
        }

        // Set up input handlers for HTTP mode (POST-based)
        this.setupInputHandlers();

        logger.info('HTTP stream mode active (no WebRTC)', {
            decoderType: this.decoder ? this.decoder.constructor.name : 'null',
            canvasDisplay: this.canvas ? this.canvas.style.display : 'no-canvas',
            videoDisplay: this.video ? this.video.style.display : 'no-video'
        });
    }

    // Fall back from WebRTC to HTTP stream (auto-detected or manual)
    fallbackToHTTPStream() {
        // Cancel any pending fallback timer
        if (this.webrtcFallbackTimer) {
            clearTimeout(this.webrtcFallbackTimer);
            this.webrtcFallbackTimer = null;
        }

        // Tear down WebRTC
        this.cleanup();
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }

        // Switch to HTTP stream
        this._connectHTTPStream();
    }

    // Walk the auto-fallback chain (vp9 → h264 → webp → png → httpstream) one step.
    // Called when the current codec fails to deliver frames or ICE never connects.
    // Returns true if a step was taken; false if we exhausted the chain.
    _stepDownFallbackChain(reason) {
        if (this.firstFrameReceived || this.connected) return false; // already working

        const chain = (typeof buildCodecFallbackChain === 'function')
            ? buildCodecFallbackChain()
            : ['httpstream'];
        const currentId = codecTypeToString(this.codecType);
        const idx = chain.indexOf(currentId);
        const next = (idx >= 0 && idx + 1 < chain.length) ? chain[idx + 1] : null;
        if (!next || next === currentId) {
            logger.warn('Fallback chain exhausted', { reason, from: currentId });
            return false;
        }

        logger.warn(`Codec fallback: ${currentId} → ${next}`, { reason });

        // Update visible codec dropdown
        const codecSelect = document.getElementById('codec-select');
        if (codecSelect) codecSelect.value = next;

        if (next === 'httpstream') {
            this.fallbackToHTTPStream();
            return true;
        }

        // Cancel pending no-frames timer; we'll arm a fresh one on reconnect.
        if (this.webrtcFallbackTimer) {
            clearTimeout(this.webrtcFallbackTimer);
            this.webrtcFallbackTimer = null;
        }

        // Tear down current connection and reconnect with the next codec.
        this.cleanup();
        if (this.ws) { try { this.ws.close(); } catch (e) {} this.ws = null; }
        this.codecType = parseCodecString(next);
        this.stats.codec = next;
        this._connect();
        return true;
    }

    _connect() {
        // HTTP stream mode: skip WebRTC entirely
        if (this.codecType === CodecType.HTTP_STREAM) {
            this._connectHTTPStream();
            return;
        }

        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            logger.warn('Already connected');
            return;
        }

        this.cleanup();
        connectionSteps.reset();

        // Note: Decoder will be initialized once server sends codec in "connected" message

        if (debugConfig.debug_connection) {
            logger.info('Connecting to signaling server', { url: this.wsUrl });
        }
        this.updateStatus('Connecting...', 'connecting');
        connectionSteps.setActive('ws');

        try {
            this.ws = new WebSocket(this.wsUrl);
            this.ws.binaryType = 'arraybuffer';  // PNG/WebP frames arrive as binary

            this.ws.onopen = () => this.onWsOpen();
            this.ws.onmessage = (e) => this.onWsMessage(e);
            this.ws.onclose = (e) => this.onWsClose(e);
            this.ws.onerror = (e) => this.onWsError(e);

            // Auto-fallback: if no frame arrives within N seconds, walk the codec
            // chain (vp9 → h264 → webp → png → httpstream) one step.
            this.webrtcFallbackTimer = setTimeout(() => {
                if (!this.firstFrameReceived && !this.connected) {
                    if (!this._stepDownFallbackChain('no-frames-timeout')) {
                        // Chain exhausted — last-ditch HTTP stream fallback
                        try { localStorage.setItem('macemu_prefer_httpstream', '1'); } catch(e) {}
                        this.fallbackToHTTPStream();
                    }
                }
            }, this.httpStreamFallbackSec * 1000);

        } catch (e) {
            logger.error('WebSocket creation failed', { error: e.message });
            this.updateStatus('Connection failed', 'error');
            connectionSteps.setError('ws');
            // WebSocket failed entirely — try HTTP stream directly
            logger.info('WebSocket unavailable, trying HTTP stream');
            this.fallbackToHTTPStream();
        }
    }

    onWsOpen() {
        if (debugConfig.debug_connection) {
            logger.info('WebSocket connected');
        }
        connectionSteps.setDone('ws');
        connectionSteps.setActive('offer');
        this.updateStatus('Signaling connected', 'connecting');
        this.updateWebRTCState('ws', 'Open');

        this._startHeartbeat();

        // Request connection with configured codec
        const codec = this.codecType || serverUIConfig.webcodec || 'h264';
        logger.debug('Sending connect request', { codec });
        this.ws.send(JSON.stringify({
            type: 'connect',
            codec: codec
        }));
    }

    _startHeartbeat() {
        this._stopHeartbeat();
        this.lastPongAt = Date.now();
        // Send ping every 20s; keeps nginx idle-timeout (default 60s) from firing.
        this._pingInterval = setInterval(() => {
            if (!this._wsIsOpen()) return;
            try { this.ws.send('{"type":"ping"}'); } catch (e) {}
        }, 20000);
        // Every 10s, kill the socket if we haven't seen a pong in 45s — triggers
        // the existing reconnect ladder via onWsClose.
        this._pongCheckInterval = setInterval(() => {
            if (!this._wsIsOpen()) return;
            if (Date.now() - this.lastPongAt > 45000) {
                logger.warn('No pong in 45s, closing stale WebSocket');
                try { this.ws.close(); } catch (e) {}
            }
        }, 10000);
    }

    _stopHeartbeat() {
        if (this._pingInterval) { clearInterval(this._pingInterval); this._pingInterval = null; }
        if (this._pongCheckInterval) { clearInterval(this._pongCheckInterval); this._pongCheckInterval = null; }
    }

    onWsMessage(event) {
        // Binary = PNG/WebP frame bytes (frame delivery rides the same WS as signaling)
        if (event.data instanceof ArrayBuffer) {
            this.handleFrameBytes(event.data);
            return;
        }
        if (event.data instanceof Blob) {
            event.data.arrayBuffer().then(buf => this.handleFrameBytes(buf));
            return;
        }

        let msg;
        try {
            msg = JSON.parse(event.data);
        } catch (e) {
            logger.error('Failed to parse message', { data: event.data });
            return;
        }

        // Skip logging high-frequency / pure-keepalive types — cursor fires on every
        // mouse move and used to flood the debug log.
        if (msg.type !== 'cursor' && msg.type !== 'pong' && msg.type !== 'ping') {
            logger.debug(`Received: ${msg.type}`, msg.type === 'offer' ? { sdpLength: msg.sdp?.length } : null);
        }

        this.handleSignaling(msg);
    }

    handleFrameBytes(arrayBuffer) {
        // Server sends the same 45-byte header + PNG/WebP bytes that previously
        // rode the data channel. Decoder is format-agnostic.
        const usesVideoTrack = (this.codecType === CodecType.H264 || this.codecType === CodecType.VP9);
        if (!this.decoder || usesVideoTrack) return;

        this.decoder.handleData(arrayBuffer);

        this.pngStats.framesReceived++;
        this.pngStats.bytesReceived += arrayBuffer.byteLength;
        this.pngStats.lastFrameTime = performance.now();
        this.pngStats.avgFrameSize = this.pngStats.bytesReceived / this.pngStats.framesReceived;

        if (!this.firstFrameReceived) {
            this.firstFrameReceived = true;
            connectionSteps.setDone('frames');
            if (debugConfig.debug_connection) {
                logger.info('First PNG/WebP frame received via WebSocket');
            }

            if (this.webrtcFallbackTimer) {
                clearTimeout(this.webrtcFallbackTimer);
                this.webrtcFallbackTimer = null;
            }
            try { localStorage.removeItem('macemu_prefer_httpstream'); } catch (e) {}

            this.connected = true;
            this.updateStatus('Connected', 'connected');
            this.hideOverlay();
            this.updateConnectionUI(true);

            const displayContainer = document.getElementById('display-container');
            if (displayContainer) {
                displayContainer.classList.remove('disconnected');
            }
        }
    }

    onWsClose(event) {
        logger.warn('WebSocket closed', { code: event.code, reason: event.reason });
        this.updateWebRTCState('ws', 'Closed');
        this.connected = false;
        this._stopHeartbeat();
        this.updateStatus('Disconnected', 'error');
        this.scheduleReconnect();
    }

    onWsError(event) {
        logger.error('WebSocket error');
        this.updateWebRTCState('ws', 'Error');
    }

    async handleSignaling(msg) {
        switch (msg.type) {
            case 'welcome':
                if (debugConfig.debug_connection) {
                    logger.info('Server acknowledged connection');
                }
                this.updateOverlayStatus('Waiting for video offer...');
                break;

            case 'pong':
                this.lastPongAt = Date.now();
                break;

            case 'cursor':
                // H.264/VP9 cursor metadata (used to ride the data channel)
                this.currentCursorX = msg.x || 0;
                this.currentCursorY = msg.y || 0;
                this.cursorVisible = !!msg.visible;
                break;

            case 'capture':
                logger.info('[Capture] Triggered by server!');
                this.startAudioCapture();
                break;

            case 'connected':
                // Server tells us which codec to use
                if (msg.codec) {
                    const serverCodec = parseCodecString(msg.codec);
                    if (serverCodec !== this.codecType) {
                        if (debugConfig.debug_connection) {
                            logger.info('Server codec', { codec: msg.codec });
                        }
                        this.codecType = serverCodec;
                        this.stats.codec = msg.codec;
                    }
                    // Always init decoder on connect — ensures display elements
                    // are toggled correctly (e.g., httpstream→h264 transition)
                    this.initDecoder();

                    // Update codec selector UI
                    const codecSelect = document.getElementById('codec-select');
                    if (codecSelect) {
                        codecSelect.value = msg.codec;
                        codecSelect.disabled = false;
                    }
                    updateCodecIndicator(this.codecType);

                    // Wire input handlers to the appropriate display element and
                    // send initial mouse mode. Input now rides the signaling WS
                    // (used to ride the data channel).
                    this.setupInputHandlers();
                    this.sendMouseModeChange(this.mouseMode);
                }
                if (debugConfig.debug_connection) {
                    logger.info('Server acknowledged connection', { codec: msg.codec, peer_id: msg.peer_id });
                }
                // PNG/WebP don't receive an SDP offer — frames just start flowing on WS.
                if (this.codecType === CodecType.H264 || this.codecType === CodecType.VP9) {
                    this.updateOverlayStatus('Waiting for video offer...');
                } else {
                    this.updateOverlayStatus('Waiting for first frame...');
                }
                break;

            case 'offer':
                if (debugConfig.debug_connection) {
                    logger.info('Received SDP offer', { sdpLength: msg.sdp.length });
                }
                connectionSteps.setDone('offer');
                connectionSteps.setActive('ice');
                this.updateOverlayStatus('Processing offer...');

                // Show SDP info in debug panel
                this.updateSdpInfo(msg.sdp);

                await this.handleOffer(msg.sdp);
                break;

            case 'reconnect':
                // Server is requesting reconnection (e.g., codec change)
                logger.info('Server requested reconnection', { reason: msg.reason, codec: msg.codec });
                if (msg.reason === 'codec_change' && msg.codec) {
                    this.codecType = parseCodecString(msg.codec);
                    this.stats.codec = msg.codec;
                    updateCodecIndicator(this.codecType);
                }
                // If auto-reconnect already fired (PC close arrived before this message),
                // skip duplicate reconnect to avoid nulling the in-flight PC
                if (this.isReconnecting) {
                    logger.info('Reconnect already in progress, skipping duplicate');
                    break;
                }
                this.isReconnecting = true;
                // Reconnect the PeerConnection with new codec
                this.reconnectPeerConnection();
                break;

            case 'candidate':
                logger.debug('Received ICE candidate', { mid: msg.mid });
                if (this.pc) {
                    try {
                        await this.pc.addIceCandidate(new RTCIceCandidate({
                            candidate: msg.candidate,
                            sdpMid: msg.mid
                        }));
                    } catch (e) {
                        logger.warn('Failed to add ICE candidate', { error: e.message });
                    }
                }
                break;

            case 'error':
                logger.error('Server error', { message: msg.message });
                this.updateStatus('Server error', 'error');
                break;

            default:
                logger.debug('Unknown message type', { type: msg.type });
        }
    }

    async handleOffer(sdp) {
        this.createPeerConnection();

        try {
            const offer = new RTCSessionDescription({ type: 'offer', sdp: sdp });
            await this.pc.setRemoteDescription(offer);
            if (debugConfig.debug_connection) {
                logger.info('Set remote description (offer)');
            }

            const answer = await this.pc.createAnswer();
            await this.pc.setLocalDescription(answer);
            if (debugConfig.debug_connection) {
                logger.info('Created and set local description (answer)');
            }

            // Wait for ICE gathering to complete before sending answer
            // This ensures all candidates are included in the SDP
            await this.waitForIceGathering();

            // Send the final answer with all ICE candidates included
            const finalAnswer = this.pc.localDescription;

            // Debug: check SDP has ICE credentials
            if (!finalAnswer.sdp.includes('a=ice-ufrag:')) {
                logger.error('Answer SDP missing ice-ufrag!', { sdp: finalAnswer.sdp });
            } else if (debugConfig.debug_connection) {
                logger.info('Answer SDP has ICE credentials');
            }

            this.ws.send(JSON.stringify({
                type: 'answer',
                sdp: finalAnswer.sdp
            }));
            logger.debug('Sent SDP answer with ICE candidates');

        } catch (e) {
            logger.error('Failed to handle offer', { error: e.message });
            connectionSteps.setError('offer');
            this.updateStatus('Offer handling failed', 'error');
        }
    }

    waitForIceGathering() {
        return new Promise((resolve) => {
            if (this.pc.iceGatheringState === 'complete') {
                resolve();
                return;
            }

            let timer = null;

            const checkState = () => {
                if (!this.pc) { clearTimeout(timer); resolve(); return; }
                if (this.pc.iceGatheringState === 'complete') {
                    clearTimeout(timer);
                    this.pc.removeEventListener('icegatheringstatechange', checkState);
                    logger.info('ICE gathering complete, sending answer');
                    resolve();
                }
            };

            this.pc.addEventListener('icegatheringstatechange', checkState);

            // Timeout after 1 second - with no STUN servers, gathering should
            // complete almost instantly. This is just a safety fallback.
            timer = setTimeout(() => {
                this.pc.removeEventListener('icegatheringstatechange', checkState);
                if (debugConfig.debug_connection) {
                    logger.warn('ICE gathering timeout, sending answer with available candidates');
                }
                resolve();
            }, 1000);
        });
    }

    createPeerConnection() {
        if (debugConfig.debug_connection) {
            logger.info('Creating RTCPeerConnection');
        }

        // No STUN servers needed for localhost/LAN — server also has STUN disabled.
        // With no STUN, ICE gathering completes instantly (host candidates only),
        // avoiding a 5-second timeout waiting for server-reflexive candidates.
        const config = {
            iceServers: []
        };

        this.pc = new RTCPeerConnection(config);

        this.pc.ontrack = (e) => this.onTrack(e);
        this.pc.onicecandidate = (e) => this.onIceCandidate(e);
        this.pc.oniceconnectionstatechange = () => this.onIceConnectionStateChange();
        this.pc.onicegatheringstatechange = () => this.onIceGatheringStateChange();
        this.pc.onconnectionstatechange = () => this.onConnectionStateChange();
        this.pc.onsignalingstatechange = () => this.onSignalingStateChange();

        this.updateWebRTCState('pc', 'Created');
    }

    onTrack(event) {
        if (debugConfig.debug_connection) {
            logger.info('Track received', { kind: event.track.kind, id: event.track.id });
        }
        connectionSteps.setDone('track');
        connectionSteps.setActive('frames');
        this.updateOverlayStatus('Receiving stream...');

        // Handle audio track
        if (event.track.kind === 'audio') {
            logger.info('Audio track received', {
                id: event.track.id,
                label: event.track.label,
                enabled: event.track.enabled,
                muted: event.track.muted,
                readyState: event.track.readyState
            });

            this.audioTrack = event.track;

            // Ensure track is enabled (not disabled)
            event.track.enabled = true;

            // Track state monitoring
            event.track.onmute = () => {
                logger.warn('Audio track muted');
                this.updateWebRTCState('audio-track-muted', 'Yes');
            };
            event.track.onunmute = () => {
                logger.info('Audio track unmuted');
                this.updateWebRTCState('audio-track-muted', 'No');
            };
            event.track.onended = () => {
                logger.warn('Audio track ended');
                this.updateWebRTCState('audio-track-state', 'Ended');
            };

            // Log initial mute state
            if (event.track.muted) {
                logger.warn('Audio track arrived MUTED - this may indicate no audio data', {
                    readyState: event.track.readyState,
                    enabled: event.track.enabled
                });
            }

            this.updateWebRTCState('audio-track-state', event.track.readyState);
            this.updateWebRTCState('audio-track-enabled', event.track.enabled ? 'Yes' : 'No');
            this.updateWebRTCState('audio-track-muted', event.track.muted ? 'Yes' : 'No');
            this.updateWebRTCState('audio-format', 'Opus 48kHz Stereo');

            // Create or get audio element
            let audioElement = document.getElementById('macemu-audio');
            if (!audioElement) {
                audioElement = document.createElement('audio');
                audioElement.id = 'macemu-audio';
                audioElement.autoplay = true;
                audioElement.volume = 1.0;
                document.body.appendChild(audioElement);
                logger.info('Created audio element for playback');
            }

            // Attach audio stream
            if (event.streams && event.streams[0]) {
                audioElement.srcObject = event.streams[0];

                // Add event listeners to monitor audio playback
                audioElement.onplay = () => logger.info('Audio element: playing');
                audioElement.onpause = () => logger.warn('Audio element: paused');
                audioElement.onvolumechange = () => logger.info('Audio volume changed', { volume: audioElement.volume, muted: audioElement.muted });

                audioElement.play().then(() => {
                    logger.info('Audio play() succeeded', {
                        volume: audioElement.volume,
                        muted: audioElement.muted,
                        paused: audioElement.paused,
                        readyState: audioElement.readyState
                    });

                    // Audio capture is now triggered by server when user presses 'C'
                    // (removed auto-start)
                }).catch(e => {
                    logger.warn('Audio play() failed', { error: e.message });
                });
            }
        }

        // Handle video track
        else if (event.track.kind === 'video') {
            this.videoTrack = event.track;
            logger.info('VIDEO TRACK received', {
                id: event.track.id,
                label: event.track.label,
                enabled: event.track.enabled,
                muted: event.track.muted,
                readyState: event.track.readyState,
                hasStreams: !!(event.streams && event.streams.length),
                streamCount: event.streams ? event.streams.length : 0,
                codecType: this.codecType,
                videoElement: this.video ? 'exists' : 'null',
                videoDisplay: this.video ? this.video.style.display : 'n/a',
                videoW: this.video ? this.video.width : 'n/a',
                videoH: this.video ? this.video.height : 'n/a'
            });

            // Track state monitoring
            event.track.onmute = () => {
                logger.warn('Video track muted');
                this.updateWebRTCState('track-muted', 'Yes');
            };
            event.track.onunmute = () => {
                logger.info('Video track unmuted');
                this.updateWebRTCState('track-muted', 'No');
            };
            event.track.onended = () => {
                logger.warn('Video track ended');
                this.updateWebRTCState('track-state', 'Ended');
            };

            this.updateWebRTCState('track-state', event.track.readyState);
            this.updateWebRTCState('track-enabled', event.track.enabled ? 'Yes' : 'No');
            this.updateWebRTCState('track-muted', event.track.muted ? 'Yes' : 'No');

            if (event.streams && event.streams[0]) {
                logger.info('VIDEO: Attaching stream to video element', {
                    streamId: event.streams[0].id,
                    trackCount: event.streams[0].getTracks().length,
                    trackKinds: event.streams[0].getTracks().map(t => t.kind).join(',')
                });
                this.video.srcObject = event.streams[0];

                // Log all video element events for debugging
                this.video.onloadstart = () => logger.info('Video: loadstart');
                this.video.onprogress = () => logger.info('Video: progress');
                this.video.onsuspend = () => logger.debug('Video: suspend');
                this.video.onemptied = () => logger.debug('Video: emptied');
                this.video.oncanplay = () => logger.info('Video: canplay');
                this.video.oncanplaythrough = () => logger.info('Video: canplaythrough');
                this.video.onerror = (e) => logger.error('Video element error', {
                    code: this.video.error?.code,
                    message: this.video.error?.message
                });

                this.video.onloadedmetadata = () => {
                    // Track resolution changes
                    if (currentConfig.debug_mode_switch) {
                        const oldRes = `${this.currentScreenWidth}x${this.currentScreenHeight}`;
                        const newRes = `${this.video.videoWidth}x${this.video.videoHeight}`;
                        logger.info(`[MODE] Browser detected resolution: ${oldRes} -> ${newRes}`, {
                            width: this.video.videoWidth,
                            height: this.video.videoHeight
                        });
                    } else {
                        logger.info('Video metadata loaded', {
                            width: this.video.videoWidth,
                            height: this.video.videoHeight
                        });
                    }

                    this.updateWebRTCState('video-size', `${this.video.videoWidth} x ${this.video.videoHeight}`);

                    // Update screen dimensions and video element size
                    this.currentScreenWidth = this.video.videoWidth;
                    this.currentScreenHeight = this.video.videoHeight;
                    this.video.width = this.video.videoWidth;
                    this.video.height = this.video.videoHeight;
                    this.cachedMouseRect = null;
                };

                this.video.onloadeddata = () => {
                    logger.info('Video: loadeddata (first frame decoded)', {
                        width: this.video.videoWidth,
                        height: this.video.videoHeight,
                        readyState: this.video.readyState
                    });
                };

                this.video.onplaying = () => {
                    logger.info('Video playing');
                    this.onVideoPlaying();
                };

                this.video.onwaiting = () => {
                    logger.warn('Video waiting/buffering');
                };

                this.video.onstalled = () => {
                    logger.warn('Video stalled');
                };

                this.video.play().catch(e => {
                    logger.warn('Video play() failed', { error: e.message });
                });

                // Log video element state periodically
                setTimeout(() => {
                    const receivers = this.pc ? this.pc.getReceivers() : [];
                    const videoReceiver = receivers.find(r => r.track && r.track.kind === 'video');
                    logger.info('VIDEO STATE after 2s', {
                        readyState: this.video.readyState,
                        networkState: this.video.networkState,
                        paused: this.video.paused,
                        ended: this.video.ended,
                        videoWidth: this.video.videoWidth,
                        videoHeight: this.video.videoHeight,
                        currentTime: this.video.currentTime,
                        srcObject: this.video.srcObject ? 'set' : 'null',
                        display: this.video.style.display,
                        muted: this.video.muted,
                        autoplay: this.video.autoplay,
                        error: this.video.error ? `${this.video.error.code}: ${this.video.error.message}` : 'none',
                        receiverTrack: videoReceiver ? videoReceiver.track.readyState : 'no receiver',
                        receiverMuted: videoReceiver ? videoReceiver.track.muted : 'n/a'
                    });
                }, 2000);
                setTimeout(() => {
                    logger.info('VIDEO STATE after 5s', {
                        readyState: this.video.readyState,
                        videoWidth: this.video.videoWidth,
                        videoHeight: this.video.videoHeight,
                        currentTime: this.video.currentTime,
                        paused: this.video.paused,
                        error: this.video.error ? `${this.video.error.code}: ${this.video.error.message}` : 'none'
                    });
                }, 5000);

                // Start frame detection
                this.startFrameDetection();

            } else {
                logger.warn('No stream in track event, creating MediaStream manually');
                const stream = new MediaStream([event.track]);
                this.video.srcObject = stream;
                this.video.play().catch(e => {
                    logger.warn('Video play() failed', { error: e.message });
                });
            }
        }
    }

    startFrameDetection() {
        // Check if we're actually receiving frames
        this.frameCheckInterval = setInterval(() => {
            if (this.video.videoWidth > 0 && this.video.videoHeight > 0) {
                if (!this.firstFrameReceived) {
                    this.firstFrameReceived = true;
                    connectionSteps.setDone('frames');
                    if (debugConfig.debug_connection) {
                        logger.info('First frame received!', {
                            width: this.video.videoWidth,
                            height: this.video.videoHeight
                        });
                    }

                    // Check if video appears black
                    this.checkForBlackScreen();
                }
            }
        }, 100);
    }

    checkForBlackScreen() {
        // Create a canvas to sample pixels
        const canvas = document.createElement('canvas');
        const ctx = canvas.getContext('2d');
        canvas.width = 10;
        canvas.height = 10;

        setTimeout(() => {
            try {
                ctx.drawImage(this.video, 0, 0, 10, 10);
                const imageData = ctx.getImageData(0, 0, 10, 10);
                const data = imageData.data;

                let totalBrightness = 0;
                for (let i = 0; i < data.length; i += 4) {
                    totalBrightness += (data[i] + data[i + 1] + data[i + 2]) / 3;
                }
                const avgBrightness = totalBrightness / (data.length / 4);

                if (avgBrightness < 5) {
                    logger.warn('VIDEO APPEARS BLACK - Average brightness: ' + avgBrightness.toFixed(1));
                    logger.warn('Possible causes: encoder issue, stride mismatch, no frames from emulator');
                } else {
                    logger.info('Video brightness check passed', { avgBrightness: avgBrightness.toFixed(1) });
                }
            } catch (e) {
                logger.debug('Could not sample video pixels', { error: e.message });
            }
        }, 1000);
    }

    onVideoPlaying() {
        this.connected = true;
        this.updateStatus('Connected', 'connected');
        this.hideOverlay();
        this.updateConnectionUI(true);

        // Cancel HTTP stream fallback timer — WebRTC succeeded
        if (this.webrtcFallbackTimer) {
            clearTimeout(this.webrtcFallbackTimer);
            this.webrtcFallbackTimer = null;
        }
        // Clear any saved fallback preference since WebRTC works now
        try { localStorage.removeItem('macemu_prefer_httpstream'); } catch(e) {}

        // Remove disconnected visual state
        const displayContainer = document.getElementById('display-container');
        if (displayContainer) {
            displayContainer.classList.remove('disconnected');
        }

        logger.info('Stream is playing');
    }

    onIceCandidate(event) {
        // We now wait for ICE gathering complete and send all candidates in the answer SDP
        // So we don't need to send individual candidates via trickle ICE
        if (event.candidate) {
            logger.debug('ICE candidate gathered', { candidate: event.candidate.candidate.substring(0, 50) + '...' });
        } else {
            logger.debug('ICE gathering complete (null candidate)');
        }
    }

    onIceConnectionStateChange() {
        const state = this.pc.iceConnectionState;
        if (debugConfig.debug_connection) {
            logger.info('ICE connection state', { state });
        }
        this.updateWebRTCState('ice', state);

        if (state === 'connected' || state === 'completed') {
            connectionSteps.setDone('ice');
        } else if (state === 'failed') {
            connectionSteps.setError('ice');
            this.updateStatus('ICE connection failed', 'error');
            logger.error('ICE connection failed - stepping codec fallback chain');
            // Fast fallback: UDP/ICE will never work here. Skip the 5s no-frames
            // timer and step to the next codec. Guarded so it fires once per chain.
            if (!this._iceFailureFallbackFired) {
                this._iceFailureFallbackFired = true;
                try { localStorage.setItem('macemu_last_fallback_reason', 'ice'); } catch (e) {}
                if (!this._stepDownFallbackChain('ice-failed')) {
                    this.fallbackToHTTPStream();
                }
            }
        } else if (state === 'disconnected') {
            logger.warn('ICE disconnected - may recover');
        }
    }

    onIceGatheringStateChange() {
        const state = this.pc.iceGatheringState;
        logger.debug('ICE gathering state', { state });
        this.updateWebRTCState('ice-gathering', state);
    }

    onConnectionStateChange() {
        const state = this.pc.connectionState;
        if (debugConfig.debug_connection) {
            logger.info('Connection state', { state });
        }
        this.updateWebRTCState('pc', state);

        if (state === 'failed' || state === 'disconnected' || state === 'closed') {
            this.updateStatus('Connection ' + state, 'error');
            this.connected = false;

            // Add disconnected visual state
            const displayContainer = document.getElementById('display-container');
            if (displayContainer) {
                displayContainer.classList.add('disconnected');
            }

            // Don't auto-reconnect if we're already in a deliberate reconnect (e.g., codec change)
            if (this.isReconnecting) {
                logger.info(`Connection ${state}, but reconnect already in progress — skipping`);
                return;
            }

            // If WebSocket is still open, delay briefly to let any pending "reconnect"
            // message arrive first (server sends reconnect before closing peer)
            if (this.ws && this.ws.readyState === WebSocket.OPEN) {
                setTimeout(() => {
                    if (this.isReconnecting) {
                        logger.info(`Connection ${state}, reconnect message arrived — skipping auto-reconnect`);
                        return;
                    }
                    logger.info(`Connection ${state}, reconnecting PeerConnection via existing WebSocket`);
                    this.reconnectPeerConnection();
                }, 100);
            } else {
                logger.info(`Connection ${state}, WebSocket also closed, full reconnect needed`);
                this.scheduleReconnect();
            }
        }
    }

    // Reconnect just the PeerConnection without closing WebSocket
    reconnectPeerConnection() {
        // Suppress auto-reconnect from connection state changes during deliberate reconnect
        this.isReconnecting = true;

        // Clean up old PeerConnection
        if (this.pc) {
            this.pc.close();
            this.pc = null;
        }

        // Reset state
        this.connected = false;
        this.firstFrameReceived = false;
        connectionSteps.reset();
        connectionSteps.setDone('ws');  // WebSocket still connected

        // Reinitialize decoder (codec may have changed)
        if (!this.initDecoder()) {
            logger.error('Failed to reinitialize decoder');
            this.scheduleReconnect();
            return;
        }

        // Send new connect request on existing WebSocket (include codec so server uses it)
        const codecName = this.codecType || 'h264';
        logger.info('Sending new connect request', { codec: codecName });
        this.ws.send(JSON.stringify({ type: 'connect', codec: codecName }));
        this.updateStatus('Reconnecting...', 'connecting');

        // Allow auto-reconnect again after a brief delay (let new PC establish)
        setTimeout(() => { this.isReconnecting = false; }, 2000);
    }

    onSignalingStateChange() {
        const state = this.pc.signalingState;
        logger.debug('Signaling state', { state });
        this.updateWebRTCState('signaling', state);
    }

    setupInputHandlers() {
        // Use the appropriate display element (video for H.264/VP9, canvas for PNG/WEBP)
        const usesVideoElement = (this.codecType === CodecType.H264 || this.codecType === CodecType.VP9);
        const displayElement = usesVideoElement ? this.video : this.canvas;
        if (!displayElement) return;

        // Mouse event handlers - support both relative and absolute modes

        // Click handler - request pointer lock only in relative mode
        displayElement.addEventListener('click', () => {
            if (this.mouseMode === 'relative' && !document.pointerLockElement) {
                displayElement.requestPointerLock();
            }
        });

        // Mouse move handler - supports both modes
        const handleMouseMove = (e) => {
            if (!this.connected) return;

            if (this.mouseMode === 'relative') {
                // Relative mode: use pointer lock and send deltas
                if (document.pointerLockElement === displayElement) {
                    this.sendMouseMove(e.movementX, e.movementY, performance.now());
                }
            } else {
                // Absolute mode: calculate Mac screen coordinates from canvas position
                const pos = this.calculateAbsoluteMousePosition(e, displayElement);
                if (pos) {
                    // Debug logging (avoid object creation unless needed)
                    if (debugConfig.debug_connection) {
                        logger.info('Absolute mouse', { x: pos.x, y: pos.y, screenW: this.currentScreenWidth, screenH: this.currentScreenHeight });
                    }

                    this.sendMouseAbsolute(pos.x, pos.y, performance.now());
                }
            }
        };
        displayElement.addEventListener('mousemove', handleMouseMove);

        // Invalidate mouse cache on resize/fullscreen (for absolute mode)
        const invalidateMouseCache = () => {
            this.cachedMouseRect = null;
        };
        window.addEventListener('resize', invalidateMouseCache);
        document.addEventListener('fullscreenchange', invalidateMouseCache);

        // Mouse buttons - work in both modes
        const handleMouseDown = (e) => {
            if (!this.connected) return;

            // In relative mode, only handle if pointer is locked
            // In absolute mode, always handle
            if (this.mouseMode === 'absolute' || document.pointerLockElement === displayElement) {
                e.preventDefault();

                // In absolute mode, update position before sending click
                if (this.mouseMode === 'absolute') {
                    const pos = this.calculateAbsoluteMousePosition(e, displayElement);
                    if (pos) {
                        this.sendMouseAbsolute(pos.x, pos.y, performance.now());
                    } else {
                        return; // Dimensions not set yet
                    }
                }

                this.sendMouseButton(e.button, true, performance.now());
            }
        };

        const handleMouseUp = (e) => {
            if (!this.connected) return;

            if (this.mouseMode === 'absolute' || document.pointerLockElement === displayElement) {
                e.preventDefault();

                // In absolute mode, update position before sending click release
                if (this.mouseMode === 'absolute') {
                    const pos = this.calculateAbsoluteMousePosition(e, displayElement);
                    if (pos) {
                        this.sendMouseAbsolute(pos.x, pos.y, performance.now());
                    } else {
                        return; // Dimensions not set yet
                    }
                }

                this.sendMouseButton(e.button, false, performance.now());
            }
        };
        displayElement.addEventListener('mousedown', handleMouseDown);
        displayElement.addEventListener('mouseup', handleMouseUp);

        displayElement.addEventListener('contextmenu', (e) => e.preventDefault());

        // Keyboard — binary protocol for minimal latency. We translate
        // KeyboardEvent.code → classic Mac ADB scancode in JS (see
        // EVENT_CODE_TO_MAC at module scope) and send the Mac code
        // straight over the wire; the server just injects.
        //
        // Held-key tracking: if a keyup is missed (window blur, alt-tab,
        // browser swallowed it for a shortcut) the Mac key matrix would
        // stay stuck. We track every down and synthesize keyups on
        // blur / visibility change / disconnect.
        if (this._keydownHandler) document.removeEventListener('keydown', this._keydownHandler);
        if (this._keyupHandler)   document.removeEventListener('keyup',   this._keyupHandler);
        if (this._blurHandler)    window.removeEventListener('blur',      this._blurHandler);
        if (this._visibilityHandler) document.removeEventListener('visibilitychange', this._visibilityHandler);

        this._heldKeys = this._heldKeys || new Set();

        this._keydownHandler = (e) => {
            if (!this.connected) return;
            if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
            const mac = macKeycodeForEvent(e);
            if (mac == null) return;  // unmapped — let the browser handle it
            e.preventDefault();
            this._heldKeys.add(mac);
            this._updateHeldModsChip();
            this.sendKey(mac, true, performance.now());
        };
        this._keyupHandler = (e) => {
            if (!this.connected) return;
            if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
            const mac = macKeycodeForEvent(e);
            if (mac == null) return;
            e.preventDefault();
            this._heldKeys.delete(mac);
            this._updateHeldModsChip();
            this.sendKey(mac, false, performance.now());
        };
        this._blurHandler = () => {
            if (keyboardConfig.release_on_blur !== false) this._releaseHeldKeys();
        };
        this._visibilityHandler = () => {
            if (document.visibilityState === 'hidden' &&
                keyboardConfig.release_on_blur !== false) {
                this._releaseHeldKeys();
            }
        };

        document.addEventListener('keydown', this._keydownHandler);
        document.addEventListener('keyup',   this._keyupHandler);
        window.addEventListener('blur',      this._blurHandler);
        document.addEventListener('visibilitychange', this._visibilityHandler);

        if (debugConfig.debug_connection) {
            logger.info('Input handlers registered, element:', displayElement.tagName, 'mouseMode:', this.mouseMode);
        }
    }

    // Binary protocol helpers (matches browser input format sent to server)
    // Format: [type:1] [data...]
    // Mouse move (relative): type=1, dx:int16, dy:int16, timestamp:float64
    // Mouse button: type=2, button:uint8, down:uint8, timestamp:float64
    // Key: type=3, keycode:uint16, down:uint8, timestamp:float64
    // Mouse move (absolute): type=5, x:uint16, y:uint16, timestamp:float64

    _wsIsOpen() {
        return this.ws && this.ws.readyState === WebSocket.OPEN;
    }

    sendMouseMove(dx, dy, timestamp) {
        if (this._wsIsOpen()) {
            const buffer = new ArrayBuffer(1 + 2 + 2 + 8);
            const view = new DataView(buffer);
            view.setUint8(0, 1);
            view.setInt16(1, dx, true);
            view.setInt16(3, dy, true);
            view.setFloat64(5, timestamp, true);
            this.ws.send(buffer);
        } else if (this.codecType === CodecType.HTTP_STREAM) {
            this._httpPostThrottled('mouse', { dx, dy });
        }
    }

    sendMouseAbsolute(x, y, timestamp) {
        if (this._wsIsOpen()) {
            const buffer = new ArrayBuffer(1 + 2 + 2 + 8);
            const view = new DataView(buffer);
            view.setUint8(0, 5);
            view.setUint16(1, x, true);
            view.setUint16(3, y, true);
            view.setFloat64(5, timestamp, true);
            this.ws.send(buffer);
        } else if (this.codecType === CodecType.HTTP_STREAM) {
            this._httpPostThrottled('mouse', { x, y });
        }
    }

    sendMouseButton(button, down, timestamp) {
        if (this._wsIsOpen()) {
            const buffer = new ArrayBuffer(1 + 1 + 1 + 8);
            const view = new DataView(buffer);
            view.setUint8(0, 2);
            view.setUint8(1, button);
            view.setUint8(2, down ? 1 : 0);
            view.setFloat64(3, timestamp, true);
            this.ws.send(buffer);
        } else if (this.codecType === CodecType.HTTP_STREAM) {
            // Mouse button events are not throttled — send immediately
            fetch(getApiUrl('mouse'), {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ button, down })
            }).catch(() => {});
        }
    }

    sendKey(keycode, down, timestamp) {
        if (this._wsIsOpen()) {
            const buffer = new ArrayBuffer(1 + 2 + 1 + 8);
            const view = new DataView(buffer);
            view.setUint8(0, 3);
            view.setUint16(1, keycode, true);
            view.setUint8(3, down ? 1 : 0);
            view.setFloat64(4, timestamp, true);
            this.ws.send(buffer);
        } else if (this.codecType === CodecType.HTTP_STREAM) {
            fetch(getApiUrl('keypress'), {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ key: keycode, down })
            }).catch(() => {});
        }
    }

    // Synthesize keyup for every Mac key we currently believe is held,
    // so the guest's ADB key matrix doesn't end up stuck after a missed
    // keyup (window blur, alt-tab, browser intercepting a shortcut).
    _releaseHeldKeys() {
        if (!this._heldKeys || this._heldKeys.size === 0) return;
        const ts = performance.now();
        for (const code of this._heldKeys) {
            this.sendKey(code, false, ts);
        }
        this._heldKeys.clear();
        this._updateHeldModsChip();
    }

    // Reflect the held-Mac-modifier state into the header chip. Driven off
    // _heldKeys (Mac scancodes), so what lights up is what the guest sees.
    _updateHeldModsChip() {
        const chip = document.getElementById('held-mods');
        if (!chip) return;
        const held = this._heldKeys || new Set();
        chip.classList.toggle('has-shift',   held.has(0x38));
        chip.classList.toggle('has-control', held.has(0x36));
        chip.classList.toggle('has-option',  held.has(0x3A));
        chip.classList.toggle('has-cmd',     held.has(0x37));
    }

    // Throttled HTTP POST for mouse moves (avoid flooding the server)
    // Sends at most one request per 16ms (~60Hz)
    _httpPostThrottled(endpoint, data) {
        if (!this._httpPostPending) {
            this._httpPostPending = {};
        }
        this._httpPostPending[endpoint] = data;

        if (!this._httpPostTimer) {
            this._httpPostTimer = setTimeout(() => {
                this._httpPostTimer = null;
                const pending = this._httpPostPending;
                this._httpPostPending = {};
                for (const [ep, body] of Object.entries(pending)) {
                    fetch(getApiUrl(ep), {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify(body)
                    }).catch(() => {});
                }
            }, 16);
        }
    }

    // Send raw text message (legacy text protocol - fallback)
    sendRaw(msg) {
        if (this._wsIsOpen()) {
            this.ws.send(msg);
        }
    }

    // Legacy JSON method (kept for restart/shutdown commands)
    sendInput(msg) {
        if (this._wsIsOpen()) {
            this.ws.send(JSON.stringify(msg));
        }
    }

    // Send mouse mode change to server (type=6, mode: 0=absolute, 1=relative)
    sendMouseModeChange(mode) {
        if (!this._wsIsOpen()) return;
        const buffer = new ArrayBuffer(1 + 1);
        const view = new DataView(buffer);
        view.setUint8(0, 6);  // type: mouse mode change
        view.setUint8(1, mode === 'relative' ? 1 : 0);  // 0=absolute, 1=relative
        this.ws.send(buffer);

        if (debugConfig.debug_connection) {
            logger.info('Mouse mode change sent to server', { mode });
        }
    }

    // Calculate absolute mouse position from mouse event
    // Returns {x, y} or null if dimensions not set
    calculateAbsoluteMousePosition(e, displayElement) {
        if (this.currentScreenWidth === 0 || this.currentScreenHeight === 0) {
            logger.warn('Absolute mouse: screen dimensions not set yet');
            return null;
        }

        // Use cached rect if available, otherwise calculate it
        if (!this.cachedMouseRect) {
            this.cachedMouseRect = displayElement.getBoundingClientRect();
            this.cachedMouseScaleX = this.currentScreenWidth / this.cachedMouseRect.width;
            this.cachedMouseScaleY = this.currentScreenHeight / this.cachedMouseRect.height;
        }

        const rect = this.cachedMouseRect;
        const macX = Math.floor((e.clientX - rect.left) * this.cachedMouseScaleX);
        const macY = Math.floor((e.clientY - rect.top) * this.cachedMouseScaleY);

        // Clamp to screen bounds
        return {
            x: Math.max(0, Math.min(this.currentScreenWidth - 1, macX)),
            y: Math.max(0, Math.min(this.currentScreenHeight - 1, macY))
        };
    }

    // Handle frame metadata for H.264/VP9 (sent via data channel)
    // Format: [cursor_x:2][cursor_y:2][cursor_visible:1]
    handleFrameMetadata(view) {
        const cursorX = view.getUint16(0, true);
        const cursorY = view.getUint16(2, true);
        const cursorVisible = view.getUint8(4);

        this.currentCursorX = cursorX;
        this.currentCursorY = cursorY;
        this.cursorVisible = (cursorVisible !== 0);
    }

    // Handle cursor update message from server (type 7) - DEPRECATED, keeping for compatibility
    // Format: [type:1] [x:uint16] [y:uint16] [visible:uint8]
    handleCursorUpdate(view) {
        const x = view.getUint16(1, true);  // little-endian
        const y = view.getUint16(3, true);
        const visible = view.getUint8(5);

        this.currentCursorX = x;
        this.currentCursorY = y;
        this.cursorVisible = (visible !== 0);
        // In absolute mode, we use the browser's native cursor, not the overlay
    }

    // Render cursor on overlay canvas
    renderCursor() {
        if (!this.cursorCtx || !this.cursorOverlay) return;

        // Get display element dimensions for scaling
        const usesVideoElement = (this.codecType === CodecType.H264 || this.codecType === CodecType.AV1 || this.codecType === CodecType.VP9);
        const displayElement = usesVideoElement ? this.video : this.canvas;
        if (!displayElement) return;

        // Update overlay canvas size to match display
        const rect = displayElement.getBoundingClientRect();
        if (this.cursorOverlay.width !== rect.width || this.cursorOverlay.height !== rect.height) {
            this.cursorOverlay.width = rect.width;
            this.cursorOverlay.height = rect.height;
            this.cursorOverlay.style.width = rect.width + 'px';
            this.cursorOverlay.style.height = rect.height + 'px';
            this.cursorOverlay.style.display = 'block';
        }

        // Clear canvas
        this.cursorCtx.clearRect(0, 0, this.cursorOverlay.width, this.cursorOverlay.height);

        if (!this.cursorVisible || this.currentScreenWidth === 0) return;

        // Scale cursor position from Mac screen coords to display coords
        const scaleX = rect.width / this.currentScreenWidth;
        const scaleY = rect.height / this.currentScreenHeight;
        const displayX = this.currentCursorX * scaleX;
        const displayY = this.currentCursorY * scaleY;

        // Draw a simple cursor (white arrow with black outline)
        this.cursorCtx.save();
        this.cursorCtx.translate(displayX, displayY);

        // Black outline
        this.cursorCtx.fillStyle = 'black';
        this.cursorCtx.beginPath();
        this.cursorCtx.moveTo(0, 0);
        this.cursorCtx.lineTo(0, 20);
        this.cursorCtx.lineTo(5, 15);
        this.cursorCtx.lineTo(9, 23);
        this.cursorCtx.lineTo(12, 21);
        this.cursorCtx.lineTo(8, 13);
        this.cursorCtx.lineTo(14, 13);
        this.cursorCtx.closePath();
        this.cursorCtx.fill();

        // White fill (slightly smaller)
        this.cursorCtx.fillStyle = 'white';
        this.cursorCtx.beginPath();
        this.cursorCtx.moveTo(1, 1);
        this.cursorCtx.lineTo(1, 18);
        this.cursorCtx.lineTo(5, 14);
        this.cursorCtx.lineTo(8, 21);
        this.cursorCtx.lineTo(10, 20);
        this.cursorCtx.lineTo(7, 13);
        this.cursorCtx.lineTo(13, 13);
        this.cursorCtx.closePath();
        this.cursorCtx.fill();

        this.cursorCtx.restore();
    }

    scheduleReconnect() {
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
        }

        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            logger.error('Max reconnection attempts reached');
            this.updateStatus('Connection failed - click Connect to retry', 'error');
            return;
        }

        this.reconnectAttempts++;
        const delay = Math.min(CONSTANTS.BASE_RECONNECT_DELAY_MS * Math.pow(2, this.reconnectAttempts - 1), CONSTANTS.MAX_RECONNECT_DELAY_MS);
        logger.info(`Reconnecting in ${delay / CONSTANTS.MS_PER_SECOND}s (attempt ${this.reconnectAttempts}/${this.maxReconnectAttempts})`);
        this.updateOverlayStatus(`Reconnecting in ${Math.round(delay / CONSTANTS.MS_PER_SECOND)}s...`);

        this.reconnectTimer = setTimeout(() => {
            if (!this.connected) {
                this._connect();
            }
        }, delay);
    }

    cleanup() {
        if (this.webrtcFallbackTimer) {
            clearTimeout(this.webrtcFallbackTimer);
            this.webrtcFallbackTimer = null;
        }
        if (this.frameCheckInterval) {
            clearInterval(this.frameCheckInterval);
            this.frameCheckInterval = null;
        }
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
        if (this.decoder) {
            this.decoder.cleanup();
            this.decoder = null;
        }
        if (this.pc) {
            this.pc.close();
            this.pc = null;
        }
        this.videoTrack = null;
        this.firstFrameReceived = false;
    }

    disconnect() {
        logger.info('Disconnecting');
        this.reconnectAttempts = this.maxReconnectAttempts; // Prevent auto-reconnect
        this.cleanup();
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        this.connected = false;
        this.updateStatus('Disconnected', 'error');
        this.updateConnectionUI(false);
        this.showOverlay('Disconnected', 'Click Connect to reconnect');

        // Disable codec selector when disconnected
        const codecSelect = document.getElementById('codec-select');
        if (codecSelect) {
            codecSelect.disabled = true;
        }

        // Add disconnected visual state
        const displayContainer = document.getElementById('display-container');
        if (displayContainer) {
            displayContainer.classList.add('disconnected');
        }
    }

    // Stats collection
    async updateStats() {
        if (!this.connected) return;

        const now = performance.now();
        const elapsed = (now - this.lastStatsTime) / CONSTANTS.MS_PER_SECOND;

        // For non-RTP codecs (PNG, WebP, HTTP Stream), calculate stats from our own tracking
        const usesVideoTrack = (this.codecType === CodecType.H264 || this.codecType === CodecType.AV1 || this.codecType === CodecType.VP9);
        if (!usesVideoTrack) {
            // For HTTP stream, pull stats from the decoder
            if (this.codecType === CodecType.HTTP_STREAM && this.decoder) {
                this.pngStats.framesReceived = this.decoder.state.frameCount;
                this.pngStats.bytesReceived = this.decoder.bytesReceived;
                this.pngStats.avgFrameSize = this.pngStats.framesReceived > 0 ?
                    this.pngStats.bytesReceived / this.pngStats.framesReceived : 0;
            }
            if (elapsed > 0) {
                const framesDelta = this.pngStats.framesReceived - this.lastPngFrameCount;
                const bytesDelta = this.pngStats.bytesReceived - this.lastPngBytesReceived;

                this.stats.fps = Math.round(framesDelta / elapsed);
                this.stats.bitrate = Math.round((bytesDelta * CONSTANTS.BITS_PER_BYTE / elapsed) / CONSTANTS.BITS_TO_KILOBITS);
                this.stats.framesDecoded = this.pngStats.framesReceived;
                this.stats.packetsLost = 0;  // DataChannel is reliable
                this.stats.jitter = 0;
                this.stats.codec = this.codecType;

                // Log detailed stats every 3 seconds
                if (debugConfig.debug_perf && (!this.lastDetailedStatsTime || (now - this.lastDetailedStatsTime) > 3000)) {
                    const avgFrameKB = Math.round(this.pngStats.avgFrameSize / 1024);
                    const totalMB = (this.pngStats.bytesReceived / (1024 * 1024)).toFixed(1);
                    logger.info(`fps=${this.stats.fps} | video: frames=${this.pngStats.framesReceived} recv=${totalMB}MB avg=${avgFrameKB}KB | bitrate: ${this.stats.bitrate}kbps`);
                    this.lastDetailedStatsTime = now;
                }

                this.lastPngFrameCount = this.pngStats.framesReceived;
                this.lastPngBytesReceived = this.pngStats.bytesReceived;
            }

            this.lastStatsTime = now;
            this.updateStatsDisplay();
            return;
        }

        // For H.264, use WebRTC stats
        if (!this.pc) return;

        try {
            const stats = await this.pc.getStats();
            let candidateRtt = 0;

            stats.forEach(report => {
                // Extract RTT from candidate-pair stats
                if (report.type === 'candidate-pair' && report.state === 'succeeded') {
                    candidateRtt = report.currentRoundTripTime || 0;
                }

                if (report.type === 'inbound-rtp' && report.kind === 'video') {
                    const bytesReceived = report.bytesReceived || 0;
                    const framesDecoded = report.framesDecoded || 0;
                    const packetsLost = report.packetsLost || 0;
                    const packetsReceived = report.packetsReceived || 0;
                    const framesDropped = report.framesDropped || 0;
                    const framesReceived = report.framesReceived || 0;
                    const keyFramesDecoded = report.keyFramesDecoded || 0;
                    const totalDecodeTime = report.totalDecodeTime || 0;
                    const jitter = report.jitter || 0;

                    if (elapsed > 0) {
                        this.stats.fps = Math.round((framesDecoded - this.lastFramesDecoded) / elapsed);
                        const bps = (bytesReceived - this.lastBytesReceived) * 8 / elapsed;
                        this.stats.bitrate = Math.round(bps / 1000);
                    }

                    this.stats.framesDecoded = framesDecoded;
                    this.stats.packetsLost = packetsLost;
                    this.stats.packetsReceived = packetsReceived;
                    this.stats.framesDropped = framesDropped;
                    this.stats.framesReceived = framesReceived;
                    this.stats.keyFramesDecoded = keyFramesDecoded;
                    this.stats.jitter = Math.round(jitter * 1000);

                    // Feed decode latency and RTT to the decoder for stats display
                    if (this.decoder && this.decoder.updateRtpLatency) {
                        this.decoder.updateRtpLatency(totalDecodeTime, framesDecoded, candidateRtt);
                    }

                    // Log detailed stats every 3 seconds
                    if (!this.lastDetailedStatsTime || (now - this.lastDetailedStatsTime) > 3000) {
                        logger.info('RTP stats', {
                            packetsRecv: packetsReceived,
                            packetsLost: packetsLost,
                            bytesRecv: bytesReceived,
                            framesRecv: framesReceived,
                            framesDecoded: framesDecoded,
                            framesDropped: framesDropped,
                            keyFrames: keyFramesDecoded,
                            decodeTime: totalDecodeTime.toFixed(2) + 's'
                        });
                        this.lastDetailedStatsTime = now;
                    }

                    this.lastBytesReceived = bytesReceived;
                    this.lastFramesDecoded = framesDecoded;
                }
            });

            this.lastStatsTime = now;
            this.updateStatsDisplay();

        } catch (e) {
            logger.debug('Stats error', { error: e.message });
        }
    }

    updateStatsDisplay() {
        // Header stats
        const fpsEl = document.getElementById('fps-display');
        const bitrateEl = document.getElementById('bitrate-display');
        if (fpsEl) fpsEl.querySelector('span:last-child').textContent = `${this.stats.fps}`;
        if (bitrateEl) bitrateEl.querySelector('span:last-child').textContent = `${this.stats.bitrate} kbps`;

        // Get resolution from appropriate element
        let width = 0, height = 0;
        const usesVideoElement = (this.codecType === CodecType.H264 || this.codecType === CodecType.AV1 || this.codecType === CodecType.VP9);
        if (usesVideoElement && this.video) {
            width = this.video.videoWidth;
            height = this.video.videoHeight;
        } else if (this.canvas) {
            width = this.canvas.width;
            height = this.canvas.height;
        }

        // Only update resolution display if it changed (avoid unnecessary DOM updates)
        if (width !== this.cachedWidth || height !== this.cachedHeight) {
            this.cachedWidth = width;
            this.cachedHeight = height;


            // Footer resolution
            const resEl = document.getElementById('resolution');
            if (resEl && width) {
                resEl.textContent = `${width} x ${height} (${getCodecLabel(this.codecType)})`;
            }

            // Header resolution
            const headerResEl = document.getElementById('header-resolution');
            if (headerResEl) {
                if (width && height) {
                    headerResEl.textContent = `${width} x ${height}`;
                } else {
                    headerResEl.textContent = '-- x --';
                }
            }
        }

        // Debug panel stats
        const statFps = document.getElementById('stat-fps');
        const statBitrate = document.getElementById('stat-bitrate');
        const statRes = document.getElementById('stat-resolution');
        const statFrames = document.getElementById('stat-frames');
        const statLost = document.getElementById('stat-packets-lost');
        const statJitter = document.getElementById('stat-jitter');

        if (statFps) {
            statFps.textContent = this.stats.fps;
            statFps.className = 'value ' + (this.stats.fps >= 25 ? 'good' : this.stats.fps >= 15 ? 'warn' : 'bad');
        }
        if (statBitrate) statBitrate.textContent = `${this.stats.bitrate} kbps`;
        if (statRes && width) {
            statRes.textContent = `${width} x ${height}`;
        }
        if (statFrames) statFrames.textContent = this.stats.framesDecoded.toLocaleString();

        // Packets Lost and Jitter only apply to RTP (H.264/AV1/VP9)
        const usesRTP = (this.codecType === CodecType.H264 || this.codecType === CodecType.AV1 || this.codecType === CodecType.VP9);
        if (statLost) {
            if (usesRTP) {
                statLost.textContent = this.stats.packetsLost;
                statLost.className = 'value ' + (this.stats.packetsLost === 0 ? 'good' : 'bad');
            } else {
                statLost.textContent = 'N/A';
                statLost.className = 'value';
            }
        }
        if (statJitter) {
            if (usesRTP) {
                statJitter.textContent = `${this.stats.jitter} ms`;
            } else {
                statJitter.textContent = 'N/A';
            }
        }

    }

    // UI helpers
    updateStatus(text, type = '') {
        const iconEl = document.getElementById('connection-icon');

        if (iconEl) {
            iconEl.className = '';
            if (type === 'connected') {
                iconEl.classList.remove('inactive', 'connecting');
            } else if (type === 'connecting') {
                iconEl.classList.add('connecting');
                iconEl.classList.remove('inactive');
            } else {
                iconEl.classList.add('inactive');
                iconEl.classList.remove('connecting');
            }
        }
    }

    updateOverlayStatus(text) {
        const el = document.getElementById('overlay-status');
        if (el) el.textContent = text;
    }

    showOverlay(title, status) {
        const overlay = document.getElementById('overlay');
        const titleEl = document.getElementById('overlay-title');
        const statusEl = document.getElementById('overlay-status');

        if (overlay) overlay.classList.remove('hidden');
        if (titleEl) titleEl.textContent = title || 'Connecting to Basilisk II';
        if (statusEl) statusEl.textContent = status || 'Initializing...';
    }

    hideOverlay() {
        const overlay = document.getElementById('overlay');
        if (overlay) overlay.classList.add('hidden');
    }

    updateConnectionUI(connected) {
        const btn = document.getElementById('connect-btn');
        if (btn) {
            btn.textContent = connected ? 'Disconnect' : 'Connect';
            btn.classList.toggle('primary', !connected);
        }
    }

    updateWebRTCState(key, value) {
        const stateMap = {
            'ws': 'ws-state',
            'pc': 'pc-state',
            'ice': 'ice-state',
            'ice-gathering': 'ice-gathering-state',
            'signaling': 'signaling-state',
            'dc': 'dc-state',
            'track-state': 'track-state',
            'track-enabled': 'track-enabled',
            'track-muted': 'track-muted',
            'video-size': 'video-size',
            'audio-track-state': 'audio-track-state',
            'audio-track-enabled': 'audio-track-enabled',
            'audio-track-muted': 'audio-track-muted',
            'audio-format': 'audio-format'
        };

        const elId = stateMap[key];
        if (!elId) return;

        const el = document.getElementById(elId);
        if (!el) return;

        el.textContent = value;

        // Color coding
        el.className = 'value';
        const goodStates = ['connected', 'complete', 'completed', 'stable', 'Open', 'open', 'Yes', 'live'];
        const badStates = ['failed', 'closed', 'Closed', 'Error', 'disconnected', 'ended'];
        const connectingStates = ['connecting', 'checking', 'new', 'gathering'];

        const lowerValue = value.toLowerCase();
        if (goodStates.some(s => lowerValue.includes(s.toLowerCase()))) {
            el.classList.add('good');
        } else if (badStates.some(s => lowerValue.includes(s.toLowerCase()))) {
            el.classList.add('bad');
        } else if (connectingStates.some(s => lowerValue.includes(s.toLowerCase()))) {
            el.classList.add('connecting');
        }
    }

    updateSdpInfo(sdp) {
        const el = document.getElementById('sdp-info');
        if (!el) return;

        // Extract key info from SDP
        const lines = sdp.split('\n');
        const info = [];

        lines.forEach(line => {
            if (line.startsWith('m=video')) info.push(line);
            if (line.startsWith('a=rtpmap')) info.push(line);
            if (line.startsWith('a=fmtp')) info.push(line.substring(0, 80) + (line.length > 80 ? '...' : ''));
        });

        el.textContent = info.join('\n') || 'No video media found in SDP';
    }

    // Synchronized audio capture (triggered by server when user presses 'C')
    startAudioCapture() {
        const SAMPLE_RATE = 48000;
        const CAPTURE_SAMPLES = SAMPLE_RATE * CONSTANTS.AUDIO_CAPTURE_DURATION_SEC * CONSTANTS.AUDIO_CHANNELS;

        if (this.audioCapturing) {
            logger.warn('[AudioCapture] Already capturing!');
            return;
        }

        const audioElement = document.getElementById('macemu-audio');
        if (!audioElement || !audioElement.srcObject) {
            logger.error('[AudioCapture] No audio element or stream found!');
            return;
        }

        logger.info('[AudioCapture] ========================================');
        logger.info('[AudioCapture] STARTING SYNCHRONIZED CAPTURE');
        logger.info('[AudioCapture] ========================================');
        logger.info(`[AudioCapture] Capturing ${CONSTANTS.AUDIO_CAPTURE_DURATION_SEC} seconds of audio...`);
        logger.info('[AudioCapture] ========================================');

        this.audioCapturing = true;

        try {
            const captureContext = new AudioContext({ sampleRate: SAMPLE_RATE });
            const source = captureContext.createMediaStreamSource(audioElement.srcObject);
            const captureProcessor = captureContext.createScriptProcessor(CONSTANTS.AUDIO_BUFFER_SIZE, CONSTANTS.AUDIO_CHANNELS, CONSTANTS.AUDIO_CHANNELS);

            let capturedSamples = new Int16Array(CAPTURE_SAMPLES);
            let sampleOffset = 0;
            const startTime = performance.now();

            captureProcessor.onaudioprocess = (e) => {
                const elapsed = (performance.now() - startTime) / CONSTANTS.MS_PER_SECOND;

                // Stop after capture duration
                if (elapsed >= CONSTANTS.AUDIO_CAPTURE_DURATION_SEC) {
                    captureProcessor.disconnect();
                    source.disconnect();
                    this.audioCapturing = false;

                    logger.info('[AudioCapture] ========================================');
                    logger.info('[AudioCapture] CAPTURE COMPLETE');
                    logger.info('[AudioCapture] ========================================');

                    // Trim to actual captured length
                    const finalSamples = capturedSamples.slice(0, sampleOffset);

                    // Create WAV file
                    const wav = this.createWAV(finalSamples, SAMPLE_RATE, 2);
                    const blob = new Blob([wav], { type: 'audio/wav' });
                    const url = URL.createObjectURL(blob);
                    const a = document.createElement('a');
                    a.href = url;
                    a.download = 'firefox-audio-synchronized.wav';
                    document.body.appendChild(a);
                    a.click();
                    document.body.removeChild(a);
                    URL.revokeObjectURL(url);

                    const durationSec = (finalSamples.length / 2 / SAMPLE_RATE).toFixed(1);
                    const sizeMB = (wav.byteLength / 1024 / 1024).toFixed(2);
                    logger.info(`[AudioCapture] Saved: firefox-audio-synchronized.wav`);
                    logger.info(`[AudioCapture] Duration: ${durationSec}s, Size: ${sizeMB}MB`);
                    logger.info(`[AudioCapture] Format: 48kHz, 16-bit, stereo, PCM`);
                    logger.info('[AudioCapture] ========================================');
                    return;
                }

                // Get stereo PCM data
                const left = e.inputBuffer.getChannelData(0);
                const right = e.inputBuffer.getChannelData(1);

                // Convert float32 to int16 stereo interleaved and append
                for (let i = 0; i < left.length && sampleOffset < CAPTURE_SAMPLES; i++) {
                    capturedSamples[sampleOffset++] = Math.max(-32768, Math.min(32767, left[i] * 32768));
                    capturedSamples[sampleOffset++] = Math.max(-32768, Math.min(32767, right[i] * 32768));
                }

                // Progress update every second
                if (Math.floor(elapsed) !== Math.floor(elapsed - 0.1)) {
                    logger.info(`[AudioCapture] ${elapsed.toFixed(1)}s / ${CAPTURE_DURATION}s`);
                }
            };

            source.connect(captureProcessor);
            captureProcessor.connect(captureContext.destination);

        } catch (e) {
            logger.error('[AudioCapture] Failed:', { error: e.message });
            this.audioCapturing = false;
        }
    }

    // Helper: Create WAV file
    createWAV(samples, sampleRate, numChannels) {
        const bytesPerSample = 2;
        const blockAlign = numChannels * bytesPerSample;
        const byteRate = sampleRate * blockAlign;
        const dataSize = samples.length * bytesPerSample;

        const buffer = new ArrayBuffer(44 + dataSize);
        const view = new DataView(buffer);

        // Helper to write string
        const writeString = (offset, string) => {
            for (let i = 0; i < string.length; i++) {
                view.setUint8(offset + i, string.charCodeAt(i));
            }
        };

        // RIFF header
        writeString(0, 'RIFF');
        view.setUint32(4, 36 + dataSize, true);
        writeString(8, 'WAVE');

        // fmt chunk
        writeString(12, 'fmt ');
        view.setUint32(16, 16, true);
        view.setUint16(20, 1, true);
        view.setUint16(22, numChannels, true);
        view.setUint32(24, sampleRate, true);
        view.setUint32(28, byteRate, true);
        view.setUint16(32, blockAlign, true);
        view.setUint16(34, 16, true);

        // data chunk
        writeString(36, 'data');
        view.setUint32(40, dataSize, true);

        // PCM data
        const offset = 44;
        for (let i = 0; i < samples.length; i++) {
            view.setInt16(offset + i * 2, samples[i], true);
        }

        return buffer;
    }
}

// ============================================================================
// Application State - Encapsulated Global Object
// ============================================================================
const App = {
    client: null,
    statsInterval: null,
    savedPresets: {},  // name → config snapshot (persisted in JSON "configs" block)
    currentConfig: {
        emulator: 'quadra',  // Default, will be overwritten by loadCurrentConfig()
        rom: '',
        disks: [],
        ram: 32,
        screen: '800x600',
        cpu: 4,
        model: 14,
        fpu: true,
        jit: true,
        sound: true
    },
    serverPaths: {
        romsPath: 'storage/roms',
        imagesPath: 'storage/images'
    },
    storageCache: null
};

// Legacy global references for backwards compatibility
Object.defineProperty(window, 'client', {
    get() { return App.client; },
    set(value) { App.client = value; }
});
Object.defineProperty(window, 'statsInterval', {
    get() { return App.statsInterval; },
    set(value) { App.statsInterval = value; }
});
Object.defineProperty(window, 'currentConfig', {
    get() { return App.currentConfig; },
    set(value) { App.currentConfig = value; }
});
Object.defineProperty(window, 'serverPaths', {
    get() { return App.serverPaths; },
    set(value) { App.serverPaths = value; }
});
Object.defineProperty(window, 'storageCache', {
    get() { return App.storageCache; },
    set(value) { App.storageCache = value; }
});

// Get base path from current page location (for reverse proxy support)
// e.g., /macemu/ from /macemu/index.html, or empty string for root
function getBasePath() {
    const pathParts = window.location.pathname.split('/');
    pathParts.pop(); // Remove filename
    const basePath = pathParts.join('/');
    return basePath ? basePath + '/' : '';
}

// Build API URL relative to current page location
function getApiUrl(endpoint) {
    return `${getBasePath()}api/${endpoint}`;
}

// Build WebSocket URL for signaling server.
// Signaling rides the same origin as the page — the server hosts /ws via an
// in-process upgrade on the HTTP port. Overrides:
//   - URL param: ?ws=wss://example.com/path
//   - <meta name="ws-url" content="wss://example.com/path">
function getWebSocketUrl() {
    const urlParams = new URLSearchParams(window.location.search);
    const wsParam = urlParams.get('ws');
    if (wsParam) return wsParam;

    const wsMeta = document.querySelector('meta[name="ws-url"]');
    if (wsMeta?.content) return wsMeta.content;

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    return `${protocol}//${window.location.host}/ws`;
}

function initClient() {
    logger.init();
    logger.info('Basilisk II WebRTC Client initialized');

    const video = document.getElementById('display');
    const canvas = document.getElementById('display-canvas');
    if (!video) {
        logger.error('No video element found');
        return;
    }

    client = new BasiliskWebRTC(video, canvas);

    // Pick the best codec the server claims to support: vp9 → h264 → webp → png → httpstream.
    // Falls back through the chain at runtime if WebRTC negotiation fails or no frames arrive.
    const chain = buildCodecFallbackChain();
    const initialCodec = chain[0] || serverUIConfig.webcodec || 'png';
    logger.info('Initial codec from auto-fallback chain', { chain, picked: initialCodec });
    client.codecType = parseCodecString(initialCodec);
    const codecSelect = document.getElementById('codec-select');
    if (codecSelect) codecSelect.value = initialCodec;

    // Apply saved mouse mode from config
    if (serverUIConfig.mousemode) {
        client.mouseMode = serverUIConfig.mousemode;
    }

    // Start stats collection
    statsInterval = setInterval(() => {
        if (client) client.updateStats();
    }, CONSTANTS.STATS_UPDATE_INTERVAL_MS);

    // Set initial disconnected visual state
    const displayContainer = document.getElementById('display-container');
    if (displayContainer) {
        displayContainer.classList.add('disconnected');
    }

    // Auto-connect
    const wsUrl = getWebSocketUrl();
    logger.info('Auto-connecting', { url: wsUrl });
    client.connect(wsUrl);
}

function toggleConnection() {
    if (!client) {
        initClient();
        return;
    }

    if (client.connected) {
        client.disconnect();
    } else {
        client.reconnectAttempts = 0;
        const wsUrl = getWebSocketUrl();
        client.connect(wsUrl);
    }
}

function toggleFullscreen() {
    const container = document.getElementById('display-container') || document.body;

    if (document.fullscreenElement) {
        document.exitFullscreen();
    } else {
        container.requestFullscreen().catch(e => {
            logger.warn('Fullscreen failed', { error: e.message });
        });
    }
}

function toggleDebugPanel() {
    const panel = document.getElementById('debug-panel');
    const btn = document.getElementById('debug-toggle');

    if (panel) {
        panel.classList.toggle('collapsed');
        if (btn) btn.classList.toggle('active', !panel.classList.contains('collapsed'));
    }
}

function showDebugTab(tabName) {
    // Update tab buttons
    document.querySelectorAll('.debug-tab').forEach(tab => {
        tab.classList.toggle('active', tab.textContent.toLowerCase() === tabName);
    });

    // Update panes
    document.querySelectorAll('.debug-pane').forEach(pane => {
        pane.classList.toggle('active', pane.id === `${tabName}-pane`);
    });
}

function clearLog() {
    logger.clear();
}

async function changeMouseMode() {
    const select = document.getElementById('mouse-mode-select');
    if (!select || !client) return;

    const newMode = select.value;  // 'absolute' or 'relative'
    client.mouseMode = newMode;

    // Release pointer lock if switching from relative to absolute
    if (newMode === 'absolute' && document.pointerLockElement) {
        document.exitPointerLock();
    }

    logger.info('Mouse mode changed', { mode: newMode });

    // Send mode change notification to server/emulator
    client.sendMouseModeChange(newMode);

    // Save to config file
    try {
        const response = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mousemode: newMode })
        });
        const result = await response.json();
        if (result.success) {
            logger.info('Mouse mode saved to config', { mode: newMode });
        } else {
            logger.warn('Failed to save mouse mode to config', { error: result.error });
        }
    } catch (e) {
        logger.warn('Error saving mouse mode to config', { error: e.message });
    }
}

// Known ROM database — loaded from rom_database.json
let ROM_DATABASE = {};

// Load ROM database from external JSON file
async function loadRomDatabase() {
    try {
        const resp = await fetch('rom_database.json');
        if (resp.ok) {
            ROM_DATABASE = await resp.json();
        } else {
            logger.warn('Failed to load ROM database', { status: resp.status });
        }
    } catch (e) {
        logger.warn('Error loading ROM database', { error: e.message });
    }
}

function getRomInfo(checksum, md5) {
    // Try MD5 first (newer, more accurate)
    if (md5 && ROM_DATABASE[md5]) {
        return ROM_DATABASE[md5];
    }
    // Fall back to checksum (older ROMs)
    if (checksum && ROM_DATABASE[checksum]) {
        return ROM_DATABASE[checksum];
    }
    return null;
}

// Mode helpers: map emulator mode to architecture
// "Emulator" dropdown is a UX persona picker; it suggests sensible defaults
// when changed but is not part of the wire format. Architecture is derived
// from the chosen backend.
function isM68kMode(mode) {
    return mode === 'quadra' || mode === 'se';
}

function backendIsPpc(backend) {
    return backend === 'kpx' || backend === 'unicorn-ppc';
}

function defaultBackendForMode(mode) {
    if (mode === 'ppc') return 'kpx';
    return 'uae';  // quadra, se → uae (m68k)
}

// Update header title with current model name
function updateHeaderTitle() {
    const titleEl = document.getElementById('emulator-title');
    if (!titleEl) return;

    // Get current ROM and look up its info
    if (!currentConfig.rom || !storageCache?.roms) {
        titleEl.textContent = 'Macintosh';
        return;
    }

    const rom = storageCache.roms.find(r => r.name === currentConfig.rom);
    if (!rom) {
        titleEl.textContent = 'Macintosh';
        return;
    }

    const info = getRomInfo(rom.checksum, rom.md5);
    if (info?.name) {
        titleEl.textContent = info.name;
    } else {
        titleEl.textContent = 'Macintosh';
    }
}

const PHASE_COLOR_CLASS = {
    'pre-reset':   'phase-red',
    'ROM init':    'phase-red',
    'boot globs':  'phase-orange',
    'drivers':     'phase-orange',
    'warm start':  'phase-orange',
    'boot blocks': 'phase-yellow',
    'extensions':  'phase-yellow',
    'Finder':      'phase-finder',
    'desktop':     'phase-desktop',
};

let _lastHeaderPhaseClass = null;
function updateHeaderPhaseColor(phase) {
    // Boot progress is now shown by a small pill chip rather than tinting
    // the whole header — header stays a static dark color, only the chip
    // takes on the phase-* tint. The chip's text label is the phase name.
    const chip = document.getElementById('boot-phase');
    if (!chip) return;
    const cls = phase == null ? 'phase-off' : (PHASE_COLOR_CLASS[phase] || 'phase-off');
    if (cls === _lastHeaderPhaseClass) return;
    _lastHeaderPhaseClass = cls;
    chip.classList.remove('phase-off', 'phase-red', 'phase-orange', 'phase-yellow', 'phase-finder', 'phase-desktop');
    chip.classList.add(cls);
    chip.textContent = phase || 'offline';
}

// Handle fullscreen changes
document.addEventListener('fullscreenchange', () => {
    document.body.classList.toggle('fullscreen', !!document.fullscreenElement);
});

// ============================================================================
// Prefs File Handling
// ============================================================================

// ============================================================================
// Configuration Modal
// ============================================================================

async function loadStorage() {
    if (storageCache) return storageCache;
    try {
        const res = await fetch(getApiUrl('storage'));
        storageCache = await res.json();
        return storageCache;
    } catch (e) {
        logger.error('Failed to load storage', { error: e.message });
        return null;
    }
}

function setConfigControlsEnabled(enabled) {
    const modal = document.getElementById('config-modal');
    if (!modal) return;
    modal.querySelectorAll('select, input, button.success').forEach(el => {
        el.disabled = !enabled;
    });
}

// ============================================================================
// Config Presets
// ============================================================================

function renderPresetTabs() {
    const container = document.getElementById('config-tabs');
    if (!container) return;

    container.innerHTML = '';
    const currentTab = document.createElement('div');
    currentTab.className = 'config-tab active';
    currentTab.dataset.preset = '__current__';
    currentTab.textContent = 'Current';
    currentTab.addEventListener('click', async () => {
        await loadCurrentConfig();
        await Promise.all([loadRomList(), loadDiskList(), loadCdromList(), loadExtfsList()]);
        updateConfigUI();
        document.querySelectorAll('#config-tabs .config-tab').forEach(t => {
            t.classList.toggle('active', t.dataset.preset === '__current__');
        });
    });
    container.appendChild(currentTab);

    for (const name of Object.keys(App.savedPresets)) {
        const tab = document.createElement('div');
        tab.className = 'config-tab';
        tab.dataset.preset = name;
        tab.textContent = name;

        const del = document.createElement('span');
        del.className = 'preset-delete';
        del.textContent = '\u00d7';
        del.title = 'Delete preset';
        del.addEventListener('click', (e) => {
            e.stopPropagation();
            deletePreset(name);
        });
        tab.appendChild(del);

        tab.addEventListener('click', () => loadPreset(name));
        container.appendChild(tab);
    }
}

function loadPreset(name) {
    const preset = App.savedPresets[name];
    if (!preset) return;
    currentConfig = configFromServerJson(preset);
    currentConfig.emulator = preset.emulator || name || guessEmulatorMode(currentConfig.backend);

    Promise.all([loadRomList(), loadDiskList(), loadCdromList(), loadExtfsList()]).then(() => {
        updateConfigUI();
    });

    document.querySelectorAll('#config-tabs .config-tab').forEach(t => {
        t.classList.toggle('active', t.dataset.preset === name);
    });
}

// Pick a sensible "Emulator" dropdown value from a backend. Used when loading
// a preset that doesn't carry an explicit emulator persona.
function guessEmulatorMode(backend) {
    return backendIsPpc(backend) ? 'ppc' : 'quadra';
}

// Convert the flat server schema to the client's `currentConfig` shape.
// Shared by loadCurrentConfig and loadPreset.
function configFromServerJson(cfg) {
    const stripPrefix = (p, dir) => {
        if (!p || p[0] !== '/') return p;
        const idx = p.indexOf(dir);
        return idx >= 0 ? p.substring(idx + dir.length) : p;
    };

    const backend = cfg.backend || 'uae';

    return {
        emulator: guessEmulatorMode(backend),
        rom: cfg.rom ? stripPrefix(cfg.rom, '/roms/') : '',
        ram: cfg.ram_mb || 64,
        screen: cfg.screen || '640x480',
        sound: cfg.audio ?? true,
        bootdriver: cfg.bootdriver || 0,
        disks: (cfg.disks || []).map(p => stripPrefix(p, '/images/')),
        cdroms: (cfg.cdroms || []).map(p => stripPrefix(p, '/images/')),
        extfs: cfg.extfs || [],
        backend,
        jit: cfg.jit ?? false,
        jit68k: cfg.jit68k ?? true,
        idlewait: cfg.idlewait ?? true,
        zappram: cfg.zappram ?? false,
        dismiss_shutdown_dialog: cfg.dismiss_shutdown_dialog ?? true,
        bridge_enabled: cfg.bridge_enabled ?? false,
        network: cfg.network || 'none',
        network_if: cfg.network_if || '',
        mitm_tls: cfg.mitm_tls ?? false,
        mitm_ports: cfg.mitm_ports || '',
        mitm_ca_dir: cfg.mitm_ca_dir || '',
        // Keyboard remap (nested in JSON for grouping). Defaults match the
        // PC-shortcut habit: Ctrl→⌘, Alt→⌥, Win→⌃.
        // Pass keyboard fields through verbatim — applyKeyboardConfig() resolves
        // empties to platform defaults at the JS layer.
        keyboard: cfg.keyboard || {},
    };
}

function buildConfigJson() {
    // Build the flat server-format JSON from current form values.
    const backend = document.getElementById('cfg-backend')?.value
                    || defaultBackendForMode(document.getElementById('cfg-emulator')?.value);

    const romDropdown = document.getElementById('cfg-rom');
    const rom = (romDropdown && romDropdown.value) ? romDropdown.value : currentConfig.rom;

    // Resolve "Boot From": a "disk:NAME" selection moves that disk to the front
    // of the list (the Mac ROM boots the first drive); everything else is a
    // numeric bootdriver value passed through as-is.
    const bootRaw = document.getElementById('cfg-bootdriver')?.value || '0';
    let disksOut = (currentConfig.disks || []).slice();
    let bootdriver = 0;
    if (bootRaw.startsWith('disk:')) {
        const pick = bootRaw.slice(5);
        const idx = disksOut.indexOf(pick);
        if (idx > 0) {
            disksOut.splice(idx, 1);
            disksOut.unshift(pick);
        }
    } else {
        bootdriver = parseInt(bootRaw, 10) || 0;
    }

    return {
        backend,
        jit:      document.getElementById('cfg-jit')?.checked ?? false,
        jit68k:   document.getElementById('cfg-jit68k')?.checked ?? true,
        idlewait: document.getElementById('cfg-idlewait')?.checked ?? true,
        rom,
        disks: disksOut,
        cdroms: currentConfig.cdroms || [],
        extfs: currentConfig.extfs || [],
        bootdriver,
        ram_mb: parseInt(document.getElementById('cfg-ram')?.value || 64),
        screen: document.getElementById('cfg-screen')?.value || '640x480',
        audio: document.getElementById('cfg-sound')?.checked ?? true,
        zappram: document.getElementById('cfg-zappram')?.checked ?? false,
        dismiss_shutdown_dialog: document.getElementById('cfg-dismiss-shutdown-dialog')?.checked ?? true,
        bridge_enabled: document.getElementById('cfg-bridge-enabled')?.checked ?? false,
        network: document.getElementById('cfg-network')?.value || 'none',
        // Socket path / MITM ports / MITM CA dir no longer have UI inputs —
        // preserve whatever was loaded from disk so saving via the UI doesn't
        // clobber custom server-side values.
        network_if: currentConfig.network_if || '',
        mitm_tls: document.getElementById('cfg-mitm-tls')?.checked ?? false,
        mitm_ports: currentConfig.mitm_ports || '',
        mitm_ca_dir: currentConfig.mitm_ca_dir || '',
        codec: document.getElementById('codec-select')?.value || 'png',
        mousemode: document.getElementById('mouse-mode-select')?.value || 'absolute',
        keyboard: {
            ctrl: document.getElementById('cfg-kb-ctrl')?.value || '',
            alt:  document.getElementById('cfg-kb-alt')?.value  || '',
            meta: document.getElementById('cfg-kb-meta')?.value || '',
            fn:   document.getElementById('cfg-kb-fn')?.value   || '',
            release_on_blur: document.getElementById('cfg-kb-release-on-blur')?.checked ?? true,
        },
    };
}

async function savePreset() {
    const name = prompt('Preset name:');
    if (!name || !name.trim()) return;

    const presetConfig = buildConfigJson();
    App.savedPresets[name.trim()] = presetConfig;

    // Persist by saving config with updated presets
    const jsonConfig = buildConfigJson();
    jsonConfig.configs = App.savedPresets;

    try {
        const res = await fetch(getApiUrl('config'), {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(jsonConfig)
        });
        const data = await res.json();
        if (data.success) {
            renderPresetTabs();
        } else {
            logger.error('Failed to save preset', { message: data.error });
        }
    } catch (e) {
        logger.error('Failed to save preset', { error: e.message });
    }
}

async function deletePreset(name) {
    if (!confirm(`Delete preset "${name}"?`)) return;

    delete App.savedPresets[name];

    // Persist by saving config with updated presets
    const jsonConfig = buildConfigJson();
    jsonConfig.configs = App.savedPresets;

    try {
        const res = await fetch(getApiUrl('config'), {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(jsonConfig)
        });
        const data = await res.json();
        if (data.success) {
            renderPresetTabs();
        }
    } catch (e) {
        logger.error('Failed to delete preset', { error: e.message });
    }
}

async function openConfig() {
    const modal = document.getElementById('config-modal');
    if (modal) {
        modal.classList.add('open');
        storageCache = null; // Clear cache to refresh
        setConfigControlsEnabled(false);
        // Load config and storage lists in parallel, then apply selections
        await Promise.all([loadCurrentConfig(), loadRomList(), loadDiskList(), loadCdromList(), loadExtfsList()]);
        renderPresetTabs();
        updateConfigUI();
        initCreateImageDialog();
        setConfigControlsEnabled(true);
    }
}

function closeConfig() {
    const modal = document.getElementById('config-modal');
    if (modal) {
        modal.classList.remove('open');
    }
}

// Generic collapsible-section toggle. The click handler binds (toggleEl,
// contentId) explicitly because there's now more than one ".advanced-toggle"
// in the modal (Advanced + Keyboard) and a class selector would match the
// wrong one.
function toggleSection(toggleEl, contentId) {
    const content = document.getElementById(contentId);
    if (toggleEl && content) {
        toggleEl.classList.toggle('open');
        content.classList.toggle('open');
    }
}

async function loadRomList() {
    const select = document.getElementById('cfg-rom');
    if (!select) return;

    try {
        const data = await loadStorage();
        if (!data) {
            select.innerHTML = '<option value="">Failed to load</option>';
            return;
        }

        if (data.roms && data.roms.length > 0) {
            const currentArch = backendIsPpc(currentConfig.backend) ? 'ppc' : 'm68k';

            // Filter and categorize ROMs
            const recommendedRoms = [];
            const otherRoms = [];
            const seenKnownMD5 = new Set();

            data.roms.forEach(rom => {
                const info = getRomInfo(rom.checksum, rom.md5);

                // Filter: only show ROMs matching current architecture (or unknown)
                if (info && info.arch && info.arch !== currentArch) {
                    return; // Skip incompatible ROMs
                }

                // Deduplicate known ROMs only (skip if we've seen this MD5 or checksum)
                const hash = rom.md5 || rom.checksum;
                if (info && seenKnownMD5.has(hash)) {
                    return; // Skip duplicate known ROM
                }
                if (info) {
                    seenKnownMD5.add(hash);
                }

                // Recommended if the ROM's mode matches the current persona.
                const currentMode = currentConfig.emulator || (currentArch === 'ppc' ? 'ppc' : 'quadra');
                if (info?.recommended && info.mode === currentMode) {
                    recommendedRoms.push(rom);
                } else {
                    otherRoms.push(rom);
                }
            });

            // Sort each category by name
            recommendedRoms.sort((a, b) => a.name.localeCompare(b.name));
            otherRoms.sort((a, b) => a.name.localeCompare(b.name));

            // Build HTML with recommended ROMs first
            let html = '';

            if (recommendedRoms.length > 0) {
                html += recommendedRoms.map(rom => {
                    const info = getRomInfo(rom.checksum, rom.md5);
                    const displayName = info ? info.name : rom.name;
                    const sizeStr = rom.size ? ` (${(rom.size / 1024 / 1024).toFixed(1)} MB)` : '';
                    const selected = currentConfig.rom === rom.name ? 'selected' : '';
                    return `<option value="${rom.name}" ${selected}>${displayName}${sizeStr}</option>`;
                }).join('');
            }

            if (otherRoms.length > 0) {
                // Add separator if we have both categories
                if (recommendedRoms.length > 0) {
                    html += '<option disabled>──────────────────</option>';
                }

                html += otherRoms.map(rom => {
                    const info = getRomInfo(rom.checksum, rom.md5);
                    const displayName = info ? info.name : rom.name;
                    const checksumStr = info ? '' : ` [${rom.checksum.substring(0, 8)}]`;
                    const sizeStr = rom.size ? ` (${(rom.size / 1024 / 1024).toFixed(1)} MB)` : '';
                    const selected = currentConfig.rom === rom.name ? 'selected' : '';
                    return `<option value="${rom.name}" ${selected}>${displayName}${checksumStr}${sizeStr}</option>`;
                }).join('');
            }

            select.innerHTML = html;

            // Auto-select first recommended ROM for this mode
            if (recommendedRoms.length > 0) {
                currentConfig.rom = recommendedRoms[0].name;
                select.value = recommendedRoms[0].name;
            } else if (!currentConfig.rom && otherRoms.length > 0) {
                currentConfig.rom = otherRoms[0].name;
                select.value = otherRoms[0].name;
            }
        } else {
            select.innerHTML = '<option value="">No ROM files found</option>';
        }
    } catch (e) {
        select.innerHTML = '<option value="">Failed to load ROMs</option>';
        logger.error('Failed to load ROM list', { error: e.message });
    }
}

async function loadDiskList() {
    const container = document.getElementById('disk-list');
    if (!container) return;

    try {
        const data = await loadStorage();
        if (!data) {
            container.innerHTML = '<div class="empty-state">Failed to load storage</div>';
            return;
        }

        if (data.disks && data.disks.length > 0) {
            container.innerHTML = data.disks.map((disk, idx) => {
                const checked = currentConfig.disks.includes(disk.name) ? 'checked' : '';
                const sizeStr = disk.size ? ` (${(disk.size / 1024 / 1024).toFixed(1)} MB)` : '';
                return `
                    <div class="checkbox-group">
                        <input type="checkbox" id="disk-${idx}" value="${disk.name}" ${checked} onchange="updateDiskSelection()">
                        <label for="disk-${idx}">${disk.name}${sizeStr}</label>
                    </div>`;
            }).join('');
        } else {
            container.innerHTML = '<div class="empty-state">No disk images found in storage/images/</div>';
        }
        refreshBootFromOptions();
    } catch (e) {
        container.innerHTML = '<div class="empty-state">Failed to load disks</div>';
        logger.error('Failed to load disk list', { error: e.message });
    }
}

function initCreateImageDialog() {
    const openBtn = document.getElementById('create-image-btn');
    const modal = document.getElementById('create-image-modal');
    if (!openBtn || !modal || openBtn.dataset.wired === '1') return;
    openBtn.dataset.wired = '1';

    const closeBtn = document.getElementById('create-image-close');
    const cancelBtn = document.getElementById('ci-cancel');
    const createBtn = document.getElementById('ci-create');
    const formatSel = document.getElementById('ci-format');
    const fnameIn = document.getElementById('ci-filename');
    const volIn = document.getElementById('ci-volume');
    const sizeIn = document.getElementById('ci-size');
    const errEl = document.getElementById('ci-error');
    const statusEl = document.getElementById('create-image-status');

    const open = () => {
        errEl.style.display = 'none';
        errEl.textContent = '';
        fnameIn.value = '';
        volIn.value = '';
        sizeIn.value = '120';
        formatSel.value = 'hfs';
        modal.classList.add('open');
        setTimeout(() => fnameIn.focus(), 50);
    };
    const close = () => modal.classList.remove('open');

    openBtn.addEventListener('click', open);
    closeBtn.addEventListener('click', close);
    cancelBtn.addEventListener('click', close);
    modal.addEventListener('click', (e) => { if (e.target === modal) close(); });

    createBtn.addEventListener('click', async () => {
        errEl.style.display = 'none';
        const payload = {
            format: formatSel.value,
            filename: fnameIn.value.trim(),
            volume_name: volIn.value.trim(),
            size_mb: parseInt(sizeIn.value, 10) || 0,
        };
        if (!payload.filename) {
            errEl.textContent = 'Filename is required.';
            errEl.style.display = 'block';
            return;
        }
        createBtn.disabled = true;
        statusEl.textContent = 'Creating…';
        try {
            const resp = await fetch(getApiUrl('storage/create-image'), {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload),
            });
            const data = await resp.json().catch(() => ({}));
            if (!resp.ok || !data.ok) {
                errEl.textContent = data.error || `Failed (HTTP ${resp.status})`;
                errEl.style.display = 'block';
                statusEl.textContent = '';
                return;
            }
            statusEl.textContent = `Created ${data.filename}`;
            close();
            await loadDiskList();
            await loadCdromList();
        } catch (e) {
            errEl.textContent = e.message || 'Request failed';
            errEl.style.display = 'block';
            statusEl.textContent = '';
        } finally {
            createBtn.disabled = false;
        }
    });
}

function updateDiskSelection() {
    const checkboxes = document.querySelectorAll('#disk-list input[type="checkbox"]:checked');
    currentConfig.disks = Array.from(checkboxes).map(cb => cb.value);
    refreshBootFromOptions();
}

// Rebuild the "Boot From" dropdown from the currently-enabled disks.
// Selecting a disk reorders disk_paths so it becomes the first drive
// (how the Mac ROM picks a boot volume); CD-ROM sets bootdriver=-62.
function refreshBootFromOptions() {
    const sel = document.getElementById('cfg-bootdriver');
    if (!sel) return;
    const prev = sel.value;

    sel.innerHTML = '';
    for (const d of currentConfig.disks || []) {
        const label = d.split('/').pop();
        sel.add(new Option(`Disk: ${label}`, `disk:${d}`));
    }
    sel.add(new Option('CD-ROM', '-62'));

    if (Array.from(sel.options).some(o => o.value === prev)) {
        sel.value = prev;
    } else if (sel.options.length > 0) {
        sel.selectedIndex = 0;
    }
}

async function loadCdromList() {
    const container = document.getElementById('cdrom-list');
    if (!container) return;

    try {
        const data = await loadStorage();
        if (!data) {
            container.innerHTML = '<div class="empty-state">Failed to load storage</div>';
            return;
        }

        if (data.cdroms && data.cdroms.length > 0) {
            container.innerHTML = data.cdroms.map((cdrom, idx) => {
                const checked = currentConfig.cdroms.includes(cdrom.name) ? 'checked' : '';
                const sizeStr = cdrom.size ? ` (${(cdrom.size / 1024 / 1024).toFixed(1)} MB)` : '';
                return `
                    <div class="checkbox-group">
                        <input type="checkbox" id="cdrom-${idx}" value="${cdrom.name}" ${checked} onchange="updateCdromSelection()">
                        <label for="cdrom-${idx}">${cdrom.name}${sizeStr}</label>
                    </div>`;
            }).join('');
        } else {
            container.innerHTML = '<div class="empty-state">No CD-ROM images (.iso) found in storage/images/</div>';
        }
    } catch (e) {
        container.innerHTML = '<div class="empty-state">Failed to load CD-ROMs</div>';
        logger.error('Failed to load cdrom list', { error: e.message });
    }
}

function updateCdromSelection() {
    const checkboxes = document.querySelectorAll('#cdrom-list input[type="checkbox"]:checked');
    currentConfig.cdroms = Array.from(checkboxes).map(cb => cb.value);
}

async function loadExtfsList() {
    renderExtfsList();
    const addBtn = document.getElementById('extfs-add-btn');
    const input = document.getElementById('extfs-path-input');
    if (addBtn && input) {
        addBtn.onclick = () => {
            const path = input.value.trim();
            if (path) {
                if (!currentConfig.extfs) currentConfig.extfs = [];
                if (!currentConfig.extfs.includes(path)) {
                    currentConfig.extfs.push(path);
                    renderExtfsList();
                }
                input.value = '';
            }
        };
        input.onkeydown = (e) => { if (e.key === 'Enter') addBtn.click(); };
    }
}

function renderExtfsList() {
    const container = document.getElementById('extfs-list');
    if (!container) return;
    const paths = currentConfig.extfs || [];
    if (paths.length === 0) {
        container.innerHTML = '<div class="empty-state">No shared folders configured</div>';
        return;
    }
    container.innerHTML = paths.map((p, idx) => `
        <div class="checkbox-group" style="display:flex;align-items:center;gap:4px">
            <span style="flex:1;font-size:12px">${p}</span>
            <button type="button" class="btn" onclick="removeExtfsPath(${idx})">Remove</button>
        </div>`).join('');
}

function removeExtfsPath(idx) {
    if (currentConfig.extfs) {
        currentConfig.extfs.splice(idx, 1);
        renderExtfsList();
    }
}

// Show/hide backend-dependent settings rows. Driven by the selected backend.
function updateEmulatorPanelVisibility() {
    const backend = document.getElementById('cfg-backend')?.value
                    || currentConfig.backend
                    || 'uae';
    const isPpc = backendIsPpc(backend);
    const supportsJit = (backend === 'uae' || backend === 'kpx');
    const isKpx = (backend === 'kpx');

    // JIT row: hide for unicorn-* backends (no JIT available there)
    const jitGroup = document.getElementById('cfg-jit-group');
    if (jitGroup) jitGroup.style.display = supportsJit ? '' : 'none';

    // 68k JIT row: KPX-only
    const jit68kGroup = document.getElementById('cfg-jit68k-group');
    if (jit68kGroup) jit68kGroup.style.display = isKpx ? '' : 'none';

    // Header logo / title follow the architecture
    const processorLogo = document.getElementById('processor-logo');
    if (processorLogo) {
        processorLogo.src = isPpc ? 'PowerPC.svg' : 'Motorola.svg';
        processorLogo.alt = isPpc ? 'PowerPC' : 'Motorola';
    }

    updateHeaderTitle();
}

// Apply mode-specific constraints (e.g., SE has fixed screen, limited RAM)
function applyModeConstraints(mode) {
    const ramEl = document.getElementById('cfg-ram');
    const screenEl = document.getElementById('cfg-screen');
    if (mode === 'se') {
        // SE: fixed 512x342 BW screen, max 4MB RAM
        if (screenEl) {
            screenEl.value = '512x342';
            screenEl.disabled = true;
        }
        if (ramEl) {
            [...ramEl.options].forEach(opt => {
                opt.disabled = parseInt(opt.value) > 4;
            });
            if (parseInt(ramEl.value) > 4) ramEl.value = 4;
        }
    } else {
        // Other modes: restore full access
        if (screenEl) screenEl.disabled = false;
        if (ramEl) {
            [...ramEl.options].forEach(opt => { opt.disabled = false; });
        }
    }
}

// Called when user changes the "Emulator" persona dropdown. Suggests sensible
// defaults; user can still tweak them in the advanced panel.
async function onEmulatorChange() {
    const emulatorType = document.getElementById('cfg-emulator')?.value;
    if (!emulatorType) return;

    currentConfig.emulator = emulatorType;

    const defaults = {
        ppc:    { ram: 128, screen: '1024x768', backend: 'kpx' },
        quadra: { ram: 32,  screen: '1024x768', backend: 'uae' },
        se:     { ram: 4,   screen: '512x342',  backend: 'uae' }
    };
    const d = defaults[emulatorType] || defaults.quadra;

    currentConfig.ram = d.ram;
    currentConfig.screen = d.screen;
    currentConfig.backend = d.backend;

    const ramEl = document.getElementById('cfg-ram');
    if (ramEl) ramEl.value = d.ram;
    const screenEl = document.getElementById('cfg-screen');
    if (screenEl) screenEl.value = d.screen;
    const backendEl = document.getElementById('cfg-backend');
    if (backendEl) backendEl.value = d.backend;

    updateEmulatorPanelVisibility();
    applyModeConstraints(emulatorType);
    await loadRomList();
}

function onRomChange() {
    const romName = document.getElementById('cfg-rom')?.value;
    if (!romName || !storageCache?.roms) return;

    // Find ROM in storage cache
    const rom = storageCache.roms.find(r => r.name === romName);
    if (!rom) return;

    // Look up ROM info and auto-set model if known
    const info = getRomInfo(rom.checksum, rom.md5);

    // Update header title to show model name
    updateHeaderTitle();
}

async function loadCurrentConfig() {
    try {
        const res = await fetch(getApiUrl('config'));
        const cfg = await res.json();
        currentConfig = configFromServerJson(cfg);
        App.savedPresets = cfg.configs || {};
        // Push keyboard remap into the live keystroke pipeline + chip render.
        applyKeyboardConfig(currentConfig.keyboard);
    } catch (e) {
        logger.warn('Failed to load current config', { error: e.message });
    }
}

function updateConfigUI() {
    // Common elements
    const emulatorEl = document.getElementById('cfg-emulator');
    const romEl = document.getElementById('cfg-rom');
    const ramEl = document.getElementById('cfg-ram');
    const screenEl = document.getElementById('cfg-screen');
    const soundEl = document.getElementById('cfg-sound');
    const zappramEl = document.getElementById('cfg-zappram');
    const dismissDialogEl = document.getElementById('cfg-dismiss-shutdown-dialog');
    const bridgeEnabledEl = document.getElementById('cfg-bridge-enabled');

    if (emulatorEl) emulatorEl.value = currentConfig.emulator || 'quadra';
    if (romEl) romEl.value = currentConfig.rom;
    if (ramEl) ramEl.value = currentConfig.ram;
    if (screenEl) screenEl.value = currentConfig.screen;
    if (soundEl) soundEl.checked = currentConfig.sound;
    if (zappramEl) zappramEl.checked = currentConfig.zappram;
    if (dismissDialogEl) dismissDialogEl.checked = currentConfig.dismiss_shutdown_dialog;
    if (bridgeEnabledEl) bridgeEnabledEl.checked = currentConfig.bridge_enabled;

    const networkEl = document.getElementById('cfg-network');
    const mitmTlsEl = document.getElementById('cfg-mitm-tls');
    const mitmGroup = document.getElementById('cfg-mitm-group');

    // MITM TLS toggle only makes sense when networking is enabled (socket mode).
    const syncNetworkVisibility = () => {
        const netMode = networkEl ? networkEl.value : (currentConfig.network || 'none');
        if (mitmGroup) mitmGroup.style.display = (netMode === 'socket') ? '' : 'none';
    };

    if (networkEl) {
        networkEl.value = currentConfig.network || 'none';
        networkEl.addEventListener('change', syncNetworkVisibility);
    }
    if (mitmTlsEl) mitmTlsEl.checked = !!currentConfig.mitm_tls;
    syncNetworkVisibility();

    refreshBootFromOptions();
    const bootdriverEl = document.getElementById('cfg-bootdriver');
    if (bootdriverEl) {
        if (currentConfig.bootdriver === -62) {
            bootdriverEl.value = '-62';
        } else if ((currentConfig.disks || []).length > 0) {
            // bootdriver=0 means "first disk" — reflect that by selecting it explicitly
            bootdriverEl.value = `disk:${currentConfig.disks[0]}`;
        }
    }

    // Backend (selecting it also reveals/hides backend-dependent fields)
    const backendEl = document.getElementById('cfg-backend');
    if (backendEl) backendEl.value = currentConfig.backend || 'uae';
    updateEmulatorPanelVisibility();

    // CPU feature toggles
    const jitEl = document.getElementById('cfg-jit');
    const jit68kEl = document.getElementById('cfg-jit68k');
    const idlewaitEl = document.getElementById('cfg-idlewait');
    if (jitEl) jitEl.checked = currentConfig.jit ?? false;
    if (jit68kEl) jit68kEl.checked = currentConfig.jit68k ?? true;
    if (idlewaitEl) idlewaitEl.checked = currentConfig.idlewait ?? true;

    // Keyboard remap dropdowns
    const kb = currentConfig.keyboard || {};
    const kbCtrlEl    = document.getElementById('cfg-kb-ctrl');
    const kbAltEl     = document.getElementById('cfg-kb-alt');
    const kbMetaEl    = document.getElementById('cfg-kb-meta');
    const kbFnEl      = document.getElementById('cfg-kb-fn');
    const kbBlurEl    = document.getElementById('cfg-kb-release-on-blur');
    // Populate dropdowns from the *saved* values (currentConfig.keyboard
    // — preserves empties), NOT the resolved keyboardConfig. An empty
    // string selects the "Platform default" option, so users keep that
    // tracking behavior across sessions/devices instead of accidentally
    // freezing in whatever the platform happens to resolve to today.
    const savedKb = currentConfig.keyboard || {};
    if (kbCtrlEl) kbCtrlEl.value = savedKb.ctrl ?? '';
    if (kbAltEl)  kbAltEl.value  = savedKb.alt  ?? '';
    if (kbMetaEl) kbMetaEl.value = savedKb.meta ?? '';
    if (kbFnEl)   kbFnEl.value   = savedKb.fn   ?? '';
    if (kbBlurEl) kbBlurEl.checked = kb.release_on_blur ?? true;

    // Update disk checkboxes
    document.querySelectorAll('#disk-list input[type="checkbox"]').forEach(cb => {
        cb.checked = currentConfig.disks.includes(cb.value);
    });

    // Update cdrom checkboxes
    document.querySelectorAll('#cdrom-list input[type="checkbox"]').forEach(cb => {
        cb.checked = currentConfig.cdroms.includes(cb.value);
    });

    // Update shared folders list
    renderExtfsList();

    // Apply mode constraints (e.g., SE fixed screen)
    applyModeConstraints(currentConfig.emulator);

    // Update header title with model name
    updateHeaderTitle();
}

async function saveConfig() {
    // Sync currentConfig from form before building JSON
    currentConfig.emulator = document.getElementById('cfg-emulator')?.value || 'quadra';
    const romDropdown = document.getElementById('cfg-rom');
    if (romDropdown && romDropdown.value) {
        currentConfig.rom = romDropdown.value;
    }
    currentConfig.ram = parseInt(document.getElementById('cfg-ram')?.value || 64);
    currentConfig.screen = document.getElementById('cfg-screen')?.value || '640x480';
    currentConfig.sound = document.getElementById('cfg-sound')?.checked ?? true;
    currentConfig.zappram = document.getElementById('cfg-zappram')?.checked ?? false;
    currentConfig.dismiss_shutdown_dialog = document.getElementById('cfg-dismiss-shutdown-dialog')?.checked ?? true;
    currentConfig.bridge_enabled = document.getElementById('cfg-bridge-enabled')?.checked ?? false;
    currentConfig.backend = document.getElementById('cfg-backend')?.value
                            || defaultBackendForMode(currentConfig.emulator);
    currentConfig.jit = document.getElementById('cfg-jit')?.checked ?? false;
    currentConfig.jit68k = document.getElementById('cfg-jit68k')?.checked ?? true;
    currentConfig.idlewait = document.getElementById('cfg-idlewait')?.checked ?? true;
    currentConfig.keyboard = {
        ctrl: document.getElementById('cfg-kb-ctrl')?.value || '',
        alt:  document.getElementById('cfg-kb-alt')?.value  || '',
        meta: document.getElementById('cfg-kb-meta')?.value || '',
        fn:   document.getElementById('cfg-kb-fn')?.value   || '',
        release_on_blur: document.getElementById('cfg-kb-release-on-blur')?.checked ?? true,
    };

    // Apply the new keyboard remap immediately — no need to wait for the
    // server round-trip; the chip relabels and the next keystroke uses
    // the new mapping.
    applyKeyboardConfig(currentConfig.keyboard);

    const jsonConfig = buildConfigJson();

    // Keep the named preset for the current "Emulator" persona in sync with
    // the top-level config we're about to save. Otherwise a "Boot From"
    // reorder (which lives in disks[]) gets clobbered next loadPreset().
    const presetName = currentConfig.emulator;
    if (presetName && App.savedPresets[presetName]) {
        App.savedPresets[presetName] = { ...jsonConfig };
    }

    // Include saved presets
    if (Object.keys(App.savedPresets).length > 0) {
        jsonConfig.configs = App.savedPresets;
    }

    try {
        const res = await fetch(getApiUrl('config'), {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(jsonConfig)
        });
        const data = await res.json();

        if (data.success) {
            console.log('CONFIG SAVED to macemu-config.json');
            closeConfig();
        } else {
            logger.error('Failed to save config', { message: data.error });
        }
    } catch (e) {
        logger.error('Failed to save config', { error: e.message });
    }
}

// ============================================================================
// Emulator Control
// ============================================================================

async function startEmulator() {
    logger.info('Starting emulator...');

    // Reset httpstream session so we get a full frame (not a stale dirty rect)
    if (client && client.decoder && client.decoder.resetSession) {
        client.decoder.resetSession();
        logger.info('Reset httpstream session for clean restart');
    }

    try {
        const res = await fetch(getApiUrl('emulator/start'), { method: 'POST' });
        const data = await res.json();
        logger.info('Start emulator', { message: data.message });
    } catch (e) {
        logger.error('Failed to start emulator', { error: e.message });
    }
}

async function stopEmulator() {
    logger.info('Stopping emulator...');

    // Immediately show disconnected state (polling will confirm in 2s)
    const displayContainer = document.getElementById('display-container');
    if (displayContainer) {
        displayContainer.classList.add('disconnected');
    }

    try {
        const res = await fetch(getApiUrl('emulator/stop'), { method: 'POST' });
        const data = await res.json();
        logger.info('Stop emulator', { message: data.message });
    } catch (e) {
        logger.error('Failed to stop emulator', { error: e.message });
    }
}

async function restartEmulator() {
    logger.info('Restarting emulator...');

    // Reset httpstream session so we get a full frame
    if (client && client.decoder && client.decoder.resetSession) {
        client.decoder.resetSession();
    }

    try {
        const res = await fetch(getApiUrl('emulator/restart'), { method: 'POST' });
        const data = await res.json();
        logger.info('Restart emulator', { message: data.message });
    } catch (e) {
        logger.error('Failed to restart emulator', { error: e.message });
    }
}

// Wire a split-button dropdown: clicking the arrow toggles the menu;
// clicking a menu item runs the mapped handler and closes the menu.
// Clicking outside or pressing Escape also closes it.
function setupSplitButton(arrowBtnId, menuId, actions) {
    const arrow = document.getElementById(arrowBtnId);
    const menu = document.getElementById(menuId);
    if (!arrow || !menu) return;
    const wrapper = arrow.closest('.split-btn');
    if (!wrapper) return;

    const close = () => {
        wrapper.classList.remove('open');
        arrow.setAttribute('aria-expanded', 'false');
    };
    const open = () => {
        document.querySelectorAll('.split-btn.open').forEach(el => {
            if (el !== wrapper) el.classList.remove('open');
        });
        wrapper.classList.add('open');
        arrow.setAttribute('aria-expanded', 'true');
    };

    arrow.addEventListener('click', (e) => {
        e.stopPropagation();
        wrapper.classList.contains('open') ? close() : open();
    });

    menu.querySelectorAll('.split-menu-item').forEach(item => {
        item.addEventListener('click', (e) => {
            e.stopPropagation();
            const action = item.getAttribute('data-action');
            close();
            const fn = actions[action];
            if (fn) fn();
        });
    });

    document.addEventListener('click', (e) => {
        if (!wrapper.contains(e.target)) close();
    });
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') close();
    });
}

// Graceful guest-OS shutdown via the BridgeAgent. Sends kAEShutDown to
// Finder; the emulator process keeps running while the guest OS powers down.
async function shutdownGuest() {
    logger.info('Shutting down guest OS...');
    try {
        const res = await fetch(getApiUrl('shutdown'), { method: 'POST' });
        const data = await res.json();
        if (data.success) {
            logger.info('Guest shutdown requested');
        } else {
            logger.error('Guest shutdown failed', { error: data.error || data.message });
        }
    } catch (e) {
        logger.error('Shutdown request failed', { error: e.message });
    }
}

// Graceful guest-OS restart via the BridgeAgent. Sends kAERestart to Finder.
async function restartGuest() {
    logger.info('Restarting guest OS...');
    if (client && client.decoder && client.decoder.resetSession) {
        client.decoder.resetSession();
    }
    try {
        const res = await fetch(getApiUrl('restart'), { method: 'POST' });
        const data = await res.json();
        if (data.success) {
            logger.info('Guest restart requested');
        } else {
            logger.error('Guest restart failed', { error: data.error || data.message });
        }
    } catch (e) {
        logger.error('Restart request failed', { error: e.message });
    }
}

async function resetEmulator() {
    // Reset = Restart (stop + start) since MACEMU_CMD_RESET crashes SheepShaver
    logger.info('Resetting emulator...');
    await restartEmulator();
}

async function invokeDebugger() {
    logger.info('Invoking debugger (Programmer\'s Key)...');
    try {
        const res = await fetch(getApiUrl('invoke-debug'), { method: 'POST' });
        const data = await res.json();
        if (data.success) {
            logger.info('Debugger invoked');
        } else {
            logger.error('Invoke debugger failed', { error: data.error });
        }
    } catch (e) {
        logger.error('Invoke debugger request failed', { error: e.message });
    }
}

// Set of codec ids the server reports as available. httpstream is always usable.
let availableCodecIds = new Set(['httpstream']);

// Build the auto-fallback chain: best-quality first, narrowing to most-compatible.
//   WebRTC tier (UDP/RTP)        : vp9, h264
//   WebSocket tier (TCP frames)  : webp, png
//   HTTP long-poll (last resort) : httpstream
function buildCodecFallbackChain() {
    const order = ['vp9', 'h264', 'webp', 'png', 'httpstream'];
    return order.filter(id => availableCodecIds.has(id));
}

// Apply codec availability to the dropdown (removes unavailable codecs)
function applyCodecAvailability(codecs) {
    const select = document.getElementById('codec-select');
    if (!codecs) return;

    availableCodecIds = new Set(['httpstream']);
    for (const c of codecs) if (c.available) availableCodecIds.add(c.id);

    if (!select) return;
    const currentValue = select.value;
    select.innerHTML = '';

    for (const codec of codecs) {
        if (codec.available) {
            const opt = document.createElement('option');
            opt.value = codec.id;
            opt.textContent = codec.name;
            select.appendChild(opt);
        }
    }

    // Always include HTTP Stream option
    const opt = document.createElement('option');
    opt.value = 'httpstream';
    opt.textContent = 'HTTP Stream';
    select.appendChild(opt);

    // Restore selection if still available, otherwise default to first
    if (select.querySelector(`option[value="${currentValue}"]`)) {
        select.value = currentValue;
    }
}

// Fetch available codecs from server and populate the dropdown
async function populateAvailableCodecs() {
    const select = document.getElementById('codec-select');
    if (!select) return;

    try {
        const response = await fetch(getApiUrl('codecs'));
        const data = await response.json();
        if (data.codecs) {
            applyCodecAvailability(data.codecs);
        }
    } catch (e) {
        logger.warn('[Browser] Failed to fetch available codecs', { error: e.message });
    }
}

// Codec management
async function changeCodec() {
    const select = document.getElementById('codec-select');
    if (!select || !client) return;

    const newCodec = select.value;
    logger.info('Changing codec', { codec: newCodec });

    // Switching to HTTP stream — client-side only, no server codec change needed
    if (newCodec === 'httpstream') {
        try { localStorage.setItem('macemu_prefer_httpstream', '1'); } catch(e) {}
        client.fallbackToHTTPStream();
        return;
    }

    // Switching away from HTTP stream — tell server to switch encoder, then reconnect via WebRTC
    if (client.codecType === CodecType.HTTP_STREAM) {
        try { localStorage.removeItem('macemu_prefer_httpstream'); } catch(e) {}
        client.cleanup();
        if (client.ws) { client.ws.close(); client.ws = null; }
        // Update server-side encoder (same as normal WebRTC codec change)
        try {
            await fetch(getApiUrl('codec'), {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ codec: newCodec })
            });
        } catch (e) {
            logger.error('Failed to set server codec', { error: e.message });
        }
        // Connect via WebRTC — server encoder now matches the requested codec
        client.codecType = parseCodecString(newCodec);
        const wsUrl = getWebSocketUrl();
        client.connect(wsUrl);
        return;
    }

    // Normal WebRTC codec change — tell server
    try {
        const res = await fetch(getApiUrl('codec'), {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ codec: newCodec })
        });
        const data = await res.json();
        if (data.ok) {
            logger.info('Codec changed successfully', { codec: newCodec });
            // Server will send "reconnect" message to trigger client reconnection
        } else if (data.error) {
            logger.error('Failed to change codec', { error: data.error });
        }
    } catch (e) {
        logger.error('Failed to change codec', { error: e.message });
    }
}

// Debug: cycle through all codecs to test switching (call from console: testCodecCycle())
async function testCodecCycle() {
    const codecs = ['png', 'h264', 'vp9', 'webp', 'png'];
    const delay = 4000;

    for (const codec of codecs) {
        console.log(`[codec-test] Switching to ${codec}...`);
        try {
            const res = await fetch(getApiUrl('codec'), {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ codec })
            });
            const data = await res.json();
            console.log(`[codec-test] ${codec}: ${JSON.stringify(data)}`);
        } catch (e) {
            console.error(`[codec-test] ${codec} failed: ${e.message}`);
        }
        console.log(`[codec-test] Waiting ${delay/1000}s on ${codec}...`);
        await new Promise(r => setTimeout(r, delay));

        // Check video element state
        const video = document.querySelector('video');
        if (video) {
            console.log(`[codec-test] ${codec}: videoWidth=${video.videoWidth} videoHeight=${video.videoHeight}`);
        }
    }
    console.log('[codec-test] Cycle complete');
}
window.testCodecCycle = testCodecCycle;

// Emulator selection
// Emulator status polling
async function pollEmulatorStatus() {
    try {
        const res = await fetch(getApiUrl('status'));
        const data = await res.json();

        const dotRunning = document.getElementById('dot-running');
        const dotConnected = document.getElementById('dot-connected');
        const emuPid = document.getElementById('emu-pid');

        if (dotRunning) {
            dotRunning.className = 'dot ' + (data.emulator_running ? 'green' : 'red');
        }

        updateHeaderPhaseColor(data.emulator_running ? data.boot_phase : null);
        if (dotConnected) {
            dotConnected.className = 'dot ' + (data.emulator_connected ? 'green' : 'red');
        }
        if (emuPid) {
            emuPid.textContent = 'PID: ' + (data.emulator_pid > 0 ? data.emulator_pid : '-');
        }

        // Update Start/Reset button based on emulator state
        const startBtn = document.getElementById('start-btn');
        if (startBtn) {
            if (data.emulator_running) {
                startBtn.textContent = 'Reset';
                startBtn.onclick = resetEmulator;
            } else {
                startBtn.textContent = 'Start';
                startBtn.onclick = startEmulator;
            }
        }

        // Default the primary Stop/Reset buttons to graceful actions only while
        // the emulator is running AND the BridgeAgent has been seen recently;
        // otherwise fall back to hard Power Off / Reset. Split-menu items
        // remain available either way.
        const useGraceful = data.emulator_running && data.bridge_agent_connected;
        const stopBtnP = document.getElementById('stop-btn');
        const resetBtnP = document.getElementById('reset-btn');
        if (stopBtnP) {
            if (useGraceful) {
                stopBtnP.textContent = 'Shut Down…';
                stopBtnP.title = 'Ask the guest OS to shut down (quits apps first)';
                stopBtnP.onclick = shutdownGuest;
            } else {
                stopBtnP.textContent = 'Power Off';
                stopBtnP.title = 'Hard power off (kill emulator process)';
                stopBtnP.onclick = stopEmulator;
            }
        }
        if (resetBtnP) {
            if (useGraceful) {
                resetBtnP.textContent = 'Restart…';
                resetBtnP.title = 'Ask the guest OS to restart (quits apps first)';
                resetBtnP.onclick = restartGuest;
            } else {
                resetBtnP.textContent = 'Reset';
                resetBtnP.title = 'Hard reset (restart emulator process)';
                resetBtnP.onclick = restartEmulator;
            }
        }

        // Update emulator status in header status bar
        const emuIcon = document.getElementById('emulator-icon');
        const displayContainer = document.getElementById('display-container');

        if (emuIcon) {
            emuIcon.className = '';
            if (data.emulator_running && data.emulator_connected) {
                // Emulator fully running - show active icon
                emuIcon.classList.remove('inactive', 'connecting');
                // Remove disconnected state when emulator is fully running
                if (displayContainer) {
                    displayContainer.classList.remove('disconnected');
                }
            } else if (data.emulator_running) {
                // Emulator starting - show pulsing icon
                emuIcon.classList.add('connecting');
                emuIcon.classList.remove('inactive');
                // Keep disconnected state while starting
                if (displayContainer) {
                    displayContainer.classList.add('disconnected');
                }
            } else {
                // Emulator off - show inactive icon
                emuIcon.classList.add('inactive');
                emuIcon.classList.remove('connecting');
                // Add disconnected state when emulator is off
                if (displayContainer) {
                    displayContainer.classList.add('disconnected');
                }
            }
        }

        // Update video latency stat
        const videoLatencyEl = document.getElementById('stat-video-latency');
        if (videoLatencyEl) {
            const avgLatency = client?.decoder?.getAverageLatency?.() || 0;
            videoLatencyEl.textContent = avgLatency > 0 ? avgLatency.toFixed(1) + ' ms' : '-- ms';
        }

        // Update Mac state tab
        const macStateEl = document.getElementById('mac-state');
        if (macStateEl) {
            macStateEl.textContent = JSON.stringify(data, null, 2);
        }

        // Update Clipboard tab from Mac scrap
        syncClipboardFromStatus(data);

    } catch (e) {
        // Silently fail status polling
    }
}

// --- MacRoman <-> UTF-16 helpers ---------------------------------------
// Browsers ship WHATWG 'macintosh' decoder; no encoder, so hand-roll reverse.
const MAC_ROMAN_DECODER = new TextDecoder('macintosh');

// 0x80..0xFF → Unicode code point (WHATWG "macintosh" table).
const MAC_ROMAN_HIGH = [
    0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,
    0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
    0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,
    0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
    0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,
    0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
    0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,
    0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
    0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,
    0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
    0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,
    0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
    0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,
    0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
    0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,
    0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7,
];

let _macRomanEncodeMap = null;
function macRomanEncodeMap() {
    if (_macRomanEncodeMap) return _macRomanEncodeMap;
    const m = new Map();
    for (let i = 0; i < MAC_ROMAN_HIGH.length; i++) m.set(MAC_ROMAN_HIGH[i], 0x80 + i);
    _macRomanEncodeMap = m;
    return m;
}

function encodeMacRoman(str) {
    const map = macRomanEncodeMap();
    // Normalize newlines to CR (Classic Mac line ending) before encoding.
    const s = str.replace(/\r\n/g, '\r').replace(/\n/g, '\r');
    const out = new Uint8Array(s.length * 2);
    let n = 0;
    for (let i = 0; i < s.length; i++) {
        let cp = s.charCodeAt(i);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.length) {
            const low = s.charCodeAt(i + 1);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                i++;
            }
        }
        if (cp < 0x80) out[n++] = cp;
        else {
            const b = map.get(cp);
            out[n++] = (b !== undefined) ? b : 0x3F; // '?'
        }
    }
    return out.subarray(0, n);
}

function bytesToBase64(bytes) {
    let s = '';
    for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
    return btoa(s);
}

function base64ToBytes(b64) {
    const s = atob(b64);
    const out = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
    return out;
}

let _lastClipboardB64 = null;
let _lastScrapCount = null;     // most recent ScrapCount seen in poll
let _sendBaseCount = null;       // ScrapCount captured at POST time; null = not awaiting
let _sendDeadline = 0;           // ms timestamp; fallback release after this
function syncClipboardFromStatus(data) {
    const ta = document.getElementById('clipboard-text');
    if (!ta) return;
    const scrap = data?.mac?.scrap;
    const b64 = scrap?.text_b64;
    const count = scrap?.count;
    if (typeof count === 'number') _lastScrapCount = count;
    if (typeof b64 !== 'string') return;

    // After a Send, suppress textarea updates until ScrapCount advances (Mac
    // accepted our PutScrap) or a 5s fallback elapses. Without this gate, a
    // status poll between POST and the BridgeAgent applying the scrap would
    // stomp the textarea with the pre-send value.
    if (_sendBaseCount !== null) {
        if (typeof count === 'number' && count !== _sendBaseCount) {
            _sendBaseCount = null;
        } else if (Date.now() > _sendDeadline) {
            _sendBaseCount = null;
        } else {
            return;
        }
    }

    if (b64 === _lastClipboardB64) return;
    if (document.activeElement === ta) return; // don't stomp while user edits
    _lastClipboardB64 = b64;
    try {
        const bytes = base64ToBytes(b64);
        // MacRoman → UTF-16; then CR → LF for textarea display.
        const text = MAC_ROMAN_DECODER.decode(bytes).replace(/\r/g, '\n');
        ta.value = text;
        const statusEl = document.getElementById('clipboard-status');
        if (statusEl) statusEl.textContent = `Mac clipboard (${bytes.length} bytes)`;
    } catch (e) {
        // ignore decode errors
    }
}

async function sendClipboardToMac() {
    const ta = document.getElementById('clipboard-text');
    if (!ta) return;
    const bytes = encodeMacRoman(ta.value);
    const b64 = bytesToBase64(bytes);
    const statusEl = document.getElementById('clipboard-status');
    const btn = document.getElementById('clipboard-send-btn');
    if (btn) btn.disabled = true;
    // Gate status-driven updates until scrap.count advances past the baseline.
    _sendBaseCount = _lastScrapCount;
    _sendDeadline = Date.now() + 5000;
    try {
        const res = await fetch('/api/clipboard', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({text_b64: b64}),
        });
        const j = await res.json().catch(() => ({}));
        if (!res.ok || j.success === false) {
            if (statusEl) statusEl.textContent = `Send failed: ${j.error || res.status}`;
            _sendBaseCount = null; // release gate on failure
        } else {
            if (statusEl) statusEl.textContent = `Sent ${bytes.length} bytes to Mac`;
        }
    } catch (e) {
        if (statusEl) statusEl.textContent = `Send error: ${e.message || e}`;
        _sendBaseCount = null;
    } finally {
        if (btn) btn.disabled = false;
    }
}

// Start status polling
setInterval(pollEmulatorStatus, CONSTANTS.STATUS_POLL_INTERVAL_MS);

// Setup event listeners for UI elements
function setupEventListeners() {
    // Populate codec dropdown based on server-compiled codecs
    populateAvailableCodecs();

    // Header controls
    const codecSelect = document.getElementById('codec-select');
    if (codecSelect) codecSelect.addEventListener('change', changeCodec);

    const mouseModeSelect = document.getElementById('mouse-mode-select');
    if (mouseModeSelect) mouseModeSelect.addEventListener('change', changeMouseMode);

    const configBtn = document.getElementById('config-btn');
    if (configBtn) configBtn.addEventListener('click', openConfig);

    // Set default start handler (status polling will switch to resetEmulator when running)
    const startBtn = document.getElementById('start-btn');
    if (startBtn) startBtn.onclick = startEmulator;

    // Primary stop/reset handlers are (re)assigned by pollEmulatorStatus based on
    // whether the BridgeAgent is connected — graceful when it is, hard when it isn't.
    const stopBtn = document.getElementById('stop-btn');
    if (stopBtn) stopBtn.onclick = stopEmulator;

    const resetBtn = document.getElementById('reset-btn');
    if (resetBtn) resetBtn.onclick = restartEmulator;

    setupSplitButton('stop-menu-btn', 'stop-menu', {
        poweroff: stopEmulator,
        shutdown: shutdownGuest,
    });
    setupSplitButton('reset-menu-btn', 'reset-menu', {
        reset: restartEmulator,
        restart: restartGuest,
    });

    const invokeDebugBtn = document.getElementById('invoke-debug-btn');
    if (invokeDebugBtn) invokeDebugBtn.addEventListener('click', invokeDebugger);

    const debugToggle = document.getElementById('debug-toggle');
    if (debugToggle) debugToggle.addEventListener('click', toggleDebugPanel);

    const fullscreenBtn = document.getElementById('fullscreen-btn');
    if (fullscreenBtn) fullscreenBtn.addEventListener('click', toggleFullscreen);

    // Debug panel
    const clearLogBtn = document.getElementById('clear-log-btn');
    if (clearLogBtn) clearLogBtn.addEventListener('click', clearLog);

    const clipboardSendBtn = document.getElementById('clipboard-send-btn');
    if (clipboardSendBtn) clipboardSendBtn.addEventListener('click', sendClipboardToMac);

    const debugTabs = document.querySelectorAll('.debug-tab');
    debugTabs.forEach(tab => {
        tab.addEventListener('click', () => {
            const tabName = tab.getAttribute('data-tab');
            if (tabName) showDebugTab(tabName);
        });
    });

    // Config modal
    const modalCloseBtn = document.getElementById('modal-close-btn');
    if (modalCloseBtn) modalCloseBtn.addEventListener('click', closeConfig);

    const cancelConfigBtn = document.getElementById('cancel-config-btn');
    if (cancelConfigBtn) cancelConfigBtn.addEventListener('click', closeConfig);

    const saveConfigBtn = document.getElementById('save-config-btn');
    if (saveConfigBtn) saveConfigBtn.addEventListener('click', saveConfig);

    const savePresetBtn = document.getElementById('save-preset-btn');
    if (savePresetBtn) savePresetBtn.addEventListener('click', savePreset);

    const cfgEmulator = document.getElementById('cfg-emulator');
    if (cfgEmulator) cfgEmulator.addEventListener('change', onEmulatorChange);

    const cfgBackend = document.getElementById('cfg-backend');
    if (cfgBackend) cfgBackend.addEventListener('change', updateEmulatorPanelVisibility);

    const cfgRom = document.getElementById('cfg-rom');
    if (cfgRom) cfgRom.addEventListener('change', onRomChange);

    const advancedToggle = document.getElementById('advanced-toggle');
    if (advancedToggle) advancedToggle.addEventListener('click',
        () => toggleSection(advancedToggle, 'advanced-settings'));

    const keyboardToggle = document.getElementById('keyboard-toggle');
    if (keyboardToggle) keyboardToggle.addEventListener('click',
        () => toggleSection(keyboardToggle, 'keyboard-settings'));
}

// Initialize on page load
window.addEventListener('DOMContentLoaded', async () => {
    // Paint the held-mods chip with bundled defaults right away so it's
    // never blank — loadCurrentConfig() below will re-render with the
    // server's persisted keyboard remap once that fetches.
    renderHeldModsChip();
    // Inject the platform-resolved target into each Keyboard dropdown's
    // "Platform default" option so the user can see what it'll do.
    relabelKeyboardDefaults();
    await loadRomDatabase();  // Load ROM database from JSON
    await fetchConfig();  // Load debug config from server
    await loadCurrentConfig();  // Load emulator config from JSON
    updateEmulatorPanelVisibility();  // Update header logo/title based on loaded config
    setupEventListeners();  // Setup all event listeners
    initClient();
    pollEmulatorStatus();
});
