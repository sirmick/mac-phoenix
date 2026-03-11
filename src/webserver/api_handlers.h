/*
 * API Handlers Module
 *
 * Handles all /api/ endpoints for server control and status
 */

#ifndef API_HANDLERS_H
#define API_HANDLERS_H

#include "http_server.h"
#include "../config/emulator_config.h"
#include "../drivers/video/encoders/codec.h"  // For CodecType
#include <string>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

class VideoOutput;  // Forward declaration
class CPUProcess;   // Forward declaration
struct SharedState;  // Forward declaration

namespace http {

/**
 * API Context - provides access to server state for API handlers
 */
struct APIContext {
    // Unified configuration (live, mutable)
    config::EmulatorConfig* config = nullptr;

    // Codec state
    CodecType* server_codec = nullptr;
    std::function<void(CodecType)> notify_codec_change_fn;

    // Video output (for screenshot API)
    VideoOutput* video_output = nullptr;

    // Fork-based CPU process (webserver mode)
    CPUProcess* cpu_process = nullptr;
    SharedState* shared_state = nullptr;

    // Legacy in-process CPU state (kept for headless compatibility)
    std::atomic<bool>* cpu_running = nullptr;
    std::mutex* cpu_mutex = nullptr;
    std::condition_variable* cpu_cv = nullptr;
};

/**
 * API Router
 *
 * Routes API requests to appropriate handlers
 */
class APIRouter {
public:
    explicit APIRouter(APIContext* context);

    // Handle API request, returns response (or empty if not an API route)
    Response handle(const Request& req, bool* handled);

private:
    // Config endpoints
    Response handle_config_get(const Request& req);     // GET /api/config
    Response handle_config_save(const Request& req);    // POST /api/config

    Response handle_storage(const Request& req);
    Response handle_restart(const Request& req);
    Response handle_status(const Request& req);
    Response handle_codec_post(const Request& req);
    Response handle_emulator_start(const Request& req);
    Response handle_emulator_stop(const Request& req);
    Response handle_emulator_restart(const Request& req);
    Response handle_emulator_reset(const Request& req);
    Response handle_log(const Request& req);
    Response handle_error(const Request& req);
    Response handle_screenshot(const Request& req);
    Response handle_mouse(const Request& req);
    Response handle_mouse_move(const Request& req);
    Response handle_keypress(const Request& req);

    // Command bridge endpoints
    Response handle_app(const Request& req);
    Response handle_windows(const Request& req);
    Response handle_launch(const Request& req);
    Response handle_quit(const Request& req);
    Response handle_wait(const Request& req);

    APIContext* ctx_;
};

} // namespace http

#endif // API_HANDLERS_H
