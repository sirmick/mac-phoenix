// Shared by the server tests: tools/standalone (the Go server, built here
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

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

// tools/standalone's binary with args; once it listens: { url (its
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

// startServer: tools/standalone built (go; null without it) and run on
// --site site, a scratch DATA unless given (its settings.json made from
// include and relay when there is none); args added. As runGo, and data,
// settingsFile, bin.
export async function startServer(site, { images, include, relay, data, args = [], bin } = {}) {
    const scratch = fs.mkdtempSync(path.join(process.env.TMPDIR || os.tmpdir(), 'executor-go-'));
    process.on('exit', () => fs.rmSync(scratch, { recursive: true, force: true }));   // (bin may outlive stop())
    const embedded = !bin && process.env.EXECUTOR_STANDALONE_BINARY;
    if (embedded)
        bin = embedded;
    if (!bin) {
        bin = path.join(scratch, 'executor-standalone');
        try {
            execFileSync(process.env.GO || 'go', ['build', '-o', bin, '.'],
                         { cwd: path.join(ROOT, 'tools/standalone'), stdio: 'inherit', env: { ...process.env, CGO_ENABLED: '0' } });
        } catch (e) {
            return null;
        }
    }
    data = data || path.join(scratch, 'data');
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
