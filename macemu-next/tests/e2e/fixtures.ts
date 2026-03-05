import { test as base } from '@playwright/test';
import { spawn, ChildProcess } from 'child_process';
import * as path from 'path';
import * as fs from 'fs';

const HTTP_PORT = parseInt(process.env.MACEMU_HTTP_PORT || '8080');
const ROM_PATH = process.env.MACEMU_ROM_PATH || '/home/mick/quadra.rom';
const BUILD_DIR = path.resolve(__dirname, '../../build');
const BINARY = path.join(BUILD_DIR, 'macemu-next');

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

type EmulatorFixture = {
  emulatorPort: number;
  hasRom: boolean;
};

// Shared emulator process across tests in the same worker
export const test = base.extend<{}, EmulatorFixture>({
  emulatorPort: [async ({}, use) => {
    await use(HTTP_PORT);
  }, { scope: 'worker' }],

  hasRom: [async ({}, use) => {
    await use(fs.existsSync(ROM_PATH));
  }, { scope: 'worker' }],
});

// Re-export expect
export { expect } from '@playwright/test';

// Helper to spawn the emulator as a child process for tests that need it
export async function spawnEmulator(): Promise<ChildProcess> {
  if (!fs.existsSync(BINARY)) {
    throw new Error(`Binary not found: ${BINARY}. Run 'ninja -C build' first.`);
  }

  const args = [ROM_PATH];
  if (!fs.existsSync(ROM_PATH)) {
    // No ROM - start in webserver-only mode (no ROM arg)
    args.length = 0;
  }

  const proc = spawn(BINARY, args, {
    env: {
      ...process.env,
      MACEMU_HTTP_PORT: String(HTTP_PORT),
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });

  // Log stderr for debugging
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
    // Force kill after 5s
    setTimeout(() => {
      if (!proc.killed) proc.kill('SIGKILL');
    }, 5000);
  });
}
