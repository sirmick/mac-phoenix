/*
 *  randr_resize.h — change Xvfb's screen size via the RandR extension.
 *
 *  When the guest grows its window and pushes BR_CMD_RESIZE, we
 *  resize the Xvfb root window to match. Modern Xvfb has the RandR
 *  extension enabled by default; xcb_randr_set_screen_size_checked
 *  is the one-shot API for this.
 *
 *  Side-effect: Firefox in --kiosk mode subscribes to RRScreenChange
 *  events and resizes its window to fill the new root. So one call
 *  here turns into a Firefox window resize + reflow + repaint.
 */
#ifndef DRIVERS_BROWSER_RANDR_RESIZE_H
#define DRIVERS_BROWSER_RANDR_RESIZE_H

namespace browser {

/* Set the Xvfb screen size on :display to (width, height). Returns
 * true on success. Idempotent: a call with the current size is a
 * no-op as far as the user is concerned. */
bool randr_resize(int display, int width, int height);

}  // namespace browser

#endif  // DRIVERS_BROWSER_RANDR_RESIZE_H
