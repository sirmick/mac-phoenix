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
    STATUS_POLL_INTERVAL_MS: 2000,
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
    webcodec: 'h264',
    mousemode: 'relative',
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
    H264: 'h264',   // WebRTC video track with H.264
    AV1: 'av1',     // WebRTC video track with AV1
    VP9: 'vp9',     // WebRTC video track with VP9
    PNG: 'png',     // PNG over DataChannel
    WEBP: 'webp'    // WebP over DataChannel (faster encoding than PNG)
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
            logger.error('Failed to decode PNG', { error: e.message });
        }
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
            return new VP9Decoder(element);  // VP9 uses canvas (DataChannel + WebCodecs)
        case CodecType.PNG:
        case CodecType.WEBP:
            // Both PNG and WebP use the same decoder (both are images over DataChannel)
            return new PNGDecoder(element);
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
        default:
            logger.warn('Unknown codec string, defaulting to PNG', { codec: codecStr });
            return CodecType.PNG;
    }
}

// Update the active codec indicator in the header
function updateCodecIndicator(codecType) {
    const el = document.getElementById('codec-active');
    if (el) {
        const label = getCodecLabel(codecType);
        const isRTP = (codecType === CodecType.H264 || codecType === CodecType.VP9);
        el.textContent = `[${label}${isRTP ? '/RTP' : '/DC'}]`;
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
        default: return 'Unknown';
    }
}


// Main WebRTC Client
class BasiliskWebRTC {
    constructor(videoElement, canvasElement = null) {
        this.video = videoElement;
        this.canvas = canvasElement;
        this.ws = null;
        this.pc = null;
        this.dataChannel = null;
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

        // H.264 and VP9 use video element (RTP); PNG/WEBP use canvas (DataChannel)
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

        // Set up frame callback for DataChannel decoders (PNG/WebP) to update screen dimensions
        if (this.codecType === CodecType.PNG || this.codecType === CodecType.WEBP) {
            this.decoder.onFrame = (frameCount, metadata) => {
                // Update screen dimensions for absolute mouse mode
                if (metadata && metadata.frameWidth && metadata.frameHeight) {
                    this.currentScreenWidth = metadata.frameWidth;
                    this.currentScreenHeight = metadata.frameHeight;
                    // Invalidate mouse cache when resolution changes
                    this.cachedMouseRect = null;
                }
            };
        }

        // Show/hide appropriate element and set initial size
        if (this.video) {
            this.video.style.display = usesVideoElement ? 'block' : 'none';
            if (usesVideoElement) {
                // Set initial size so video element isn't a tiny black box before metadata loads
                this.video.width = this.currentScreenWidth || 640;
                this.video.height = this.currentScreenHeight || 480;
            }
        }
        if (this.canvas) this.canvas.style.display = !usesVideoElement ? 'block' : 'none';

        return this.decoder.init();
    }

    connect(wsUrl) {
        this.wsUrl = wsUrl;
        this.reconnectAttempts = 0;
        this._connect();
    }

    _connect() {
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

            this.ws.onopen = () => this.onWsOpen();
            this.ws.onmessage = (e) => this.onWsMessage(e);
            this.ws.onclose = (e) => this.onWsClose(e);
            this.ws.onerror = (e) => this.onWsError(e);
        } catch (e) {
            logger.error('WebSocket creation failed', { error: e.message });
            this.updateStatus('Connection failed', 'error');
            connectionSteps.setError('ws');
            this.scheduleReconnect();
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

        // Request connection with configured codec
        const codec = this.codecType || serverUIConfig.webcodec || 'h264';
        logger.debug('Sending connect request', { codec });
        this.ws.send(JSON.stringify({
            type: 'connect',
            codec: codec
        }));
    }

    onWsMessage(event) {
        let msg;
        try {
            msg = JSON.parse(event.data);
        } catch (e) {
            logger.error('Failed to parse message', { data: event.data });
            return;
        }

        logger.debug(`Received: ${msg.type}`, msg.type === 'offer' ? { sdpLength: msg.sdp?.length } : null);

        this.handleSignaling(msg);
    }

    onWsClose(event) {
        logger.warn('WebSocket closed', { code: event.code, reason: event.reason });
        this.updateWebRTCState('ws', 'Closed');
        this.connected = false;
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
                        // Reinitialize decoder for server's codec
                        this.initDecoder();
                    }

                    // Update codec selector UI
                    const codecSelect = document.getElementById('codec-select');
                    if (codecSelect) {
                        codecSelect.value = msg.codec;
                        codecSelect.disabled = false;
                    }
                    updateCodecIndicator(this.codecType);

                }
                if (debugConfig.debug_connection) {
                    logger.info('Server acknowledged connection', { codec: msg.codec, peer_id: msg.peer_id });
                }
                this.updateOverlayStatus('Waiting for video offer...');
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
        this.pc.ondatachannel = (e) => this.onDataChannel(e);
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

        // Remove disconnected visual state
        const displayContainer = document.getElementById('display-container');
        if (displayContainer) {
            displayContainer.classList.remove('disconnected');
        }

        logger.info('Stream is playing');
    }

    onDataChannel(event) {
        if (debugConfig.debug_connection) {
            logger.info('Data channel received', { label: event.channel.label });
        }
        this.dataChannel = event.channel;
        this.setupDataChannel();
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
            logger.error('ICE connection failed - may need TURN server');
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
        if (this.dataChannel) {
            this.dataChannel = null;
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

    setupDataChannel() {
        if (!this.dataChannel) return;

        // Set binary type for receiving PNG frames
        this.dataChannel.binaryType = 'arraybuffer';

        this.dataChannel.onopen = () => {
            if (debugConfig.debug_connection) {
                logger.info('Data channel open');
            }
            this.updateWebRTCState('dc', 'Open');
            this.setupInputHandlers();

            // Send initial mouse mode to emulator
            this.sendMouseModeChange(this.mouseMode);

        };

        this.dataChannel.onclose = () => {
            logger.warn('Data channel closed');
            this.updateWebRTCState('dc', 'Closed');
        };

        this.dataChannel.onerror = (e) => {
            logger.error('Data channel error');
            this.updateWebRTCState('dc', 'Error');
        };

        // Handle incoming messages (frames for PNG, or other messages)
        this.dataChannel.onmessage = (event) => {
            if (event.data instanceof ArrayBuffer) {
                // Check if this is a frame metadata message for H.264/VP9
                // Format: [cursor_x:2][cursor_y:2][cursor_visible:1]
                if (event.data.byteLength === CONSTANTS.H264_METADATA_SIZE) {
                    const view = new DataView(event.data);
                    this.handleFrameMetadata(view);
                    return;
                }

                // Binary data - this is a video frame for DataChannel codecs (PNG/WEBP)
                const usesVideoTrack = (this.codecType === CodecType.H264 || this.codecType === CodecType.VP9);
                if (this.decoder && !usesVideoTrack) {
                    this.decoder.handleData(event.data);

                    // Track PNG frame stats
                    this.pngStats.framesReceived++;
                    this.pngStats.bytesReceived += event.data.byteLength;
                    this.pngStats.lastFrameTime = performance.now();
                    this.pngStats.avgFrameSize = this.pngStats.bytesReceived / this.pngStats.framesReceived;

                    // Update first frame received flag
                    if (!this.firstFrameReceived) {
                        this.firstFrameReceived = true;
                        connectionSteps.setDone('frames');
                        if (debugConfig.debug_connection) {
                            logger.info('First frame received via DataChannel');
                        }

                        // For PNG codec, mark as connected and hide overlay
                        this.connected = true;
                        this.updateStatus('Connected', 'connected');
                        this.hideOverlay();
                        this.updateConnectionUI(true);

                        // Remove disconnected visual state
                        const displayContainer = document.getElementById('display-container');
                        if (displayContainer) {
                            displayContainer.classList.remove('disconnected');
                        }
                    }
                }
            } else {
                // Text data - might be control messages
                try {
                    const msg = JSON.parse(event.data);
                    if (msg.type === 'capture') {
                        logger.info('[Capture] Triggered by server!');
                        this.startAudioCapture();
                    } else {
                        logger.debug('DataChannel text message', { data: event.data });
                    }
                } catch (e) {
                    logger.debug('DataChannel text message (not JSON)', { data: event.data });
                }
            }
        };
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

        // Keyboard - binary protocol for minimal latency
        // Remove previous listeners to avoid duplicates on reconnect
        if (this._keydownHandler) document.removeEventListener('keydown', this._keydownHandler);
        if (this._keyupHandler) document.removeEventListener('keyup', this._keyupHandler);

        this._keydownHandler = (e) => {
            if (!this.connected) return;
            if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
            e.preventDefault();
            this.sendKey(e.keyCode, true, performance.now());
        };
        this._keyupHandler = (e) => {
            if (!this.connected) return;
            if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
            e.preventDefault();
            this.sendKey(e.keyCode, false, performance.now());
        };

        document.addEventListener('keydown', this._keydownHandler);
        document.addEventListener('keyup', this._keyupHandler);

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

    sendMouseMove(dx, dy, timestamp) {
        if (!this.dataChannel || this.dataChannel.readyState !== 'open') return;
        const buffer = new ArrayBuffer(1 + 2 + 2 + 8);
        const view = new DataView(buffer);
        view.setUint8(0, 1);  // type: mouse move (relative)
        view.setInt16(1, dx, true);  // little-endian
        view.setInt16(3, dy, true);
        view.setFloat64(5, timestamp, true);
        this.dataChannel.send(buffer);
    }

    sendMouseAbsolute(x, y, timestamp) {
        if (!this.dataChannel || this.dataChannel.readyState !== 'open') return;
        const buffer = new ArrayBuffer(1 + 2 + 2 + 8);
        const view = new DataView(buffer);
        view.setUint8(0, 5);  // type: mouse move (absolute)
        view.setUint16(1, x, true);  // absolute X coordinate
        view.setUint16(3, y, true);  // absolute Y coordinate
        view.setFloat64(5, timestamp, true);
        this.dataChannel.send(buffer);
    }

    sendMouseButton(button, down, timestamp) {
        if (!this.dataChannel || this.dataChannel.readyState !== 'open') return;
        const buffer = new ArrayBuffer(1 + 1 + 1 + 8);
        const view = new DataView(buffer);
        view.setUint8(0, 2);  // type: mouse button
        view.setUint8(1, button);
        view.setUint8(2, down ? 1 : 0);
        view.setFloat64(3, timestamp, true);
        this.dataChannel.send(buffer);
    }

    sendKey(keycode, down, timestamp) {
        if (!this.dataChannel || this.dataChannel.readyState !== 'open') return;
        const buffer = new ArrayBuffer(1 + 2 + 1 + 8);
        const view = new DataView(buffer);
        view.setUint8(0, 3);  // type: key
        view.setUint16(1, keycode, true);
        view.setUint8(3, down ? 1 : 0);
        view.setFloat64(4, timestamp, true);
        this.dataChannel.send(buffer);
    }

    // Send raw text message (legacy text protocol - fallback)
    sendRaw(msg) {
        if (this.dataChannel && this.dataChannel.readyState === 'open') {
            this.dataChannel.send(msg);
        }
    }

    // Legacy JSON method (kept for restart/shutdown commands)
    sendInput(msg) {
        if (this.dataChannel && this.dataChannel.readyState === 'open') {
            this.dataChannel.send(JSON.stringify(msg));
        }
    }

    // Send mouse mode change to server (type=6, mode: 0=absolute, 1=relative)
    sendMouseModeChange(mode) {
        if (!this.dataChannel || this.dataChannel.readyState !== 'open') return;
        const buffer = new ArrayBuffer(1 + 1);
        const view = new DataView(buffer);
        view.setUint8(0, 6);  // type: mouse mode change
        view.setUint8(1, mode === 'relative' ? 1 : 0);  // 0=absolute, 1=relative
        this.dataChannel.send(buffer);

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
        if (this.dataChannel) {
            this.dataChannel.close();
            this.dataChannel = null;
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

        // For PNG codec, calculate stats from our own tracking
        const usesVideoTrack = (this.codecType === CodecType.H264 || this.codecType === CodecType.AV1 || this.codecType === CodecType.VP9);
        if (!usesVideoTrack) {
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
    currentConfig: {
        emulator: 'basilisk',  // Default, will be overwritten by loadCurrentConfig()
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

// Build WebSocket URL for signaling server
// Can be overridden via:
//   - URL param: ?ws=wss://example.com/path
//   - <meta name="ws-url" content="wss://example.com/path">
// Default: uses signaling_port from server config (fetched before init)
function getWebSocketUrl() {
    // Check URL parameter first
    const urlParams = new URLSearchParams(window.location.search);
    const wsParam = urlParams.get('ws');
    if (wsParam) return wsParam;

    // Check meta tag
    const wsMeta = document.querySelector('meta[name="ws-url"]');
    if (wsMeta?.content) return wsMeta.content;

    // Use signaling_port from server config, fall back to HTTP port + 1
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const hostname = window.location.hostname;
    const signalingPort = debugConfig.signaling_port
        || (parseInt(window.location.port, 10) + 1)
        || 8090;
    return `${protocol}//${hostname}:${signalingPort}/`;
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

    // Note: Codec is determined by server (from prefs file webcodec setting)
    // Client will receive codec in "connected" message and initialize decoder then

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

// Known ROM database with checksums and recommendations
const ROM_DATABASE = {
    // ========================================
    // 68k ROMs (BasiliskII)
    // ========================================

    // ⭐ RECOMMENDED 68k ROMs
    '420dbff3': { name: 'Quadra 700', model: 22, recommended: true, arch: 'm68k' },
    '3dc27823': { name: 'Quadra 900', model: 14, recommended: true, arch: 'm68k' },
    '368cadfe': { name: 'Mac IIci', model: 11, recommended: true, arch: 'm68k' },

    // ========================================
    // PPC ROMs (SheepShaver)
    // ========================================

    // ⭐ RECOMMENDED PPC ROMs
    '960e4be9': { name: 'Power Mac 9600', model: 14, recommended: true, arch: 'ppc' },
    'be65e1c4f04a3f2881d6e8de47d66454': { name: 'Mac OS ROM 1.6', model: 14, recommended: true, arch: 'ppc' },
    'bf9f186ba2dcaaa0bc2b9762a4bf0c4a': { name: 'Mac OS 9.0.4 installed on iMac (2000)', model: 14, recommended: true, arch: 'ppc' },

    // Other PPC ROMs
    '4c4f5744': { name: 'PowerBook G3', model: 14, recommended: false, arch: 'ppc' },
};

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

async function openConfig() {
    const modal = document.getElementById('config-modal');
    if (modal) {
        modal.classList.add('open');
        storageCache = null; // Clear cache to refresh
        setConfigControlsEnabled(false);
        // Load config and storage lists in parallel, then apply selections
        await Promise.all([loadCurrentConfig(), loadRomList(), loadDiskList(), loadCdromList(), loadExtfsList()]);
        updateConfigUI();
        setConfigControlsEnabled(true);
    }
}

function closeConfig() {
    const modal = document.getElementById('config-modal');
    if (modal) {
        modal.classList.remove('open');
    }
}

function toggleAdvanced() {
    const toggle = document.querySelector('.advanced-toggle');
    const content = document.getElementById('advanced-settings');
    if (toggle && content) {
        toggle.classList.toggle('open');
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
            // Determine current emulator architecture
            const currentArch = (currentConfig.emulator === 'sheepshaver') ? 'ppc' : 'm68k';

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

                if (info?.recommended && info.arch === currentArch) {
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

            // Auto-select first recommended ROM if none selected
            if (!currentConfig.rom && recommendedRoms.length > 0) {
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
    } catch (e) {
        container.innerHTML = '<div class="empty-state">Failed to load disks</div>';
        logger.error('Failed to load disk list', { error: e.message });
    }
}

function updateDiskSelection() {
    const checkboxes = document.querySelectorAll('#disk-list input[type="checkbox"]:checked');
    currentConfig.disks = Array.from(checkboxes).map(cb => cb.value);
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
            <button type="button" style="font-size:10px;padding:1px 6px" onclick="removeExtfsPath(${idx})">Remove</button>
        </div>`).join('');
}

function removeExtfsPath(idx) {
    if (currentConfig.extfs) {
        currentConfig.extfs.splice(idx, 1);
        renderExtfsList();
    }
}

// Helper to show/hide emulator-specific settings panels (no side effects)
function updateEmulatorPanelVisibility() {
    const emulatorType = document.getElementById('cfg-emulator')?.value;
    if (!emulatorType) return;

    const basiliskSettings = document.getElementById('basilisk-settings');
    const sheepshaverSettings = document.getElementById('sheepshaver-settings');

    if (emulatorType === 'sheepshaver') {
        if (basiliskSettings) basiliskSettings.style.display = 'none';
        if (sheepshaverSettings) sheepshaverSettings.style.display = 'block';
    } else {
        if (basiliskSettings) basiliskSettings.style.display = 'block';
        if (sheepshaverSettings) sheepshaverSettings.style.display = 'none';
    }

    // Update processor logo based on emulator type
    const processorLogo = document.getElementById('processor-logo');
    if (processorLogo) {
        processorLogo.src = emulatorType === 'sheepshaver' ? 'PowerPC.svg' : 'Motorola.svg';
        processorLogo.alt = emulatorType === 'sheepshaver' ? 'PowerPC' : 'Motorola';
    }

    // Update title with current model name
    updateHeaderTitle();
}

// Called when user changes emulator dropdown
async function onEmulatorChange() {
    const emulatorType = document.getElementById('cfg-emulator')?.value;
    if (!emulatorType) return;

    // Show/hide appropriate settings
    updateEmulatorPanelVisibility();

    // Update current config emulator type
    currentConfig.emulator = emulatorType;

    // Reload ROM list to show only compatible ROMs
    await loadRomList();

    console.log('🔄 SWITCHED EMULATOR:', {
        emulator: emulatorType,
        cpu: currentConfig.cpu,
        jit: currentConfig.jit
    });
}

function onRomChange() {
    const romName = document.getElementById('cfg-rom')?.value;
    if (!romName || !storageCache?.roms) return;

    // Find ROM in storage cache
    const rom = storageCache.roms.find(r => r.name === romName);
    if (!rom) return;

    // Look up ROM info and auto-set model if known
    const info = getRomInfo(rom.checksum, rom.md5);

    // ONLY set modelid for 68k ROMs (BasiliskII)
    // PPC/SheepShaver always uses model 14 (hardcoded, no user selection)
    if (info?.model && info.arch === 'm68k') {
        const modelSelect = document.getElementById('cfg-model');
        if (modelSelect) {
            modelSelect.value = info.model;
            currentConfig.model = info.model;
            console.log(`Auto-set model ID to ${info.model} for ${info.name}`);
        }
    }

    // Update header title to show model name
    updateHeaderTitle();
}

async function loadCurrentConfig() {
    try {
        const res = await fetch(getApiUrl('config'));
        const cfg = await res.json();

        // Flat format from server
        const isM68k = (cfg.architecture || 'm68k') === 'm68k';

        // Strip storage_dir prefix from paths to get relative names matching storage scan
        // Only strip if the path is absolute (starts with /), otherwise it's already relative
        const stripPrefix = (p, dir) => {
            if (!p || p[0] !== '/') return p;
            const idx = p.indexOf(dir);
            return idx >= 0 ? p.substring(idx + dir.length) : p;
        };

        currentConfig = {
            emulator: isM68k ? 'basilisk' : 'sheepshaver',
            rom: cfg.rom ? stripPrefix(cfg.rom, '/roms/') : '',
            ram: cfg.ram_mb || 32,
            screen: cfg.screen || '640x480',
            sound: cfg.audio ?? true,
            cpu: isM68k ? (cfg.m68k?.cpu_type || 4) : (cfg.ppc?.cpu_type || 4),
            model: isM68k ? (cfg.m68k?.modelid || 14) : (cfg.ppc?.modelid || 14),
            fpu: isM68k ? (cfg.m68k?.fpu ?? true) : (cfg.ppc?.fpu ?? true),
            jit: isM68k ? (cfg.m68k?.jit ?? true) : (cfg.ppc?.jit ?? true),
            jit68k: cfg.ppc?.jit68k ?? false,
            bootdriver: cfg.bootdriver || 0,
            disks: (cfg.disks || []).map(p => stripPrefix(p, '/images/')),
            cdroms: (cfg.cdroms || []).map(p => stripPrefix(p, '/images/')),
            extfs: cfg.extfs || [],
            idlewait: isM68k ? (cfg.m68k?.idlewait ?? true) : (cfg.ppc?.idlewait ?? true),
            ignoresegv: isM68k ? (cfg.m68k?.ignoresegv ?? true) : (cfg.ppc?.ignoresegv ?? true),
            ignoreillegal: cfg.ppc?.ignoreillegal ?? false,
            swap_opt_cmd: cfg.m68k?.swap_opt_cmd ?? true,
            keyboardtype: isM68k ? (cfg.m68k?.keyboardtype || 5) : (cfg.ppc?.keyboardtype || 5),
            zappram: cfg.zappram ?? false,
            dismiss_shutdown_dialog: cfg.dismiss_shutdown_dialog ?? false,
            network: cfg.network || 'none',
            network_if: cfg.network_if || ''
        };
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

    if (emulatorEl) emulatorEl.value = currentConfig.emulator || 'basilisk';
    if (romEl) romEl.value = currentConfig.rom;
    if (ramEl) ramEl.value = currentConfig.ram;
    if (screenEl) screenEl.value = currentConfig.screen;
    if (soundEl) soundEl.checked = currentConfig.sound;
    if (zappramEl) zappramEl.checked = currentConfig.zappram;
    if (dismissDialogEl) dismissDialogEl.checked = currentConfig.dismiss_shutdown_dialog;

    const networkEl = document.getElementById('cfg-network');
    const networkIfEl = document.getElementById('cfg-network-if');
    const networkIfGroup = document.getElementById('cfg-network-if-group');
    if (networkEl) {
        networkEl.value = currentConfig.network || 'none';
        networkEl.addEventListener('change', () => {
            if (networkIfGroup) networkIfGroup.style.display = networkEl.value === 'raw' ? '' : 'none';
        });
    }
    if (networkIfEl) networkIfEl.value = currentConfig.network_if || '';
    if (networkIfGroup) networkIfGroup.style.display = (currentConfig.network === 'raw') ? '' : 'none';

    const bootdriverEl = document.getElementById('cfg-bootdriver');
    if (bootdriverEl) bootdriverEl.value = currentConfig.bootdriver || 0;

    // Basilisk II specific
    const cpuEl = document.getElementById('cfg-cpu');
    const modelEl = document.getElementById('cfg-model');
    const fpuEl = document.getElementById('cfg-fpu');
    const jitEl = document.getElementById('cfg-jit');
    const idlewaitB2El = document.getElementById('cfg-idlewait-b2');
    const ignoresegvEl = document.getElementById('cfg-ignoresegv');

    if (cpuEl) cpuEl.value = currentConfig.cpu;
    if (modelEl) modelEl.value = currentConfig.model;
    if (fpuEl) fpuEl.checked = currentConfig.fpu;
    if (jitEl) jitEl.checked = currentConfig.jit;
    if (idlewaitB2El) idlewaitB2El.checked = currentConfig.idlewait ?? true;
    if (ignoresegvEl) ignoresegvEl.checked = currentConfig.ignoresegv ?? true;

    // SheepShaver specific
    const fpuSSEl = document.getElementById('cfg-fpu-ss');
    const jitSSEl = document.getElementById('cfg-jit-ss');
    const jit68kEl = document.getElementById('cfg-jit68k');
    const idlewaitEl = document.getElementById('cfg-idlewait');
    const ignoresegvSSEl = document.getElementById('cfg-ignoresegv-ss');
    const ignoreillegalEl = document.getElementById('cfg-ignoreillegal');

    if (fpuSSEl) fpuSSEl.checked = currentConfig.fpu ?? true;
    if (jitSSEl) jitSSEl.checked = currentConfig.jit ?? true;
    if (jit68kEl) jit68kEl.checked = currentConfig.jit68k ?? true;
    if (idlewaitEl) idlewaitEl.checked = currentConfig.idlewait ?? true;
    if (ignoresegvSSEl) ignoresegvSSEl.checked = currentConfig.ignoresegv ?? true;
    if (ignoreillegalEl) ignoreillegalEl.checked = currentConfig.ignoreillegal ?? true;

    // Show/hide appropriate settings (without triggering reload)
    updateEmulatorPanelVisibility();

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

    // Update header title with model name
    updateHeaderTitle();
}

async function saveConfig() {
    // Gather common values
    currentConfig.emulator = document.getElementById('cfg-emulator')?.value || 'basilisk';

    // Only update ROM if dropdown has a value (preserve existing if dropdown not populated)
    const romDropdown = document.getElementById('cfg-rom');
    if (romDropdown && romDropdown.value) {
        currentConfig.rom = romDropdown.value;
    }
    // If dropdown is empty/null, keep currentConfig.rom as-is (from loadCurrentConfig)

    currentConfig.ram = parseInt(document.getElementById('cfg-ram')?.value || 32);
    currentConfig.screen = document.getElementById('cfg-screen')?.value || '800x600';
    currentConfig.sound = document.getElementById('cfg-sound')?.checked ?? true;
    currentConfig.zappram = document.getElementById('cfg-zappram')?.checked ?? false;
    currentConfig.dismiss_shutdown_dialog = document.getElementById('cfg-dismiss-shutdown-dialog')?.checked ?? false;
    currentConfig.bootdriver = parseInt(document.getElementById('cfg-bootdriver')?.value || 0);

    // Gather emulator-specific values
    if (currentConfig.emulator === 'basilisk') {
        currentConfig.cpu = parseInt(document.getElementById('cfg-cpu')?.value || 4);
        currentConfig.model = parseInt(document.getElementById('cfg-model')?.value || 14);
        currentConfig.fpu = document.getElementById('cfg-fpu')?.checked ?? true;
        currentConfig.jit = document.getElementById('cfg-jit')?.checked ?? true;
        currentConfig.idlewait = document.getElementById('cfg-idlewait-b2')?.checked ?? true;
        currentConfig.ignoresegv = document.getElementById('cfg-ignoresegv')?.checked ?? true;
    } else {
        // SheepShaver
        currentConfig.cpu = 4;
        currentConfig.model = 14;
        currentConfig.fpu = document.getElementById('cfg-fpu-ss')?.checked ?? true;
        currentConfig.jit = document.getElementById('cfg-jit-ss')?.checked ?? true;
        currentConfig.jit68k = document.getElementById('cfg-jit68k')?.checked ?? true;
        currentConfig.idlewait = document.getElementById('cfg-idlewait')?.checked ?? true;
        currentConfig.ignoresegv = document.getElementById('cfg-ignoresegv-ss')?.checked ?? true;
        currentConfig.ignoreillegal = document.getElementById('cfg-ignoreillegal')?.checked ?? true;
    }

    // Build flat JSON config
    const isM68k = (currentConfig.emulator === 'basilisk');
    const archConfig = {
        cpu_type: currentConfig.cpu,
        modelid: currentConfig.model,
        fpu: currentConfig.fpu,
        jit: currentConfig.jit,
        idlewait: currentConfig.idlewait,
        ignoresegv: currentConfig.ignoresegv,
        swap_opt_cmd: currentConfig.swap_opt_cmd ?? true,
        keyboardtype: currentConfig.keyboardtype || 5
    };
    if (!isM68k) {
        archConfig.jit68k = currentConfig.jit68k ?? false;
        archConfig.ignoreillegal = currentConfig.ignoreillegal ?? false;
    }

    const jsonConfig = {
        architecture: isM68k ? 'm68k' : 'ppc',
        rom: currentConfig.rom,
        disks: currentConfig.disks,
        cdroms: currentConfig.cdroms || [],
        extfs: currentConfig.extfs || [],
        bootdriver: currentConfig.bootdriver || 0,
        ram_mb: currentConfig.ram,
        screen: currentConfig.screen,
        audio: currentConfig.sound,
        zappram: currentConfig.zappram,
        dismiss_shutdown_dialog: currentConfig.dismiss_shutdown_dialog,
        network: document.getElementById('cfg-network')?.value || 'none',
        network_if: document.getElementById('cfg-network-if')?.value || '',
        codec: document.getElementById('codec-select')?.value || 'png',
        mousemode: document.getElementById('mouse-mode-select')?.value || 'absolute',
        m68k: isM68k ? archConfig : undefined,
        ppc: isM68k ? undefined : archConfig
    };

    try {
        const res = await fetch(getApiUrl('config'), {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(jsonConfig)
        });
        const data = await res.json();

        if (data.success) {
            console.log('✅ CONFIG SAVED to macemu-config.json');
            closeConfig();
            // Don't auto-restart - user can restart manually if needed
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
    try {
        const res = await fetch(getApiUrl('emulator/restart'), { method: 'POST' });
        const data = await res.json();
        logger.info('Restart emulator', { message: data.message });
    } catch (e) {
        logger.error('Failed to restart emulator', { error: e.message });
    }
}

async function resetEmulator() {
    // Reset = Restart (stop + start) since MACEMU_CMD_RESET crashes SheepShaver
    logger.info('Resetting emulator...');
    await restartEmulator();
}

// Codec management
async function changeCodec() {
    const select = document.getElementById('codec-select');
    if (!select) return;

    const newCodec = select.value;
    logger.info('Changing codec', { codec: newCodec });

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

        // Update mouse latency stat (from emulator via server)
        const mouseLatencyEl = document.getElementById('stat-mouse-latency');
        if (mouseLatencyEl && data.mouse_latency_ms !== undefined) {
            if (data.mouse_latency_samples > 0) {
                mouseLatencyEl.textContent = data.mouse_latency_ms.toFixed(1) + ' ms';
            } else {
                mouseLatencyEl.textContent = '-- ms';
            }
        }

        // Update video latency stat
        const videoLatencyEl = document.getElementById('stat-video-latency');
        if (videoLatencyEl) {
            const avgLatency = client?.decoder?.getAverageLatency?.() || 0;
            videoLatencyEl.textContent = avgLatency > 0 ? avgLatency.toFixed(1) + ' ms' : '-- ms';
        }

        // Update RTT stat
        const rttEl = document.getElementById('stat-rtt');
        if (rttEl) {
            const avgRtt = client?.decoder?.getAverageRtt?.() || 0;
            rttEl.textContent = avgRtt > 0 ? avgRtt.toFixed(1) + ' ms' : '-- ms';
        }

    } catch (e) {
        // Silently fail status polling
    }
}

// Start status polling
setInterval(pollEmulatorStatus, CONSTANTS.STATUS_POLL_INTERVAL_MS);

// Setup event listeners for UI elements
function setupEventListeners() {
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

    const stopBtn = document.getElementById('stop-btn');
    if (stopBtn) stopBtn.addEventListener('click', stopEmulator);

    const debugToggle = document.getElementById('debug-toggle');
    if (debugToggle) debugToggle.addEventListener('click', toggleDebugPanel);

    const fullscreenBtn = document.getElementById('fullscreen-btn');
    if (fullscreenBtn) fullscreenBtn.addEventListener('click', toggleFullscreen);

    // Debug panel
    const clearLogBtn = document.getElementById('clear-log-btn');
    if (clearLogBtn) clearLogBtn.addEventListener('click', clearLog);

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

    const cfgEmulator = document.getElementById('cfg-emulator');
    if (cfgEmulator) cfgEmulator.addEventListener('change', onEmulatorChange);

    const cfgRom = document.getElementById('cfg-rom');
    if (cfgRom) cfgRom.addEventListener('change', onRomChange);

    const advancedToggle = document.getElementById('advanced-toggle');
    if (advancedToggle) advancedToggle.addEventListener('click', toggleAdvanced);
}

// Initialize on page load
window.addEventListener('DOMContentLoaded', async () => {
    await fetchConfig();  // Load debug config from server
    await loadCurrentConfig();  // Load emulator config from JSON
    updateEmulatorPanelVisibility();  // Update header logo/title based on loaded config
    setupEventListeners();  // Setup all event listeners
    initClient();
    pollEmulatorStatus();
});
