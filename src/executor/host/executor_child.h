#pragma once
/* Entry points for `--backend executor` in src/main.cpp. */
#include "ipc_protocol.h"

namespace config { struct EmulatorConfig; }

// IPC child: publishes frames to BUF and serves the control socket.
int executor_child_main(const config::EmulatorConfig& cfg, IPCBuffer *buf);
// In-process, no web server (debugging with --no-webserver --timeout).
int executor_direct_main(const config::EmulatorConfig& cfg);
