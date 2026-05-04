/*
 *  cmd.h — guest command dispatcher.
 *
 *  Routes BR_CMD_* messages drained from the g2h ring to QtWebEngine
 *  via the qt_dispatch_* free functions in qtwebengine_browser.h.
 *  Pre-Phase-8 versions targeted Firefox over WebDriver-BiDi; that
 *  whole layer was deleted in 8i.
 */
#ifndef DRIVERS_BROWSER_CMD_H
#define DRIVERS_BROWSER_CMD_H

#include <cstdint>

namespace browser {

// Dispatch one ring message. `type` is BR_CMD_*; `payload` is the raw
// bytes pulled from g2h, length `len`. Returns true if the message was
// recognized; QtWebEngine event delivery happens asynchronously on the
// GUI thread and the success of the page-side action isn't reported
// back here.
bool cmd_dispatch(uint16_t type, const uint8_t* payload, uint16_t len);

}  // namespace browser

#endif  // DRIVERS_BROWSER_CMD_H
