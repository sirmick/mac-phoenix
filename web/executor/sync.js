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
        const d = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
        return Array.from(d, (b) => b.toString(16).padStart(2, '0')).join('');
    },

    async digest(form) {
        return form ? this.sha256(new TextEncoder().encode(JSON.stringify(form))) : null;
    },
};

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

    // The volume's manifest (null: nothing kept), its files written into FS.
    async volumeDown(key) {
        this.volume = { key, version: 0, digest: null };
        const { version, manifest } = await Sync.request('GET', `volumes/${key}`);
        this.volume.version = version;
        if (!manifest)
            return null;
        for (const d of manifest.dirs)
            FS.mkdirTree(d);
        for (const [path, f] of Object.entries(manifest.files)) {
            const res = await Sync.request('GET', `objects/${f.sha}`, undefined, { raw: true });
            FS.writeFile(path, new Uint8Array(await res.arrayBuffer()));
            FS.utime(path, f.mtime, f.mtime);
        }
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
            for (const sha of missing) {
                const path = Object.keys(form.files).find((p) => form.files[p].sha === sha);
                await Sync.request('PUT', `objects/${sha}`, bytes.get(sha) || FS.readFile(path));
            }
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
