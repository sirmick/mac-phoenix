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
#include "../drivers/video/video_output.h"  // For snapshot_frame()
#include "../core/boot_progress.h"  // For boot phase query
#include "../drivers/video/encoders/fpng.h"  // For PNG encoding
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <fstream>
#include <pwd.h>
#include <unistd.h>

// Globals from main.cpp for checking emulator state
extern uint32 ROMSize;  // 0 if no ROM loaded

// ADB functions (from adb.cpp)
extern void ADBKeyDown(int code);
extern void ADBKeyUp(int code);

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
    if (req.path == "/api/keypress" && req.method == "POST") {
        return handle_keypress(req);
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

    // Get CPU running state
    bool cpu_running = ctx_->cpu_running ? ctx_->cpu_running->load(std::memory_order_acquire) : false;

    std::ostringstream json;
    json << std::fixed << std::setprecision(2);
    json << "{";
    json << "\"emulator_connected\": true";
    json << ", \"emulator_running\": " << (cpu_running ? "true" : "false");
    json << ", \"cpu_state\": \"" << (cpu_running ? "running" : "stopped") << "\"";
    json << ", \"boot_phase\": \"" << boot_progress_phase() << "\"";
    json << ", \"checkload_count\": " << boot_progress_checkloads();
    json << ", \"boot_elapsed\": " << boot_progress_elapsed();
    json << "}";
    return Response::json(json.str());
}

Response APIRouter::handle_emulator_start(const Request& req) {
    (void)req;

    // Check if already initialized
    if (g_emulator_initialized) {
        fprintf(stderr, "[API] Emulator already initialized - resuming CPU\n");

        if (!ctx_->cpu_running || !ctx_->cpu_cv) {
            return Response::json("{\"success\": false, \"error\": \"CPU state not available\"}");
        }

        // Resume CPU execution
        {
            std::lock_guard<std::mutex> lock(*ctx_->cpu_mutex);
            ctx_->cpu_running->store(true, std::memory_order_release);
        }
        ctx_->cpu_cv->notify_one();
        fprintf(stderr, "[API] CPU resumed via web UI\n");

        return Response::json("{\"success\": true, \"message\": \"CPU resumed\"}");
    }

    // Not initialized - need to load ROM and initialize emulator
    fprintf(stderr, "[API] Emulator not initialized - loading ROM and initializing...\n");

    if (!ctx_->config) {
        return Response::json("{\"success\": false, \"error\": \"No config available\"}");
    }

    std::string rom_filename = ctx_->config->rom_path;
    if (rom_filename.empty()) {
        fprintf(stderr, "[API] ERROR: No ROM configured in config file\n");
        return Response::json(
            "{\"success\": false, "
            "\"error\": \"No ROM configured\", "
            "\"message\": \"Please configure a ROM path in the settings\"}"
        );
    }

    // Initialize emulator with ROM
    std::string emulator_type = ctx_->config->architecture_string();
    std::string storage_dir = ctx_->config->storage_dir;

    fprintf(stderr, "[API] Initializing emulator: %s with ROM: %s\n",
            emulator_type.c_str(), rom_filename.c_str());

    if (!init_emulator_from_config(emulator_type.c_str(),
                                    storage_dir.c_str(),
                                    rom_filename.c_str())) {
        fprintf(stderr, "[API] ERROR: Failed to initialize emulator\n");
        return Response::json(
            "{\"success\": false, "
            "\"error\": \"Failed to load ROM and initialize CPU\", "
            "\"message\": \"Check that the ROM file exists and is valid\"}"
        );
    }

    fprintf(stderr, "[API] Emulator initialized successfully\n");

    // Start CPU execution
    if (!ctx_->cpu_running || !ctx_->cpu_cv) {
        return Response::json("{\"success\": false, \"error\": \"CPU state not available\"}");
    }

    {
        std::lock_guard<std::mutex> lock(*ctx_->cpu_mutex);
        ctx_->cpu_running->store(true, std::memory_order_release);
    }
    ctx_->cpu_cv->notify_one();
    fprintf(stderr, "[API] CPU started via web UI\n");

    return Response::json("{\"success\": true, \"message\": \"Emulator initialized and CPU started\"}");
}

Response APIRouter::handle_emulator_stop(const Request& req) {
    (void)req;

    if (!ctx_->cpu_running) {
        return Response::json("{\"success\": false, \"error\": \"CPU state not available\"}");
    }

    // Stop CPU execution
    {
        std::lock_guard<std::mutex> lock(*ctx_->cpu_mutex);
        ctx_->cpu_running->store(false, std::memory_order_release);
    }
    fprintf(stderr, "[API] CPU stopped via web UI\n");

    return Response::json("{\"success\": true, \"message\": \"CPU stopped\"}");
}

Response APIRouter::handle_emulator_restart(const Request& req) {
    (void)req;
    return Response::json("{\"success\": false, \"message\": \"Not implemented yet (in-process integration pending)\"}");
}

Response APIRouter::handle_emulator_reset(const Request& req) {
    (void)req;
    fprintf(stderr, "API: Reset requested via web UI (not implemented yet)\n");
    return Response::json("{\"success\": false, \"message\": \"Not implemented yet (in-process integration pending)\"}");
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
    } else if (codec_str == "av1" || codec_str == "AV1") {
        new_codec = CodecType::AV1;
    } else if (codec_str == "vp9" || codec_str == "VP9") {
        new_codec = CodecType::VP9;
    } else if (codec_str == "png" || codec_str == "PNG") {
        new_codec = CodecType::PNG;
    } else if (codec_str == "webp" || codec_str == "WEBP" || codec_str == "WebP") {
        new_codec = CodecType::WEBP;
    } else {
        return Response::json("{\"error\": \"Invalid codec. Use h264, av1, vp9, png, or webp\"}");
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

    return Response::json(ctx_->config->to_json().dump(2));
}

Response APIRouter::handle_config_save(const Request& req) {
    if (req.body.empty()) {
        return Response::json("{\"success\": false, \"error\": \"Empty request body\"}");
    }

    if (!ctx_->config) {
        return Response::json("{\"success\": false, \"error\": \"No config available\"}");
    }

    nlohmann::json j;
    try {
        j = json_utils::parse(req.body);
    } catch (const std::exception& e) {
        fprintf(stderr, "ERROR: Failed to parse config JSON: %s\n", e.what());
        return Response::json("{\"success\": false, \"error\": \"Invalid JSON\"}");
    }

    // Merge updates into live config
    ctx_->config->merge_json(j);

    // Save to disk
    if (!ctx_->config->save()) {
        return Response::json("{\"success\": false, \"error\": \"Failed to save config file\"}");
    }

    fprintf(stderr, "[API] Config saved (codec=%s, mousemode=%s)\n",
            ctx_->config->codec.c_str(), ctx_->config->mousemode.c_str());

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

    bool cpu_running = ctx_->cpu_running ? ctx_->cpu_running->load(std::memory_order_acquire) : false;
    if (!cpu_running) {
        Response resp;
        resp.set_status(503, "Service Unavailable");
        resp.set_body("{\"error\": \"Emulator not running\"}");
        resp.set_content_type("application/json");
        return resp;
    }

    MacCursorState cs;
    boot_progress_get_cursor_state(&cs);

    std::ostringstream json;
    json << "{"
         << "\"x\": " << cs.cursor_x << ", \"y\": " << cs.cursor_y
         << ", \"raw_x\": " << cs.raw_x << ", \"raw_y\": " << cs.raw_y
         << ", \"mtemp_x\": " << cs.mtemp_x << ", \"mtemp_y\": " << cs.mtemp_y
         << ", \"crsr_new\": " << cs.crsr_new
         << ", \"crsr_couple\": " << cs.crsr_couple
         << ", \"crsr_busy\": " << cs.crsr_busy
         << "}";
    return Response::json(json.str());
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

    ::ADBKeyDown(keycode);
    usleep(50000);  // 50ms press
    ::ADBKeyUp(keycode);

    return Response::json("{\"success\": true, \"keycode\": " + std::to_string(keycode) + "}");
}

} // namespace http
