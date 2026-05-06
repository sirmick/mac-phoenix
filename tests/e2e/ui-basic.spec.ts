import { test, expect } from './fixtures';

test.describe('UI Basic', () => {
  test('page loads without JS errors', async ({ page, emulatorPort }) => {
    const errors: string[] = [];
    page.on('pageerror', (err) => errors.push(err.message));

    await page.goto(`http://localhost:${emulatorPort}/`);
    await page.waitForLoadState('domcontentloaded');

    expect(errors).toEqual([]);
  });

  test('key UI elements are present', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    // Both start and stop live in the DOM; one is visible at a time based
    // on emulator state. Just assert they're attached.
    await expect(page.locator('#start-btn')).toBeAttached();
    await expect(page.locator('#stop-btn')).toBeAttached();
    await expect(page.locator('#config-btn')).toBeVisible();
    await expect(page.locator('#codec-select')).toBeVisible();
    await expect(page.locator('#mouse-mode-select')).toBeVisible();

    // Display area
    await expect(page.locator('#display-container')).toBeVisible();
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
