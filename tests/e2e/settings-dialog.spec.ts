import { test, expect, spawnEmulator, killEmulator } from './fixtures';
import { ChildProcess } from 'child_process';

let emu: ChildProcess;

test.describe('Settings Dialog', () => {
  test.beforeAll(async () => {
    emu = await spawnEmulator({ timeoutSeconds: 30 });
  });

  test.afterAll(async () => {
    if (emu) await killEmulator(emu);
  });

  test('controls are disabled during load, then enabled', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    // Open settings - controls should be disabled briefly during load
    await page.locator('#config-btn').click();
    await expect(page.locator('#config-modal')).toBeVisible();

    // Wait for controls to be enabled (lists loaded)
    await expect(page.locator('#cfg-ram')).toBeEnabled({ timeout: 5000 });
    await expect(page.locator('#cfg-screen')).toBeEnabled();
    await expect(page.locator('#cfg-emulator')).toBeEnabled();
    await expect(page.locator('#save-config-btn')).toBeEnabled();
  });

  test('boot priority dropdown exists and has options', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.locator('#config-btn').click();
    await expect(page.locator('#cfg-bootdriver')).toBeEnabled({ timeout: 5000 });

    // Should have "Any" and "CD-ROM" options
    const options = await page.locator('#cfg-bootdriver option').allTextContents();
    expect(options).toContain('Any (first bootable disk)');
    expect(options).toContain('CD-ROM');
  });

  test('disk checkboxes populated from storage', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.locator('#config-btn').click();

    // Wait for disk list to load (checkboxes replace "Loading...")
    await expect(page.locator('#disk-list input[type="checkbox"]').first()).toBeVisible({ timeout: 5000 });

    const diskCount = await page.locator('#disk-list input[type="checkbox"]').count();
    expect(diskCount).toBeGreaterThan(0);
  });

  test('cdrom checkboxes populated from storage', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.locator('#config-btn').click();

    await expect(page.locator('#cdrom-list input[type="checkbox"]').first()).toBeVisible({ timeout: 5000 });

    const cdromCount = await page.locator('#cdrom-list input[type="checkbox"]').count();
    expect(cdromCount).toBeGreaterThan(0);
  });

  test('ROM dropdown populated with options', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.locator('#config-btn').click();

    // Wait for ROM list to load
    await expect(page.locator('#cfg-rom')).toBeEnabled({ timeout: 5000 });
    const romOptions = await page.locator('#cfg-rom option').count();
    expect(romOptions).toBeGreaterThan(1); // More than just "Loading..."
  });

  test('saved config values reflected in dialog', async ({ page, request, emulatorPort }) => {
    // Save a known config via API
    await request.post(`http://localhost:${emulatorPort}/api/config`, {
      data: { ram_mb: 64, bootdriver: -62 },
    });

    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.locator('#config-btn').click();

    // Wait for controls to load
    await expect(page.locator('#cfg-ram')).toBeEnabled({ timeout: 5000 });

    // Verify values match
    await expect(page.locator('#cfg-ram')).toHaveValue('64');
    await expect(page.locator('#cfg-bootdriver')).toHaveValue('-62');
  });

  test('save config persists and reloads correctly', async ({ page, request, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.locator('#config-btn').click();
    await expect(page.locator('#cfg-ram')).toBeEnabled({ timeout: 5000 });

    // Change RAM to 128MB
    await page.locator('#cfg-ram').selectOption('128');

    // Change boot priority to CD-ROM
    await page.locator('#cfg-bootdriver').selectOption('-62');

    // Save
    await page.locator('#save-config-btn').click();

    // Modal should close
    await expect(page.locator('#config-modal')).not.toBeVisible();

    // Reopen and verify values persisted
    await page.locator('#config-btn').click();
    await expect(page.locator('#cfg-ram')).toBeEnabled({ timeout: 5000 });
    await expect(page.locator('#cfg-ram')).toHaveValue('128');
    await expect(page.locator('#cfg-bootdriver')).toHaveValue('-62');

    // Verify via API too
    const resp = await request.get(`http://localhost:${emulatorPort}/api/config`);
    const config = await resp.json();
    expect(config.ram_mb).toBe(128);
    expect(config.bootdriver).toBe(-62);
  });
});
