import { test, expect } from './fixtures';

test.describe('UI Basic', () => {
  test('page loads without JS errors', async ({ page, emulatorPort }) => {
    const errors: string[] = [];
    page.on('pageerror', (err) => errors.push(err.message));

    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.waitForLoadState('networkidle');

    expect(errors).toEqual([]);
  });

  test('key UI elements are present', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    // Header controls
    await expect(page.locator('#start-btn')).toBeVisible();
    await expect(page.locator('#stop-btn')).toBeVisible();
    await expect(page.locator('#config-btn')).toBeVisible();
    await expect(page.locator('#codec-select')).toBeVisible();
    await expect(page.locator('#mouse-mode-select')).toBeVisible();

    // Display area
    await expect(page.locator('#display-container')).toBeVisible();

    // Status indicators
    await expect(page.locator('#fps-display')).toBeVisible();
    await expect(page.locator('#connection-icon')).toBeVisible();
  });

  test('codec dropdown has expected options', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    const options = await page.locator('#codec-select option').allTextContents();
    // Should have at least PNG (always available)
    expect(options.length).toBeGreaterThan(0);
  });

  test('mouse mode dropdown has options', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    const options = await page.locator('#mouse-mode-select option').allTextContents();
    expect(options.length).toBeGreaterThanOrEqual(2);
  });

  test('status API returns JSON', async ({ request, emulatorPort }) => {
    const resp = await request.get(`http://localhost:${emulatorPort}/api/status`);
    expect(resp.ok()).toBeTruthy();
    const body = await resp.json();
    expect(body).toHaveProperty('emulator_connected');
    expect(body).toHaveProperty('emulator_running');
  });
});
