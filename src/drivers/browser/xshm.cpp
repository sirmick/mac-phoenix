/*
 *  xshm.cpp — XShm + XDamage capture from Xvfb. See xshm.h.
 */
#include "xshm.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/ipc.h>
#include <sys/shm.h>

#define BR_HOST 1
#include "MacBrowser.h"
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/damage.h>
#include <xcb/composite.h>

namespace browser {

namespace {

xcb_connection_t* as_conn(void* p) { return (xcb_connection_t*)p; }

XShmCapture::Rect rect_union(XShmCapture::Rect a, XShmCapture::Rect b)
{
    if (a.w == 0 || a.h == 0) return b;
    if (b.w == 0 || b.h == 0) return a;
    int16_t x0 = std::min(a.x, b.x);
    int16_t y0 = std::min(a.y, b.y);
    int16_t x1 = std::max((int)a.x + a.w, (int)b.x + b.w);
    int16_t y1 = std::max((int)a.y + a.h, (int)b.y + b.h);
    return { x0, y0, (uint16_t)(x1 - x0), (uint16_t)(y1 - y0) };
}

}  // namespace

XShmCapture::XShmCapture() = default;
XShmCapture::~XShmCapture() { stop(); }

bool XShmCapture::start(int display)
{
    if (running_.load()) return true;

    char display_str[16];
    snprintf(display_str, sizeof(display_str), ":%d", display);

    conn_ = xcb_connect(display_str, nullptr);
    if (xcb_connection_has_error(as_conn(conn_))) {
        fprintf(stderr, "[XShm] xcb_connect(%s) failed\n", display_str);
        conn_ = nullptr;
        return false;
    }

    /* Verify MIT-SHM extension is available. */
    {
        auto cookie = xcb_shm_query_version(as_conn(conn_));
        auto* reply = xcb_shm_query_version_reply(as_conn(conn_), cookie, nullptr);
        if (!reply || !reply->shared_pixmaps) {
            fprintf(stderr, "[XShm] MIT-SHM not available on Xvfb\n");
            free(reply);
            stop();
            return false;
        }
        free(reply);
    }

    /* Probe XDamage and grab its first event code. */
    {
        const xcb_query_extension_reply_t* ext =
            xcb_get_extension_data(as_conn(conn_), &xcb_damage_id);
        if (!ext || !ext->present) {
            fprintf(stderr, "[XShm] XDamage extension missing\n");
            stop();
            return false;
        }
        damage_event_base_ = ext->first_event;

        auto cookie = xcb_damage_query_version(as_conn(conn_), 1, 1);
        auto* reply = xcb_damage_query_version_reply(as_conn(conn_), cookie, nullptr);
        if (!reply) {
            fprintf(stderr, "[XShm] xcb_damage_query_version failed\n");
            stop();
            return false;
        }
        free(reply);
    }

    /* Find the root window + dimensions. */
    const xcb_setup_t* setup = xcb_get_setup(as_conn(conn_));
    auto screen_iter = xcb_setup_roots_iterator(setup);
    if (screen_iter.rem == 0) {
        fprintf(stderr, "[XShm] no X screens reported by Xvfb\n");
        stop();
        return false;
    }
    root_   = screen_iter.data->root;
    root_w_ = screen_iter.data->width_in_pixels;
    root_h_ = screen_iter.data->height_in_pixels;
    fprintf(stderr, "[XShm] connected display=:%d root=0x%x size=%dx%d\n",
            display, root_, root_w_, root_h_);

    /* Allocate SHM segment at the max viewport size so RandR resize
     * (BR_CMD_RESIZE → randr_resize) can grow the root without
     * overflowing our buffer. We capture damage rects clipped to
     * the live root_w_/root_h_, so we never xcb_shm_get_image past
     * what we've allocated. */
    size_t shm_bytes = (size_t)BR_FB_MAX_W * BR_FB_MAX_H * 4;
    shmid_ = shmget(IPC_PRIVATE, shm_bytes, IPC_CREAT | 0600);
    if (shmid_ < 0) {
        fprintf(stderr, "[XShm] shmget(%zu) failed: %s\n",
                shm_bytes, strerror(errno));
        stop();
        return false;
    }
    shm_addr_ = (uint8_t*)shmat(shmid_, nullptr, 0);
    if (shm_addr_ == (uint8_t*)-1) {
        fprintf(stderr, "[XShm] shmat failed: %s\n", strerror(errno));
        shm_addr_ = nullptr;
        stop();
        return false;
    }
    shmctl(shmid_, IPC_RMID, nullptr);  /* segment lives until detach */

    shmseg_ = xcb_generate_id(as_conn(conn_));
    auto attach_cookie = xcb_shm_attach_checked(
        as_conn(conn_), shmseg_, (uint32_t)shmid_, /*read_only=*/0);
    auto* err = xcb_request_check(as_conn(conn_), attach_cookie);
    if (err) {
        fprintf(stderr, "[XShm] xcb_shm_attach failed: code=%u\n",
                err->error_code);
        free(err);
        stop();
        return false;
    }

    /* Redirect every child of root through the X COMPOSITE extension.
     * Without this, GetImage on the root window returns only root's
     * own pixels (typically all-black) — Firefox renders into a child
     * window and root has no idea. With AUTOMATIC redirect the X
     * server keeps a backing pixmap of root's compositing-result, so
     * xcb_shm_get_image(root, ...) returns the visible scene including
     * all child windows, and XDamage on root reflects child repaints.
     *
     * Has to happen before damage_create — damage events on root only
     * cover the regions the server actually paints into root's backing
     * store, which exists only once redirect is active. */
    {
        auto cookie = xcb_composite_redirect_subwindows_checked(
            as_conn(conn_), root_, XCB_COMPOSITE_REDIRECT_AUTOMATIC);
        auto* err2 = xcb_request_check(as_conn(conn_), cookie);
        if (err2) {
            fprintf(stderr, "[XShm] xcb_composite_redirect_subwindows "
                    "failed: code=%u (Composite ext required)\n",
                    err2->error_code);
            free(err2);
            stop();
            return false;
        }
    }

    /* Subscribe XDamage on root. With COMPOSITE redirect active, root
     * receives composited child repaints; DAMAGE_NOTIFY fires for the
     * union of changed regions across all visible windows. */
    damage_ = xcb_generate_id(as_conn(conn_));
    auto dmg_cookie = xcb_damage_create_checked(
        as_conn(conn_), damage_, root_,
        XCB_DAMAGE_REPORT_LEVEL_BOUNDING_BOX);
    err = xcb_request_check(as_conn(conn_), dmg_cookie);
    if (err) {
        fprintf(stderr, "[XShm] xcb_damage_create failed: code=%u\n",
                err->error_code);
        free(err);
        stop();
        return false;
    }
    xcb_flush(as_conn(conn_));

    /* Prime: mark the whole root dirty so the first drain captures
     * the initial frame even if Firefox hasn't repainted yet. */
    {
        std::lock_guard<std::mutex> lk(mtx_);
        dirty_ = { 0, 0, (uint16_t)root_w_, (uint16_t)root_h_ };
    }

    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { thread_main(); });
    fprintf(stderr, "[XShm] capture running (shm=%p, %zu bytes)\n",
            shm_addr_, shm_bytes);
    return true;
}

void XShmCapture::stop()
{
    bool was_running = running_.exchange(false, std::memory_order_acq_rel);
    /* Closing the xcb connection makes the wait_for_event call return
     * null and lets the thread exit. */
    if (thread_.joinable()) {
        if (conn_) xcb_disconnect(as_conn(conn_));
        thread_.join();
        conn_ = nullptr;
    } else if (conn_) {
        xcb_disconnect(as_conn(conn_));
        conn_ = nullptr;
    }
    (void)was_running;

    if (shm_addr_) { shmdt(shm_addr_); shm_addr_ = nullptr; }
    shmid_  = -1;
    shmseg_ = 0;
    damage_ = 0;
    root_   = 0;
    root_w_ = root_h_ = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        dirty_ = { 0, 0, 0, 0 };
    }
}

void XShmCapture::thread_main()
{
    while (running_.load(std::memory_order_acquire)) {
        xcb_generic_event_t* ev = xcb_wait_for_event(as_conn(conn_));
        if (!ev) break;  /* connection closed by stop() */

        uint8_t type = ev->response_type & 0x7F;
        if (type == damage_event_base_ + XCB_DAMAGE_NOTIFY) {
            auto* dn = (xcb_damage_notify_event_t*)ev;
            Rect r{ dn->area.x, dn->area.y, dn->area.width, dn->area.height };
            {
                std::lock_guard<std::mutex> lk(mtx_);
                dirty_ = rect_union(dirty_, r);
            }
            xcb_damage_subtract(as_conn(conn_), damage_, XCB_NONE, XCB_NONE);
            xcb_flush(as_conn(conn_));
        }
        free(ev);
    }
}

bool XShmCapture::drain(const uint8_t** out_image, Rect* out_dirty)
{
    Rect r;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (dirty_.w == 0 || dirty_.h == 0) return false;
        r = dirty_;
        dirty_ = { 0, 0, 0, 0 };
    }

    /* Pull the dirty rect into the top-left of our SHM segment.
     * out_dirty reports the source rect in root coords; the caller
     * indexes into shm_addr_ as a w*h*4 BGRX image at offset 0. */
    auto cookie = xcb_shm_get_image(
        as_conn(conn_), root_,
        r.x, r.y, r.w, r.h,
        ~0u,
        XCB_IMAGE_FORMAT_Z_PIXMAP,
        shmseg_, /*offset=*/0);
    auto* reply = xcb_shm_get_image_reply(as_conn(conn_), cookie, nullptr);
    if (!reply) return false;
    free(reply);

    *out_image = shm_addr_;
    *out_dirty = r;
    return true;
}

}  // namespace browser
