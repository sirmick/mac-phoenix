import { test, expect } from './fixtures';

test.describe('Codec Controls', () => {
  test('codec dropdown is interactive', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    const select = page.locator('#codec-select');
    await expect(select).toBeEnabled();

    // Get all options
    const options = await select.locator('option').allTextContents();
    expect(options.length).toBeGreaterThan(0);
  });

  test('changing codec sends POST /api/codec', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    const select = page.locator('#codec-select');
    const options = await select.locator('option').allInnerTexts();

    // Skip if only one option
    test.skip(options.length < 2, 'Need at least 2 codec options');

    // Select the second option and wait for the API call
    const [request] = await Promise.all([
      page.waitForRequest(req =>
        req.url().includes('/api/codec') && req.method() === 'POST',
        { timeout: 5000 }
      ),
      select.selectOption({ index: 1 }),
    ]);

    expect(request.method()).toBe('POST');
  });

  test('mouse mode dropdown is interactive', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    const select = page.locator('#mouse-mode-select');
    await expect(select).toBeEnabled();

    const options = await select.locator('option').allTextContents();
    expect(options.length).toBeGreaterThanOrEqual(2);
  });
});
