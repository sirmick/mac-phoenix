import { test, expect } from './fixtures';

test.describe('Codec Controls', () => {
  test('mouse mode dropdown is interactive', async ({ page, emulatorPort }) => {
    await page.goto(`http://localhost:${emulatorPort}/`);

    const select = page.locator('#mouse-mode-select');
    await expect(select).toBeEnabled();

    const options = await select.locator('option').allTextContents();
    expect(options.length).toBeGreaterThanOrEqual(2);
  });
});
