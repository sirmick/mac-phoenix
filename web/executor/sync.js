// The Worker's side of sync with tools/web/server (signed in only: the
// page says so). What the tab keeps in OPFS goes to the user's account,
// and comes back in another browser:
//
// Overlays (overlay.js): their changed blocks, uploaded every few seconds
// as they change; at start the blocks newer than the version this browser
// has come down. Volumes (worker.js: Executor's own volume): the manifest
// with each file's SHA-256, the files the server lacks first.
//
// Who wins: what the account has, over changes made here before this
// browser was signed in; then the last to upload. At start, an overlay or
// volume changed both here (not uploaded yet: the tab closed first) and
// on the account since: this browser's, whole (the account's is replaced;
// a disk's blocks from two browsers would not make one disk).
'use strict';

const SYNC_EVERY = 5000;                 // ms between uploads at most
const SYNC_BATCH = 256;                  // blocks a request (1 MB)
const SYNC_RECORD = 4 + OVERLAY_BLOCK;

const Sync = {
    enabled: false,
    busy: false,
    last: 0,

    async request(method, path, body, json = true) {
        const res = await fetch(`api/sync/${path}`, {
            method, cache: 'no-store', credentials: 'same-origin', body,
            headers: body && !(body instanceof Uint8Array) ? { 'Content-Type': 'application/json' } : {},
        });
        if (!res.ok)
            throw new Error(`${method} ${path}: ${res.status}`);
        return json ? res.json() : res;
    },

    // ---- overlays ----
    async overlayDown(ov) {
        if (!this.enabled || !ov.writable)
            return;
        try {
            const since = ov.sync.synced ? ov.sync.version : 0;
            const res = await this.request('GET', `overlays/${ov.key}?since=${since}`, undefined, false);
            const version = Number(res.headers.get('X-Sync-Version'));
            const reset = res.headers.get('X-Sync-Reset') === '1';
            const body = new Uint8Array(await res.arrayBuffer());
            const apply = () => {
                for (let i = 0; i + SYNC_RECORD <= body.length; i += SYNC_RECORD) {
                    const block = new DataView(body.buffer, i, 4).getUint32(0, true);
                    if (block * OVERLAY_BLOCK < ov.size)
                        ov.putBlock(block, body.subarray(i + 4, i + SYNC_RECORD));
                }
            };
            if (!ov.sync.synced) {
                if (version > 0) {                       // the account's
                    if (ov.modified)
                        console.log(`[web] ${ov.name}: this browser's changes replaced by the account's`);
                    ov.clear();
                    apply();
                } else {                                 // ours, all of them, to the account
                    ov.pending = new Set(ov.map.keys());
                }
                ov.sync = { version, synced: true };
            } else if (version > ov.sync.version) {
                if (ov.pending.size) {
                    console.log(`[web] ${ov.name}: changed here and on the account: this browser's kept`);
                    const r = await this.request('DELETE', `overlays/${ov.key}`);
                    ov.pending = new Set(ov.map.keys());
                    ov.sync.version = r.version;
                } else {
                    if (reset)
                        ov.clear();
                    apply();
                    ov.sync.version = version;
                }
            }
            ov.save();
            const n = body.length / SYNC_RECORD;
            if (n)
                console.log(`[web] ${ov.name}: ${n} blocks from the account`);
        } catch (e) {
            console.log(`[web] ${ov.name}: no sync (${e.message || e})`);
        }
    },

    async overlayUp(ov) {
        while (ov.writable && ov.sync.synced && ov.pending.size) {
            const take = [...ov.pending].slice(0, SYNC_BATCH);
            const blocks = take.filter((b) => ov.map.has(b));
            const body = new Uint8Array(blocks.length * SYNC_RECORD);
            blocks.forEach((b, i) => {
                new DataView(body.buffer, i * SYNC_RECORD, 4).setUint32(0, b, true);
                body.set(ov.getBlock(b), i * SYNC_RECORD + 4);
            });
            // Taken now: a write while this uploads makes the block pending again.
            for (const b of take)
                ov.pending.delete(b);
            try {
                const r = await this.request('POST',
                    `overlays/${ov.key}?name=${encodeURIComponent(ov.name)}&size=${ov.size}`, body);
                ov.sync.version = r.version;
            } catch (e) {
                for (const b of blocks)
                    ov.pending.add(b);
                throw e;
            } finally {
                ov.saveSync();
            }
        }
    },

    // ---- volumes: dir is volumes/KEY in OPFS ----
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
            const { version, manifest } = await this.request('GET', `volumes/${key}`);
            const state = await this.readJson(dir, 'sync.json');
            const local = await this.readJson(dir, 'manifest.json');
            const localDigest = local ? await this.digest(await this.serverForm(local, dir)) : null;
            const dirty = local && (!state || localDigest !== state.digest);
            const adopt = state ? version > state.version && !dirty : version > 0;
            if (adopt) {
                if (local)
                    console.log(`[web] Executor's disk: the account's (version ${version})`);
                await this.adoptVolume(dir, manifest, local);
                await this.writeJson(dir, 'sync.json', { version, digest: await this.digest(manifest) });
            } else if (!state) {
                await this.writeJson(dir, 'sync.json', { version: 0, digest: null });
            } else if (version > state.version) {
                console.log("[web] Executor's disk: changed here and on the account: this browser's kept");
            }
        } catch (e) {
            console.log(`[web] Executor's disk: no sync (${e.message || e})`);
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
                const res = await this.request('GET', `objects/${f.sha}`, undefined, false);
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
        if (!state || !saved.dirs.length)
            return;
        const form = await this.serverForm(saved, dir);
        const digest = await this.digest(form);
        if (digest === state.digest)
            return;
        const { missing } = await this.request('POST', 'objects/missing',
                                               JSON.stringify({ shas: Object.values(form.files).map((f) => f.sha) }));
        for (const sha of missing) {
            const f = Object.values(saved.files).find((x) => x.sha === sha);
            // A blob the next save already replaced: the next round.
            const bytes = new Uint8Array(await (await (await dir.getFileHandle(f.blob)).getFile()).arrayBuffer());
            await this.request('PUT', `objects/${sha}`, bytes, false);
        }
        const { version } = await this.request('PUT', `volumes/${key}`, JSON.stringify({ manifest: form }));
        await this.writeJson(dir, 'sync.json', { version, digest });
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
            console.log(`[web] sync: ${e.message || e}`);
        } finally {
            this.busy = false;
        }
    },
};
