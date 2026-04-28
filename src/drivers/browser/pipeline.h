/*
 *  pipeline.h — CDP screencast → BrowserShm.fb pixel pump.
 *
 *  Subscribes to Page.screencastFrame on the attached page session.
 *  Per frame:
 *    1. base64-decode JPEG payload
 *    2. stb_image-decode to RGBA8
 *    3. diff against the previous frame (row granularity) to extract
 *       a single bounding-box dirty rect
 *    4. BGRA→RGB555 convert + write into BrowserShm.fb.pixels
 *    5. publish dirty rect + bump fb.seq
 *    6. ACK back to chromium via Page.screencastFrameAck
 *
 *  Dirty-rect tracking is preserved end-to-end: we read a full frame
 *  from CDP but only push the *changed* region into BrowserShm.fb,
 *  so the guest's CopyBits-only-dirty-rects optimization still
 *  applies.
 */
#ifndef DRIVERS_BROWSER_PIPELINE_H
#define DRIVERS_BROWSER_PIPELINE_H

namespace browser {

class CdpClient;

/* Issue Page.startScreencast on the attached session and register the
 * frame handler. Idempotent. The pipeline keeps internal state for
 * frame diffing — keep called once per BrowserModule lifecycle. */
void pipeline_start(CdpClient* cdp);

/* Stop the screencast and clear pipeline state. Idempotent. */
void pipeline_stop();

}  // namespace browser

#endif  // DRIVERS_BROWSER_PIPELINE_H
