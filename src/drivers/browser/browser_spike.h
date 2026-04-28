/*
 *  browser_spike.h — M1 host-side gradient writer.
 *
 *  When --browser is set, browser_spike_start() launches a background
 *  thread that writes an animated 16-bit RGB555 gradient into
 *  BrowserShm.fb.pixels every ~100 ms and bumps fb.seq. Used by the
 *  guest BrowserSpike app to validate the host→guest pixel pipe before
 *  any of the M2+ infrastructure (rings, VBL hook, Chromium) lands.
 *
 *  Idempotent: a second call after a successful start is a no-op.
 *  browser_spike_stop() joins the thread on shutdown.
 */
#ifndef BROWSER_SPIKE_H
#define BROWSER_SPIKE_H

#ifdef __cplusplus
extern "C" {
#endif

void browser_spike_start();
void browser_spike_stop();

#ifdef __cplusplus
}
#endif

#endif /* BROWSER_SPIKE_H */
