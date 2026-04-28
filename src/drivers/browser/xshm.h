/*
 *  xshm.h — capture Firefox's pixels off the Xvfb root via XShm + XDamage.
 *
 *  Firefox in --kiosk mode fills the entire Xvfb root window with the
 *  page content (plus its own minimal chrome). Its renderer paints
 *  directly to X via Cairo/XRender, so the X drawable actually has
 *  the page pixels — unlike chromium's GPU compositor which keeps
 *  page content in a private buffer.
 *
 *  start(display) opens an xcb connection to Xvfb on :display,
 *  allocates a single shared-memory segment sized to the root window,
 *  attaches it via MIT-SHM, and registers an XDamage notifier on the
 *  root with REPORT_BOUNDING_BOX. A background thread blocks on
 *  xcb_wait_for_event(); on each DAMAGE_NOTIFY it accumulates the
 *  union into a pending dirty bbox and calls xcb_damage_subtract.
 *
 *  drain(out_image, out_dirty) is the per-VBL pull point: fences,
 *  reads the current bbox, issues a single xcb_shm_get_image() for
 *  that region, and hands the caller a pointer to the SHM buffer
 *  plus the rect that's now valid in it. Returns false if no damage
 *  has accumulated since the last drain.
 *
 *  Pixel format is Xvfb depth-24 BGRX on little-endian hosts; the
 *  pipeline converts to RGB555 BE before writing to BrowserShm.fb.
 */
#ifndef DRIVERS_BROWSER_XSHM_H
#define DRIVERS_BROWSER_XSHM_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace browser {

class XShmCapture {
public:
    struct Rect {
        int16_t  x, y;
        uint16_t w, h;
    };

    XShmCapture();
    ~XShmCapture();

    /* Open the xcb connection to Xvfb :display, allocate the SHM
     * segment, register XDamage on the root. Returns true on success.
     * Idempotent: a second call after a successful start is a no-op. */
    bool start(int display);

    /* Tear everything down. Idempotent; called by destructor. */
    void stop();

    /* Pull the current accumulated damage. If non-empty: issues an
     * xcb_shm_get_image for the damage bbox into the top-left of the
     * SHM, fills *out_image (host pointer to SHM start) and
     * *out_dirty (the rect in screen coords that was just captured;
     * SHM holds w*h pixels at stride w*4 starting at offset 0). */
    bool drain(const uint8_t** out_image, Rect* out_dirty);

    int width()  const { return root_w_; }
    int height() const { return root_h_; }
    int depth()  const { return 24; }
    int bpp()    const { return 32; }

private:
    void thread_main();

    /* xcb opaque types kept as void* to avoid leaking xcb headers. */
    void*    conn_              = nullptr;
    uint32_t root_              = 0;
    uint32_t damage_            = 0;
    uint32_t shmseg_            = 0;
    int      shmid_             = -1;
    uint8_t* shm_addr_          = nullptr;
    int      root_w_            = 0;
    int      root_h_            = 0;
    int      damage_event_base_ = 0;

    std::thread       thread_;
    std::atomic<bool> running_{false};

    std::mutex mtx_;
    Rect       dirty_{0, 0, 0, 0};   /* {0,0,0,0} = clean */
};

}  // namespace browser

#endif  // DRIVERS_BROWSER_XSHM_H
