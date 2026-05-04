/*
 *  mouse_poll.h — host-side mouse poller for the MacBrowser pipeline.
 *
 *  Per docs/MacBrowser.md "Mouse model — host-side polling": rather
 *  than have the guest forward each mouseDown / mouseMove through the
 *  g2h ring (one ring push per pixel of cursor motion!), the host
 *  reads:
 *
 *    - Mouse global at $082C (Point: v, h)            — cursor on Mac screen
 *    - MBState at $0172                               — 0xFF up / 0x00 down
 *    - CurApName at $0910 (Pascal string)             — frontmost app
 *    - BrowserShm.viewport.{screen_left,screen_top}   — viewport top-left
 *    - BrowserShm.fb.{width,height}                   — viewport extent
 *
 *  …each ~16 ms tick, gates on CurApName == "MacBrowser", computes
 *  page-relative (x, y), and forwards via qt_dispatch_mouse_move /
 *  qt_dispatch_click — same routing the BR_CMD_* g2h commands use.
 *
 *  Page metrics are no longer polled here — QtWebEngineBrowser owns
 *  that timer (see metrics_tick). Mouse polling stays separate because
 *  it needs raw access to Mac low-memory globals via ReadMacInt8/16,
 *  which is the CPU-thread-affinity layer rather than QtWebEngine.
 */
#ifndef DRIVERS_BROWSER_MOUSE_POLL_H
#define DRIVERS_BROWSER_MOUSE_POLL_H

namespace browser {

// Spawn the poll thread. Idempotent — second call is a no-op until
// mouse_poll_stop() is called.
void mouse_poll_start();

// Tear down the poll thread. Idempotent.
void mouse_poll_stop();

}  // namespace browser

#endif  // DRIVERS_BROWSER_MOUSE_POLL_H
