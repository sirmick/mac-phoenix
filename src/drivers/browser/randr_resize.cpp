/*
 *  randr_resize.cpp — RandR-driven Xvfb screen resize.
 */
#include "randr_resize.h"

#include <cstdio>
#include <cstdlib>

#include <xcb/xcb.h>
#include <xcb/randr.h>

namespace browser {

bool randr_resize(int display, int width, int height)
{
    char display_str[16];
    snprintf(display_str, sizeof(display_str), ":%d", display);

    xcb_connection_t* c = xcb_connect(display_str, nullptr);
    if (xcb_connection_has_error(c)) {
        fprintf(stderr, "[RandR] xcb_connect(%s) failed\n", display_str);
        if (c) xcb_disconnect(c);
        return false;
    }

    /* Probe the RandR extension version so the server sets up the
     * matching dispatch. Required even if we don't use the version
     * info ourselves. */
    {
        auto cookie = xcb_randr_query_version(c, 1, 5);
        auto* reply = xcb_randr_query_version_reply(c, cookie, nullptr);
        if (!reply) {
            fprintf(stderr, "[RandR] query_version failed (extension not "
                    "present?)\n");
            xcb_disconnect(c);
            return false;
        }
        free(reply);
    }

    /* Find the root window. */
    const xcb_setup_t* setup = xcb_get_setup(c);
    auto screen_iter = xcb_setup_roots_iterator(setup);
    if (screen_iter.rem == 0) {
        fprintf(stderr, "[RandR] no X screens reported\n");
        xcb_disconnect(c);
        return false;
    }
    xcb_window_t root = screen_iter.data->root;

    /* set_screen_size accepts mm dimensions too; use 96 dpi defaults
     * (one inch = 25.4 mm; 96 px/inch → 0.265 mm/px). Xvfb doesn't
     * care about the physical-size hint for our use case. */
    int mm_w = (width  * 254 + 480) / 960;   /* round to nearest mm */
    int mm_h = (height * 254 + 480) / 960;

    auto cookie = xcb_randr_set_screen_size_checked(
        c, root,
        (uint16_t)width, (uint16_t)height,
        (uint32_t)mm_w,  (uint32_t)mm_h);
    auto* err = xcb_request_check(c, cookie);
    if (err) {
        fprintf(stderr,
                "[RandR] set_screen_size %dx%d failed: code=%u\n",
                width, height, err->error_code);
        free(err);
        xcb_disconnect(c);
        return false;
    }

    fprintf(stderr, "[RandR] Xvfb :%d resized to %dx%d\n",
            display, width, height);
    xcb_disconnect(c);
    return true;
}

}  // namespace browser
