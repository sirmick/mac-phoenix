// Shared by the server tests: executor/server (the Go server, built here
// with go, $GO) on a free port, serving SITE (the wasm build's page) with a
// scratch data directory and a library of IMAGES (images with NAME.img.json
// records; include: the patterns offered). relay: settings for the relay
// (network.relay_*: { ports, allow_private, ... }). The first page to load
// the tab has the server (claim.go). EXECUTOR_STANDALONE_BINARY: that
// binary instead, its own embedded tab (SITE unused).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn, execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

// executor/server's binary with args; once it listens: { url (its
// printed address, no slash), stop() }. The process: child.
export async function runGo(binary, args) {
    const child = spawn(binary, args, { stdio: ['ignore', 'pipe', 'inherit'] });
    let out = '';
    const url = await new Promise((resolve, reject) => {
        child.stdout.on('data', (d) => {
            out += d.toString();
            const m = /^Open (https?:\/\/[^/\s]+)\/ /m.exec(out);
            if (m)
                resolve(m[1]);
        });
        child.on('exit', (code) => reject(new Error(`the Go server exited (${code}): ${out}`)));
    });
    return { url, child, output: () => out, stop() { child.kill(); } };
}

// A library the test didn't make (outside TMPDIR: ~/storage/web-library,
// ~/storage/images) seen through a directory of links that can't be
// written: the server keeps a library disk's changes beside the image when
// it can (overlay.go), where every other server on that library (a site's
// own included) would find them; so they go to the scratch DATA instead.
// An overlay that turns up in the library all the same fails the test.
function libraryView(images, scratch) {
    const real = fs.realpathSync(images);
    const tmp = fs.realpathSync(process.env.TMPDIR || os.tmpdir());
    if (real === tmp || real.startsWith(tmp + path.sep))
        return images;
    const view = path.join(scratch, 'library');
    fs.mkdirSync(view);
    for (const f of fs.readdirSync(real))
        if (f !== 'uploads' && !f.endsWith('.overlay'))
            fs.symlinkSync(path.join(real, f), path.join(view, f));
    fs.mkdirSync(path.join(view, 'uploads'));               // the server's, writable
    fs.chmodSync(view, 0o555);
    const overlays = () => {
        const found = new Map();
        for (const dir of [real, path.join(real, 'uploads')])
            for (const f of fs.existsSync(dir) ? fs.readdirSync(dir) : [])
                if (f.endsWith('.overlay'))
                    found.set(path.join(dir, f), fs.statSync(path.join(dir, f)).mtimeMs);
        return found;
    };
    const before = overlays();
    process.on('exit', () => {
        const wrote = [...overlays()].filter(([f, t]) => before.get(f) !== t).map(([f]) => f);
        if (wrote.length) {
            console.log(`FAIL: the test wrote into the library ${real}: ${wrote.join(', ')}`);
            process.exitCode = 1;
        }
    });
    return view;
}

// startServer: executor/server built (go; null without it) and run on
// --site site, a scratch DATA unless given (its settings.json made from
// include and relay when there is none); args added. As runGo, and data,
// settingsFile, bin.
export async function startServer(site, { images, include, relay, data, args = [], bin } = {}) {
    const scratch = fs.mkdtempSync(path.join(process.env.TMPDIR || os.tmpdir(), 'executor-go-'));
    process.on('exit', () => {                          // (bin may outlive stop())
        try {
            fs.chmodSync(path.join(scratch, 'library'), 0o755);   // libraryView's
        } catch {}
        fs.rmSync(scratch, { recursive: true, force: true });
    });
    const embedded = !bin && process.env.EXECUTOR_STANDALONE_BINARY;
    if (embedded)
        bin = embedded;
    if (!bin) {
        bin = path.join(scratch, 'executor-standalone');
        try {
            execFileSync(process.env.GO || 'go', ['build', '-o', bin, '.'],
                         { cwd: path.join(ROOT, 'executor/server'), stdio: 'inherit', env: { ...process.env, CGO_ENABLED: '0' } });
        } catch (e) {
            return null;
        }
    }
    data = data || path.join(scratch, 'data');
    if (images)
        images = libraryView(images, scratch);
    const settingsFile = path.join(data, 'settings.json');
    if ((include || relay) && !fs.existsSync(settingsFile)) {
        const network = {};
        for (const [k, v] of Object.entries(relay || {}))
            network[`relay_${k}`] = v;
        fs.mkdirSync(data, { recursive: true });
        fs.writeFileSync(settingsFile, JSON.stringify({ version: 2, ...(include ? { disks: { include } } : {}),
                                                        ...(relay ? { network } : {}) }));
    }
    const go = await runGo(bin, ['--data', data, ...(embedded ? [] : ['--site', path.resolve(site)]),
                                 ...(images ? ['--images', images] : []),
                                 '--bind', '127.0.0.1', '--port', '0', '--quiet', ...args]);
    return { ...go, bin, data, settingsFile };
}
