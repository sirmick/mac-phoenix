import { test as base } from '@playwright/test';
import { spawn, ChildProcess } from 'child_process';
import * as path from 'path';
import * as fs from 'fs';

const HTTP_PORT = parseInt(process.env.MACEMU_HTTP_PORT || '18094');
const SIG_PORT = HTTP_PORT + 1;
const ROM_PATH = process.env.MACEMU_ROM || '/home/mick/quadra.rom';
const BUILD_DIR = path.resolve(__dirname, '../../build');
const BINARY = path.join(BUILD_DIR, 'mac-phoenix');

async function waitForServer(port: number, timeoutMs = 10_000): Promise<void> {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      const resp = await fetch(`http://localhost:${port}/api/status`);
      if (resp.ok) return;
    } catch {
      // Server not ready yet
    }
    await new Promise(r => setTimeout(r, 200));
  }
  throw new Error(`Server did not start within ${timeoutMs}ms`);
}

async function waitForBootPhase(port: number, phase: string, timeoutMs = 30_000): Promise<string> {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      const resp = await fetch(`http://localhost:${port}/api/status`);
      if (resp.ok) {
        const body = await resp.json();
        if (body.boot_phase === phase) return body.boot_phase;
      }
    } catch {
      // Not ready yet
    }
    await new Promise(r => setTimeout(r, 500));
  }
  throw new Error(`Boot did not reach phase '${phase}' within ${timeoutMs}ms`);
}

type EmulatorFixture = {
  emulatorPort: number;
  hasRom: boolean;
};

// Shared emulator process across tests in the same worker.
// Auto-spawns the emulator so tests don't need an external process.
let sharedEmulatorProc: ChildProcess | null = null;

export const test = base.extend<{}, EmulatorFixture>({
  emulatorPort: [async ({}, use) => {
    // Spawn emulator once per worker if not already running
    if (!sharedEmulatorProc) {
      sharedEmulatorProc = await spawnEmulator({ timeoutSeconds: 300 });
    }
    await use(HTTP_PORT);
  }, { scope: 'worker' }],

  hasRom: [async ({}, use) => {
    await use(fs.existsSync(ROM_PATH));
  }, { scope: 'worker' }],
});

// Re-export expect
export { expect } from '@playwright/test';

// Helper to spawn the emulator as a child process
export async function spawnEmulator(opts?: { timeoutSeconds?: number }): Promise<ChildProcess> {
  if (!fs.existsSync(BINARY)) {
    throw new Error(`Binary not found: ${BINARY}. Run 'ninja -C build' first.`);
  }

  const timeout = opts?.timeoutSeconds ?? 60;
  const args = [
    '--backend', 'uae',
    '--timeout', String(timeout),
    '--port', String(HTTP_PORT),
    '--signaling-port', String(SIG_PORT),
  ];
  if (fs.existsSync(ROM_PATH)) {
    args.push(ROM_PATH);
  }

  const proc = spawn(BINARY, args, {
    env: process.env,
    stdio: ['ignore', 'pipe', 'pipe'],
  });

  proc.stderr?.on('data', (data) => {
    if (process.env.DEBUG_EMULATOR) {
      process.stderr.write(`[emu] ${data}`);
    }
  });

  await waitForServer(HTTP_PORT);
  return proc;
}

export function killEmulator(proc: ChildProcess): Promise<void> {
  return new Promise((resolve) => {
    if (proc.killed || proc.exitCode !== null) {
      resolve();
      return;
    }
    proc.on('exit', () => resolve());
    proc.kill('SIGTERM');
    setTimeout(() => {
      if (!proc.killed) proc.kill('SIGKILL');
    }, 5000);
  });
}

export { waitForBootPhase };
