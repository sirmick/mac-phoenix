// The Worker's side of the server's store (tools/standalone, /api/sync):
// when the tab is served by it (main.js asks /api/me), the server is the
// only store. Sync has the requests: an overlay's blocks as records
// (block number, uint32 LE, then the 4 KB block), uploaded on the version
// they are on (409, a SyncConflict, when the server has another); a
// volume's manifest and the files by SHA-256. Store (below) uses them.
'use strict';

const SYNC_BATCH = 256;                  // blocks a request (1 MB)
const SYNC_RECORD = 4 + OVERLAY_BLOCK;

class SyncConflict {
    constructor(body) { Object.assign(this, body); }
}

const Sync = {
    async request(method, path, body, { raw = false } = {}) {
        const res = await fetch(`api/sync/${path}`, {
            method, cache: 'no-store', credentials: 'same-origin', body,
            headers: body && !(body instanceof Uint8Array) ? { 'Content-Type': 'application/json' } : {},
        });
        if (res.status === 409)
            throw new SyncConflict(await res.json());
        if (!res.ok)
            throw new Error(`${method} ${path}: ${res.status}`);
        return raw ? res : res.status === 204 ? null : res.json();
    },

    // ---- overlays ----
    async fetchBlocks(ov, since) {
        const res = await this.request('GET', `overlays/${ov.key}?since=${since}`, undefined, { raw: true });
        return { version: Number(res.headers.get('X-Sync-Version')),
                 reset: res.headers.get('X-Sync-Reset') === '1',
                 body: new Uint8Array(await res.arrayBuffer()) };
    },

    apply(ov, body) {
        for (let i = 0; i + SYNC_RECORD <= body.length; i += SYNC_RECORD) {
            const block = new DataView(body.buffer, i, 4).getUint32(0, true);
            if (block * OVERLAY_BLOCK < ov.size)
                ov.putBlock(block, body.subarray(i + 4, i + SYNC_RECORD));
        }
        if (body.length)
            console.log(`[web] ${ov.name}: ${body.length / SYNC_RECORD} blocks from the server`);
    },

    // The blocks' records: block number (uint32 LE), the block.
    records(ov, blocks) {
        const body = new Uint8Array(blocks.length * SYNC_RECORD);
        blocks.forEach((b, i) => {
            new DataView(body.buffer, i * SYNC_RECORD, 4).setUint32(0, b, true);
            body.set(ov.getBlock(b), i * SYNC_RECORD + 4);
        });
        return body;
    },

    // blocks as version base + 1: one request, or staged and committed.
    // A SyncConflict when base isn't the server's.
    async upload(ov, blocks, base, replace) {
        const token = blocks.length > SYNC_BATCH || replace
            ? Array.from(crypto.getRandomValues(new Uint8Array(12)), (b) => b.toString(16).padStart(2, '0')).join('')
            : null;
        let at = 0;
        const batch = () => {
            const part = blocks.slice(at, at + SYNC_BATCH);
            at += part.length;
            return this.records(ov, part);
        };
        while (blocks.length - at > SYNC_BATCH)
            await this.request('POST', `overlays/${ov.key}?stage=${token}`, batch());
        const q = `base=${base}${token ? `&stage=${token}` : ''}${replace ? '&replace=1' : ''}`
            + `&name=${encodeURIComponent(ov.name)}&size=${ov.size}`;
        return (await this.request('POST', `overlays/${ov.key}?${q}`, batch())).version;
    },

    // ---- volumes ----
    async sha256(bytes) {
        return this.hex(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)));
    },

    async digest(form) {
        return form ? this.sha256(new TextEncoder().encode(JSON.stringify(form))) : null;
    },

    // ---- bundles (tools/standalone volumes.go): records of a sha (32
    // bytes), a length (uint32 LE) and the bytes, ended by a zero sha and
    // length. The volume's starts with its JSON { version, manifest } (a
    // length, uint32 LE, then the text). ----
    hex(bytes) {
        return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
    },

    // shas -> bytes as one bundle, gzip-compressed when the browser can.
    async bundle(objects) {
        const parts = [];
        for (const [sha, bytes] of objects) {
            const head = new Uint8Array(36);
            head.set(sha.match(/../g).map((h) => parseInt(h, 16)));
            new DataView(head.buffer).setUint32(32, bytes.length, true);
            parts.push(head, bytes);
        }
        parts.push(new Uint8Array(36));
        const blob = new Blob(parts);
        if (typeof CompressionStream === 'undefined')
            return { body: new Uint8Array(await blob.arrayBuffer()), gzip: false };
        const z = blob.stream().pipeThrough(new CompressionStream('gzip'));
        return { body: new Uint8Array(await new Response(z).arrayBuffer()), gzip: true };
    },
};

// A bundle as it streams in: take(n) the next n bytes (an Error when it
// ends first).
class BundleReader {
    constructor(body) {
        this.reader = body.getReader();
        this.chunks = [];
        this.have = 0;
        this.bytes = 0;                  // read so far (decompressed)
    }

    async take(n) {
        if (!n)
            return new Uint8Array(0);
        while (this.have < n) {
            const { done, value } = await this.reader.read();
            if (done)
                throw new Error('the bundle was cut short');
            this.chunks.push(value);
            this.have += value.length;
        }
        this.have -= n;
        this.bytes += n;
        const first = this.chunks[0];
        if (first.length >= n) {
            const out = first.subarray(0, n);
            if (first.length === n)
                this.chunks.shift();
            else
                this.chunks[0] = first.subarray(n);
            return out;
        }
        const out = new Uint8Array(n);
        for (let at = 0; at < n;) {
            const c = this.chunks[0];
            const k = Math.min(c.length, n - at);
            out.set(c.subarray(0, k), at);
            at += k;
            if (k === c.length)
                this.chunks.shift();
            else
                this.chunks[0] = c.subarray(k);
        }
        return out;
    }

    async u32() {
        const b = await this.take(4);
        return new DataView(b.buffer, b.byteOffset, 4).getUint32(0, true);
    }

    // The next object: { sha, bytes }, or null at the end.
    async record() {
        const head = await this.take(36);
        const n = new DataView(head.buffer, head.byteOffset + 32, 4).getUint32(0, true);
        if (!n && head.subarray(0, 32).every((b) => !b))
            return null;
        const sha = Sync.hex(head.subarray(0, 32));
        return { sha, bytes: await this.take(n) };
    }

    cancel() {
        this.reader.cancel().catch(() => {});
    }
}

// Requests a time when a bundle can't be had (a server without them, one
// cut short): objects/SHA, this many at once.
const OBJECT_REQUESTS = 6;
const BUNDLE_UP = 8 << 20;              // bytes of objects a bundle up (before compression)

// fn on each item, at most `width` at once.
async function eachLimited(items, width, fn) {
    let next = 0;
    const run = async () => {
        while (next < items.length)
            await fn(items[next++]);
    };
    await Promise.all(Array.from({ length: Math.min(width, items.length) }, run));
}

// ---- standalone (the server says so: main.js): the server is the only
// store. No OPFS: an overlay is in memory (overlay.js), its blocks come
// from the server before Executor mounts the image and go back about
// every second (STORE_EVERY), at once when Executor flushes (FlushVol, a
// file closed, an eject: OVERLAYFS's fsync and close), and what is left
// when the page goes in a beacon the page sends (main.js; BEACON_BLOCKS
// at most). Executor's volume: restored from the server's manifest and
// objects, saved there the same way (worker.js). One tab is in control
// (main.js), so a refused upload (409) is a version this tab hasn't
// seen, an earlier session's beacon landing late: this tab's goes on top.
const STORE_EVERY = 1000;
const BEACON_BLOCKS = 14;               // records of 4 + 4 KB: a beacon's 64 KB

const Store = {
    enabled: false,
    round: null,                         // the round running (a promise)
    again: false,                        // asked for meanwhile: another after it
    volumeOff: false,                    // not restored: not saved either
    failing: false,                      // the last round failed (said once)
    volume: { key: '', version: 0, digest: null },
    flying: new Map(),                   // overlay -> its blocks on their way up

    async overlayDown(ov) {
        try {
            const got = await Sync.fetchBlocks(ov, 0);
            Sync.apply(ov, got.body);
            ov.pending.clear();
            ov.sync = { version: got.version };
        } catch (e) {
            // Its blocks on the server unknown: this tab's would land on them.
            ov.syncOff = true;
            notice(`${ov.name}: the server can't be reached: changes to it here are not kept`);
        }
    },

    async overlayUp(ov) {
        if (!ov.writable || ov.syncOff || !ov.pending.size)
            return;
        const taken = [...ov.pending];
        ov.pending.clear();                      // a write meanwhile: pending again
        this.flying.set(ov, taken);
        try {
            for (let attempt = 0; ; attempt++) {
                try {
                    ov.sync.version = await Sync.upload(ov, taken.filter((b) => ov.map.has(b)), ov.sync.version, false);
                    return;
                } catch (e) {
                    if (!(e instanceof SyncConflict) || attempt >= 2)
                        throw e;
                    ov.sync.version = e.version;
                }
            }
        } catch (e) {
            for (const b of taken)
                ov.pending.add(b);
            throw e;
        } finally {
            this.flying.delete(ov);
        }
    },

    // The volume's manifest (null: nothing kept), its files written into FS
    // (not before ready: the module's runtime; what comes before waits).
    // One request, the bundle, its objects written as they arrive; a
    // server without bundles, or one cut short: the files still missing
    // by objects/SHA, a few at once.
    async volumeDown(key, ready = Promise.resolve()) {
        this.volume = { key, version: 0, digest: null };
        const t0 = performance.now();
        let isReady = false;
        ready.then(() => { isReady = true; });
        const waiting = [];
        let manifest = null, bySha = null, bundled = 0;
        const write = (sha, bytes) => {
            for (const path of bySha.get(sha) || []) {
                const f = manifest.files[path];
                FS.writeFile(path, bytes);
                FS.utime(path, f.mtime, f.mtime);
            }
            bySha.delete(sha);
        };
        let begun = false;
        const begin = () => {
            if (!begun)
                for (const d of manifest.dirs)
                    FS.mkdirTree(d);
            begun = true;
            for (const [sha, bytes] of waiting.splice(0))
                write(sha, bytes);
        };
        const res = await fetch(`api/sync/volumes/${key}/bundle`, { cache: 'no-store', credentials: 'same-origin' });
        let r = null;
        if (res.ok && res.body) {
            try {
                r = new BundleReader(res.body);
                const head = JSON.parse(new TextDecoder().decode(await r.take(await r.u32())));
                this.volume.version = head.version;
                manifest = head.manifest;
            } catch (e) {
                console.log(`[web] Executor's disk: no bundle (${e.message || e})`);
                r.cancel();
                r = null;
            }
        }
        if (!r) {
            const got = await Sync.request('GET', `volumes/${key}`);
            this.volume.version = got.version;
            manifest = got.manifest;
        }
        if (!manifest) {
            if (r)
                r.cancel();
            return null;
        }
        bySha = new Map();
        for (const [path, f] of Object.entries(manifest.files))
            bySha.set(f.sha, [...(bySha.get(f.sha) || []), path]);
        if (r) {
            try {
                for (let o; (o = await r.record());) {
                    bundled++;
                    if (isReady) {
                        begin();
                        write(o.sha, o.bytes);
                    } else
                        waiting.push([o.sha, o.bytes]);
                }
            } catch (e) {
                if (e instanceof FS.ErrnoError)
                    throw e;
                console.log(`[web] Executor's disk: the bundle stopped after ${bundled} files (${e.message || e}): the rest one by one`);
                r.cancel();
            }
        }
        await ready;
        begin();
        const rest = [...bySha.keys()];
        await eachLimited(rest, OBJECT_REQUESTS, async (sha) => {
            const res = await Sync.request('GET', `objects/${sha}`, undefined, { raw: true });
            write(sha, new Uint8Array(await res.arrayBuffer()));
        });
        const size = Object.values(manifest.files).reduce((n, f) => n + f.size, 0);
        console.log(`[web] Executor's disk: ${Object.keys(manifest.files).length} files, ${(size / 1e6).toFixed(1)} MB in `
                    + `${Math.round(performance.now() - t0)} ms (${bundled} objects bundled, ${rest.length} one by one)`);
        this.volume.digest = await Sync.digest(manifest);
        return manifest;
    },

    // form: { dirs, files: { path: { size, mtime, sha } } }; bytes: sha ->
    // contents read for it (the others read again if the server lacks them).
    async volumeUp(form, bytes) {
        if (this.volumeOff || !form.dirs.length)
            return;
        const digest = await Sync.digest(form);
        if (digest === this.volume.digest)
            return;
        for (let attempt = 0; attempt < 3; attempt++) {
            const { missing } = await Sync.request('POST', 'objects/missing',
                JSON.stringify({ shas: Object.values(form.files).map((f) => f.sha) }));
            await this.objectsUp(form, bytes, missing);
            try {
                const { version } = await Sync.request('PUT', `volumes/${this.volume.key}`,
                    JSON.stringify({ manifest: form, base: this.volume.version }));
                this.volume.version = version;
                this.volume.digest = digest;
                return;
            } catch (e) {
                if (!(e instanceof SyncConflict))
                    throw e;
                if (!e.missing)
                    this.volume.version = e.version;
            }
        }
    },

    // The objects the server lacks, in bundles of BUNDLE_UP bytes or so
    // (objects/bundle); a server without them: objects/SHA, a few at once.
    noBundles: false,
    async objectsUp(form, bytes, missing) {
        const pathOf = new Map(Object.entries(form.files).map(([p, f]) => [f.sha, p]));
        const read = (sha) => bytes.get(sha) || FS.readFile(pathOf.get(sha));
        let at = 0;
        while (!this.noBundles && at < missing.length) {
            const batch = [];
            for (let size = 0; at < missing.length && (!batch.length || size < BUNDLE_UP); at++) {
                const data = read(missing[at]);
                batch.push([missing[at], data]);
                size += data.length;
            }
            const { body, gzip } = await Sync.bundle(batch);
            const res = await fetch('api/sync/objects/bundle', {
                method: 'POST', body, cache: 'no-store', credentials: 'same-origin',
                headers: gzip ? { 'Content-Encoding': 'gzip' } : {} });
            if (res.status === 404 || res.status === 405 || res.status === 415) {
                this.noBundles = true;                  // (an older server)
                at -= batch.length;
            } else if (!res.ok)
                throw new Error(`POST objects/bundle: ${res.status}`);
        }
        await eachLimited(missing.slice(at), OBJECT_REQUESTS,
                          (sha) => Sync.request('PUT', `objects/${sha}`, read(sha)));
    },

    // A round: every overlay's changes, then the volume (volumeUp: worker.js's,
    // which scans it). Asked for while one runs: another after it, and
    // the promise is for both.
    up(overlays, volumeUp) {
        if (this.round) {
            this.again = true;
            return this.round;
        }
        this.round = this.rounds(overlays, volumeUp).finally(() => { this.round = null; });
        return this.round;
    },

    async rounds(overlays, volumeUp) {
        try {
            do {
                this.again = false;
                for (const ov of overlays)
                    await this.overlayUp(ov);
                await volumeUp();
            } while (this.again);
            if (this.failing)
                notice('Saved on the server again');
            this.failing = false;
        } catch (e) {
            if (!this.failing)
                notice(`Not saved on the server (${e.message || e.constructor.name}): trying again`);
            this.failing = true;
        }
    },

    // What the page sends if it goes now (main.js, pagehide): the changed
    // blocks not on the server yet, as uploads on the version this tab has.
    beacons(overlays) {
        const out = [];
        let room = BEACON_BLOCKS;
        for (const ov of overlays) {
            if (!ov.writable || ov.syncOff)
                continue;
            const blocks = [...new Set([...(this.flying.get(ov) || []), ...ov.pending])]
                .filter((b) => ov.map.has(b)).slice(0, room);
            if (!blocks.length)
                continue;
            room -= blocks.length;
            out.push({ url: `api/sync/overlays/${ov.key}?base=${ov.sync.version}`
                            + `&name=${encodeURIComponent(ov.name)}&size=${ov.size}`,
                       body: Sync.records(ov, blocks) });
        }
        return out;
    },
};
