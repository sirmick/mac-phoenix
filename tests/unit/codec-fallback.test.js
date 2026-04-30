'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const { CodecFallbackController, buildChain } = require(
    path.join(__dirname, '..', '..', 'client', 'codec-fallback.js')
);

// Tiny fake clock + setTimeout/clearTimeout. tick(ms) advances time and fires
// any timers whose deadline has passed. Order matches scheduling order.
function makeFakeClock() {
    let nowMs = 0;
    let nextId = 1;
    const timers = new Map();

    return {
        now: () => nowMs,
        setTimeoutFn: (fn, ms) => {
            const id = nextId++;
            timers.set(id, { fireAt: nowMs + ms, fn });
            return id;
        },
        clearTimeoutFn: (id) => { timers.delete(id); },
        tick(ms) {
            const target = nowMs + ms;
            // Fire timers in scheduled order until we've reached target time.
            // Re-scan after each fire because callbacks may schedule new timers.
            // (Safe: we only fire timers whose fireAt <= target.)
            // eslint-disable-next-line no-constant-condition
            while (true) {
                let nextEntry = null;
                for (const [id, t] of timers) {
                    if (t.fireAt > target) continue;
                    if (!nextEntry || t.fireAt < nextEntry[1].fireAt) nextEntry = [id, t];
                }
                if (!nextEntry) break;
                const [id, t] = nextEntry;
                timers.delete(id);
                nowMs = t.fireAt;
                t.fn();
            }
            nowMs = target;
        },
        get pendingTimerCount() { return timers.size; },
    };
}

function makeRecorder() {
    const events = { selectCodec: [], uiState: [], debug: [] };
    return {
        events,
        wire(c) {
            c.on('selectCodec', e => events.selectCodec.push(e));
            c.on('uiState', e => events.uiState.push(e));
            c.on('debug', e => events.debug.push(e));
        },
        codecs: () => events.selectCodec.map(e => e.codec),
        phases: () => events.uiState.map(e => e.phase),
        debugEvents: () => events.debug.map(e => e.event),
    };
}

function makeController(clock, opts = {}) {
    return new CodecFallbackController({
        now: clock.now,
        setTimeoutFn: clock.setTimeoutFn,
        clearTimeoutFn: clock.clearTimeoutFn,
        noFramesMs: opts.noFramesMs != null ? opts.noFramesMs : 350,
    });
}

// ── chain building ──────────────────────────────────────────────────────

test('buildChain: full availability picks tier-best', () => {
    assert.deepEqual(
        buildChain(['vp9', 'h264', 'webp', 'png', 'httpstream']),
        ['vp9', 'webp', 'httpstream'],
    );
});

test('buildChain: vp9 unavailable falls to h264 in tier 1', () => {
    assert.deepEqual(
        buildChain(['h264', 'webp', 'httpstream']),
        ['h264', 'webp', 'httpstream'],
    );
});

test('buildChain: webp unavailable falls to png in tier 2', () => {
    assert.deepEqual(
        buildChain(['vp9', 'png', 'httpstream']),
        ['vp9', 'png', 'httpstream'],
    );
});

test('buildChain: only httpstream available', () => {
    assert.deepEqual(buildChain(['httpstream']), ['httpstream']);
});

test('buildChain: empty input yields empty chain', () => {
    assert.deepEqual(buildChain([]), []);
});

// ── happy path ──────────────────────────────────────────────────────────

test('happy path: vp9 connects, frame arrives, no fallback fires', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    const first = c.start({ availableCodecs: ['vp9', 'h264', 'webp', 'png', 'httpstream'] });
    assert.equal(first, 'vp9');

    c.onIceState('connected');
    clock.tick(50);
    c.onFrame();

    assert.deepEqual(r.codecs(), ['vp9']);
    assert.equal(c.phase, 'connected');
    assert.deepEqual(r.debugEvents(), ['codec.attempt', 'codec.first_frame']);

    // No leftover timer
    assert.equal(clock.pendingTimerCount, 0);

    // Time passes — no spurious fallback
    clock.tick(2000);
    assert.deepEqual(r.codecs(), ['vp9']);
});

// ── ICE fast-fail ───────────────────────────────────────────────────────

test('ICE fast-fail: vp9 fails immediately, jumps to webp (tier 2)', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    c.start({ availableCodecs: ['vp9', 'h264', 'webp', 'png', 'httpstream'] });
    clock.tick(80);
    c.onIceState('failed');

    assert.deepEqual(r.codecs(), ['vp9', 'webp']);
    const iceFail = r.events.debug.find(e => e.event === 'codec.ice_failed');
    assert.ok(iceFail, 'ice_failed debug event emitted');
    assert.equal(iceFail.codec, 'vp9');
    assert.equal(iceFail.elapsedMs, 80);
});

test('ICE fast-fail from connected phase: established session that drops steps chain', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    // Get to a stable vp9 connection
    c.start({ availableCodecs: ['vp9', 'webp', 'httpstream'] });
    c.onIceState('connected');
    c.onFrame();
    assert.equal(c.phase, 'connected');

    // Network drops, ICE goes to failed — must still step the chain
    clock.tick(2000);
    c.onIceState('failed');
    assert.deepEqual(r.codecs(), ['vp9', 'webp']);
});

test('ICE-failed on non-webrtc codec: ignored (httpstream has no ICE)', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    c.start({ availableCodecs: ['httpstream'] });
    c.onIceState('failed');  // bogus event for httpstream
    assert.deepEqual(r.codecs(), ['httpstream'], 'no spurious step');
});

test('ICE fast-fail: skips h264 — does not retry within tier 1', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    c.start({ availableCodecs: ['vp9', 'h264', 'webp', 'png', 'httpstream'] });
    c.onIceState('failed');
    assert.equal(r.codecs()[1], 'webp', 'jumps to ws tier, not h264');
    assert.ok(!r.codecs().includes('h264'), 'h264 never selected');
});

// ── no-frames timeout ───────────────────────────────────────────────────

test('no-frames timeout: vp9 connects but no frame, falls to webp at noFramesMs', () => {
    const clock = makeFakeClock();
    const c = makeController(clock, { noFramesMs: 350 });
    const r = makeRecorder();
    r.wire(c);

    c.start({ availableCodecs: ['vp9', 'webp', 'httpstream'] });
    c.onIceState('connected');

    clock.tick(349);
    assert.deepEqual(r.codecs(), ['vp9'], 'still on vp9 just before deadline');

    clock.tick(2);
    assert.deepEqual(r.codecs(), ['vp9', 'webp']);
    const timeoutEv = r.events.debug.find(e => e.event === 'codec.no_frames_timeout');
    assert.ok(timeoutEv);
    assert.equal(timeoutEv.codec, 'vp9');
});

// ── full chain exhaustion within budget ─────────────────────────────────

test('budget: vp9 → webp → httpstream within 1000ms when all hang', () => {
    const clock = makeFakeClock();
    const c = makeController(clock, { noFramesMs: 350 });
    const r = makeRecorder();
    r.wire(c);

    const startAt = clock.now();
    c.start({ availableCodecs: ['vp9', 'webp', 'httpstream'] });

    // vp9 hangs → noFramesMs trips → webp
    clock.tick(351);
    // webp hangs → noFramesMs trips → httpstream
    clock.tick(351);

    const elapsed = clock.now() - startAt;
    assert.ok(elapsed < 1000, `chain walked in ${elapsed}ms (must be <1000)`);
    assert.deepEqual(r.codecs(), ['vp9', 'webp', 'httpstream']);

    // httpstream is terminal — no further fallback timer should be running
    assert.equal(clock.pendingTimerCount, 0);
});

// ── single-tier scenarios ───────────────────────────────────────────────

test('only httpstream available: starts there, no fallback timer armed', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    const first = c.start({ availableCodecs: ['httpstream'] });
    assert.equal(first, 'httpstream');
    assert.equal(clock.pendingTimerCount, 0, 'httpstream is terminal — no timer');

    clock.tick(5000);
    assert.deepEqual(r.codecs(), ['httpstream']);
});

test('h264 only (no vp9) starts on h264', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    c.start({ availableCodecs: ['h264', 'png', 'httpstream'] });
    assert.deepEqual(r.codecs(), ['h264']);
});

// ── empty / unavailable ─────────────────────────────────────────────────

test('no codecs available: phase = exhausted, no codec emitted', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    const first = c.start({ availableCodecs: [] });
    assert.equal(first, null);
    assert.equal(c.phase, 'exhausted');
    assert.deepEqual(r.codecs(), []);
});

// ── emulator state ──────────────────────────────────────────────────────

test('emulator off at start: phase = idle-mac-off, no chain runs', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    const first = c.start({
        availableCodecs: ['vp9', 'webp', 'httpstream'],
        emulatorRunning: false,
    });
    assert.equal(first, null);
    assert.equal(c.phase, 'idle-mac-off');
    assert.deepEqual(r.codecs(), []);
    assert.equal(clock.pendingTimerCount, 0);
});

test('emulator goes off mid-connect: cancels timer, phase = idle-mac-off', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    c.start({ availableCodecs: ['vp9', 'webp', 'httpstream'] });
    assert.equal(clock.pendingTimerCount, 1, 'noFrames timer armed');

    c.onEmulatorState({ running: false });
    assert.equal(c.phase, 'idle-mac-off');
    assert.equal(clock.pendingTimerCount, 0, 'timer cancelled when mac goes off');

    // No spurious fallbacks once mac is off
    clock.tick(5000);
    assert.deepEqual(r.codecs(), ['vp9'], 'no further selectCodec emitted');
});

// ── manual override ─────────────────────────────────────────────────────

test('manual pick: user downgrades vp9 → png, fallback continues to httpstream', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    c.start({ availableCodecs: ['vp9', 'webp', 'png', 'httpstream'] });
    // chain is [vp9, webp, httpstream]; user picks png which isn't in auto chain
    c.onCodecChangeRequested('png');

    assert.equal(r.codecs()[1], 'png', 'manual pick selected');
    assert.equal(r.events.selectCodec[1].reason, 'manual');

    // png hangs → falls through to next in chain (httpstream)
    clock.tick(351);
    assert.deepEqual(r.codecs().slice(-1), ['httpstream']);
});

test('manual pick: upgrade httpstream → vp9 (user wants better codec)', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    // Chain naturally walks to httpstream
    c.start({ availableCodecs: ['vp9', 'webp', 'httpstream'] });
    clock.tick(351); clock.tick(351);
    assert.equal(r.codecs().slice(-1)[0], 'httpstream');

    // User upgrades back to vp9
    c.onCodecChangeRequested('vp9');
    assert.equal(r.codecs().slice(-1)[0], 'vp9');
});

test('manual pick while mac off: no-op', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    c.start({ availableCodecs: ['vp9', 'webp', 'httpstream'], emulatorRunning: false });
    const result = c.onCodecChangeRequested('webp');
    assert.equal(result, null);
    assert.deepEqual(r.codecs(), []);
});

// ── WS transport health ─────────────────────────────────────────────────

test('WS close on webp: falls to httpstream', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    c.start({ availableCodecs: ['webp', 'httpstream'] });
    assert.equal(r.codecs()[0], 'webp');

    c.onWsState('closed');
    assert.deepEqual(r.codecs(), ['webp', 'httpstream']);
    assert.ok(r.events.debug.some(e => e.event === 'codec.ws_closed'));
});

test('WS close on vp9 (tier 1): ignored — does not fall back', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    c.start({ availableCodecs: ['vp9', 'webp', 'httpstream'] });
    c.onWsState('closed');
    assert.deepEqual(r.codecs(), ['vp9'], 'tier 1 immune to WS close (RTP rides UDP)');
});

// ── debug timing payload ────────────────────────────────────────────────

test('debug events include elapsedMs measured from attempt start', () => {
    const clock = makeFakeClock();
    const c = makeController(clock);
    const r = makeRecorder();
    r.wire(c);

    c.start({ availableCodecs: ['vp9', 'webp', 'httpstream'] });
    clock.tick(120);
    c.onFrame();

    const ff = r.events.debug.find(e => e.event === 'codec.first_frame');
    assert.equal(ff.elapsedMs, 120);
    assert.equal(ff.codec, 'vp9');
});
