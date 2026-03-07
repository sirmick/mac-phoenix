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

namespace http {

/**
 * API Context - provides access to server state for API handlers
 */
struct APIContext {
    // Unified configuration (live, mutable)
    config::EmulatorConfig* config = nullptr;

    // Codec state
    CodecType* server_codec;  // Pointer to g_server_codec
    std::function<void(CodecType)> notify_codec_change_fn;  // Notify clients of codec change

    // Video output (for screenshot API)
    VideoOutput* video_output = nullptr;

    // CPU state (in-process integration)
    std::atomic<bool>* cpu_running;  // Pointer to cpu_state::g_running
    std::mutex* cpu_mutex;  // Pointer to cpu_state::g_mutex
    std::condition_variable* cpu_cv;  // Pointer to cpu_state::g_cv
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

    APIContext* ctx_;
};

} // namespace http

#endif // API_HANDLERS_H
