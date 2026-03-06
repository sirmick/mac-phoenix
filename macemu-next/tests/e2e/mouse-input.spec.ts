/**
 * Full-stack mouse input test
 *
 * Tests the complete input pipeline:
 *   Browser JS -> DataChannel binary -> WebRTC server -> ADB -> Mac OS
 *
 * Requires: emulator running with web server (./build/macemu-next /path/to/rom)
 * The test connects via the web UI, establishes WebRTC, and sends mouse events
 * through the data channel, verifying the connection stays healthy.
 */
import { test, expect } from '@playwright/test';

// Wait for emulator web server to be ready
test.beforeAll(async ({ }, testInfo) => {
    // Quick check that the server is up
    const baseURL = testInfo.project.use?.baseURL || 'http://localhost:8080';
    try {
        const resp = await fetch(`${baseURL}/api/status`);
        if (!resp.ok) test.skip(true, 'Emulator not running');
    } catch {
        test.skip(true, 'Emulator not running');
    }
});

test.describe('Mouse Input Pipeline', () => {

    test('data channel opens and accepts mouse events', async ({ page }) => {
        // Navigate to the web UI
        await page.goto('/');

        // Wait for the page to load
        await page.waitForLoadState('networkidle');

        // Wait for WebRTC data channel to open (the client logs state in the UI)
        // The client sets data-dc-state attribute or updates status text
        // We'll check by evaluating the client's connection state
        const dcOpen = await page.evaluate(async () => {
            // Wait up to 10 seconds for data channel to open
            const deadline = Date.now() + 10000;
            while (Date.now() < deadline) {
                // @ts-ignore - global client object
                const client = window.client;
                if (client && client.dataChannel && client.dataChannel.readyState === 'open') {
                    return true;
                }
                await new Promise(r => setTimeout(r, 200));
            }
            return false;
        });

        expect(dcOpen).toBe(true);
    });

    test('sends relative mouse move without errors', async ({ page }) => {
        await page.goto('/');
        await page.waitForLoadState('networkidle');

        // Wait for data channel
        await page.evaluate(async () => {
            const deadline = Date.now() + 10000;
            while (Date.now() < deadline) {
                // @ts-ignore
                const c = window.client;
                if (c?.dataChannel?.readyState === 'open') return;
                await new Promise(r => setTimeout(r, 200));
            }
        });

        // Send mouse moves through the client's binary protocol
        const errors: string[] = [];
        page.on('pageerror', (err) => errors.push(err.message));

        const sent = await page.evaluate(() => {
            // @ts-ignore
            const client = window.client;
            if (!client || !client.dataChannel || client.dataChannel.readyState !== 'open') {
                return false;
            }

            // Send 10 relative mouse moves
            for (let i = 0; i < 10; i++) {
                client.sendMouseMove(5, 3, performance.now());
            }
            return true;
        });

        expect(sent).toBe(true);
        expect(errors).toHaveLength(0);

        // Small wait to ensure events are processed without crashing
        await page.waitForTimeout(500);

        // Verify data channel is still open after sending
        const stillOpen = await page.evaluate(() => {
            // @ts-ignore
            return window.client?.dataChannel?.readyState === 'open';
        });
        expect(stillOpen).toBe(true);
    });

    test('sends absolute mouse move without errors', async ({ page }) => {
        await page.goto('/');
        await page.waitForLoadState('networkidle');

        await page.evaluate(async () => {
            const deadline = Date.now() + 10000;
            while (Date.now() < deadline) {
                // @ts-ignore
                const c = window.client;
                if (c?.dataChannel?.readyState === 'open') return;
                await new Promise(r => setTimeout(r, 200));
            }
        });

        const errors: string[] = [];
        page.on('pageerror', (err) => errors.push(err.message));

        const sent = await page.evaluate(() => {
            // @ts-ignore
            const client = window.client;
            if (!client?.dataChannel || client.dataChannel.readyState !== 'open') return false;

            // Switch to absolute mode
            client.sendMouseModeChange('absolute');

            // Send absolute mouse positions
            client.sendMouseAbsolute(100, 200, performance.now());
            client.sendMouseAbsolute(400, 300, performance.now());
            return true;
        });

        expect(sent).toBe(true);
        expect(errors).toHaveLength(0);

        await page.waitForTimeout(500);

        const stillOpen = await page.evaluate(() => {
            // @ts-ignore
            return window.client?.dataChannel?.readyState === 'open';
        });
        expect(stillOpen).toBe(true);
    });

    test('sends mouse button events without errors', async ({ page }) => {
        await page.goto('/');
        await page.waitForLoadState('networkidle');

        await page.evaluate(async () => {
            const deadline = Date.now() + 10000;
            while (Date.now() < deadline) {
                // @ts-ignore
                const c = window.client;
                if (c?.dataChannel?.readyState === 'open') return;
                await new Promise(r => setTimeout(r, 200));
            }
        });

        const errors: string[] = [];
        page.on('pageerror', (err) => errors.push(err.message));

        const sent = await page.evaluate(() => {
            // @ts-ignore
            const client = window.client;
            if (!client?.dataChannel || client.dataChannel.readyState !== 'open') return false;

            // Left click
            client.sendMouseButton(0, true, performance.now());
            client.sendMouseButton(0, false, performance.now());

            // Right click
            client.sendMouseButton(2, true, performance.now());
            client.sendMouseButton(2, false, performance.now());
            return true;
        });

        expect(sent).toBe(true);
        expect(errors).toHaveLength(0);

        await page.waitForTimeout(500);

        const stillOpen = await page.evaluate(() => {
            // @ts-ignore
            return window.client?.dataChannel?.readyState === 'open';
        });
        expect(stillOpen).toBe(true);
    });

    test('sends keyboard events without errors', async ({ page }) => {
        await page.goto('/');
        await page.waitForLoadState('networkidle');

        await page.evaluate(async () => {
            const deadline = Date.now() + 10000;
            while (Date.now() < deadline) {
                // @ts-ignore
                const c = window.client;
                if (c?.dataChannel?.readyState === 'open') return;
                await new Promise(r => setTimeout(r, 200));
            }
        });

        const errors: string[] = [];
        page.on('pageerror', (err) => errors.push(err.message));

        const sent = await page.evaluate(() => {
            // @ts-ignore
            const client = window.client;
            if (!client?.dataChannel || client.dataChannel.readyState !== 'open') return false;

            // Send key press (browser keycode 65 = 'A')
            client.sendKey(65, true, performance.now());
            client.sendKey(65, false, performance.now());
            return true;
        });

        expect(sent).toBe(true);
        expect(errors).toHaveLength(0);

        await page.waitForTimeout(500);

        const stillOpen = await page.evaluate(() => {
            // @ts-ignore
            return window.client?.dataChannel?.readyState === 'open';
        });
        expect(stillOpen).toBe(true);
    });

    test('native mouse interaction on video element sends events', async ({ page }) => {
        await page.goto('/');
        await page.waitForLoadState('networkidle');

        // Wait for data channel
        await page.evaluate(async () => {
            const deadline = Date.now() + 10000;
            while (Date.now() < deadline) {
                // @ts-ignore
                const c = window.client;
                if (c?.dataChannel?.readyState === 'open') return;
                await new Promise(r => setTimeout(r, 200));
            }
        });

        // Switch to absolute mode (no pointer lock needed)
        await page.evaluate(() => {
            // @ts-ignore
            window.client?.sendMouseModeChange('absolute');
            // @ts-ignore
            if (window.client) window.client.mouseMode = 'absolute';
        });

        await page.waitForTimeout(200);

        // Find the video/canvas display element
        const display = await page.$('#display') || await page.$('video') || await page.$('canvas');
        if (!display) {
            test.skip(true, 'No display element found');
            return;
        }

        const errors: string[] = [];
        page.on('pageerror', (err) => errors.push(err.message));

        // Move mouse over the display element
        const box = await display.boundingBox();
        if (!box) {
            test.skip(true, 'Display element has no bounding box');
            return;
        }

        // Mouse move
        await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
        await page.waitForTimeout(100);

        // Click
        await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
        await page.waitForTimeout(100);

        expect(errors).toHaveLength(0);

        // Data channel still healthy
        const stillOpen = await page.evaluate(() => {
            // @ts-ignore
            return window.client?.dataChannel?.readyState === 'open';
        });
        expect(stillOpen).toBe(true);
    });
});
