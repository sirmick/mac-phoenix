/*
 *  module.cpp — see module.h.
 */
#include "module.h"
#include "mouse_poll.h"
#include "qtwebengine_browser.h"

namespace browser {

void browser_module_start()
{
    qtwebengine_module_start();
    mouse_poll_start();
}

void browser_module_stop()
{
    mouse_poll_stop();
    qtwebengine_module_stop();
}

}  // namespace browser
