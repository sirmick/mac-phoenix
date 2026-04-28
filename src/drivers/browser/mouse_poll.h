/*
 *  mouse_poll.h — host-side mouse poller for the MacBrowser pipeline.
 *
 *  Per docs/plan/MacBrowser.md "Mouse model — host-side polling":
 *  rather than have the guest forward each mouseDown / mouseMove
 *  through the g2h ring (one ring push per pixel of cursor motion!),
 *  the host reads:
 *
 *    - Mouse global at $082C (Point: v, h)            — cursor on Mac screen
 *    - MBState at $0172                               — 0xFF up / 0x00 down
 *    - CurApName at $0910 (Pascal string)             — frontmost app
 *    - BrowserShm.viewport.{screen_left,screen_top}   — viewport top-left
 *    - BrowserShm.fb.{width,height}                   — viewport extent
 *
 *  …each ~16 ms tick, gates on CurApName == "MacBrowser", computes
 *  page-relative (x, y), and synthesizes BiDi events directly:
 *    - mouse moved → bidi.mouse_move(x, y)
 *    - button transition to down → bidi.click(x, y, 0, 1)
 *
 *  Zero ring traffic for input. Hover support comes for free.
 */
#ifndef DRIVERS_BROWSER_MOUSE_POLL_H
#define DRIVERS_BROWSER_MOUSE_POLL_H

namespace browser {

class BidiClient;

/* Spawn the poll thread. Idempotent — second call is a no-op until
 * mouse_poll_stop() is called. nullptr disables (e.g. when BiDi
 * never came up). */
void mouse_poll_start(BidiClient* bidi);

/* Tear down the poll thread. Idempotent. */
void mouse_poll_stop();

}  // namespace browser

#endif  // DRIVERS_BROWSER_MOUSE_POLL_H
