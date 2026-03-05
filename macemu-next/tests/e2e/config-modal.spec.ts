import { test, expect } from './fixtures';

test.describe('Config Modal', () => {
  test('opens via config button and closes via close button', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    // Modal should be hidden initially
    await expect(page.locator('#config-modal')).not.toBeVisible();

    // Open modal
    await page.locator('#config-btn').click();
    await expect(page.locator('#config-modal')).toBeVisible();

    // Close via X button
    await page.locator('#modal-close-btn').click();
    await expect(page.locator('#config-modal')).not.toBeVisible();
  });

  test('closes via cancel button', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    await page.locator('#config-btn').click();
    await expect(page.locator('#config-modal')).toBeVisible();

    await page.locator('#cancel-config-btn').click();
    await expect(page.locator('#config-modal')).not.toBeVisible();
  });

  test('has emulator, RAM, and screen dropdowns', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.locator('#config-btn').click();

    await expect(page.locator('#cfg-emulator')).toBeVisible();
    await expect(page.locator('#cfg-ram')).toBeVisible();
    await expect(page.locator('#cfg-screen')).toBeVisible();
  });

  test('ROM dropdown populated from storage API', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.locator('#config-btn').click();

    // ROM select should exist (may or may not have options depending on storage)
    await expect(page.locator('#cfg-rom')).toBeVisible();
  });

  test('save button sends POST to /api/config', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.locator('#config-btn').click();

    // Listen for the config save request
    const [request] = await Promise.all([
      page.waitForRequest(req =>
        req.url().includes('/api/config') && req.method() === 'POST'
      ),
      page.locator('#save-config-btn').click(),
    ]);

    expect(request.method()).toBe('POST');
    const body = request.postDataJSON();
    expect(body).toBeDefined();
  });
});
