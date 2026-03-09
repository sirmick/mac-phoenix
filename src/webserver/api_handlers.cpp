/*
 * API Handlers Module
 *
 * Implementation of all /api/ endpoints
 */

#include "api_handlers.h"
#include "file_scanner.h"
#include "../config/json_utils.h"
#include "../common/include/sysdeps.h"  // For uint32 type
#include "../core/emulator_init.h"  // For deferred initialization
#include "../core/cpu_process.h"  // For fork-based CPU process
#include "../core/shared_state.h"  // For shared memory struct
#include "../drivers/video/video_output.h"  // For snapshot_frame()
#include "../core/boot_progress.h"  // For boot phase query
#include "../core/command_bridge.h"  // For command bridge
#include "../drivers/video/encoders/fpng.h"  // For PNG encoding
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <fstream>
#include <chrono>
#include <thread>
#include <pwd.h>
#include <unistd.h>
#include <time.h>

// Globals from main.cpp for checking emulator state
extern uint32 ROMSize;  // 0 if no ROM loaded

// ADB functions (from adb.cpp)
extern void ADBKeyDown(int code);
extern void ADBKeyUp(int code);
extern void ADBMouseMoved(int x, int y);
extern void ADBSetRelMouseMode(bool relative);

namespace http {

APIRouter::APIRouter(APIContext* context)
    : ctx_(context)
{}

Response APIRouter::handle(const Request& req, bool* handled) {
    *handled = false;

    // Check if this is an API route
    if (req.path.rfind("/api/", 0) != 0) {
        return Response::not_found();
    }

    *handled = true;

    // Route to handlers
    if (req.path == "/api/config" && req.method == "GET") {
        return handle_config_get(req);
    }
    if (req.path == "/api/config" && req.method == "POST") {
        return handle_config_save(req);
    }
    if (req.path == "/api/storage" && req.method == "GET") {
        return handle_storage(req);
    }
    if (req.path == "/api/restart" && req.method == "POST") {
        return handle_restart(req);
    }
    if (req.path == "/api/status" && req.method == "GET") {
        return handle_status(req);
    }
    if (req.path == "/api/codec" && req.method == "POST") {
        return handle_codec_post(req);
    }
    if (req.path == "/api/emulator/start" && req.method == "POST") {
        return handle_emulator_start(req);
    }
    if (req.path == "/api/emulator/stop" && req.method == "POST") {
        return handle_emulator_stop(req);
    }
    if (req.path == "/api/emulator/restart" && req.method == "POST") {
        return handle_emulator_restart(req);
    }
    if (req.path == "/api/emulator/reset" && req.method == "POST") {
        return handle_emulator_reset(req);
    }
    if (req.path == "/api/log" && req.method == "POST") {
        return handle_log(req);
    }
    if (req.path == "/api/error" && req.method == "POST") {
        return handle_error(req);
    }
    if (req.path == "/api/screenshot" && req.method == "GET") {
        return handle_screenshot(req);
    }
    if (req.path == "/api/mouse" && req.method == "GET") {
        return handle_mouse(req);
    }
    if (req.path == "/api/mouse" && req.method == "POST") {
        return handle_mouse_move(req);
    }
    if (req.path == "/api/keypress" && req.method == "POST") {
        return handle_keypress(req);
    }
    if (req.path == "/api/app" && req.method == "GET") {
        return handle_app(req);
    }
    if (req.path == "/api/windows" && req.method == "GET") {
        return handle_windows(req);
    }
    if (req.path == "/api/launch" && req.method == "POST") {
        return handle_launch(req);
    }
    if (req.path == "/api/quit" && req.method == "POST") {
        return handle_quit(req);
    }
    if (req.path == "/api/wait" && req.method == "POST") {
        return handle_wait(req);
    }

    // Unknown API endpoint
    Response resp;
    resp.set_status(404);
    resp.set_body("{\"error\": \"Unknown API endpoint\"}");
    resp.set_content_type("application/json");
    return resp;
}

Response APIRouter::handle_storage(const Request& req) {
    std::string storage_dir = ctx_->config ? ctx_->config->storage_dir : "";
    std::string roms_path = storage_dir + "/roms";
    std::string images_path = storage_dir + "/images";
    std::string json_body = storage::get_storage_json(roms_path, images_path);
    return Response::json(json_body);
}

Response APIRouter::handle_restart(const Request& req) {
    (void)req;
    fprintf(stderr, "Server: Restart requested via API (not implemented yet)\n");
    return Response::json("{\"success\": false, \"message\": \"Not implemented yet\"}");
}

Response APIRouter::handle_status(const Request& req) {
    (void)req;

    std::ostringstream json;
    json << std::fixed << std::setprecision(2);
    json << "{";
    json << "\"emulator_connected\": true";

    if (ctx_->shared_state) {
        // Fork mode: read from shared memory
        SharedState* shm = ctx_->shared_state;
        int32_t state = shm->child_state.load(std::memory_order_acquire);
        bool running = (state == SHM_STATE_STARTING || state == SHM_STATE_RUNNING);
        const char* state_str = "stopped";
        if (state == SHM_STATE_STARTING) state_str = "starting";
        else if (state == SHM_STATE_RUNNING) state_str = "running";
        else if (state == SHM_STATE_ERROR) state_str = "error";

        double elapsed = 0.0;
        int64_t start_us = shm->boot_start_us.load(std::memory_order_acquire);
        if (start_us > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            int64_t now_us = now.tv_sec * 1000000LL + now.tv_nsec / 1000;
            elapsed = (now_us - start_us) / 1e6;
        }

        json << ", \"emulator_running\": " << (running ? "true" : "false");
        json << ", \"cpu_state\": \"" << state_str << "\"";
        json << ", \"boot_phase\": \"" << shm->boot_phase_name << "\"";
        json << ", \"checkload_count\": " << shm->checkload_count.load(std::memory_order_acquire);
        json << ", \"boot_elapsed\": " << elapsed;
        if (state == SHM_STATE_ERROR && shm->error_msg[0]) {
            json << ", \"error\": \"" << shm->error_msg << "\"";
        }
    } else {
        // Legacy in-process mode
        bool cpu_running = ctx_->cpu_running ? ctx_->cpu_running->load(std::memory_order_acquire) : false;
        json << ", \"emulator_running\": " << (cpu_running ? "true" : "false");
        json << ", \"cpu_state\": \"" << (cpu_running ? "running" : "stopped") << "\"";
        json << ", \"boot_phase\": \"" << boot_progress_phase() << "\"";
        json << ", \"checkload_count\": " << boot_progress_checkloads();
        json << ", \"boot_elapsed\": " << boot_progress_elapsed();
    }

    json << "}";
    return Response::json(json.str());
}

Response APIRouter::handle_emulator_start(const Request& req) {
    (void)req;

    // Fork mode: use CpuProcess
    if (ctx_->cpu_process) {
        if (ctx_->cpu_process->is_running()) {
            return Response::json("{\"success\": false, \"error\": \"Already running\"}");
        }

        if (!ctx_->config || ctx_->config->rom_path.empty()) {
            return Response::json(
                "{\"success\": false, "
                "\"error\": \"No ROM configured\", "
                "\"message\": \"Please configure a ROM path in the settings\"}");
        }

        fprintf(stderr, "[API] Starting CPU process (fork mode)\n");
        if (!ctx_->cpu_process->start()) {
            std::string err = "Failed to start CPU process";
            if (ctx_->shared_state && ctx_->shared_state->error_msg[0]) {
                err = ctx_->shared_state->error_msg;
            }
            return Response::json("{\"success\": false, \"error\": \"" + err + "\"}");
        }

        return Response::json("{\"success\": true, \"message\": \"CPU process started\"}");
    }

    // Legacy in-process mode
    if (g_emulator_initialized) {
        if (!ctx_->cpu_running || !ctx_->cpu_cv) {
            return Response::json("{\"success\": false, \"error\": \"CPU state not available\"}");
        }
        {
            std::lock_guard<std::mutex> lock(*ctx_->cpu_mutex);
            ctx_->cpu_running->store(true, std::memory_order_release);
        }
        ctx_->cpu_cv->notify_one();
        return Response::json("{\"success\": true, \"message\": \"CPU resumed\"}");
    }

    return Response::json("{\"success\": false, \"error\": \"Not initialized\"}");
}

Response APIRouter::handle_emulator_stop(const Request& req) {
    (void)req;

    // Fork mode
    if (ctx_->cpu_process) {
        fprintf(stderr, "[API] Stopping CPU process (fork mode)\n");
        ctx_->cpu_process->stop();
        return Response::json("{\"success\": true, \"message\": \"CPU process stopped\"}");
    }

    // Legacy in-process mode
    if (ctx_->cpu_running) {
        {
            std::lock_guard<std::mutex> lock(*ctx_->cpu_mutex);
            ctx_->cpu_running->store(false, std::memory_order_release);
        }
        return Response::json("{\"success\": true, \"message\": \"CPU stopped\"}");
    }

    return Response::json("{\"success\": false, \"error\": \"CPU state not available\"}");
}

Response APIRouter::handle_emulator_restart(const Request& req) {
    (void)req;

    // Fork mode: stop + start (clean state)
    if (ctx_->cpu_process) {
        fprintf(stderr, "[API] Restarting CPU process (fork mode)\n");
        ctx_->cpu_process->reset();
        return Response::json("{\"success\": true, \"message\": \"CPU process restarted\"}");
    }

    // Legacy in-process mode
    if (ctx_->cpu_running && ctx_->cpu_cv) {
        {
            std::lock_guard<std::mutex> lock(*ctx_->cpu_mutex);
            ctx_->cpu_running->store(false, std::memory_order_release);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        {
            std::lock_guard<std::mutex> lock(*ctx_->cpu_mutex);
            ctx_->cpu_running->store(true, std::memory_order_release);
        }
        ctx_->cpu_cv->notify_one();
        return Response::json("{\"success\": true, \"message\": \"CPU restarted\"}");
    }

    return Response::json("{\"success\": false, \"error\": \"CPU state not available\"}");
}

Response APIRouter::handle_emulator_reset(const Request& req) {
    return handle_emulator_restart(req);
}

Response APIRouter::handle_log(const Request& req) {
    auto j = json_utils::parse(req.body);
    std::string level = json_utils::get_string(j, "level");
    std::string msg = json_utils::get_string(j, "message");
    std::string data = json_utils::get_string(j, "data");

    const char* prefix = "[Browser]";
    if (level == "error") {
        fprintf(stderr, "\033[31m%s ERROR: %s%s%s\033[0m\n", prefix, msg.c_str(),
                data.empty() ? "" : " | ", data.c_str());
    } else if (level == "warn") {
        fprintf(stderr, "\033[33m%s WARN: %s%s%s\033[0m\n", prefix, msg.c_str(),
                data.empty() ? "" : " | ", data.c_str());
    } else {
        fprintf(stderr, "%s %s: %s%s%s\n", prefix, level.c_str(), msg.c_str(),
                data.empty() ? "" : " | ", data.c_str());
    }

    return Response::json("{\"ok\": true}");
}

Response APIRouter::handle_error(const Request& req) {
    auto j = json_utils::parse(req.body);
    std::string message = json_utils::get_string(j, "message");
    std::string stack = json_utils::get_string(j, "stack");
    std::string url = json_utils::get_string(j, "url");
    std::string line = json_utils::get_string(j, "line");
    std::string col = json_utils::get_string(j, "col");
    std::string type = json_utils::get_string(j, "type");

    fprintf(stderr, "\033[1;31m[Browser ERROR]\033[0m ");

    if (!type.empty()) {
        fprintf(stderr, "%s: ", type.c_str());
    }

    fprintf(stderr, "%s", message.c_str());

    if (!url.empty()) {
        fprintf(stderr, "\n  at %s", url.c_str());
        if (!line.empty()) {
            fprintf(stderr, ":%s", line.c_str());
            if (!col.empty()) {
                fprintf(stderr, ":%s", col.c_str());
            }
        }
    }

    if (!stack.empty()) {
        fprintf(stderr, "\n  Stack trace:\n");
        size_t pos = 0;
        std::string stack_copy = stack;
        while ((pos = stack_copy.find('\n')) != std::string::npos) {
            std::string line_str = stack_copy.substr(0, pos);
            if (!line_str.empty()) {
                fprintf(stderr, "    %s\n", line_str.c_str());
            }
            stack_copy.erase(0, pos + 1);
        }
        if (!stack_copy.empty()) {
            fprintf(stderr, "    %s\n", stack_copy.c_str());
        }
    } else {
        fprintf(stderr, "\n");
    }

    return Response::json("{\"ok\": true}");
}

Response APIRouter::handle_codec_post(const Request& req) {
    if (!ctx_->server_codec || !ctx_->notify_codec_change_fn) {
        return Response::json("{\"error\": \"Codec change not available\"}");
    }

    auto j = json_utils::parse(req.body);
    std::string codec_str = json_utils::get_string(j, "codec");

    CodecType new_codec;
    if (codec_str == "h264" || codec_str == "H264") {
        new_codec = CodecType::H264;
    } else if (codec_str == "vp9" || codec_str == "VP9") {
        new_codec = CodecType::VP9;
    } else if (codec_str == "png" || codec_str == "PNG") {
        new_codec = CodecType::PNG;
    } else if (codec_str == "webp" || codec_str == "WEBP" || codec_str == "WebP") {
        new_codec = CodecType::WEBP;
    } else {
        return Response::json("{\"error\": \"Invalid codec. Use h264, vp9, png, or webp\"}");
    }

    // Update codec
    CodecType old_codec = *ctx_->server_codec;
    *ctx_->server_codec = new_codec;

    // Also update config (so encoder thread picks it up)
    if (ctx_->config) {
        ctx_->config->codec = codec_str;
    }

    fprintf(stderr, "Config: Codec changed from %d to %d via API\n", (int)old_codec, (int)new_codec);

    // Notify all clients
    if (new_codec != old_codec) {
        ctx_->notify_codec_change_fn(new_codec);
    }

    return Response::json("{\"ok\": true}");
}

// ============================================================================
// Config API (new flat JSON format)
// ============================================================================

Response APIRouter::handle_config_get(const Request& req) {
    (void)req;

    if (!ctx_->config) {
        return Response::json("{\"error\": \"No config available\"}");
    }

    std::string json_str = ctx_->config->to_json().dump(2);
    fprintf(stderr, "[API] GET /api/config → %zu bytes\n", json_str.size());
    return Response::json(json_str);
}

Response APIRouter::handle_config_save(const Request& req) {
    if (req.body.empty()) {
        return Response::json("{\"success\": false, \"error\": \"Empty request body\"}");
    }

    if (!ctx_->config) {
        return Response::json("{\"success\": false, \"error\": \"No config available\"}");
    }

    fprintf(stderr, "[API] POST /api/config ← %zu bytes: %s\n",
            req.body.size(), req.body.c_str());

    nlohmann::json j;
    try {
        j = json_utils::parse(req.body);
    } catch (const std::exception& e) {
        fprintf(stderr, "[API] Config parse error: %s\n", e.what());
        return Response::json("{\"success\": false, \"error\": \"Invalid JSON\"}");
    }

    // Merge updates into live config AND file-level config
    ctx_->config->merge_ui_json(j);

    // Save to disk (writes file_config_ only, not CLI overrides)
    if (!ctx_->config->save()) {
        return Response::json("{\"success\": false, \"error\": \"Failed to save config file\"}");
    }

    std::string saved_json = ctx_->config->to_json().dump(2);
    fprintf(stderr, "[API] Config saved → %s:\n%s\n", ctx_->config->config_path.c_str(), saved_json.c_str());

    return Response::json("{\"success\": true}");
}

Response APIRouter::handle_screenshot(const Request& req) {
    (void)req;

    if (!ctx_->video_output) {
        Response resp;
        resp.set_status(503, "Service Unavailable");
        resp.set_body("{\"error\": \"Video output not available\"}");
        resp.set_content_type("application/json");
        return resp;
    }

    // In fork mode, return 503 if CPU is not running (no live frames)
    if (ctx_->shared_state) {
        int32_t state = ctx_->shared_state->child_state.load(std::memory_order_acquire);
        if (state != SHM_STATE_STARTING && state != SHM_STATE_RUNNING) {
            Response resp;
            resp.set_status(503, "Service Unavailable");
            resp.set_body("{\"error\": \"Emulator not running\"}");
            resp.set_content_type("application/json");
            return resp;
        }
    }

    // Allocate buffer for snapshot (max 1920x1080x4 = ~8MB)
    int width = 0, height = 0;
    PixelFormat format;
    std::vector<uint32_t> pixels(1920 * 1080);

    if (!ctx_->video_output->snapshot_frame(pixels.data(), &width, &height, &format)) {
        Response resp;
        resp.set_status(503, "Service Unavailable");
        resp.set_body("{\"error\": \"No frame available yet\"}");
        resp.set_content_type("application/json");
        return resp;
    }

    // Convert to RGB (3 bytes/pixel) for PNG encoding
    std::vector<uint8_t> rgb(width * height * 3);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(pixels.data());
    for (int i = 0; i < width * height; i++) {
        if (format == PIXFMT_ARGB) {
            rgb[i * 3 + 0] = src[i * 4 + 1];  // R
            rgb[i * 3 + 1] = src[i * 4 + 2];  // G
            rgb[i * 3 + 2] = src[i * 4 + 3];  // B
        } else {
            rgb[i * 3 + 0] = src[i * 4 + 2];  // R
            rgb[i * 3 + 1] = src[i * 4 + 1];  // G
            rgb[i * 3 + 2] = src[i * 4 + 0];  // B
        }
    }

    // Encode to PNG using fpng
    static bool fpng_inited = false;
    if (!fpng_inited) {
        fpng::fpng_init();
        fpng_inited = true;
    }
    std::vector<uint8_t> png_data;
    if (!fpng::fpng_encode_image_to_memory(rgb.data(), width, height, 3, png_data)) {
        Response resp;
        resp.set_status(500, "Internal Server Error");
        resp.set_body("{\"error\": \"PNG encoding failed\"}");
        resp.set_content_type("application/json");
        return resp;
    }

    Response resp;
    resp.set_status(200, "OK");
    resp.set_content_type("image/png");
    resp.set_body(std::string(reinterpret_cast<const char*>(png_data.data()), png_data.size()));
    return resp;
}

Response APIRouter::handle_mouse(const Request& req) {
    (void)req;

    // Check if CPU is running
    bool running = false;
    if (ctx_->shared_state) {
        running = ctx_->shared_state->child_state.load(std::memory_order_acquire) == SHM_STATE_RUNNING;
    } else if (ctx_->cpu_running) {
        running = ctx_->cpu_running->load(std::memory_order_acquire);
    }

    if (!running) {
        Response resp;
        resp.set_status(503, "Service Unavailable");
        resp.set_body("{\"error\": \"Emulator not running\"}");
        resp.set_content_type("application/json");
        return resp;
    }

    std::ostringstream json;

    if (ctx_->shared_state) {
        // Fork mode: read cursor state from shared memory
        SharedState* shm = ctx_->shared_state;
        json << "{"
             << "\"x\": " << shm->cursor_x.load(std::memory_order_acquire)
             << ", \"y\": " << shm->cursor_y.load(std::memory_order_acquire)
             << ", \"raw_x\": " << shm->raw_x.load(std::memory_order_acquire)
             << ", \"raw_y\": " << shm->raw_y.load(std::memory_order_acquire)
             << ", \"mtemp_x\": " << shm->mtemp_x.load(std::memory_order_acquire)
             << ", \"mtemp_y\": " << shm->mtemp_y.load(std::memory_order_acquire)
             << ", \"crsr_new\": " << shm->crsr_new.load(std::memory_order_acquire)
             << ", \"crsr_couple\": " << shm->crsr_couple.load(std::memory_order_acquire)
             << ", \"crsr_busy\": " << shm->crsr_busy.load(std::memory_order_acquire)
             << "}";
    } else {
        // In-process mode: read from Mac low-memory globals
        MacCursorState cs;
        boot_progress_get_cursor_state(&cs);
        json << "{"
             << "\"x\": " << cs.cursor_x << ", \"y\": " << cs.cursor_y
             << ", \"raw_x\": " << cs.raw_x << ", \"raw_y\": " << cs.raw_y
             << ", \"mtemp_x\": " << cs.mtemp_x << ", \"mtemp_y\": " << cs.mtemp_y
             << ", \"crsr_new\": " << cs.crsr_new
             << ", \"crsr_couple\": " << cs.crsr_couple
             << ", \"crsr_busy\": " << cs.crsr_busy
             << "}";
    }

    return Response::json(json.str());
}

// POST /api/mouse - move the mouse
// Absolute: {"x": 100, "y": 200}
// Relative: {"dx": 10, "dy": -5}
Response APIRouter::handle_mouse_move(const Request& req) {
    auto body = req.body;

    // Check if this is a relative move (has "dx" field)
    bool relative = body.find("\"dx\"") != std::string::npos;

    if (relative) {
        auto dx_pos = body.find("\"dx\"");
        auto dx_colon = body.find(':', dx_pos);
        auto dx_start = body.find_first_not_of(" \t\n", dx_colon + 1);
        int dx = std::stoi(body.substr(dx_start));

        auto dy_pos = body.find("\"dy\"");
        if (dy_pos == std::string::npos) {
            Response r; r.set_status(400); r.set_body("{\"error\": \"missing 'dy' field\"}"); r.add_header("Content-Type", "application/json"); return r;
        }
        auto dy_colon = body.find(':', dy_pos);
        auto dy_start = body.find_first_not_of(" \t\n", dy_colon + 1);
        int dy = std::stoi(body.substr(dy_start));

        if (ctx_->shared_state) {
            shared_input_push(ctx_->shared_state, SHM_INPUT_MOUSE_MODE, 1, 0, 0, 0);
            shared_input_push(ctx_->shared_state, SHM_INPUT_MOUSE_REL, 0, (int16_t)dx, (int16_t)dy, 0);
        } else {
            ADBSetRelMouseMode(true);
            ADBMouseMoved(dx, dy);
        }

        return Response::json("{\"success\": true, \"dx\": " + std::to_string(dx) + ", \"dy\": " + std::to_string(dy) + ", \"mode\": \"relative\"}");
    }

    // Absolute move
    auto x_pos = body.find("\"x\"");
    if (x_pos == std::string::npos) {
        Response r; r.set_status(400); r.set_body("{\"error\": \"missing 'x' (or 'dx' for relative)\"}"); r.add_header("Content-Type", "application/json"); return r;
    }
    auto x_colon = body.find(':', x_pos);
    auto x_start = body.find_first_not_of(" \t\n", x_colon + 1);
    int x = std::stoi(body.substr(x_start));

    auto y_pos = body.find("\"y\"");
    if (y_pos == std::string::npos) {
        Response r; r.set_status(400); r.set_body("{\"error\": \"missing 'y' field\"}"); r.add_header("Content-Type", "application/json"); return r;
    }
    auto y_colon = body.find(':', y_pos);
    auto y_start = body.find_first_not_of(" \t\n", y_colon + 1);
    int y = std::stoi(body.substr(y_start));

    if (ctx_->shared_state) {
        shared_input_push(ctx_->shared_state, SHM_INPUT_MOUSE_MODE, 0, 0, 0, 0);
        shared_input_push(ctx_->shared_state, SHM_INPUT_MOUSE_ABS, 0, (int16_t)x, (int16_t)y, 0);
    } else {
        ADBSetRelMouseMode(false);
        ADBMouseMoved(x, y);
    }

    return Response::json("{\"success\": true, \"x\": " + std::to_string(x) + ", \"y\": " + std::to_string(y) + ", \"mode\": \"absolute\"}");
}

// POST /api/keypress - send a key event to the emulator
Response APIRouter::handle_keypress(const Request& req) {
    int keycode = -1;

    auto body = req.body;
    auto key_pos = body.find("\"key\"");
    if (key_pos == std::string::npos) {
        Response resp;
        resp.set_status(400);
        resp.set_body("{\"error\": \"missing 'key' field\"}");
        resp.add_header("Content-Type", "application/json");
        return resp;
    }

    auto colon_pos = body.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        Response r1; r1.set_status(400); r1.set_body("{\"error\": \"malformed JSON\"}"); r1.add_header("Content-Type", "application/json"); return r1;
    }

    auto val_start = body.find_first_not_of(" \t\n", colon_pos + 1);
    if (val_start != std::string::npos && body[val_start] == '"') {
        auto val_end = body.find('"', val_start + 1);
        std::string name = body.substr(val_start + 1, val_end - val_start - 1);
        if (name == "return" || name == "enter") keycode = 0x24;
        else if (name == "escape" || name == "esc") keycode = 0x35;
        else if (name == "space") keycode = 0x31;
        else if (name == "tab") keycode = 0x30;
        else if (name == "delete" || name == "backspace") keycode = 0x33;
        else if (name == "up") keycode = 0x3e;
        else if (name == "down") keycode = 0x3d;
        else if (name == "left") keycode = 0x3b;
        else if (name == "right") keycode = 0x3c;
        else {
            Response r2; r2.set_status(400); r2.set_body("{\"error\": \"unknown key name: " + name + "\"}"); r2.add_header("Content-Type", "application/json"); return r2;
        }
    } else {
        keycode = std::stoi(body.substr(val_start));
    }

    if (keycode < 0 || keycode > 127) {
        Response r3; r3.set_status(400); r3.set_body("{\"error\": \"invalid keycode\"}"); r3.add_header("Content-Type", "application/json"); return r3;
    }

    if (ctx_->shared_state) {
        shared_input_push(ctx_->shared_state, SHM_INPUT_KEY, 1, 0, 0, (uint8_t)keycode);
        usleep(50000);  // 50ms press
        shared_input_push(ctx_->shared_state, SHM_INPUT_KEY, 0, 0, 0, (uint8_t)keycode);
    } else {
        ::ADBKeyDown(keycode);
        usleep(50000);  // 50ms press
        ::ADBKeyUp(keycode);
    }

    return Response::json("{\"success\": true, \"keycode\": " + std::to_string(keycode) + "}");
}

// ── Command Bridge Endpoints ──

Response APIRouter::handle_app(const Request& req) {
    (void)req;

    if (ctx_->shared_state) {
        SharedState* shm = ctx_->shared_state;
        int32_t state = shm->child_state.load(std::memory_order_acquire);
        if (state != SHM_STATE_RUNNING) {
            return Response::json("{\"app\": \"\", \"error\": \"emulator not running\"}");
        }
        // Read passive field written by child at 60Hz
        std::string app(shm->cur_app_name);
        return Response::json("{\"app\": \"" + app + "\"}");
    }

    auto result = CommandBridge::execute_read(CmdType::GET_APP_NAME);
    return Response::json("{\"app\": \"" + result.data + "\"}");
}

Response APIRouter::handle_windows(const Request& req) {
    (void)req;

    if (ctx_->shared_state) {
        SharedState* shm = ctx_->shared_state;
        int32_t state = shm->child_state.load(std::memory_order_acquire);
        if (state != SHM_STATE_RUNNING) {
            return Response::json("{\"windows\": [], \"error\": \"emulator not running\"}");
        }
        uint32_t id = shared_cmd_submit(shm, SharedState::CMD_GET_WINDOW_LIST);
        char data[SharedState::CMD_DATA_SIZE];
        int16_t err = 0;
        // Poll for result (child processes on next 60Hz tick, ~16ms max wait)
        for (int i = 0; i < 50; i++) {
            if (shared_cmd_poll(shm, id, &err, data, sizeof(data))) {
                return Response::json("{\"windows\": " + std::string(data) + "}");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return Response::json("{\"windows\": [], \"error\": \"timeout\"}");
    }

    auto result = CommandBridge::execute_read(CmdType::GET_WINDOW_LIST);
    return Response::json("{\"windows\": " + result.data + "}");
}

Response APIRouter::handle_launch(const Request& req) {
    auto& body = req.body;

    // Parse {"path": "Macintosh HD:SomeApp"}
    auto path_pos = body.find("\"path\"");
    if (path_pos == std::string::npos) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"missing 'path' field\"}");
        r.set_content_type("application/json");
        return r;
    }
    auto quote1 = body.find('"', body.find(':', path_pos) + 1);
    auto quote2 = body.find('"', quote1 + 1);
    if (quote1 == std::string::npos || quote2 == std::string::npos) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"malformed 'path' value\"}");
        r.set_content_type("application/json");
        return r;
    }
    std::string path = body.substr(quote1 + 1, quote2 - quote1 - 1);

    if (ctx_->shared_state) {
        SharedState* shm = ctx_->shared_state;
        int32_t state = shm->child_state.load(std::memory_order_acquire);
        if (state != SHM_STATE_RUNNING) {
            return Response::json("{\"success\": false, \"error\": \"emulator not running\"}");
        }
        uint32_t id = shared_cmd_submit(shm, SharedState::CMD_LAUNCH_APP, path.c_str());
        char data[SharedState::CMD_DATA_SIZE];
        int16_t err = 0;
        for (int i = 0; i < 200; i++) {  // 10s timeout
            if (shared_cmd_poll(shm, id, &err, data, sizeof(data))) {
                std::ostringstream json;
                json << "{\"success\": " << (err == 0 ? "true" : "false")
                     << ", \"error_code\": " << err
                     << ", \"message\": \"" << data << "\"}";
                return Response::json(json.str());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return Response::json("{\"success\": false, \"error\": \"timeout waiting for launch\"}");
    }

    Command cmd;
    cmd.type = CmdType::LAUNCH_APP;
    cmd.arg = path;
    uint32_t id = g_command_bridge.submit(cmd);

    CommandResult result;
    if (g_command_bridge.wait_result(id, result, 10000)) {
        std::ostringstream json;
        json << "{\"success\": " << (result.err == 0 ? "true" : "false")
             << ", \"error_code\": " << result.err
             << ", \"message\": \"" << result.data << "\"}";
        return Response::json(json.str());
    }

    return Response::json("{\"success\": false, \"error\": \"timeout waiting for launch\"}");
}

Response APIRouter::handle_quit(const Request& req) {
    (void)req;

    if (ctx_->shared_state) {
        SharedState* shm = ctx_->shared_state;
        int32_t state = shm->child_state.load(std::memory_order_acquire);
        if (state != SHM_STATE_RUNNING) {
            return Response::json("{\"success\": false, \"error\": \"emulator not running\"}");
        }
        uint32_t id = shared_cmd_submit(shm, SharedState::CMD_QUIT_APP);
        char data[SharedState::CMD_DATA_SIZE];
        int16_t err = 0;
        for (int i = 0; i < 100; i++) {  // 5s timeout
            if (shared_cmd_poll(shm, id, &err, data, sizeof(data))) {
                return Response::json("{\"success\": true}");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return Response::json("{\"success\": false, \"error\": \"timeout\"}");
    }

    Command cmd;
    cmd.type = CmdType::QUIT_APP;
    uint32_t id = g_command_bridge.submit(cmd);

    CommandResult result;
    if (g_command_bridge.wait_result(id, result, 5000)) {
        return Response::json("{\"success\": true}");
    }

    return Response::json("{\"success\": false, \"error\": \"timeout\"}");
}

Response APIRouter::handle_wait(const Request& req) {
    auto& body = req.body;

    // Parse {"condition": "app=Finder", "timeout": 30}
    // Supported conditions: "app=Name", "boot=phase"
    auto cond_pos = body.find("\"condition\"");
    if (cond_pos == std::string::npos) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"missing 'condition' field\"}");
        r.set_content_type("application/json");
        return r;
    }
    auto cq1 = body.find('"', body.find(':', cond_pos) + 1);
    auto cq2 = body.find('"', cq1 + 1);
    std::string condition = body.substr(cq1 + 1, cq2 - cq1 - 1);

    // Parse timeout (default 30s)
    int timeout_s = 30;
    auto timeout_pos = body.find("\"timeout\"");
    if (timeout_pos != std::string::npos) {
        auto tc = body.find(':', timeout_pos);
        auto ts = body.find_first_of("0123456789", tc + 1);
        if (ts != std::string::npos) timeout_s = std::stoi(body.substr(ts));
    }

    // Parse condition type
    auto eq_pos = condition.find('=');
    if (eq_pos == std::string::npos) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"condition must be 'app=Name' or 'boot=phase'\"}");
        r.set_content_type("application/json");
        return r;
    }
    std::string cond_type = condition.substr(0, eq_pos);
    std::string cond_value = condition.substr(eq_pos + 1);

    // Poll loop
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cond_type == "app") {
            std::string app;
            if (ctx_->shared_state) {
                app = ctx_->shared_state->cur_app_name;
            } else {
                auto result = CommandBridge::execute_read(CmdType::GET_APP_NAME);
                app = result.data;
            }
            if (app == cond_value) {
                return Response::json("{\"ok\": true, \"app\": \"" + app + "\"}");
            }
        } else if (cond_type == "boot") {
            std::string phase;
            if (ctx_->shared_state) {
                phase = ctx_->shared_state->boot_phase_name;
            } else {
                phase = boot_progress_phase();
            }
            if (phase == cond_value) {
                return Response::json("{\"ok\": true, \"boot_phase\": \"" + phase + "\"}");
            }
        } else {
            Response r; r.set_status(400);
            r.set_body("{\"error\": \"unknown condition type: " + cond_type + "\"}");
            r.set_content_type("application/json");
            return r;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return Response::json("{\"ok\": false, \"error\": \"timeout\"}");
}

} // namespace http
