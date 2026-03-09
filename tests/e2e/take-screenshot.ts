#!/usr/bin/env npx tsx
/**
 * Utility to boot the emulator, connect via browser, and take a screenshot.
 *
 * Usage:
 *   npx tsx tests/e2e/take-screenshot.ts [options]
 *
 * Options:
 *   --rom <path>        ROM file (default: /home/mick/quadra.rom or MACEMU_ROM)
 *   --disk <path>       Hard drive image (repeatable)
 *   --cdrom <path>      CD-ROM image (repeatable)
 *   --wait <seconds>    Time to wait after boot before screenshot (default: 15)
 *   --port <number>     HTTP port (default: 18094)
 *   --output <path>     Screenshot output path (default: /tmp/screenshot.png)
 *   --dismiss-dialog    Press Return to dismiss startup dialog before screenshot
 */

import { chromium } from '@playwright/test';
import { spawn, ChildProcess } from 'child_process';
import * as path from 'path';
import * as fs from 'fs';

function parseArgs(): {
  rom: string;
  disks: string[];
  cdroms: string[];
  wait: number;
  port: number;
  output: string;
  dismissDialog: boolean;
} {
  const args = process.argv.slice(2);
  const result = {
    rom: process.env.MACEMU_ROM || '/home/mick/quadra.rom',
    disks: [] as string[],
    cdroms: [] as string[],
    wait: 15,
    port: 18094,
    output: '/tmp/screenshot.png',
    dismissDialog: false,
  };

  for (let i = 0; i < args.length; i++) {
    switch (args[i]) {
      case '--rom':
        result.rom = args[++i];
        break;
      case '--disk':
        result.disks.push(args[++i]);
        break;
      case '--cdrom':
        result.cdroms.push(args[++i]);
        break;
      case '--wait':
        result.wait = parseInt(args[++i]);
        break;
      case '--port':
        result.port = parseInt(args[++i]);
        break;
      case '--output':
        result.output = args[++i];
        break;
      case '--dismiss-dialog':
        result.dismissDialog = true;
        break;
      case '--help':
      case '-h':
        console.log(`Usage: npx tsx tests/e2e/take-screenshot.ts [options]

Options:
  --rom <path>        ROM file (default: MACEMU_ROM or /home/mick/quadra.rom)
  --disk <path>       Hard drive image (repeatable)
  --cdrom <path>      CD-ROM image (repeatable)
  --wait <seconds>    Time to wait after boot before screenshot (default: 15)
  --port <number>     HTTP port (default: 18094)
  --output <path>     Screenshot output path (default: /tmp/screenshot.png)
  --dismiss-dialog    Press Return to dismiss startup dialog`);
        process.exit(0);
        break;
      default:
        console.error(`Unknown option: ${args[i]}`);
        process.exit(1);
    }
  }
  return result;
}

async function waitForServer(port: number, timeoutMs = 30_000): Promise<void> {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      const resp = await fetch(`http://localhost:${port}/api/status`);
      if (resp.ok) return;
    } catch {
      // not ready
    }
    await new Promise(r => setTimeout(r, 200));
  }
  throw new Error(`Server did not start within ${timeoutMs}ms`);
}

async function waitForBoot(port: number, timeoutMs = 120_000): Promise<void> {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      const resp = await fetch(`http://localhost:${port}/api/status`);
      if (resp.ok) {
        const body = await resp.json() as { boot_phase: string };
        if (body.boot_phase === 'Finder' || body.boot_phase === 'desktop') return;
      }
    } catch {
      // not ready
    }
    await new Promise(r => setTimeout(r, 500));
  }
  throw new Error(`Boot did not reach Finder within ${timeoutMs}ms`);
}

async function main() {
  const opts = parseArgs();
  const sigPort = opts.port + 1;
  const buildDir = path.resolve(__dirname, '../../build');
  const binary = path.join(buildDir, 'mac-phoenix');

  if (!fs.existsSync(binary)) {
    console.error(`Binary not found: ${binary}. Run 'ninja -C build' first.`);
    process.exit(1);
  }
  if (!fs.existsSync(opts.rom)) {
    console.error(`ROM not found: ${opts.rom}`);
    process.exit(1);
  }

  // Build emulator args
  const emuArgs = [
    '--backend', 'uae',
    '--port', String(opts.port),
    '--signaling-port', String(sigPort),
    opts.rom,
  ];
  for (const disk of opts.disks) {
    emuArgs.push('--disk', disk);
  }
  for (const cdrom of opts.cdroms) {
    emuArgs.push('--cdrom', cdrom);
  }

  console.log(`Starting emulator on port ${opts.port}...`);
  const proc = spawn(binary, emuArgs, {
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  proc.stderr?.on('data', (d: Buffer) => process.stderr.write(`[emu] ${d}`));

  let exitCode = 0;
  try {
    await waitForServer(opts.port);
    console.log('Server ready, waiting for boot...');

    await waitForBoot(opts.port);
    console.log('Booted to Finder.');

    if (opts.dismissDialog) {
      await fetch(`http://localhost:${opts.port}/api/keypress`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ key: 'return' }),
      });
      console.log('Dismissed dialog.');
      await new Promise(r => setTimeout(r, 2000));
    }

    if (opts.wait > 0) {
      console.log(`Waiting ${opts.wait}s...`);
      await new Promise(r => setTimeout(r, opts.wait * 1000));
    }

    // Launch browser and take screenshot
    const browser = await chromium.launch({
      headless: true,
      args: ['--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream'],
    });
    const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
    await page.goto(`http://localhost:${opts.port}`);
    await page.waitForTimeout(3000);

    // Switch codec to PNG (headless Chrome can't decode H.264)
    const selects = await page.$$('select');
    for (const sel of selects) {
      const options = await sel.$$eval('option', opts => opts.map(o => o.value));
      if (options.includes('png')) {
        await sel.selectOption('png');
        break;
      }
    }
    await page.waitForTimeout(5000);

    // Hide debug console for clean screenshot
    const debugBtn = await page.$('#debug-toggle');
    if (debugBtn) {
      const debugPanel = await page.$('#debug-panel');
      if (debugPanel && await debugPanel.isVisible()) {
        await debugBtn.click();
        await page.waitForTimeout(500);
      }
    }

    await page.screenshot({ path: opts.output });
    console.log(`Screenshot saved: ${opts.output}`);

    await browser.close();
  } catch (err) {
    console.error(err);
    exitCode = 1;
  } finally {
    proc.kill('SIGTERM');
    await new Promise<void>(resolve => {
      proc.on('exit', () => resolve());
      setTimeout(() => { proc.kill('SIGKILL'); resolve(); }, 5000);
    });
  }
  process.exit(exitCode);
}

main();
