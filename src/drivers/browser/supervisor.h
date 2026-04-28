/*
 *  supervisor.h — owns the Xvfb + Firefox child processes.
 *
 *  start(initial_url) picks a free X display, spawns Xvfb on it,
 *  pre-populates a Firefox profile (silences first-run / promo /
 *  telemetry popups), then spawns
 *
 *    /opt/firefox/firefox --no-remote --kiosk --profile <dir> <url>
 *
 *  Firefox in kiosk mode fills the Xvfb root with the page; its
 *  Cairo/XRender renderer paints directly to the X drawable, so the
 *  XShmCapture in this directory can pull live pixels via
 *  XShmGetImage with XDamage-driven dirty rects.
 *
 *  stop() SIGTERMs the firefox process group then Xvfb, waitpid()s.
 *  Idempotent.
 */
#ifndef DRIVERS_BROWSER_SUPERVISOR_H
#define DRIVERS_BROWSER_SUPERVISOR_H

#include <string>

namespace browser {

class Supervisor {
public:
    Supervisor();
    ~Supervisor();

    bool start(const std::string& initial_url);
    void stop();

    int display() const { return display_; }

private:
    bool spawn_xvfb();
    bool wait_for_x_socket(int timeout_ms);
    bool prepare_firefox_profile();
    bool spawn_firefox(const std::string& url);

    int  pick_free_display();

    int  display_     = -1;
    int  xvfb_pid_    = 0;
    int  firefox_pid_ = 0;
    std::string profile_dir_;
};

}  // namespace browser

#endif  // DRIVERS_BROWSER_SUPERVISOR_H
