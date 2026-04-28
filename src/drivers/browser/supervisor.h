/*
 *  supervisor.h — owns the Xvfb + Chromium child processes.
 *
 *  start(initial_url) picks a free X display + remote-debugging
 *  port, spawns Xvfb on the display, waits for the X socket, then
 *  spawns Chromium with --kiosk pointing at that display. The Xvfb
 *  is purely there so chromium agrees to run in headed mode —
 *  pixels are captured via CDP Page.startScreencast (which pulls
 *  from chromium's compositor, not from X), not from Xvfb itself.
 *
 *  Chromium binary is resolved from playwright's bundled "Chrome
 *  for Testing" first, then deb-installed chrome / chromium. Snap
 *  chromium is deliberately not supported.
 *
 *  stop() SIGTERMs the chromium process group then Xvfb and
 *  waitpid()s. Idempotent.
 */
#ifndef DRIVERS_BROWSER_SUPERVISOR_H
#define DRIVERS_BROWSER_SUPERVISOR_H

#include <string>

namespace browser {

class Supervisor {
public:
    Supervisor();
    ~Supervisor();

    /* Spawn Chromium. `initial_url` (if non-empty) is loaded by
     * chromium directly on its command line, bypassing an attach-
     * to-about:blank-then-Page.navigate race in flatten-mode
     * session routing. Returns true iff /json/version responded. */
    bool start(const std::string& initial_url);

    /* Tear down the child. Always called by destructor. */
    void stop();

    /* WebSocket URL of the browser-level CDP target, populated on
     * successful start(). Empty before start / after stop. */
    const std::string& browser_ws_url() const { return browser_ws_url_; }

    int cdp_port() const { return cdp_port_; }
    int display()  const { return display_; }

private:
    bool spawn_xvfb();
    bool wait_for_x_socket(int timeout_ms);
    bool spawn_chromium(const std::string& url);
    bool wait_for_cdp(int timeout_ms);
    bool fetch_browser_ws_url();
    int  pick_free_port();
    int  pick_free_display();

    int  display_      = -1;
    int  cdp_port_     = 0;
    int  xvfb_pid_     = 0;
    int  chromium_pid_ = 0;
    std::string browser_ws_url_;
    std::string user_data_dir_;
};

}  // namespace browser

#endif  // DRIVERS_BROWSER_SUPERVISOR_H
