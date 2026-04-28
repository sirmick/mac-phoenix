/*
 *  pipeline.h — XShm → BrowserShm.fb pixel pump.
 *
 *  Polls XShmCapture::drain() at 60 Hz. On each pull:
 *    1. drain() returns a pointer to the dirty rect's pixels (BGRX,
 *       packed at the top-left of the SHM segment, stride=w*4) plus
 *       the rect in screen coords.
 *    2. BGRX → big-endian RGB555 convert per row into
 *       BrowserShm.fb.pixels at the matching screen-coord offset.
 *    3. Publish the rect into BrowserShm.fb.dirty[0], release-fence,
 *       bump fb.seq.
 *
 *  No PNG decode, no host-side diff — XDamage already gives us the
 *  rects, the X server already has the pixels in shared memory.
 */
#ifndef DRIVERS_BROWSER_PIPELINE_H
#define DRIVERS_BROWSER_PIPELINE_H

namespace browser {

class XShmCapture;

void pipeline_start(XShmCapture* capture);
void pipeline_stop();

}  // namespace browser

#endif  // DRIVERS_BROWSER_PIPELINE_H
