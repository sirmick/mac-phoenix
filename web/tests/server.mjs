// Shared by the server tests: tools/web/server on a free port, with a
// scratch data directory and an owner account ("owner", OWNER_PASSWORD),
// serving SITE (the wasm build's page) and a library of IMAGES (images
// with NAME.img.json records; include: the patterns offered). relay: lines
// for the configuration's [relay] section; public: the page without signing in;
// standalone: mode = "standalone" (no accounts, the server the tab's store);
// with EXECUTOR_STANDALONE_BINARY, that server instead (tools/standalone's,
// its own embedded tab: SITE unused; pair(page) pairs a page's browser
// with it). startGoServer: tools/standalone, built here, on --site SITE.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import net from 'node:net';
import { spawn, execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const SERVER = path.join(ROOT, 'tools/web/server');
export const OWNER_PASSWORD = 'owner-password';

async function freePort() {
    return new Promise((resolve) => {
        const s = net.createServer().listen(0, '127.0.0.1', () => {
            const { port } = s.address();
            s.close(() => resolve(port));
        });
    });
}

export async function startServer(site, { images = '', include = ['*.img'], relay = '', public: open = false,
                                    standalone = false } = {}) {
    const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'executor-web-'));
    const port = await freePort();
    const conf = path.join(scratch, 'server.toml');
    fs.writeFileSync(conf, `[server]\nport = ${port}\nsite = ${JSON.stringify(path.resolve(site))}\n`
        + `data = ${JSON.stringify(path.join(scratch, 'data'))}\n`
        + (open ? 'public = true\n' : '')
        + (standalone ? 'mode = "standalone"\n' : '')
        + `[library]\nimages = ${JSON.stringify(images)}\ninclude = ${JSON.stringify(include)}\n`
        + (relay ? `[relay]\n${relay}\n` : ''));
    const binary = standalone && process.env.EXECUTOR_STANDALONE_BINARY;
    const cli = (...args) => execFileSync('python3', [SERVER, '--config', conf, ...args],
                                          { input: `${OWNER_PASSWORD}\n` }).toString().trim();
    if (!binary)
        cli('adduser', 'owner', '--owner');
    if (binary) {
        const go = await runGo(binary, ['--data', path.join(scratch, 'data'), '--images', images,
                                        '--bind', '127.0.0.1', '--port', `${port}`, '--quiet']);
        return { ...go, stop() { go.stop(); fs.rmSync(scratch, { recursive: true, force: true }); } };
    }
    const child = spawn('python3', [SERVER, '--config', conf, '--quiet'], { stdio: ['ignore', 'pipe', 'inherit'] });
    await new Promise((resolve, reject) => {
        child.stdout.on('data', (d) => { if (d.toString().includes('Serving')) resolve(); });
        child.on('exit', (code) => reject(new Error(`the server exited (${code})`)));
    });
    return {
        url: `http://127.0.0.1:${port}`,
        pair: async () => {},
        invite: () => cli('invite'),
        stop() {
            child.kill();
            fs.rmSync(scratch, { recursive: true, force: true });
        },
    };
}

// tools/standalone's binary with args; once it listens: { url, key,
// pairURL (its printed address, #key=), pair(page) (that page's browser
// paired: POST /api/pair from its context), stop() }. The process: child.
export async function runGo(binary, args) {
    const child = spawn(binary, args, { stdio: ['ignore', 'pipe', 'inherit'] });
    let out = '';
    const [url, pairURL] = await new Promise((resolve, reject) => {
        child.stdout.on('data', (d) => {
            out += d.toString();
            const m = /^Pair a browser: open ((https?:\/\/[^/\s]+)\/#key=[0-9a-f]+)$/m.exec(out);
            if (m)
                resolve([m[2], m[1]]);
        });
        child.on('exit', (code) => reject(new Error(`the Go server exited (${code}): ${out}`)));
    });
    const key = pairURL.split('#key=')[1];
    return {
        url, pairURL, key, child, output: () => out,
        async pair(page, name = 'test') {
            const res = await page.context().request.post(`${url}/api/pair`, { data: { key, name } });
            if (!res.ok())
                throw new Error(`pairing: ${res.status()}`);
        },
        stop() { child.kill(); },
    };
}

// startGoServer: tools/standalone built (go; null without it) and run on
// --site site, a scratch DATA unless given; args added. As runGo, and
// data, settingsFile, bin.
export async function startGoServer(site, { images, data, args = [], bin } = {}) {
    const scratch = fs.mkdtempSync(path.join(process.env.TMPDIR || os.tmpdir(), 'executor-go-'));
    process.on('exit', () => fs.rmSync(scratch, { recursive: true, force: true }));   // (bin may outlive stop())
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
    const go = await runGo(bin, ['--data', data, '--site', path.resolve(site), ...(images ? ['--images', images] : []),
                                 '--bind', '127.0.0.1', '--port', '0', '--quiet', ...args]);
    return { ...go, bin, data, settingsFile: path.join(data, 'settings.json') };
}

// Signs page in through /login (or makes the account, with an invitation),
// then opens the tab at query.
export async function signIn(page, url, name, password, { invite = '', query = '' } = {}) {
    await page.goto(`${url}/login?next=${encodeURIComponent('/' + query)}${invite ? `&invite=${invite}` : ''}`);
    await page.fill('#name', name);
    await page.fill('#password', password);
    await Promise.all([page.waitForURL((u) => !u.pathname.startsWith('/login')), page.click('#go')]);
}
