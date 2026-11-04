// Writable disk images for Executor's tab: a copy-on-write overlay in
// memory over each image, which itself is never written (a disk on the
// server, or the user's upload).
//
// Overlay: 4 KB blocks, the changed ones in `data` (a MemoryHandle), each
// in a slot; map: block number -> slot. KEY names the exact base image: a
// disk from the server by its content (the server's X-Image-Id: main.js),
// so its changes survive the image being touched; an upload by its name,
// size and time. The server keeps a server disk's changes under KEY
// (sync.js, Store): they come down before Executor mounts the image and go
// back as they are made. An upload's last while this Worker runs.
//
// OVERLAYFS mounts them for Executor: reads take each block from the
// overlay if changed, else from the image (runs of unchanged blocks in one
// read); writes go to the overlay.
//
// The image is a File (FileImage: an upload, a downloaded copy), or one on
// the server read as it is needed (RemoteImage): a large disk mounts
// before it has downloaded.
//
// Executor flushing an image (FlushVol, a file closed, an eject: fsync on
// it) calls Overlay.onFlush (worker.js: the changes go to the server at
// once).
'use strict';

const OVERLAY_BLOCK = 4096;

// A FileSystemSyncAccessHandle's calls on a growable buffer in memory.
class MemoryHandle {
    constructor() {
        this.bytes = new Uint8Array(0);
        this.size = 0;
    }

    view(buf) { return new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength); }

    read(buf, { at }) {
        const out = this.view(buf);
        const n = Math.max(0, Math.min(out.length, this.size - at));
        out.set(this.bytes.subarray(at, at + n));
        out.fill(0, n);
        return n;
    }

    write(buf, { at }) {
        const src = this.view(buf);
        const end = at + src.length;
        if (end > this.bytes.length) {
            const grown = new Uint8Array(Math.max(end, this.bytes.length * 2, 1 << 16));
            grown.set(this.bytes.subarray(0, this.size));
            this.bytes = grown;
        }
        this.bytes.set(src, at);
        this.size = Math.max(this.size, end);
        return src.length;
    }

    getSize() { return this.size; }

    truncate(n) {
        if (n < this.size)
            this.bytes.fill(0, n, this.size);
        this.size = n;
    }

    flush() { }

    close() { }
}

class FileImage {
    constructor(file) {
        this.file = file;
        this.size = file.size;
        this.reader = new FileReaderSync();
    }

    read(pos, length) {
        return new Uint8Array(this.reader.readAsArrayBuffer(this.file.slice(pos, pos + length)));
    }

    slice(start, end) { return this.file.slice(start, end); }

    has() { return true; }
}

// A disk image on the server, read by Range requests (synchronous
// XMLHttpRequests: Workers may, and Emscripten's filesystem calls are
// synchronous) in 256 KB chunks, kept in memory: at most REMOTE_MEMORY
// bytes of it, the chunks read longest ago dropped (and read again if
// needed). desc: { name, size, lastModified, remote: { url, etag } } (main.js).
const REMOTE_CHUNK = 256 * 1024;
const REMOTE_AHEAD = 4;                  // chunks a request, the ones needed or not
const REMOTE_TIMEOUT = 20000;            // ms a request
const REMOTE_TRIES = 3;
const REMOTE_MEMORY = 128 << 20;

// A line for the page's status (worker.js).
function notice(text) {
    console.log(`[web] ${text}`);
    if (typeof postMessage === 'function')
        postMessage({ type: 'notice', text });
}

// A cache of immutable bytes in this browser (IndexedDB database NAME:
// "bytes" by key, "meta" { size, used } by key, for the eviction): at
// most cap bytes, the least recently used out first. Disposable: no
// IndexedDB, an error, deleted meanwhile: it answers nothing (said once,
// in the log) and the caller goes to the network. Volume objects
// (sync.js: ObjectCache) and disk image chunks (ChunkCache, below).
class IdbCache {
    constructor(name, cap) {
        this.name = name;
        this.cap = cap;
        this.db = null;
        this.off = false;
    }

    open() {
        if (!this.db)
            this.db = new Promise((resolve) => {
                try {
                    const r = indexedDB.open(this.name, 1);
                    r.onupgradeneeded = () => {
                        r.result.createObjectStore('bytes');
                        r.result.createObjectStore('meta');
                    };
                    r.onsuccess = () => {
                        r.result.onversionchange = () => r.result.close();     // (deleted: let it go)
                        resolve(r.result);
                    };
                    r.onerror = r.onblocked = () => resolve(null);
                } catch {
                    resolve(null);
                }
            });
        return this.db;
    }

    // fn(bytes, meta) in a transaction of the two stores; what it returns,
    // once the transaction is done; null for no cache.
    async run(mode, fn) {
        if (this.off)
            return null;
        try {
            const db = await this.open();
            if (!db)
                return null;
            const tx = db.transaction(['bytes', 'meta'], mode);
            const out = fn(tx.objectStore('bytes'), tx.objectStore('meta'));
            await new Promise((resolve, reject) => {
                tx.oncomplete = resolve;
                tx.onerror = tx.onabort = () => reject(tx.error);
            });
            return out;
        } catch (e) {
            console.log(`[web] this browser's cache ${this.name}: ${e.message || e}; not used`);
            this.off = true;
            return null;
        }
    }

    // [key, { size, used }] of all, or of the keys in range.
    async metas(range) {
        let all = [];
        await this.run('readonly', (bytes, meta) => {
            const keys = meta.getAllKeys(range), vals = meta.getAll(range);
            vals.onsuccess = () => { all = keys.result.map((k, i) => [k, vals.result[i]]); };
        });
        return all;
    }

    // key -> bytes of those it has.
    async get(keys) {
        const got = new Map();
        if (keys.length)
            await this.run('readonly', (bytes) => {
                for (const k of keys)
                    bytes.get(k).onsuccess = (e) => {
                        if (e.target.result instanceof Uint8Array)
                            got.set(k, e.target.result);
                    };
            });
        return got;
    }

    touch(keys, sizes) {
        const now = Date.now();
        return this.run('readwrite', (bytes, meta) => {
            for (const k of keys)
                meta.put({ size: sizes.get(k), used: now }, k);
        });
    }

    drop(keys) {
        return this.run('readwrite', (bytes, meta) => {
            for (const k of keys) {
                bytes.delete(k);
                meta.delete(k);
            }
        });
    }

    // [key, bytes] in (used: when, default now), then the least recently
    // used out past cap.
    async put(entries, now = Date.now()) {
        if (!entries.length || this.off)
            return;
        await this.run('readwrite', (bytes, meta) => {
            for (const [k, b] of entries) {
                bytes.put(b.slice(), k);            // (a view's own bytes, not its buffer's)
                meta.put({ size: b.length, used: now }, k);
            }
        });
        const all = await this.metas();
        let total = all.reduce((n, [, m]) => n + m.size, 0);
        if (total <= this.cap)
            return;
        all.sort((a, b) => a[1].used - b[1].used);
        const out = [];
        for (const [k, m] of all) {
            if (total <= this.cap)
                break;
            out.push(k);
            total -= m.size;
        }
        await this.drop(out);
    }
}

// Disk image chunks, by the image's ETag (its content id on
// tools/standalone) and chunk number. Read synchronously, they can't come
// from IndexedDB as Executor reads: an image's most recently read ones
// (up to CHUNKS_PRELOAD bytes: what the last start read; fetched but not
// read, a read-ahead, doesn't count) go into its memory before Executor
// starts (RemoteImage.preload); the ones fetched, and the ones read, are
// written back now and then (RemoteImage.keep).
const ChunkCache = new IdbCache('executor-chunks', 256 << 20);
const CHUNKS_PRELOAD = 8 << 20;
const CHUNKS_KEEP_EVERY = 2000;          // ms

class RemoteImage {
    constructor(desc) {
        this.name = desc.name;
        this.size = desc.size;
        this.url = desc.remote.url;
        this.etag = desc.remote.etag;
        this.gzip = !!desc.remote.gzip;  // ranges gzip-compressed (tools/standalone offers it: fetchChunks)
        this.chunks = Math.ceil(this.size / REMOTE_CHUNK);
        this.have = new Uint8Array(this.chunks);
        this.memory = new Map();         // chunk -> bytes; oldest read first
        this.fetched = 0;                // bytes, and requests, for 'stats'
        this.requests = 0;
        this.fresh = new Set();          // chunks fetched, not in ChunkCache yet
        this.used = new Set();           // chunks read since the last keep
        this.keeping = null;
        this.preloaded = 0;              // chunks, for 'stats'
    }

    cacheKey(c) {
        return `${this.etag}:${String(c).padStart(8, '0')}`;
    }

    // The cached chunks the last starts read, into memory (not waited for
    // by anything but the start: worker.js openImage).
    async preload() {
        if (!this.etag)
            return;
        const range = IDBKeyRange.bound(`${this.etag}:`, `${this.etag}:~`);
        const metas = (await ChunkCache.metas(range)).filter(([, m]) => m.used)
            .sort((a, b) => b[1].used - a[1].used).slice(0, Math.floor(CHUNKS_PRELOAD / REMOTE_CHUNK));
        const got = await ChunkCache.get(metas.map(([k]) => k));
        for (const [k, bytes] of got) {
            const c = Number(k.slice(k.lastIndexOf(':') + 1));
            const want = Math.min(REMOTE_CHUNK, this.size - c * REMOTE_CHUNK);
            if (c < this.chunks && bytes.length === want && !this.have[c]) {
                this.memory.set(c, bytes);
                this.have[c] = 1;
                this.preloaded++;
            }
        }
    }

    // Chunks fetched into ChunkCache, the ones read marked used: soon after
    // a read, at most every CHUNKS_KEEP_EVERY ms.
    keep() {
        if (this.keeping || !this.etag || ChunkCache.off)
            return;
        this.keeping = setTimeout(async () => {
            const fresh = [...this.fresh].filter((c) => this.memory.has(c));
            const used = [...this.used].filter((c) => this.memory.has(c));
            this.fresh.clear();
            this.used.clear();
            try {
                await ChunkCache.put(fresh.map((c) => [this.cacheKey(c), this.memory.get(c)]), 0);
                await ChunkCache.touch(used.map((c) => this.cacheKey(c)),
                                       new Map(used.map((c) => [this.cacheKey(c), this.memory.get(c).length])));
            } finally {
                this.keeping = null;
                if (this.fresh.size || this.used.size)
                    this.keep();
            }
        }, CHUNKS_KEEP_EVERY);
    }

    fetchChunks(first, count) {
        const start = first * REMOTE_CHUNK;
        const end = Math.min(this.size, (first + count) * REMOTE_CHUNK);
        const x = new XMLHttpRequest();
        // A stall or a blip: tried again, a few times; the Worker waits
        // meanwhile (Executor's read is synchronous).
        // Compressed (this.gzip): ?bytes=, a 200 the browser decodes
        // (tools/standalone static.go); anything wrong with it: plain Range
        // requests from then on.
        const plain = () => {
            console.log(`[web] ${this.name}: compressed ranges failed: plain ones`);
            this.gzip = false;
            return this.fetchChunks(first, count);
        };
        const want = this.gzip ? 200 : 206;
        for (let attempt = 1; ; attempt++) {
            x.open('GET', this.gzip ? `${this.url}${this.url.includes('?') ? '&' : '?'}bytes=${start}-${end - 1}` : this.url, false);
            x.responseType = 'arraybuffer';
            x.timeout = REMOTE_TIMEOUT;
            if (!this.gzip)
                x.setRequestHeader('Range', `bytes=${start}-${end - 1}`);
            try {
                x.send();
            } catch (e) {
                if (attempt < REMOTE_TRIES)
                    continue;
                if (this.gzip)
                    return plain();
                notice(`${this.name}: the server can't be reached (${e.name})`);
                throw e;
            }
            if (x.status === want || attempt >= REMOTE_TRIES || (x.status >= 400 && x.status < 500))
                break;
        }
        if (x.status === 401 || x.status === 403)
            notice(`${this.name}: refused (HTTP ${x.status}): reload the page`);
        const etag = x.getResponseHeader('ETag');
        // (main.js checked that Range requests get 206: a proxy dropping
        // Range shows at once, not after the whole image.)
        if (this.gzip && (x.status !== 200 || x.response.byteLength !== end - start)
            && x.status !== 401 && x.status !== 403)
            return plain();
        if (x.status !== want || x.response.byteLength !== end - start)
            throw new Error(`bytes ${start}-${end - 1}: HTTP ${x.status}`);
        if (this.etag && etag && etag !== this.etag)
            throw new Error('the image changed on the server');
        const bytes = new Uint8Array(x.response);
        this.fetched += bytes.length;
        this.requests++;
        for (let c = 0; c < count; c++) {
            this.memory.set(first + c, bytes.slice(c * REMOTE_CHUNK, (c + 1) * REMOTE_CHUNK));
            this.fresh.add(first + c);
        }
        this.have.fill(1, first, first + count);
        this.keep();
        for (const [c] of this.memory) {
            if (this.memory.size * REMOTE_CHUNK <= REMOTE_MEMORY)
                break;
            this.memory.delete(c);
            this.have[c] = 0;
        }
    }

    read(pos, length) {
        const out = new Uint8Array(length);
        const last = Math.floor((pos + length - 1) / REMOTE_CHUNK);
        for (let c = Math.floor(pos / REMOTE_CHUNK); c <= last; c++) {
            if (!this.have[c]) {
                // This chunk and the missing ones after it: the rest of this
                // read, or a few ahead.
                let n = 1;
                while (c + n < this.chunks && !this.have[c + n] && (c + n <= last || n < REMOTE_AHEAD))
                    n++;
                this.fetchChunks(c, n);
            }
        }
        for (let at = 0; at < length;) {
            const c = Math.floor((pos + at) / REMOTE_CHUNK);
            const off = pos + at - c * REMOTE_CHUNK;
            const chunk = this.memory.get(c);
            this.memory.delete(c);               // the newest read now
            this.memory.set(c, chunk);
            if (!this.used.has(c)) {
                this.used.add(c);
                this.keep();
            }
            const piece = chunk.subarray(off, off + length - at);
            out.set(piece, at);
            at += piece.length;
        }
        return out;
    }

    // [pos, pos + length) here already: read without a request.
    has(pos, length) {
        const last = Math.floor((pos + length - 1) / REMOTE_CHUNK);
        for (let c = Math.floor(pos / REMOTE_CHUNK); c <= last; c++)
            if (!this.have[c])
                return false;
        return true;
    }

    // All of [start, end): a download of the image reads the rest.
    slice(start, end) {
        const parts = [];
        for (let p = start; p < end; p += 4 << 20)
            parts.push(this.read(p, Math.min(4 << 20, end - p)));
        return new Blob(parts);
    }
}

// Emscripten's errno values.
const EIO = 29, EINVAL = 28, ENOENT = 44, EPERM = 63, EROFS = 69;

class Overlay {
    constructor(name, file, key, image) {
        this.name = name;
        this.file = file;               // a File, or a remote image's description
        this.image = image || new FileImage(file);
        this.key = key;
        this.size = file.size;
        this.map = new Map();           // block -> slot
        this.nextSlot = 0;
        this.data = new MemoryHandle(); // the changed blocks
        this.sync = { version: 0 };     // the server's (standalone: sync.js, Store)
        this.pending = new Set();       // blocks changed since the last upload
        this.writes = 0;                // changes so far (sync.js: Store's beacons)
    }

    static open(name, file, key) {
        return new Overlay(name, file, key, file.remote ? new RemoteImage(file) : new FileImage(file));
    }

    get writable() { return true; }
    get modified() { return this.map.size > 0; }

    // The bytes [pos, pos + length), length within the image.
    read(pos, length) {
        const out = new Uint8Array(length);
        let at = 0;
        while (at < length) {
            const p = pos + at;
            const block = Math.floor(p / OVERLAY_BLOCK);
            const slot = this.map.get(block);
            if (slot !== undefined) {
                const off = p - block * OVERLAY_BLOCK;
                const n = Math.min(OVERLAY_BLOCK - off, length - at);
                this.data.read(out.subarray(at, at + n), { at: slot * OVERLAY_BLOCK + off });
                at += n;
                continue;
            }
            // Unchanged: through to the next changed block, in one slice.
            let end = (block + 1) * OVERLAY_BLOCK;
            while (end < pos + length && !this.map.has(end / OVERLAY_BLOCK))
                end += OVERLAY_BLOCK;
            const n = Math.min(end, pos + length) - p;
            out.set(this.image.read(p, n), at);
            at += n;
        }
        return out;
    }

    write(pos, bytes) {
        let at = 0;
        while (at < bytes.length) {
            const p = pos + at;
            const block = Math.floor(p / OVERLAY_BLOCK);
            const off = p - block * OVERLAY_BLOCK;
            const n = Math.min(OVERLAY_BLOCK - off, bytes.length - at);
            let slot = this.map.get(block);
            if (this.unchanged(slot, p, bytes.subarray(at, at + n))) {
                at += n;
                continue;
            }
            if (slot === undefined) {
                // First change to this block: the rest of it from the image.
                const start = block * OVERLAY_BLOCK;
                const buf = new Uint8Array(OVERLAY_BLOCK);
                if (n < OVERLAY_BLOCK)
                    buf.set(this.image.read(start, Math.min(OVERLAY_BLOCK, this.size - start)));
                buf.set(bytes.subarray(at, at + n), off);
                slot = this.nextSlot++;
                this.data.write(buf, { at: slot * OVERLAY_BLOCK });
                this.map.set(block, slot);
            } else {
                this.data.write(bytes.subarray(at, at + n), { at: slot * OVERLAY_BLOCK + off });
            }
            this.pending.add(block);
            this.writes++;
            at += n;
        }
    }

    // bytes are what [pos, ...) holds already (slot: its block's, if
    // changed before). Such a write is no change: Executor writes a
    // volume's header back as it was when it mounts it, and counted, every
    // disk opened would have changes of its own to send to the server.
    // A remote image's part not downloaded yet counts as different.
    unchanged(slot, pos, bytes) {
        bytes = new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.length);   // Emscripten's: HEAP8's
        let now;
        if (slot !== undefined) {
            now = new Uint8Array(bytes.length);
            this.data.read(now, { at: slot * OVERLAY_BLOCK + pos % OVERLAY_BLOCK });
        } else if (this.image.has(pos, bytes.length)) {
            now = this.image.read(pos, bytes.length);
        } else {
            return false;
        }
        for (let i = 0; i < bytes.length; i++)
            if (now[i] !== bytes[i])
                return false;
        return true;
    }

    // A whole block (from the server: sync.js).
    putBlock(block, bytes) {
        let slot = this.map.get(block);
        if (slot === undefined) {
            slot = this.nextSlot++;
            this.map.set(block, slot);
        }
        this.data.write(bytes, { at: slot * OVERLAY_BLOCK });
    }

    getBlock(block) {
        const buf = new Uint8Array(OVERLAY_BLOCK);
        this.data.read(buf, { at: this.map.get(block) * OVERLAY_BLOCK });
        return buf;
    }

    // No changes: the image as it is.
    clear() {
        this.map.clear();
        this.nextSlot = 0;
        this.pending.clear();
        this.data.truncate(0);
    }

    // The image as it is now: the original's slices and the changed blocks.
    blob() {
        const parts = [];
        let clean = 0;                       // start of the current unchanged run
        const blocks = Math.ceil(this.size / OVERLAY_BLOCK);
        for (let b = 0; b < blocks; b++) {
            const slot = this.map.get(b);
            if (slot === undefined)
                continue;
            const start = b * OVERLAY_BLOCK;
            if (clean < start)
                parts.push(this.image.slice(clean, start));
            const n = Math.min(OVERLAY_BLOCK, this.size - start);
            const buf = new Uint8Array(n);
            this.data.read(buf, { at: slot * OVERLAY_BLOCK });
            parts.push(buf);
            clean = start + n;
        }
        if (clean < this.size)
            parts.push(this.image.slice(clean, this.size));
        return new Blob(parts, { type: 'application/octet-stream' });
    }
}

// The Emscripten filesystem over them: one flat folder of images.
const OVERLAYFS = {
    DIR_MODE: 0o40777,
    FILE_MODE: 0o100666,

    flush(ov) {
        if (Overlay.onFlush)
            Overlay.onFlush(ov);
    },

    mount(mount) {
        const root = OVERLAYFS.root = OVERLAYFS.createNode(null, '/', OVERLAYFS.DIR_MODE);
        for (const ov of mount.opts.overlays)
            OVERLAYFS.add(ov);
        return root;
    },

    // Images put in and taken out while Executor runs (the Disks panel).
    add(ov) {
        OVERLAYFS.createNode(OVERLAYFS.root, ov.name, ov.writable ? OVERLAYFS.FILE_MODE : 0o100444, ov);
    },
    remove(name) {
        delete OVERLAYFS.root.contents[name];
    },

    createNode(parent, name, mode, overlay) {
        const node = FS.createNode(parent, name, mode);
        node.mode = mode;
        node.node_ops = OVERLAYFS.node_ops;
        node.stream_ops = OVERLAYFS.stream_ops;
        node.atime = node.mtime = node.ctime = overlay ? overlay.file.lastModified : Date.now();
        if (overlay) {
            node.overlay = overlay;
            node.size = overlay.size;
        } else {
            node.size = 4096;
            node.contents = {};
        }
        if (parent)
            parent.contents[name] = node;
        return node;
    },

    node_ops: {
        getattr(node) {
            return {
                dev: 1, ino: node.id, mode: node.mode, nlink: 1, uid: 0, gid: 0, rdev: 0,
                size: node.size,
                atime: new Date(node.atime), mtime: new Date(node.mtime), ctime: new Date(node.ctime),
                blksize: OVERLAY_BLOCK, blocks: Math.ceil(node.size / OVERLAY_BLOCK),
            };
        },
        setattr(node, attr) {
            // Times only: an image keeps its size.
            for (const key of ['mode', 'atime', 'mtime', 'ctime'])
                if (attr[key] != null)
                    node[key] = attr[key];
        },
        lookup() { throw new FS.ErrnoError(ENOENT); },
        mknod() { throw new FS.ErrnoError(EPERM); },
        rename() { throw new FS.ErrnoError(EPERM); },
        unlink() { throw new FS.ErrnoError(EPERM); },
        rmdir() { throw new FS.ErrnoError(EPERM); },
        readdir(node) { return ['.', '..', ...Object.keys(node.contents)]; },
        symlink() { throw new FS.ErrnoError(EPERM); },
    },

    stream_ops: {
        read(stream, buffer, offset, length, position) {
            const ov = stream.node.overlay;
            const n = Math.max(0, Math.min(length, ov.size - position));
            if (n === 0)
                return 0;
            try {
                buffer.set(ov.read(position, n), offset);
            } catch (e) {
                console.log(`[web] ${ov.name}: read at ${position}: ${e}`);
                throw new FS.ErrnoError(EIO);
            }
            return n;
        },
        write(stream, buffer, offset, length, position) {
            const ov = stream.node.overlay;
            if (!ov.writable)
                throw new FS.ErrnoError(EROFS);
            const n = Math.max(0, Math.min(length, ov.size - position));
            try {
                ov.write(position, buffer.subarray(offset, offset + n));
            } catch (e) {
                console.log(`[web] ${ov.name}: write at ${position}: ${e}`);
                throw new FS.ErrnoError(EIO);
            }
            return n;
        },
        llseek(stream, offset, whence) {
            let position = offset;
            if (whence === 1)                    // SEEK_CUR
                position += stream.position;
            else if (whence === 2)               // SEEK_END
                position += stream.node.size;
            if (position < 0)
                throw new FS.ErrnoError(EINVAL);
            return position;
        },
        fsync(stream) {
            OVERLAYFS.flush(stream.node.overlay);
            return 0;
        },
        close(stream) {
            if (stream.flags & 3)                // O_WRONLY, O_RDWR
                OVERLAYFS.flush(stream.node.overlay);
        },
    },
};
