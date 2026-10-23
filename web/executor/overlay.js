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
// FileReaderSync slice); writes go to the overlay.
'use strict';

const OVERLAY_BLOCK = 4096;

// Emscripten's errno values.
const EIO = 29, EINVAL = 28, ENOENT = 44, EPERM = 63, EROFS = 69;

class Overlay {
    constructor(name, file, key) {
        this.name = name;
        this.file = file;
        this.key = key;
        this.size = file.size;
        this.map = new Map();           // block -> slot
        this.nextSlot = 0;
        this.data = null;               // sync access handles; null: read-only
        this.index = null;
        this.indexDirty = false;
        this.reader = new FileReaderSync();
    }

    static async open(root, name, file, key) {
        const ov = new Overlay(name, file, key);
        try {
            const overlays = await root.getDirectoryHandle('overlays', { create: true });
            // Overlays of this image's other versions: theirs, not ours.
            for await (const [dirName, handle] of overlays.entries()) {
                if (handle.kind !== 'directory' || dirName === key)
                    continue;
                try {
                    const meta = JSON.parse(await (await (await handle.getFileHandle('meta.json')).getFile()).text());
                    if (meta.name === name)
                        await overlays.removeEntry(dirName, { recursive: true });
                } catch { }
            }
            const dir = await overlays.getDirectoryHandle(key, { create: true });
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
        } catch (e) {
            console.log(`[web] ${name}: read-only (${e.message || e})`);
            if (ov.data)
                ov.data.close();
            ov.data = ov.index = null;
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
            out.set(new Uint8Array(this.reader.readAsArrayBuffer(this.file.slice(p, p + n))), at);
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
                    buf.set(new Uint8Array(this.reader.readAsArrayBuffer(
                        this.file.slice(start, Math.min(start + OVERLAY_BLOCK, this.size)))));
                buf.set(bytes.subarray(at, at + n), off);
                slot = this.nextSlot++;
                this.data.write(buf, { at: slot * OVERLAY_BLOCK });
                this.map.set(block, slot);
                this.indexDirty = true;
            } else {
                this.data.write(bytes.subarray(at, at + n), { at: slot * OVERLAY_BLOCK + off });
            }
            at += n;
        }
    }

    // The block index to OPFS, and both files flushed.
    save() {
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
                parts.push(this.file.slice(clean, start));
            const n = Math.min(OVERLAY_BLOCK, this.size - start);
            const buf = new Uint8Array(n);
            this.data.read(buf, { at: slot * OVERLAY_BLOCK });
            parts.push(buf);
            clean = start + n;
        }
        if (clean < this.size)
            parts.push(this.file.slice(clean, this.size));
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
