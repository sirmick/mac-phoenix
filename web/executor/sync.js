// The Worker's side of sync with tools/web/server (signed in only: the
// page says so). What the tab keeps in OPFS goes to the user's account,
// and comes back in another browser:
//
// Overlays (overlay.js): their changed blocks, uploaded every few seconds
// as they change; at start the blocks newer than the version this browser
// has come down. Volumes (worker.js: Executor's own volume): the manifest
// with each file's SHA-256, the files the server lacks first.
//
// Every upload names the version it is on; the server refuses it when
// another browser's came in between (409), and so does a start that finds
// changes both here (not uploaded yet, or made signed out) and on the
// account: the page asks which to keep (a disk's blocks from two browsers
// would not make one disk). This browser's replaces the account's in one
// version (staged, then committed); the account's is taken at the next
// start (the Worker asks the page to restart). Revert and Reset leave a
// mark in OPFS (main.js) that is taken to the account at the next start,
// and stays until it gets there.
'use strict';

const SYNC_EVERY = 5000;                 // ms between uploads at most
const SYNC_BATCH = 256;                  // blocks a request (1 MB)
const SYNC_RECORD = 4 + OVERLAY_BLOCK;
const VOLUME_NAME = "Executor's disk";

class SyncConflict {
    constructor(body) { Object.assign(this, body); }
}

const Sync = {
    enabled: false,
    busy: false,
    last: 0,
    volumeOff: false,
    asks: new Map(),                     // id -> resolve, for 'resolve' messages
    nextAsk: 1,

    // 'mine' or 'account', from the page (main.js asks the user).
    ask(what, why) {
        const id = this.nextAsk++;
        return new Promise((resolve) => {
            this.asks.set(id, resolve);
            postMessage({ type: 'conflict', id, what, why });
        });
    },

    resolve(id, keep) {
        const r = this.asks.get(id);
        this.asks.delete(id);
        if (r)
            r(keep);
    },

    async request(method, path, body, { raw = false } = {}) {
        const res = await fetch(`api/sync/${path}`, {
            method, cache: 'no-store', credentials: 'same-origin', body,
            headers: body && !(body instanceof Uint8Array) ? { 'Content-Type': 'application/json' } : {},
        });
        if (res.status === 401) {
            this.enabled = false;
            notice('Signed out: changes are kept in this browser only until you sign in again');
        }
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
            console.log(`[web] ${ov.name}: ${body.length / SYNC_RECORD} blocks from the account`);
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
    // A SyncConflict when base isn't the account's.
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

    // This browser's overlay as the account's, whole.
    async replace(ov, base) {
        for (let attempt = 0; ; attempt++) {
            ov.pending.clear();                  // a write meanwhile: pending again
            try {
                ov.sync = { version: await this.upload(ov, [...ov.map.keys()], base, true), synced: true, adopt: false };
                ov.saveSync();
                return;
            } catch (e) {
                ov.pending = new Set(ov.map.keys());
                if (!(e instanceof SyncConflict) || attempt >= 2)
                    throw e;
                base = e.version;                // another came in: still ours
            }
        }
    },

    // The account's at the next start; this browser's dropped then.
    takeAccountsLater(ov) {
        ov.sync = { version: 0, synced: false, adopt: true };
        ov.saveSync();
        postMessage({ type: 'restart' });
    },

    async overlayDown(ov) {
        if (!this.enabled || !ov.writable || ov.borrowed)
            return;
        try {
            if (ov.reverted) {                   // Revert, to the account
                const r = await this.request('DELETE', `overlays/${ov.key}`);
                await ov.dir.removeEntry('revert');
                ov.reverted = false;
                ov.sync = { version: r.version, synced: true, adopt: false };
                ov.pending = new Set(ov.map.keys());
                ov.save();
                return;
            }
            const fresh = !ov.sync.synced;
            const since = fresh ? 0 : ov.sync.version;
            let got = await this.fetchBlocks(ov, since);
            if (got.version <= since) {          // nothing new there
                if (fresh) {
                    ov.pending = new Set(ov.map.keys());
                    ov.sync = { version: got.version, synced: true, adopt: false };
                }
                ov.save();
                return;
            }
            const local = fresh ? ov.modified : ov.pending.size > 0;
            let take = ov.sync.adopt || !local;
            if (!take)
                take = await this.ask(ov.name, fresh ? 'before' : 'conflict') === 'account';
            if (take) {
                if (local || got.reset || ov.sync.adopt) {
                    ov.clear();
                    if (since)
                        got = await this.fetchBlocks(ov, 0);
                }
                this.apply(ov, got.body);
                ov.pending.clear();
                ov.sync = { version: got.version, synced: true, adopt: false };
            } else {
                await this.replace(ov, got.version);
            }
            ov.save();
        } catch (e) {
            ov.syncOff = true;
            notice(`${ov.name}: not synced this time (${e.message || e.constructor.name})`);
        }
    },

    async overlayUp(ov) {
        if (!ov.writable || !ov.sync.synced || ov.sync.adopt || ov.syncOff || !ov.pending.size)
            return;
        const taken = [...ov.pending];
        ov.pending.clear();                      // a write meanwhile: pending again
        try {
            ov.sync.version = await this.upload(ov, taken.filter((b) => ov.map.has(b)), ov.sync.version, false);
        } catch (e) {
            for (const b of taken)
                ov.pending.add(b);
            if (!(e instanceof SyncConflict))
                throw e;
            if (await this.ask(ov.name, 'conflict') === 'mine')
                await this.replace(ov, e.version);
            else
                this.takeAccountsLater(ov);
        } finally {
            ov.saveSync();
        }
    },

    // ---- volumes: dir is volumes/KEY in OPFS; sync.json there ----
    async readJson(dir, name) {
        try {
            return JSON.parse(await (await (await dir.getFileHandle(name)).getFile()).text());
        } catch {
            return null;
        }
    },

    async writeJson(dir, name, value) {
        const w = await (await dir.getFileHandle(name, { create: true })).createWritable();
        await w.write(JSON.stringify(value));
        await w.close();
    },

    async sha256(bytes) {
        const d = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
        return Array.from(d, (b) => b.toString(16).padStart(2, '0')).join('');
    },

    // The manifest as the server keeps it: no blob names, paths in order.
    async serverForm(manifest, dir) {
        const files = {};
        for (const path of Object.keys(manifest.files).sort()) {
            const f = manifest.files[path];
            if (!f.sha)              // saved before sync knew it
                f.sha = await this.sha256(await (await (await dir.getFileHandle(f.blob)).getFile()).arrayBuffer());
            files[path] = { size: f.size, mtime: f.mtime, sha: f.sha };
        }
        return { dirs: manifest.dirs, files };
    },

    async digest(form) {
        return form ? this.sha256(new TextEncoder().encode(JSON.stringify(form))) : null;
    },

    // Before the volume is restored: the account's, if it is newer.
    async volumeDown(dir, key) {
        if (!this.enabled || !dir)
            return;
        try {
            const state = await this.readJson(dir, 'sync.json');
            if (state && state.reset) {          // Reset, to the account
                const r = await this.request('DELETE', `volumes/${key}`);
                await this.writeJson(dir, 'sync.json', { version: r.version, digest: null });
                return;
            }
            const { version, manifest } = await this.request('GET', `volumes/${key}`);
            const since = state ? state.version : 0;
            if (version <= since) {
                if (!state)
                    await this.writeJson(dir, 'sync.json', { version, digest: null });
                return;
            }
            const local = await this.readJson(dir, 'manifest.json');
            const localDigest = local && await this.digest(await this.serverForm(local, dir));
            // Changed here: since the last sync, and not to what the account has.
            const changed = local && (!state || localDigest !== state.digest)
                && localDigest !== await this.digest(manifest);
            let take = (state && state.adopt) || !changed;
            if (!take)
                take = await this.ask(VOLUME_NAME, state ? 'conflict' : 'before') === 'account';
            if (take) {
                await this.adoptVolume(dir, manifest, local);
                await this.writeJson(dir, 'sync.json', { version, digest: await this.digest(manifest) });
            } else {                             // ours goes up, on the account's version
                await this.writeJson(dir, 'sync.json', { version, digest: null });
            }
        } catch (e) {
            this.volumeOff = true;
            notice(`${VOLUME_NAME}: not synced this time (${e.message || e.constructor.name})`);
        }
    },

    // The local volume made the server's manifest (null: empty), with the
    // blobs this browser has already kept, by SHA.
    async adoptVolume(dir, manifest, local) {
        const have = new Map();
        let next = 1;
        for (const f of Object.values(local ? local.files : {})) {
            if (f.sha)
                have.set(f.sha, f.blob);
            next = Math.max(next, Number(f.blob.slice(1)) + 1);
        }
        const files = {}, used = new Set();
        for (const [path, f] of Object.entries(manifest ? manifest.files : {})) {
            let blob = have.get(f.sha);
            if (!blob) {
                const res = await this.request('GET', `objects/${f.sha}`, undefined, { raw: true });
                blob = `b${next++}`;
                const w = await (await dir.getFileHandle(blob, { create: true })).createWritable();
                await w.write(await res.arrayBuffer());
                await w.close();
                have.set(f.sha, blob);
            }
            used.add(blob);
            files[path] = { size: f.size, mtime: f.mtime, blob, sha: f.sha };
        }
        if (manifest)
            await this.writeJson(dir, 'manifest.json', { dirs: manifest.dirs, files });
        else
            await dir.removeEntry('manifest.json').catch(() => {});
        for (const f of Object.values(local ? local.files : {}))
            if (!used.has(f.blob))
                await dir.removeEntry(f.blob).catch(() => {});
    },

    async volumeUp(dir, key, saved) {
        const state = await this.readJson(dir, 'sync.json');
        if (!state || state.adopt || state.reset || this.volumeOff || !saved.dirs.length)
            return;
        const form = await this.serverForm(saved, dir);
        const digest = await this.digest(form);
        if (digest === state.digest)
            return;
        let base = state.version;
        for (let attempt = 0; attempt < 3; attempt++) {
            const { missing } = await this.request('POST', 'objects/missing',
                JSON.stringify({ shas: Object.values(form.files).map((f) => f.sha) }));
            for (const sha of missing) {
                const f = Object.values(saved.files).find((x) => x.sha === sha);
                // A blob the next save already replaced: thrown, the next round.
                const bytes = new Uint8Array(await (await (await dir.getFileHandle(f.blob)).getFile()).arrayBuffer());
                await this.request('PUT', `objects/${sha}`, bytes);
            }
            try {
                const { version } = await this.request('PUT', `volumes/${key}`, JSON.stringify({ manifest: form, base }));
                await this.writeJson(dir, 'sync.json', { version, digest });
                return;
            } catch (e) {
                if (!(e instanceof SyncConflict))
                    throw e;
                if (e.missing)
                    continue;                    // collected meanwhile: up again
                if (await this.ask(VOLUME_NAME, 'conflict') !== 'mine') {
                    await this.writeJson(dir, 'sync.json', { version: 0, digest: null, adopt: true });
                    postMessage({ type: 'restart' });
                    return;
                }
                base = e.version;
            }
        }
    },

    // After a save: what changed, to the account (at most every SYNC_EVERY ms,
    // unless now).
    async up(overlays, dir, key, saved, now = false) {
        if (!this.enabled || this.busy || (!now && performance.now() - this.last < SYNC_EVERY))
            return;
        this.busy = true;
        this.last = performance.now();
        try {
            for (const ov of overlays)
                await this.overlayUp(ov);
            if (dir)
                await this.volumeUp(dir, key, saved);
        } catch (e) {
            console.log(`[web] sync: ${e.message || e.constructor.name}`);
        } finally {
            this.busy = false;
        }
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
            ov.sync = { version: got.version, synced: true, adopt: false };
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
