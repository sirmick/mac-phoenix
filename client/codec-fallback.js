// Pure state machine for codec selection + auto-fallback.
//
// Runs in the browser (attaches CodecFallbackController to window) and in
// node:test (CommonJS export at the bottom). No DOM, no fetch, no
// RTCPeerConnection — the host wires those up and feeds events in.
//
// Chain: at most 3 attempts, picking the best available within each tier.
//   tier 1 (UDP/RTP):   vp9 ?? h264
//   tier 2 (TCP/WS):    webp ?? png
//   tier 3 (long-poll): httpstream
//
// Failures within a tier are assumed to be transport problems, so the loser
// in a tier is skipped (e.g., if vp9 fails, we go to webp, not h264).

(function (root) {
    const TIERS = [
        { name: 'webrtc', codecs: ['vp9', 'h264'] },
        { name: 'ws',     codecs: ['webp', 'png'] },
        { name: 'http',   codecs: ['httpstream'] },
    ];

    function buildChain(availableCodecs) {
        const set = availableCodecs instanceof Set ? availableCodecs : new Set(availableCodecs);
        const chain = [];
        for (const tier of TIERS) {
            const pick = tier.codecs.find(c => set.has(c));
            if (pick) chain.push(pick);
        }
        return chain;
    }

    function tierOf(codec) {
        for (const t of TIERS) if (t.codecs.includes(codec)) return t.name;
        return null;
    }

    class CodecFallbackController {
        constructor(opts = {}) {
            this.now = opts.now || (() => Date.now());
            this.setTimeoutFn = opts.setTimeoutFn || ((fn, ms) => setTimeout(fn, ms));
            this.clearTimeoutFn = opts.clearTimeoutFn || (id => clearTimeout(id));
            this.noFramesMs = opts.noFramesMs != null ? opts.noFramesMs : 350;
            this.listeners = { selectCodec: [], uiState: [], debug: [] };
            this._reset();
        }

        on(event, fn) {
            if (!this.listeners[event]) throw new Error('unknown event: ' + event);
            this.listeners[event].push(fn);
        }

        _emit(event, payload) {
            for (const fn of this.listeners[event]) fn(payload);
        }

        _reset() {
            this.chain = [];
            this.chainIdx = -1;
            this.attemptStart = 0;
            this.firstFrameSeen = false;
            this.emulatorRunning = true;
            this._cancelTimer();
            this.phase = 'idle';
        }

        _cancelTimer() {
            if (this.timer != null) {
                this.clearTimeoutFn(this.timer);
                this.timer = null;
            }
        }

        _armNoFramesTimer() {
            this._cancelTimer();
            this.timer = this.setTimeoutFn(() => {
                this.timer = null;
                if (this.firstFrameSeen) return;
                const codec = this.chain[this.chainIdx];
                this._emit('debug', {
                    event: 'codec.no_frames_timeout',
                    codec,
                    elapsedMs: this.now() - this.attemptStart,
                });
                this._tryNext('no-frames-timeout');
            }, this.noFramesMs);
        }

        _setPhase(phase, extra) {
            this.phase = phase;
            const codec = this.chain[this.chainIdx] || null;
            const payload = Object.assign({ phase, codec }, extra || {});
            this._emit('uiState', payload);
        }

        _tryNext(reason) {
            this.chainIdx += 1;
            if (this.chainIdx >= this.chain.length) {
                this._cancelTimer();
                this._setPhase('exhausted', { reason: reason || 'chain-end' });
                this._emit('debug', { event: 'codec.exhausted', reason: reason || 'chain-end' });
                return null;
            }
            const codec = this.chain[this.chainIdx];
            this.attemptStart = this.now();
            this.firstFrameSeen = false;
            this._setPhase('connecting', { codec, reason: reason || null });
            this._emit('debug', { event: 'codec.attempt', codec, reason: reason || 'start' });
            this._emit('selectCodec', { codec, reason: reason || 'start' });
            // httpstream is terminal — no fallback timer (it's the last resort
            // and has its own long-poll retry semantics).
            if (tierOf(codec) !== 'http') {
                this._armNoFramesTimer();
            }
            return codec;
        }

        // Host calls this once per session (page load, or when the emulator
        // comes back up after being off).
        start(opts) {
            const availableCodecs = (opts && opts.availableCodecs) || [];
            const emulatorRunning = !opts || opts.emulatorRunning !== false;
            this._reset();
            this.emulatorRunning = emulatorRunning;
            this.chain = buildChain(availableCodecs);
            if (!this.emulatorRunning) {
                this._setPhase('idle-mac-off');
                return null;
            }
            if (this.chain.length === 0) {
                this._setPhase('exhausted', { reason: 'no-codecs-available' });
                return null;
            }
            return this._tryNext('start');
        }

        // ICE state from RTCPeerConnection. ICE-failed steps the chain even
        // from 'connected' phase — an established RTP session that drops to
        // ICE-failed is unrecoverable, exactly when fast-fallback should fire.
        // Guarded by tier so a phantom event on a non-RTP codec is a no-op.
        onIceState(state) {
            if (state !== 'failed') return;
            if (this.chainIdx < 0) return;
            const codec = this.chain[this.chainIdx];
            if (tierOf(codec) !== 'webrtc') return;
            this._cancelTimer();
            this._emit('debug', {
                event: 'codec.ice_failed',
                codec,
                elapsedMs: this.now() - this.attemptStart,
            });
            this._tryNext('ice-failed');
        }

        // WebSocket state (tier 2 + 3 transport).
        onWsState(state) {
            if (this.phase !== 'connecting' && this.phase !== 'connected') return;
            if (state === 'closed' || state === 'error') {
                // Only fall back if we're in a WS-tier codec; tier 1 has its own
                // signaling path and a WS hiccup there isn't fatal to RTP.
                const codec = this.chain[this.chainIdx];
                if (tierOf(codec) === 'ws' || tierOf(codec) === 'http') {
                    this._cancelTimer();
                    this._emit('debug', {
                        event: 'codec.ws_' + state,
                        codec,
                        elapsedMs: this.now() - this.attemptStart,
                    });
                    this._tryNext('ws-' + state);
                }
            }
        }

        // First decoded frame (RTP) or first frame bytes (WS/HTTP) seen.
        onFrame() {
            if (this.firstFrameSeen) return;
            this.firstFrameSeen = true;
            this._cancelTimer();
            this._emit('debug', {
                event: 'codec.first_frame',
                codec: this.chain[this.chainIdx],
                elapsedMs: this.now() - this.attemptStart,
            });
            this._setPhase('connected');
        }

        onEmulatorState(opts) {
            const running = !!(opts && opts.running);
            if (this.emulatorRunning === running) return;
            this.emulatorRunning = running;
            if (!running) {
                this._cancelTimer();
                this._setPhase('idle-mac-off');
                this.chainIdx = -1;
                this.firstFrameSeen = false;
            }
            // running=true after off does NOT auto-restart; host calls start()
            // when it has fresh availableCodecs from /api/codecs.
        }

        // Manual user pick. Rebuilds the chain rooted at the requested codec's
        // tier: higher tiers are skipped (user explicitly passed on them),
        // lower tiers stay as fallback. Within the requested tier, the user's
        // pick replaces the auto pick (e.g., user chose png even though webp
        // was the tier-2 winner). No persistence.
        onCodecChangeRequested(codec) {
            if (!this.emulatorRunning) return null;
            this._cancelTimer();
            const requestedTier = tierOf(codec);
            const startIdx = requestedTier
                ? TIERS.findIndex(t => t.name === requestedTier)
                : 0;
            const newChain = [codec];
            for (let i = startIdx + 1; i < TIERS.length; i++) {
                const original = this.chain.find(c => tierOf(c) === TIERS[i].name);
                if (original) newChain.push(original);
            }
            this.chain = newChain;
            this.chainIdx = -1;
            return this._tryNext('manual');
        }
    }

    const api = { CodecFallbackController, buildChain, tierOf, TIERS };

    if (typeof module !== 'undefined' && module.exports) {
        module.exports = api;
    }
    if (root) {
        root.CodecFallbackController = CodecFallbackController;
        root.buildCodecFallbackChain = buildChain;
    }
})(typeof window !== 'undefined' ? window : null);
