import { test, chromium } from '@playwright/test';

test('take readme screenshots', async () => {
  const browser = await chromium.launch({
    headless: true,
    args: ['--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream'],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });

  await page.goto('http://localhost:8085');
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

  // Wait for video stream to connect and render
  await page.waitForTimeout(15000);

  // Dismiss "improper shutdown" dialog via API keypress
  await fetch('http://localhost:8085/api/keypress', {
    method: 'POST',
    body: JSON.stringify({ key: 'return' }),
  });
  await page.waitForTimeout(3000);

  // Hide debug console for clean screenshot
  const debugBtn = await page.$('#debug-toggle');
  if (debugBtn) {
    const debugPanel = await page.$('#debug-panel');
    if (debugPanel && await debugPanel.isVisible()) {
      await debugBtn.click();
      await page.waitForTimeout(500);
    }
  }

  await page.screenshot({ path: '/tmp/browser_desktop.png' });
  console.log('Screenshot 1: desktop saved');

  // Show debug panel, open Settings
  if (debugBtn) await debugBtn.click();
  await page.waitForTimeout(300);

  const settingsBtn = await page.$('button:has-text("Settings")');
  if (settingsBtn) {
    await settingsBtn.click();
    await page.waitForTimeout(1000);
  }
  await page.screenshot({ path: '/tmp/browser_config.png' });
  console.log('Screenshot 2: config saved');

  await browser.close();
});
