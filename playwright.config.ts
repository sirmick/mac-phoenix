import { defineConfig } from '@playwright/test';
import { execFileSync } from 'node:child_process';

// The emulator's port: a free one for nominal 18094 (tests/lib/ports.py,
// docs/Testing.md "Ports"), set before the workers start so fixtures.ts and
// the specs read the same one.
process.env.MACEMU_HTTP_PORT ||= execFileSync('python3',
  [`${__dirname}/tests/lib/ports.py`, 'port', '18094', '--pid', String(process.pid)], { encoding: 'utf8' }).trim();

export default defineConfig({
  testDir: './tests/e2e',
  timeout: 60_000,
  retries: 0,
  workers: 1,
  use: {
    baseURL: `http://localhost:${process.env.MACEMU_HTTP_PORT}`,
    headless: true,
    video: undefined,
  },
  projects: [
    {
      name: 'chromium',
      use: {
        browserName: 'chromium',
      },
    },
  ],
});
