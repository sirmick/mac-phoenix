/*
 *  cmd.h — guest command dispatcher.
 *
 *  Maps `BR_CMD_*` ring messages from the guest into BiDi method
 *  calls on Firefox. Long-running calls (navigate, click-with-load)
 *  are dispatched on detached threads so the spike's ring-drain
 *  loop doesn't stall.
 */
#ifndef DRIVERS_BROWSER_CMD_H
#define DRIVERS_BROWSER_CMD_H

#include <cstdint>

namespace browser {

class BidiClient;

/* Set the BidiClient to dispatch into. nullptr disables dispatch
 * (commands are silently dropped — caller likely is in a CEF or
 * test context where BiDi isn't available). Idempotent. */
void cmd_set_bidi(BidiClient* bidi);

/* Dispatch one ring message. type is BR_CMD_*; payload is the raw
 * bytes pulled from g2h, length `len`. Returns true if the message
 * was recognized (not necessarily that the BiDi call succeeded —
 * remote failures are logged + dropped). */
bool cmd_dispatch(uint16_t type, const uint8_t* payload, uint16_t len);

}  // namespace browser

#endif  // DRIVERS_BROWSER_CMD_H
