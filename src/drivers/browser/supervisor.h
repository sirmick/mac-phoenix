/*
 *  supervisor.h — owns the Xvfb + Chromium child processes.
 *
 *  start() picks a free X display + remote-debugging port, spawns
 *  Xvfb on the display, waits for the X socket to appear, then
 *  spawns Chromium pointing at that display with
 *  --remote-debugging-port=<port> and --app=about:blank so the page
 *  fills the Xvfb root with no browser chrome.
 *
 *  stop() SIGTERMs the kids (Chromium first, Xvfb second) and
 *  waitpid()s. Idempotent. Safe to call from main thread on shutdown.
 *
 *  We use Xvfb (not --headless=new) because M4's pixel pipeline
 *  reads frames via XShmGetImage off the Xvfb root — that gives us
 *  pre-decoded raw pixels in a host-mapped shared region, so the VBL
 *  hook is just memcpy + RGB→RGB555 conversion. The CDP screencast
 *  alternative would force base64 + JPEG decode per frame.
 *
 *  On accidental child exit (Chromium crash, Xvfb crash) the
 *  supervisor logs and currently does NOT auto-restart — restart
 *  semantics depend on what state we want to preserve (open page,
 *  scroll position, login cookies). Defer until we hit a real crash
 *  and have data on what users care about.
 */
#ifndef DRIVERS_BROWSER_SUPERVISOR_H
#define DRIVERS_BROWSER_SUPERVISOR_H

#include <string>

namespace browser {

class Supervisor {
public:
    Supervisor();
    ~Supervisor();

    /* Spawn Xvfb + Chromium. Returns true iff both came up and the
     * CDP /json/version endpoint responded. */
    bool start();

    /* Tear down the children. Always called on Supervisor destruction;
     * exposed so callers can stop early. */
    void stop();

    /* Once start() succeeds, this is the WebSocket URL of the browser
     * target that the CDP client should connect to. Empty before
     * start() / after stop(). */
    const std::string& browser_ws_url() const { return browser_ws_url_; }

    /* Display ":N" we picked (informational; useful for log messages
     * and for the M4 XShm capture code). */
    int display() const { return display_; }
    int cdp_port() const { return cdp_port_; }

private:
    bool spawn_xvfb();
    bool spawn_chromium();
    bool wait_for_x_socket(int timeout_ms);
    bool wait_for_cdp(int timeout_ms);
    bool fetch_browser_ws_url();

    int  pick_free_display();
    int  pick_free_port();

    int  display_         = -1;          /* :N where Xvfb runs        */
    int  cdp_port_        = 0;
    int  xvfb_pid_        = 0;
    int  chromium_pid_    = 0;
    std::string browser_ws_url_;
    std::string user_data_dir_;          /* persisted under ~/.cache  */
};

}  // namespace browser

#endif  // DRIVERS_BROWSER_SUPERVISOR_H
