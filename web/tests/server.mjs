// Shared by the server tests: tools/web/server on a free port, with a
// scratch data directory and an owner account ("owner", OWNER_PASSWORD),
// serving SITE (the wasm build's page) and a library of IMAGES (images
// with NAME.img.json records; include: the patterns offered). relay: lines
// for the configuration's [relay] section; public: the page without signing in;
// standalone: mode = "standalone" (no accounts, the server the tab's store);
// with EXECUTOR_STANDALONE_BINARY, that server instead (tools/standalone's,
// its own embedded tab: SITE unused).
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
    const child = binary
        ? spawn(binary, ['-data', path.join(scratch, 'data'), '-images', images, '-port', `${port}`, '-quiet'],
                { stdio: ['ignore', 'pipe', 'inherit'] })
        : spawn('python3', [SERVER, '--config', conf, '--quiet'], { stdio: ['ignore', 'pipe', 'inherit'] });
    await new Promise((resolve, reject) => {
        child.stdout.on('data', (d) => { if (d.toString().includes('Serving')) resolve(); });
        child.on('exit', (code) => reject(new Error(`the server exited (${code})`)));
    });
    return {
        url: `http://127.0.0.1:${port}`,
        invite: () => cli('invite'),
        stop() {
            child.kill();
            fs.rmSync(scratch, { recursive: true, force: true });
        },
    };
}

// Signs page in through /login (or makes the account, with an invitation),
// then opens the tab at query.
export async function signIn(page, url, name, password, { invite = '', query = '' } = {}) {
    await page.goto(`${url}/login?next=${encodeURIComponent('/' + query)}${invite ? `&invite=${invite}` : ''}`);
    await page.fill('#name', name);
    await page.fill('#password', password);
    await Promise.all([page.waitForURL((u) => !u.pathname.startsWith('/login')), page.click('#go')]);
}
