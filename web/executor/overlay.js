// Writable disk images for Executor's tab: a copy-on-write overlay in OPFS
// over each image, which itself is never written (the user's upload, or a
// cached ?disk= download).
//
// Overlay: 4 KB blocks. overlays/KEY/ holds `data` (changed blocks, each in
// a slot), `index` (pairs of block number and slot, little-endian uint32)
// and meta.json ({ name, key }). KEY names the exact base image (name, size,
// time): a changed base gets a new overlay, and its old one is dropped,
// since its blocks would land on different data. data and index are opened
// with FileSystemSyncAccessHandle (Workers only), whose reads and writes are
// synchronous, as Emscripten's filesystem calls are; a block is in `data`
// when written, the index is written by save() (worker.js, with the
// volume). Another tab holding the overlay: the image is read-only here.
//
// OVERLAYFS mounts them for Executor: reads take each block from the
// overlay if changed, else from the image (runs of unchanged blocks in one
// read); writes go to the overlay.
//
// The image is a File (FileImage: an upload, a downloaded copy), or one on
// the server read as it is needed (RemoteImage): a large disk mounts
// before it has downloaded.
//
// Signed in to tools/web/server, an overlay's changes go to the account
// too (sync.js): `sync` beside data and index keeps what was sent
// (uint32s: the version the server gave, flags (1: synced at all, 2: take
// the account's at the next start), the count and the numbers of the
// blocks changed since); `revert` there says Revert hasn't reached the
// account yet.
//
// KEY: a disk from the server is keyed on its content (the server's
// X-Image-Id: main.js), so its changes survive the image being touched;
// an upload on its name, size and time. An overlay under a disk's old key
// (its name, size and download time) is moved to the new one.
'use strict';

const OVERLAY_BLOCK = 4096;

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
}

// A disk image on the server, read by Range requests (synchronous
// XMLHttpRequests: Workers may, and Emscripten's filesystem calls are
// synchronous) in 256 KB chunks, kept in OPFS as they arrive:
// disk-cache/KEY.part (the image, sparse), KEY.have (a byte a chunk) and
// KEY.part.json (its URL and ETag: another ETag, another image, the copy
// starts again). desc: { name, size, lastModified, remote: { url, etag, key } }
// (main.js). Without OPFS, or another tab holding the copy: in memory.
const REMOTE_CHUNK = 256 * 1024;
const REMOTE_AHEAD = 4;                  // chunks a request, the ones needed or not
const REMOTE_TIMEOUT = 20000;            // ms a request
const REMOTE_TRIES = 3;

// A line for the page's status (worker.js).
function notice(text) {
    console.log(`[web] ${text}`);
    if (typeof postMessage === 'function')
        postMessage({ type: 'notice', text });
}

class RemoteImage {
    constructor(desc) {
        this.name = desc.name;
        this.size = desc.size;
        this.url = desc.remote.url;
        this.etag = desc.remote.etag;
        this.chunks = Math.ceil(this.size / REMOTE_CHUNK);
        this.have = new Uint8Array(this.chunks);
        this.part = null;                // sync access handles; null: memory
        this.haveFile = null;
        this.memory = new Map();         // chunk -> bytes, without them
        this.fetched = 0;                // bytes, and requests, for 'stats'
        this.requests = 0;
    }

    static async open(root, desc) {
        const img = new RemoteImage(desc);
        if (!root)
            return img;
        const key = desc.remote.key;
        try {
            const dir = await root.getDirectoryHandle('disk-cache', { create: true });
            let meta = null;
            try {
                meta = JSON.parse(await (await (await dir.getFileHandle(`${key}.part.json`)).getFile()).text());
            } catch { }
            if (!meta || meta.etag !== img.etag || meta.size !== img.size) {
                for (const n of [`${key}.part`, `${key}.have`])
                    await dir.removeEntry(n).catch(() => {});
                const w = await (await dir.getFileHandle(`${key}.part.json`, { create: true })).createWritable();
                await w.write(JSON.stringify({ url: img.url, etag: img.etag, size: img.size }));
                await w.close();
            }
            img.part = await (await dir.getFileHandle(`${key}.part`, { create: true })).createSyncAccessHandle();
            img.haveFile = await (await dir.getFileHandle(`${key}.have`, { create: true })).createSyncAccessHandle();
            img.haveFile.read(img.have, { at: 0 });
        } catch (e) {
            console.log(`[web] ${desc.name}: the downloaded part in memory only (${e.message || e})`);
            if (img.part)
                img.part.close();
            img.part = img.haveFile = null;
            img.have.fill(0);
        }
        const n = img.have.reduce((a, b) => a + b, 0);
        console.log(`[web] ${desc.name}: ${(img.size / 1e6).toFixed(0)} MB on the server, ${n} of ${img.chunks} chunks here`);
        return img;
    }

    fetchChunks(first, count) {
        const start = first * REMOTE_CHUNK;
        const end = Math.min(this.size, (first + count) * REMOTE_CHUNK);
        const x = new XMLHttpRequest();
        // A stall or a blip: tried again, a few times; the Worker waits
        // meanwhile (Executor's read is synchronous).
        for (let attempt = 1; ; attempt++) {
            x.open('GET', this.url, false);
            x.responseType = 'arraybuffer';
            x.timeout = REMOTE_TIMEOUT;
            x.setRequestHeader('Range', `bytes=${start}-${end - 1}`);
            try {
                x.send();
            } catch (e) {
                if (attempt < REMOTE_TRIES)
                    continue;
                notice(`${this.name}: the server can't be reached (${e.name})`);
                throw e;
            }
            if (x.status === 206 || attempt >= REMOTE_TRIES || (x.status >= 400 && x.status < 500))
                break;
        }
        if (x.status === 401)
            notice(`Signed out: sign in again to read ${this.name}`);
        const etag = x.getResponseHeader('ETag');
        // (main.js checked that Range requests get 206: a proxy dropping
        // Range shows at once, not after the whole image.)
        if (x.status !== 206 || x.response.byteLength !== end - start)
            throw new Error(`bytes ${start}-${end - 1}: HTTP ${x.status}`);
        if (this.etag && etag && etag !== this.etag)
            throw new Error('the image changed on the server');
        const bytes = new Uint8Array(x.response);
        this.fetched += bytes.length;
        this.requests++;
        if (this.part) {
            // The bytes on disk before the map says they are there.
            this.part.write(bytes, { at: start });
            this.part.flush();
            this.have.fill(1, first, first + count);
            this.haveFile.write(this.have.subarray(first, first + count), { at: first });
            this.haveFile.flush();
        } else {
            for (let c = 0; c < count; c++)
                this.memory.set(first + c, bytes.subarray(c * REMOTE_CHUNK, (c + 1) * REMOTE_CHUNK));
            this.have.fill(1, first, first + count);
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
        if (this.part) {
            this.part.read(out, { at: pos });
            return out;
        }
        for (let at = 0; at < length;) {
            const c = Math.floor((pos + at) / REMOTE_CHUNK);
            const off = pos + at - c * REMOTE_CHUNK;
            const piece = this.memory.get(c).subarray(off, off + length - at);
            out.set(piece, at);
            at += piece.length;
        }
        return out;
    }

    // All of [start, end): a download of the image reads the rest.
    slice(start, end) {
        const parts = [];
        for (let p = start; p < end; p += 4 << 20)
            parts.push(this.read(p, Math.min(4 << 20, end - p)));
        return new Blob(parts);
    }

    flush() {
        if (this.part) {
            this.part.flush();
            this.haveFile.flush();
        }
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
        this.data = null;               // sync access handles; null: read-only
        this.index = null;
        this.indexDirty = false;
        this.syncFile = null;
        this.sync = { version: 0, synced: false, adopt: false };
        this.pending = new Set();       // blocks changed since the last upload
        this.dir = null;
        this.reverted = false;          // Revert not on the account yet
    }

    // root: OPFS (none: read-only). legacyKey: the key this image's
    // overlay may have under the old scheme.
    static async open(root, name, file, key, legacyKey) {
        const image = file.remote ? await RemoteImage.open(root, file) : new FileImage(file);
        const ov = new Overlay(name, file, key, image);
        if (!root)
            return ov;
        try {
            const overlays = await root.getDirectoryHandle('overlays', { create: true });
            let exists = false;
            try {
                await overlays.getDirectoryHandle(key);
                exists = true;
            } catch { }
            // Overlays of this image's other versions: theirs, not ours;
            // but this one's under its old key, moved.
            for await (const [dirName, handle] of overlays.entries()) {
                if (handle.kind !== 'directory' || dirName === key)
                    continue;
                let meta = null;
                try {
                    meta = JSON.parse(await (await (await handle.getFileHandle('meta.json')).getFile()).text());
                } catch { }
                if (!meta || meta.name !== name)
                    continue;
                if (dirName === legacyKey && !exists) {
                    const to = await overlays.getDirectoryHandle(key, { create: true });
                    for await (const [n, f] of handle.entries()) {
                        if (f.kind !== 'file' || n === 'meta.json')
                            continue;
                        const w = await (await to.getFileHandle(n, { create: true })).createWritable();
                        await w.write(await f.getFile());
                        await w.close();
                    }
                    exists = true;
                    console.log(`[web] ${name}: its changes moved to the new key`);
                } else {
                    notice(`${name} is another image now: the changes to the earlier one are dropped`);
                }
                await overlays.removeEntry(dirName, { recursive: true });
            }
            const dir = await overlays.getDirectoryHandle(key, { create: true });
            ov.dir = dir;
            try {
                await dir.getFileHandle('revert');
                ov.reverted = true;
            } catch { }
            const meta = await (await dir.getFileHandle('meta.json', { create: true })).createWritable();
            await meta.write(JSON.stringify({ name, key }));
            await meta.close();
            ov.data = await (await dir.getFileHandle('data', { create: true })).createSyncAccessHandle();
            ov.index = await (await dir.getFileHandle('index', { create: true })).createSyncAccessHandle();
            const n = ov.index.getSize();
            if (n >= 8) {
                const pairs = new Uint32Array(n >> 2);
                ov.index.read(pairs, { at: 0 });
                for (let i = 0; i + 1 < pairs.length; i += 2) {
                    ov.map.set(pairs[i], pairs[i + 1]);
                    ov.nextSlot = Math.max(ov.nextSlot, pairs[i + 1] + 1);
                }
            }
            ov.syncFile = await (await dir.getFileHandle('sync', { create: true })).createSyncAccessHandle();
            const s = new Uint32Array(ov.syncFile.getSize() >> 2);
            ov.syncFile.read(s, { at: 0 });
            if (s.length >= 3) {
                ov.sync = { version: s[0], synced: (s[1] & 1) !== 0, adopt: (s[1] & 2) !== 0 };
                for (const b of s.subarray(3, 3 + s[2]))
                    ov.pending.add(b);
            }
        } catch (e) {
            console.log(`[web] ${name}: read-only (${e.message || e})`);
            for (const h of [ov.data, ov.index, ov.syncFile])
                if (h)
                    h.close();
            ov.data = ov.index = ov.syncFile = null;
        }
        return ov;
    }

    get writable() { return this.data !== null; }
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
                this.indexDirty = true;
            } else {
                this.data.write(bytes.subarray(at, at + n), { at: slot * OVERLAY_BLOCK + off });
            }
            this.pending.add(block);
            at += n;
        }
    }

    // A whole block (from the account: sync.js).
    putBlock(block, bytes) {
        let slot = this.map.get(block);
        if (slot === undefined) {
            slot = this.nextSlot++;
            this.map.set(block, slot);
            this.indexDirty = true;
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
        this.indexDirty = true;
    }

    saveSync() {
        if (!this.syncFile)
            return;
        const s = new Uint32Array(3 + this.pending.size);
        s[0] = this.sync.version;
        s[1] = (this.sync.synced ? 1 : 0) | (this.sync.adopt ? 2 : 0);
        s[2] = this.pending.size;
        s.set([...this.pending], 3);
        this.syncFile.truncate(0);
        this.syncFile.write(s, { at: 0 });
        this.syncFile.flush();
    }

    // The block index to OPFS, and both files flushed.
    save() {
        if (this.image.flush)
            this.image.flush();
        if (!this.writable)
            return;
        if (this.indexDirty) {
            const pairs = new Uint32Array(this.map.size * 2);
            let i = 0;
            for (const [block, slot] of this.map) {
                pairs[i++] = block;
                pairs[i++] = slot;
            }
            this.index.truncate(0);
            this.index.write(pairs, { at: 0 });
            this.indexDirty = false;
        }
        this.data.flush();
        this.index.flush();
        this.saveSync();
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

    mount(mount) {
        const root = OVERLAYFS.createNode(null, '/', OVERLAYFS.DIR_MODE);
        for (const ov of mount.opts.overlays)
            OVERLAYFS.createNode(root, ov.name, ov.writable ? OVERLAYFS.FILE_MODE : 0o100444, ov);
        return root;
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
            stream.node.overlay.save();
            return 0;
        },
    },
};
