// The network relay (executor/server's /relay, host/wasm/net.cpp): the
// tab's MacTCP and Open Transport against an echo server on this host,
// through the relay. MacPhoenixSuite's network and mactcp
// sections and OTTestRetro68K (Open Transport, Executor's own) report all
// PASS, as natively (mactcp_executor, ot_retro68_68k_executor); their
// passive opens and listeners connect inside the tab.
//
//   node executor/tests/web/tab_relay.mjs DIR GUEST_DIR
//
// The relay may reach this host here (allow_private): the echo server is on
// it, at the address the guest knows as 127.0.0.1.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { launch, newPage, firstFrame, request, until } from './harness.mjs';
import { startServer } from './server.mjs';

const [dir, guest] = process.argv.slice(2);
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
let failed = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
    failed += !ok;
};

const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'tab-relay-'));
const portFile = path.join(scratch, 'echo_port');
const echo = spawn('python3', [path.join(ROOT, 'tests/lib/echo_server.py'), portFile], { stdio: 'inherit' });
const echoPort = await until(async () => fs.existsSync(portFile) && fs.readFileSync(portFile, 'utf8').trim(), 5000, 100);
const server = await startServer(dir, { relay: { ports: ['1-65535'], allow_private: true } });
if (!server) {
    console.log('SKIP: no go');
    echo.kill();
    process.exit(77);
}
const browser = await launch();

// One run of an uploaded application: its results file, ending in "---".
async function run(page, app, results, cfg) {
    if (cfg)
        await request(page, 'write', ['/host/suite.cfg', cfg]);
    check(await request(page, 'launch', `executor:Uploads:${app}`) === 0, `launch ${app}`);
    const t = await until(() => request(page, 'read', `/host/${results}`)
                                    .then((x) => x && x.includes('---') && x), 120000, 500);
    console.log(t || `(no ${results})`);
    return t || '';
}
const fails = (t) => t.split(/\r?\n|\r/).filter((l) => /^FAIL/.test(l));
const passes = (t) => t.split(/\r?\n|\r/).filter((l) => /^PASS/.test(l)).length;

try {
    const page = await newPage(browser);
    await page.goto(`${server.url}/`);
    await firstFrame(page);
    await page.setInputFiles('#add', [path.join(guest, 'suite/MacPhoenixSuite.bin'),
                                      path.join(guest, 'ot/OTTestRetro68K.bin')]);
    await firstFrame(page);   // restarted with the uploads
    check(await until(() => request(page, 'app').then((a) => a === 'Browser'), 30000), 'Browser up');
    await request(page, 'write', ['/host/net_echo.txt', `127.0.0.1 ${echoPort}\r`]);

    // MacTCP: the driver, the resolver (example.com through the host's DNS),
    // TCP and UDP echo, then the mactcp section in depth.
    const tcp = await run(page, 'MacPhoenixSuite.bin', 'mactcp_results.txt',
                          'mactcp_results.txt\rnetwork mactcp\r');
    check(passes(tcp) >= 20 && fails(tcp).length === 0,
          `MacPhoenixSuite network + mactcp: ${passes(tcp)} PASS, ${fails(tcp).length} FAIL`);

    // Open Transport.
    const ot = await run(page, 'OTTestRetro68K.bin', 'ot_results.txt');
    check(passes(ot) >= 10 && fails(ot).length === 0 && /DONE 0/.test(ot),
          `OTTestRetro68K: ${passes(ot)} PASS, ${fails(ot).length} FAIL`);
    await page.locator('#screen').screenshot({ path: path.join(dir, 'tab_relay.png') });
} finally {
    await browser.close();
    server.stop();
    echo.kill();
    fs.rmSync(scratch, { recursive: true, force: true });
}
console.log(failed ? `${failed} check(s) failed` : 'all passed');
process.exit(failed ? 1 : 0);
