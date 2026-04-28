/*
 *  window_resize.h — resize the Firefox top-level window inside Xvfb.
 *
 *  Replaces the earlier randr_resize approach (which had to invent
 *  CRTC modes per-size). Xvfb root stays a fixed canvas;
 *  xcb_configure_window grows/shrinks the Firefox sub-window
 *  directly. xshm captures the entire root and pipeline crops to
 *  the current Firefox dimensions — no X-server-level resize at all.
 *
 *  Returns true on success. Logs to stderr on failure with the
 *  underlying X error code.
 */
#pragma once

namespace browser {

bool resize_firefox_window(int display, int width, int height);

}  // namespace browser
