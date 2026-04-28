/*
 *  window_resize.cpp — resize the Firefox top-level window via xcb.
 *
 *  Identification heuristic: walk root's children (xcb_query_tree)
 *  in stacking order, pick the first one that's at +0+0 and has
 *  width > 100. Firefox's main browser window is always the first
 *  to be mapped at the origin; the only other top-levels under our
 *  control are the 10×10 hidden helper at +10+10 and miscellaneous
 *  popup / context-menu windows that aren't at the origin.
 *
 *  Using WM_CLASS lookup would be more robust but pulls a property
 *  fetch per child — the +0+0 + size heuristic has been adequate
 *  in practice and keeps this file dependency-light.
 */
#include "window_resize.h"

#include <cstdio>
#include <cstdlib>

#include <xcb/xcb.h>

namespace browser {

namespace {

/* Find the Firefox-likely window among root's children. Returns 0
 * if none match. Children are returned by xcb_query_tree in stacking
 * order from bottom to top — Firefox's main window is typically the
 * first non-trivial entry. */
xcb_window_t find_firefox_window(xcb_connection_t* c, xcb_window_t root)
{
    auto qt_cookie = xcb_query_tree(c, root);
    auto* qt = xcb_query_tree_reply(c, qt_cookie, nullptr);
    if (!qt) return 0;

    int n = xcb_query_tree_children_length(qt);
    auto* kids = xcb_query_tree_children(qt);

    xcb_window_t found = 0;
    /* Walk top-of-stack first (last in the array is topmost). The
     * topmost mapped, sized, +0+0 child is the kiosk Firefox. */
    for (int i = n - 1; i >= 0; i--) {
        xcb_window_t w = kids[i];
        auto ga_cookie = xcb_get_geometry(c, w);
        auto* ga = xcb_get_geometry_reply(c, ga_cookie, nullptr);
        if (!ga) continue;
        bool ok = (ga->x == 0 && ga->y == 0 &&
                   ga->width > 100 && ga->height > 100);
        free(ga);
        if (ok) { found = w; break; }
    }
    free(qt);
    return found;
}

}  // namespace

bool resize_firefox_window(int display, int width, int height)
{
    char display_str[16];
    snprintf(display_str, sizeof(display_str), ":%d", display);

    xcb_connection_t* c = xcb_connect(display_str, nullptr);
    if (xcb_connection_has_error(c)) {
        fprintf(stderr, "[WindowResize] xcb_connect(%s) failed\n",
                display_str);
        if (c) xcb_disconnect(c);
        return false;
    }

    const xcb_setup_t* setup = xcb_get_setup(c);
    auto screen_iter = xcb_setup_roots_iterator(setup);
    if (screen_iter.rem == 0) {
        fprintf(stderr, "[WindowResize] no X screens\n");
        xcb_disconnect(c);
        return false;
    }
    xcb_window_t root = screen_iter.data->root;

    xcb_window_t ff = find_firefox_window(c, root);
    if (!ff) {
        fprintf(stderr, "[WindowResize] no Firefox window found under root\n");
        xcb_disconnect(c);
        return false;
    }

    /* Force exact dimensions; Firefox in --kiosk respects the size
     * as long as no fullscreen-toggle keystroke fires. */
    uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                    XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    uint32_t values[4] = { 0, 0, (uint32_t)width, (uint32_t)height };
    auto cookie = xcb_configure_window_checked(c, ff, mask, values);
    auto* err = xcb_request_check(c, cookie);
    if (err) {
        fprintf(stderr, "[WindowResize] configure_window 0x%x to %dx%d "
                "failed: code=%u\n", ff, width, height, err->error_code);
        free(err);
        xcb_disconnect(c);
        return false;
    }

    xcb_flush(c);
    fprintf(stderr, "[WindowResize] Firefox win 0x%x → %dx%d\n",
            ff, width, height);
    xcb_disconnect(c);
    return true;
}

}  // namespace browser
