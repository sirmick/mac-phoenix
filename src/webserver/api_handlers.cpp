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
#include "../core/emulator_subprocess.h"  // For subprocess
#include "../ipc/ipc_client.h"    // For g_ipc_shm_mutex
#include "../ipc/ipc_protocol.h"  // For IPC buffer
#include "../drivers/video/video_output.h"  // For snapshot_frame()
#include "../core/boot_progress.h"  // For boot phase query
#include "../core/command_bridge.h"  // For command bridge
#include <sys/stat.h>
#include <unistd.h>
#include "../drivers/video/encoders/fpng.h"  // For PNG encoding
#include "../drivers/video/encoders/codec.h"  // For codec_available()

// Clear all frame sessions on emulator stop/start (forces full frames on reconnect)
static bool g_frame_sessions_dirty = false;
static void invalidate_frame_sessions() {
    g_frame_sessions_dirty = true;
}

// Forward-declare WebRTC peer reset (avoids pulling in rtc/rtc.hpp)
namespace webrtc {
    class WebRTCServer;
    extern WebRTCServer* g_server;
    void reset_webrtc_peers();  // Defined in webrtc_server.cpp
}
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <fstream>
#include <chrono>
#include <shared_mutex>
#include <thread>
#include <pwd.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <vector>

// Globals from main.cpp for checking emulator state
extern uint32 ROMSize;  // 0 if no ROM loaded

// ADB functions (from adb.cpp)
extern void ADBKeyDown(int code);
extern void ADBKeyUp(int code);
extern void ADBMouseMoved(int x, int y);
extern void ADBMouseDown(int button);
extern void ADBMouseUp(int button);
extern void ADBSetRelMouseMode(bool relative);
extern "C" void InvokeDebugger(void);

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
    if (req.path == "/api/storage/create-image" && req.method == "POST") {
        return handle_create_image(req);
    }
    if (req.path == "/api/restart" && req.method == "POST") {
        return handle_restart(req);
    }
    if (req.path == "/api/shutdown" && req.method == "POST") {
        return handle_shutdown(req);
    }
    if (req.path == "/api/status" && req.method == "GET") {
        return handle_status(req);
    }
    if (req.path == "/api/codec" && req.method == "POST") {
        return handle_codec_post(req);
    }
    if (req.path == "/api/codecs" && req.method == "GET") {
        return handle_codecs_get(req);
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
    if (req.path == "/api/invoke-debug" && req.method == "POST") {
        return handle_invoke_debug(req);
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
    if (req.path == "/api/script" && req.method == "POST") {
        return handle_script(req);
    }
    if (req.path == "/api/quit" && req.method == "POST") {
        return handle_quit(req);
    }
    if (req.path == "/api/wait" && req.method == "POST") {
        return handle_wait(req);
    }
    if (req.path == "/api/clipboard" && req.method == "POST") {
        return handle_clipboard_set(req);
    }
    if (req.path == "/api/frame" && req.method == "GET") {
        return handle_frame(req);
    }

    // Unknown API endpoint
    Response resp;
    resp.set_status(404);
    resp.set_body("{\"error\": \"Unknown API endpoint\"}");
    resp.set_content_type("application/json");
    return resp;
}

Response APIRouter::handle_storage(const Request& /*req*/) {
    std::string storage_dir = ctx_->config ? ctx_->config->storage_dir : "";
    std::string roms_path = storage_dir + "/roms";
    std::string images_path = storage_dir + "/images";
    std::string json_body = storage::get_storage_json(roms_path, images_path);
    return Response::json(json_body);
}

static bool run_fork_exec(const std::vector<std::string>& args, std::string& err_out) {
    pid_t pid = fork();
    if (pid < 0) { err_out = "fork failed"; return false; }
    if (pid == 0) {
        std::vector<char*> argv;
        for (const auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDOUT_FILENO); dup2(devnull, STDERR_FILENO); close(devnull); }
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) { err_out = "waitpid failed"; return false; }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        err_out = args[0] + " not found (install hfsutils/hfsprogs)";
    } else {
        err_out = args[0] + " failed";
    }
    return false;
}

Response APIRouter::handle_create_image(const Request& req) {
    auto bad = [](int code, const std::string& msg) {
        Response r;
        r.set_status(code);
        r.set_body("{\"error\":\"" + storage::json_escape(msg) + "\"}");
        r.set_content_type("application/json");
        return r;
    };

    if (!ctx_->config || ctx_->config->storage_dir.empty()) {
        return bad(500, "storage_dir not configured");
    }

    auto j = json_utils::parse(req.body);
    std::string format = json_utils::get_string(j, "format");
    std::string filename = json_utils::get_string(j, "filename");
    std::string volume_name = json_utils::get_string(j, "volume_name");
    int size_mb = json_utils::get_int(j, "size_mb", 120);

    if (format != "hfs" && format != "hfs+") {
        return bad(400, "format must be 'hfs' or 'hfs+'");
    }
    if (filename.empty()) {
        return bad(400, "filename required");
    }
    if (filename.find('/') != std::string::npos ||
        filename.find("..") != std::string::npos ||
        filename[0] == '.') {
        return bad(400, "invalid filename");
    }
    if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".img") {
        filename += ".img";
    }
    int min_mb = (format == "hfs+") ? 4 : 1;
    if (size_mb < min_mb || size_mb > 8192) {
        return bad(400, "size_mb out of range");
    }
    if (volume_name.empty()) {
        volume_name = (format == "hfs+") ? "Macintosh HD" : "Untitled";
    }
    for (char c : volume_name) {
        if ((unsigned char)c < 0x20 || c == ':') {
            return bad(400, "invalid volume name");
        }
    }
    if (format == "hfs" && volume_name.size() > 27) {
        return bad(400, "HFS volume name must be 27 chars or fewer");
    }

    std::string images_dir = ctx_->config->storage_dir + "/images";
    mkdir(images_dir.c_str(), 0755);
    std::string out_path = images_dir + "/" + filename;

    struct stat st;
    if (stat(out_path.c_str(), &st) == 0) {
        return bad(409, "file already exists");
    }

    int fd = open(out_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        return bad(500, "failed to create image file");
    }
    off_t size_bytes = (off_t)size_mb * 1024 * 1024;
    if (ftruncate(fd, size_bytes) != 0) {
        close(fd);
        unlink(out_path.c_str());
        return bad(500, "ftruncate failed");
    }
    close(fd);

    std::string err;
    bool ok;
    if (format == "hfs") {
        ok = run_fork_exec({"hformat", "-l", volume_name, out_path}, err);
    } else {
        ok = run_fork_exec({"mkfs.hfsplus", "-v", volume_name, out_path}, err);
    }
    if (!ok) {
        unlink(out_path.c_str());
        return bad(500, err);
    }

    std::ostringstream body;
    body << "{\"ok\":true,\"filename\":\"" << storage::json_escape(filename)
         << "\",\"size_mb\":" << size_mb
         << ",\"format\":\"" << format << "\"}";
    return Response::json(body.str());
}

// Disk-backed bridge file helpers. Files live in cfg.bridge_dir, which is
// the same directory the guest sees as the "Host:" ExtFS volume — so the
// agent reads/writes via standard File Manager calls and the host reads
// via plain POSIX. This is what makes the bridge work across the parent /
// IPC-child process split.
static bool bridge_read_file(const std::string& dir, const char* name, std::string& out) {
    std::string path = dir + "/" + name;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

static void bridge_write_file(const std::string& dir, const char* name, const std::string& data) {
    std::string path = dir + "/" + name;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

static void bridge_remove_file(const std::string& dir, const char* name) {
    std::string path = dir + "/" + name;
    unlink(path.c_str());
}

static bool bridge_has_file(const std::string& dir, const char* name) {
    std::string path = dir + "/" + name;
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

// True iff the bridge is enabled, Finder has launched, and the BridgeAgent
// dropped a heartbeat in the last ~10 seconds. Drives the UI's choice between
// graceful (Shut Down / Restart) and hard (Power Off / Reset) default actions.
static bool bridge_agent_is_connected(bool finder_reached) {
    auto& cfg = config::EmulatorConfig::instance();
    if (!cfg.bridge_enabled || cfg.bridge_dir.empty()) return false;
    if (!finder_reached) return false;
    std::string path = cfg.bridge_dir + "/bridge_heartbeat";
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    time_t now = time(nullptr);
    return (now - st.st_mtime) <= 10;
}

// Send a command to the guest BridgeAgent and poll for a result.
// Shared by handle_shutdown / handle_restart / handle_quit-style endpoints
// that go through the guest-OS bridge rather than manipulating the subprocess.
static Response bridge_command(const char* cmd, int poll_iterations) {
    auto& cfg = config::EmulatorConfig::instance();
    if (!cfg.bridge_enabled) {
        return Response::json("{\"success\": false, \"error\": \"bridge not enabled\"}");
    }
    if (cfg.bridge_dir.empty()) {
        return Response::json("{\"success\": false, \"error\": \"bridge_dir not set\"}");
    }

    bridge_remove_file(cfg.bridge_dir, "_bridge_result");
    bridge_write_file(cfg.bridge_dir, "_bridge_cmd", cmd);

    for (int i = 0; i < poll_iterations; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::string result;
        if (bridge_read_file(cfg.bridge_dir, "_bridge_result", result)) {
            bridge_remove_file(cfg.bridge_dir, "_bridge_result");
            int mac_err = std::atoi(result.c_str());
            if (mac_err != 0) {
                std::ostringstream json;
                json << "{\"success\": false, \"error_code\": " << mac_err
                     << ", \"message\": \"bridge command failed (Mac OS error "
                     << mac_err << ")\"}";
                return Response::json(json.str());
            }
            return Response::json("{\"success\": true, \"error_code\": 0}");
        }
        if (!bridge_has_file(cfg.bridge_dir, "_bridge_cmd")) {
            return Response::json("{\"success\": true, \"error_code\": 0}");
        }
    }
    bridge_remove_file(cfg.bridge_dir, "_bridge_cmd");
    return Response::json("{\"success\": false, \"error\": \"timeout waiting for bridge agent\"}");
}

Response APIRouter::handle_restart(const Request& req) {
    (void)req;
    // Graceful OS restart — the guest agent sends kAERestart to Finder,
    // which quits apps and invokes the Shutdown Manager. The emulator
    // subprocess itself keeps running; the guest OS reboots inside it.
    return bridge_command("RESTART", 30);
}

Response APIRouter::handle_shutdown(const Request& req) {
    (void)req;
    // Graceful OS shutdown — the guest agent sends kAEShutDown to Finder.
    // Same path as Special → Shut Down in the Finder menu.
    return bridge_command("SHUTDOWN", 30);
}

Response APIRouter::handle_status(const Request& req) {
    (void)req;

    std::ostringstream json;
    json << std::fixed << std::setprecision(2);
    json << "{";
    json << "\"emulator_connected\": true";

    bool finder_reached = false;

    if (ctx_->subprocess) {
        // Subprocess mode: read from IPC SHM (shared lock prevents a
        // concurrent stop()/restart from munmapping the page underneath us)
        std::shared_lock<std::shared_mutex> shm_lock(g_ipc_shm_mutex);
        bool running = ctx_->subprocess->is_running();
        const IPCBuffer* buf = ctx_->subprocess->ipc_client()->shm();

        json << ", \"emulator_running\": " << (running ? "true" : "false");
        json << ", \"cpu_state\": \"" << (running ? "running" : "stopped") << "\"";

        if (buf) {
            json << ", \"boot_phase\": \"" << buf->boot_phase << "\"";
            json << ", \"checkload_count\": " << IPC_ATOMIC_LOAD(buf->checkload_count);

            double elapsed = 0.0;
            uint64_t start_us = IPC_ATOMIC_LOAD(buf->boot_start_us);
            if (start_us > 0) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                uint64_t now_us = now.tv_sec * 1000000ULL + now.tv_nsec / 1000;
                elapsed = (now_us - start_us) / 1e6;
            }
            json << ", \"boot_elapsed\": " << elapsed;

            finder_reached = boot_progress_phase_reached_by_name(buf->boot_phase, "Finder");

            // Mac OS state (pre-serialized JSON from child process)
            if (buf->mac_state_json[0] == '{') {
                json << ", \"mac\": " << buf->mac_state_json;
            }
        } else {
            json << ", \"boot_phase\": \"pre-reset\"";
            json << ", \"checkload_count\": 0";
            json << ", \"boot_elapsed\": 0.0";
        }
    } else {
        // Legacy in-process mode
        bool cpu_running = ctx_->cpu_running ? ctx_->cpu_running->load(std::memory_order_acquire) : false;
        json << ", \"emulator_running\": " << (cpu_running ? "true" : "false");
        json << ", \"cpu_state\": \"" << (cpu_running ? "running" : "stopped") << "\"";
        json << ", \"boot_phase\": \"" << boot_progress_phase() << "\"";
        json << ", \"checkload_count\": " << boot_progress_checkloads();
        json << ", \"boot_elapsed\": " << boot_progress_elapsed();

        finder_reached = boot_progress_phase_reached("Finder");

        // Mac OS state (cached in-process)
        const char* mac_state = boot_progress_get_mac_state();
        if (mac_state && mac_state[0] == '{') {
            json << ", \"mac\": " << mac_state;
        }
    }

    json << ", \"bridge_agent_connected\": "
         << (bridge_agent_is_connected(finder_reached) ? "true" : "false");

    json << "}";
    return Response::json(json.str());
}

Response APIRouter::handle_emulator_start(const Request& req) {
    (void)req;

    if (ctx_->subprocess) {
        if (ctx_->subprocess->is_running()) {
            return Response::json("{\"success\": false, \"error\": \"Already running\"}");
        }
        if (!ctx_->config || ctx_->config->rom_path.empty()) {
            return Response::json(
                "{\"success\": false, "
                "\"error\": \"No ROM configured\", "
                "\"message\": \"Please configure a ROM path in the settings\"}");
        }
        fprintf(stderr, "[API] Starting subprocess\n");
        if (!ctx_->subprocess->start()) {
            return Response::json("{\"success\": false, \"error\": \"Failed to start subprocess\"}");
        }
        // Reset WebRTC peer state so all peers request fresh keyframes
        webrtc::reset_webrtc_peers();
        invalidate_frame_sessions();
        return Response::json("{\"success\": true, \"message\": \"Subprocess started\"}");
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

    if (ctx_->subprocess) {
        fprintf(stderr, "[API] Stopping subprocess\n");
        ctx_->subprocess->stop();
        invalidate_frame_sessions();
        return Response::json("{\"success\": true, \"message\": \"Subprocess stopped\"}");
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

    if (ctx_->subprocess) {
        fprintf(stderr, "[API] Restarting subprocess\n");
        ctx_->subprocess->reset();
        // Reset WebRTC peer state so all peers request fresh keyframes
        webrtc::reset_webrtc_peers();
        return Response::json("{\"success\": true, \"message\": \"Subprocess restarted\"}");
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

    // Only forward error/warn from browser; suppress info-level chatter
    const char* prefix = "[Browser]";
    if (level == "error") {
        fprintf(stderr, "\033[31m%s ERROR: %s%s%s\033[0m\n", prefix, msg.c_str(),
                data.empty() ? "" : " | ", data.c_str());
    } else if (level == "warn") {
        fprintf(stderr, "\033[33m%s WARN: %s%s%s\033[0m\n", prefix, msg.c_str(),
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

    fprintf(stderr, "Config: Codec changed from %d to %d via API\n", static_cast<int>(old_codec), static_cast<int>(new_codec));

    // Notify all clients
    if (new_codec != old_codec) {
        ctx_->notify_codec_change_fn(new_codec);
    }

    return Response::json("{\"ok\": true}");
}

Response APIRouter::handle_codecs_get(const Request& /*req*/) {
    std::string json = "{\"codecs\":[";
    json += "{\"id\":\"png\",\"name\":\"PNG\",\"available\":true}";
    json += ",{\"id\":\"h264\",\"name\":\"H.264\",\"available\":";
    json += codec_available(CodecType::H264) ? "true" : "false";
    json += "}";
    json += ",{\"id\":\"vp9\",\"name\":\"VP9\",\"available\":";
    json += codec_available(CodecType::VP9) ? "true" : "false";
    json += "}";
    json += ",{\"id\":\"webp\",\"name\":\"WebP\",\"available\":";
    json += codec_available(CodecType::WEBP) ? "true" : "false";
    json += "}";
    json += "]}";
    return Response::json(json);
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

    // Return 503 if CPU is not running (no live frames)
    if (ctx_->subprocess) {
        if (!ctx_->subprocess->is_running()) {
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
    static bool fpng_inited = []() { fpng::fpng_init(); return true; }();
    (void)fpng_inited;
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

/**
 * GET /api/frame — Single-frame endpoint for long-poll HTTP streaming.
 *
 * Returns one PNG frame with the same 45-byte metadata header used by
 * the WebSocket path. Client polls in a loop. Works through any proxy.
 *
 * Query params (in req.query):
 *   sid=X  — Session ID for dirty rect tracking. Omit on first request;
 *            server returns a new sid in X-Frame-Sid header.
 *
 * Response: application/octet-stream
 *   [45-byte header][PNG image data]
 *
 * Headers: X-Frame-Seq (sequence number), X-Frame-Sid (session ID)
 * Returns 204 if no new frame since last request for this session.
 */

// Per-client session state for dirty rect tracking
struct FrameSession {
    std::vector<uint32_t> prev_frame;
    uint64_t last_sequence = 0;
    std::chrono::steady_clock::time_point last_access;
};

// Helper to parse a query param from the query string
static std::string get_query_param(const std::string& query, const std::string& key) {
    std::string search = key + "=";
    size_t pos = query.find(search);
    if (pos == std::string::npos) return "";
    size_t start = pos + search.size();
    size_t end = query.find('&', start);
    if (end == std::string::npos) end = query.size();
    return query.substr(start, end - start);
}

// Dirty rect computation (same algorithm as video_encoder_thread / http_stream)
static bool frame_compute_dirty_rect(const uint32_t* curr, const uint32_t* prev,
                                      int width, int height,
                                      int& out_x, int& out_y, int& out_w, int& out_h) {
    int min_x = width, max_x = 0;
    int min_y = height, max_y = 0;
    bool found = false;

    for (int y = 0; y < height; y++) {
        const uint32_t* curr_row = curr + y * width;
        const uint32_t* prev_row = prev + y * width;
        for (int x = 0; x < width; x++) {
            if (curr_row[x] != prev_row[x]) {
                if (!found) { found = true; min_y = y; }
                max_y = y;
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
            }
        }
    }
    if (!found) return false;

    out_x = (min_x > 1) ? min_x - 1 : 0;
    out_y = (min_y > 1) ? min_y - 1 : 0;
    out_w = (max_x < width - 2) ? (max_x - out_x + 2) : (width - out_x);
    out_h = (max_y < height - 2) ? (max_y - out_y + 2) : (height - out_y);

    // If dirty rect > 75% of screen, use full frame
    if (out_w * out_h > (width * height * 3 / 4)) {
        out_x = 0; out_y = 0; out_w = width; out_h = height;
    }
    return true;
}

Response APIRouter::handle_frame(const Request& req) {
    if (!ctx_->video_output) {
        Response resp;
        resp.set_status(503, "Service Unavailable");
        resp.set_body("{\"error\": \"Video output not available\"}");
        resp.set_content_type("application/json");
        return resp;
    }

    // Session management (static — HTTP server is single-threaded)
    static std::unordered_map<std::string, FrameSession> sessions;
    static uint64_t next_sid = 1;
    static auto last_cleanup = std::chrono::steady_clock::now();

    // Clear all sessions on emulator stop/start (forces full frame on reconnect)
    if (g_frame_sessions_dirty) {
        sessions.clear();
        g_frame_sessions_dirty = false;
    }

    // Expire stale sessions every 30 seconds (idle > 10s)
    auto now_steady = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now_steady - last_cleanup).count() >= 30) {
        for (auto it = sessions.begin(); it != sessions.end(); ) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now_steady - it->second.last_access).count() > 10) {
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }
        last_cleanup = now_steady;
    }

    // Look up or create session
    std::string sid_str = get_query_param(req.query, "sid");
    FrameSession* session = nullptr;
    if (!sid_str.empty()) {
        auto it = sessions.find(sid_str);
        if (it != sessions.end()) {
            session = &it->second;
        }
    }
    if (!session) {
        sid_str = std::to_string(next_sid++);
        session = &sessions[sid_str];
    }
    session->last_access = now_steady;

    // Snapshot current frame
    static thread_local std::vector<uint32_t> pixels(1920 * 1080);
    int width = 0, height = 0;
    PixelFormat format;
    uint64_t sequence = 0;
    uint16_t cursor_x = 0, cursor_y = 0;
    uint8_t cursor_visible = 0;

    bool got = ctx_->video_output->snapshot_frame_ex(
        pixels.data(), &width, &height, &format,
        &sequence, &cursor_x, &cursor_y, &cursor_visible);

    if (!got) {
        Response resp;
        resp.set_status(204, "No Content");
        resp.add_header("X-Frame-Sid", sid_str);
        return resp;
    }

    // Skip if same sequence as last request from this session
    if (sequence == session->last_sequence) {
        Response resp;
        resp.set_status(204, "No Content");
        resp.add_header("X-Frame-Sid", sid_str);
        return resp;
    }
    session->last_sequence = sequence;

    // Compute dirty rect against this session's previous frame
    int dx = 0, dy = 0, dw = width, dh = height;
    bool have_prev = !session->prev_frame.empty() && (int)session->prev_frame.size() == width * height;

    if (have_prev) {
        bool changed = frame_compute_dirty_rect(
            pixels.data(), session->prev_frame.data(), width, height,
            dx, dy, dw, dh);
        if (!changed) {
            // Identical frame
            Response resp;
            resp.set_status(204, "No Content");
            resp.add_header("X-Frame-Sid", sid_str);
            return resp;
        }
    }

    // Save current frame as prev for next request
    if ((int)session->prev_frame.size() != width * height) {
        session->prev_frame.resize(width * height);
    }
    memcpy(session->prev_frame.data(), pixels.data(), width * height * 4);

    // Encode dirty rect as PNG
    static bool fpng_inited2 = []() { fpng::fpng_init(); return true; }();
    (void)fpng_inited2;

    // Extract dirty rect and convert to RGB
    std::vector<uint8_t> rgb(dw * dh * 3);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(pixels.data());
    for (int ry = 0; ry < dh; ry++) {
        for (int rx = 0; rx < dw; rx++) {
            int si = (dy + ry) * width + (dx + rx);
            int di = ry * dw + rx;
            if (format == PIXFMT_ARGB) {
                rgb[di * 3 + 0] = src[si * 4 + 1];
                rgb[di * 3 + 1] = src[si * 4 + 2];
                rgb[di * 3 + 2] = src[si * 4 + 3];
            } else {
                rgb[di * 3 + 0] = src[si * 4 + 2];
                rgb[di * 3 + 1] = src[si * 4 + 1];
                rgb[di * 3 + 2] = src[si * 4 + 0];
            }
        }
    }

    std::vector<uint8_t> png_data;
    if (!fpng::fpng_encode_image_to_memory(rgb.data(), dw, dh, 3, png_data)) {
        Response resp;
        resp.set_status(500, "Internal Server Error");
        resp.set_body("{\"error\": \"PNG encoding failed\"}");
        resp.set_content_type("application/json");
        return resp;
    }

    // Build response: [45-byte header][PNG data]
    const int HEADER_SIZE = 45;
    std::string body;
    body.resize(HEADER_SIZE + png_data.size());
    uint8_t* p = reinterpret_cast<uint8_t*>(&body[0]);

    auto now_ms = []() -> uint64_t {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
    };
    uint64_t t = now_ms();

    auto write_le32 = [](uint8_t* buf, uint32_t val) {
        buf[0] = val & 0xFF; buf[1] = (val >> 8) & 0xFF;
        buf[2] = (val >> 16) & 0xFF; buf[3] = (val >> 24) & 0xFF;
    };
    auto write_le64 = [&write_le32](uint8_t* buf, uint64_t val) {
        write_le32(buf, (uint32_t)val);
        write_le32(buf + 4, (uint32_t)(val >> 32));
    };
    auto write_le16 = [](uint8_t* buf, uint16_t val) {
        buf[0] = val & 0xFF; buf[1] = (val >> 8) & 0xFF;
    };

    write_le64(p + 0, t);                     // t1_frame_ready
    write_le32(p + 8, (uint32_t)dx);          // dirty_x
    write_le32(p + 12, (uint32_t)dy);         // dirty_y
    write_le32(p + 16, (uint32_t)dw);         // dirty_width
    write_le32(p + 20, (uint32_t)dh);         // dirty_height
    write_le32(p + 24, (uint32_t)width);      // frame_width
    write_le32(p + 28, (uint32_t)height);     // frame_height
    write_le64(p + 32, t);                    // t4_send_time
    write_le16(p + 40, cursor_x);             // cursor_x
    write_le16(p + 42, cursor_y);             // cursor_y
    p[44] = cursor_visible;                   // cursor_visible

    memcpy(p + HEADER_SIZE, png_data.data(), png_data.size());

    Response resp;
    resp.set_status(200, "OK");
    resp.set_content_type("application/octet-stream");
    resp.add_header("X-Frame-Seq", std::to_string(sequence));
    resp.add_header("X-Frame-Sid", sid_str);
    resp.add_header("Access-Control-Allow-Origin", "*");
    resp.add_header("Access-Control-Expose-Headers", "X-Frame-Seq, X-Frame-Sid");
    resp.set_body(body);
    return resp;
}


Response APIRouter::handle_mouse(const Request& req) {
    (void)req;

    // Check if CPU is running
    bool running = false;
    if (ctx_->subprocess) {
        running = ctx_->subprocess->is_running();
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

    if (ctx_->subprocess) {
        std::shared_lock<std::shared_mutex> shm_lock(g_ipc_shm_mutex);
        const IPCBuffer* buf = ctx_->subprocess->ipc_client()->shm();
        if (buf) {
            json << "{"
                 << "\"x\": " << IPC_ATOMIC_LOAD(buf->shm_cursor_x)
                 << ", \"y\": " << IPC_ATOMIC_LOAD(buf->shm_cursor_y)
                 << ", \"raw_x\": " << IPC_ATOMIC_LOAD(buf->shm_raw_x)
                 << ", \"raw_y\": " << IPC_ATOMIC_LOAD(buf->shm_raw_y)
                 << ", \"mtemp_x\": " << IPC_ATOMIC_LOAD(buf->shm_mtemp_x)
                 << ", \"mtemp_y\": " << IPC_ATOMIC_LOAD(buf->shm_mtemp_y)
                 << ", \"crsr_new\": " << IPC_ATOMIC_LOAD(buf->shm_crsr_new)
                 << ", \"crsr_couple\": " << IPC_ATOMIC_LOAD(buf->shm_crsr_couple)
                 << ", \"crsr_busy\": " << IPC_ATOMIC_LOAD(buf->shm_crsr_busy)
                 << "}";
        } else {
            json << "{\"x\": 0, \"y\": 0}";
        }
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
Response APIRouter::handle_mouse_move(const Request& req) {
    nlohmann::json j;
    try {
        j = json_utils::parse(req.body);
    } catch (const std::exception& e) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"invalid JSON\"}");
        r.set_content_type("application/json");
        return r;
    }

    // Track button state so move events carry correct button mask over IPC
    static uint8_t api_buttons = 0;

    // Mouse button event: {"button": 0, "down": true}
    if (j.contains("button") && j.contains("down")) {
        int button = json_utils::get_int(j, "button");
        bool down = j["down"].get<bool>();

        if (ctx_->subprocess && ctx_->subprocess->ipc_client()->is_connected()) {
            uint8_t mask = (1 << button);
            if (down) api_buttons |= mask;
            else      api_buttons &= ~mask;
            ctx_->subprocess->ipc_client()->send_mouse(0, 0, api_buttons, false);
        } else {
            if (down) ADBMouseDown(button);
            else ADBMouseUp(button);
        }

        return Response::json("{\"success\": true, \"button\": " + std::to_string(button) +
                             ", \"down\": " + (down ? "true" : "false") + "}");
    }

    // Check if this is a relative move (has "dx" field)
    if (j.contains("dx")) {
        int dx = json_utils::get_int(j, "dx");
        if (!j.contains("dy")) {
            Response r; r.set_status(400); r.set_body("{\"error\": \"missing 'dy' field\"}"); r.set_content_type("application/json"); return r;
        }
        int dy = json_utils::get_int(j, "dy");

        if (ctx_->subprocess && ctx_->subprocess->ipc_client()->is_connected()) {
            ctx_->subprocess->ipc_client()->send_mouse(dx, dy, api_buttons, false);
        } else {
            ADBSetRelMouseMode(true);
            ADBMouseMoved(dx, dy);
        }

        return Response::json("{\"success\": true, \"dx\": " + std::to_string(dx) + ", \"dy\": " + std::to_string(dy) + ", \"mode\": \"relative\"}");
    }

    // Absolute move
    if (!j.contains("x")) {
        Response r; r.set_status(400); r.set_body("{\"error\": \"missing 'x' (or 'dx' for relative)\"}"); r.set_content_type("application/json"); return r;
    }
    int x = json_utils::get_int(j, "x");
    if (!j.contains("y")) {
        Response r; r.set_status(400); r.set_body("{\"error\": \"missing 'y' field\"}"); r.set_content_type("application/json"); return r;
    }
    int y = json_utils::get_int(j, "y");

    if (ctx_->subprocess && ctx_->subprocess->ipc_client()->is_connected()) {
        ctx_->subprocess->ipc_client()->send_mouse(x, y, api_buttons, true);
    } else {
        ADBSetRelMouseMode(false);
        ADBMouseMoved(x, y);
    }

    return Response::json("{\"success\": true, \"x\": " + std::to_string(x) + ", \"y\": " + std::to_string(y) + ", \"mode\": \"absolute\"}");
}

// POST /api/keypress - send a key event to the emulator
Response APIRouter::handle_keypress(const Request& req) {
    int keycode = -1;

    nlohmann::json j;
    try {
        j = json_utils::parse(req.body);
    } catch (const std::exception&) {
        Response resp; resp.set_status(400);
        resp.set_body("{\"error\": \"invalid JSON\"}");
        resp.set_content_type("application/json");
        return resp;
    }

    if (!j.contains("key")) {
        Response resp; resp.set_status(400);
        resp.set_body("{\"error\": \"missing 'key' field\"}");
        resp.set_content_type("application/json");
        return resp;
    }

    if (j["key"].is_string()) {
        // Small set of friendly names for shell/CI callers. The web client
        // does its own KeyboardEvent.code → Mac mapping in JS and posts
        // numeric Mac keycodes via the WS path, so this list only needs
        // to cover the few keys tests poke (return/escape/arrows/etc.).
        // Mac ADB scancodes — classic, not modern HIToolbox.
        std::string name = j["key"].get<std::string>();
        if      (name == "return" || name == "enter")     keycode = 0x24;
        else if (name == "escape" || name == "esc")       keycode = 0x35;
        else if (name == "space")                         keycode = 0x31;
        else if (name == "tab")                           keycode = 0x30;
        else if (name == "delete" || name == "backspace") keycode = 0x33;
        else if (name == "left")                          keycode = 0x3B;
        else if (name == "right")                         keycode = 0x3C;
        else if (name == "down")                          keycode = 0x3D;
        else if (name == "up")                            keycode = 0x3E;
        else {
            Response r2; r2.set_status(400);
            r2.set_body("{\"error\": \"unknown key name: " + name + "\"}");
            r2.set_content_type("application/json");
            return r2;
        }
    } else if (j["key"].is_number()) {
        // Numeric `key` is a Mac ADB virtual keycode (0x00–0x7F).
        keycode = j["key"].get<int>();
    } else {
        Response resp; resp.set_status(400);
        resp.set_body("{\"error\": \"'key' must be a string or number\"}");
        resp.set_content_type("application/json");
        return resp;
    }

    if (keycode < 0 || keycode > 127) {
        Response r3; r3.set_status(400); r3.set_body("{\"error\": \"invalid keycode\"}"); r3.add_header("Content-Type", "application/json"); return r3;
    }

    // If "down" field is present, send a discrete key down or key up event.
    // Otherwise, send a complete keypress (down + 50ms + up) for backward compatibility.
    if (j.contains("down")) {
        bool down = j["down"].get<bool>();
        if (ctx_->subprocess && ctx_->subprocess->ipc_client()->is_connected()) {
            ctx_->subprocess->ipc_client()->send_key(keycode, down);
        } else {
            if (down) ::ADBKeyDown(keycode);
            else ::ADBKeyUp(keycode);
        }
        return Response::json("{\"success\": true, \"keycode\": " + std::to_string(keycode) +
                             ", \"down\": " + (down ? "true" : "false") + "}");
    }

    // Legacy: full keypress (down + delay + up)
    if (ctx_->subprocess && ctx_->subprocess->ipc_client()->is_connected()) {
        ctx_->subprocess->ipc_client()->send_key(keycode, true);
        usleep(50000);  // 50ms press
        ctx_->subprocess->ipc_client()->send_key(keycode, false);
    } else {
        ::ADBKeyDown(keycode);
        usleep(50000);  // 50ms press
        ::ADBKeyUp(keycode);
    }

    return Response::json("{\"success\": true, \"keycode\": " + std::to_string(keycode) + "}");
}

// POST /api/invoke-debug - invoke debugger (Programmer's Key)
Response APIRouter::handle_invoke_debug(const Request& req) {
    (void)req;
    if (ctx_->subprocess && ctx_->subprocess->ipc_client()->is_connected()) {
        ctx_->subprocess->ipc_client()->send_command(IPC_CMD_INVOKE_DEBUG);
    } else {
        ::InvokeDebugger();
    }
    return Response::json("{\"success\": true, \"message\": \"debugger invoked\"}");
}

// ── Command Bridge Endpoints ──

Response APIRouter::handle_app(const Request& req) {
    (void)req;

    if (ctx_->subprocess) {
        if (!ctx_->subprocess->is_running()) {
            return Response::json("{\"app\": \"\", \"error\": \"emulator not running\"}");
        }
        std::shared_lock<std::shared_mutex> shm_lock(g_ipc_shm_mutex);
        const IPCBuffer* buf = ctx_->subprocess->ipc_client()->shm();
        std::string app = buf ? buf->cur_app_name : "";
        return Response::json("{\"app\": \"" + storage::json_escape(app) + "\"}");
    }

    auto result = command_bridge_read(CmdType::GET_APP_NAME);
    return Response::json("{\"app\": \"" + storage::json_escape(result.data) + "\"}");
}

Response APIRouter::handle_windows(const Request& req) {
    (void)req;
    if (ctx_->subprocess) {
        // Window list requires reading Mac RAM — not available over IPC yet
        return Response::json("{\"windows\": [], \"error\": \"not available in subprocess mode\"}");
    }
    auto result = command_bridge_read(CmdType::GET_WINDOW_LIST);
    return Response::json("{\"windows\": " + result.data + "}");
}

Response APIRouter::handle_launch(const Request& req) {
    nlohmann::json j;
    try {
        j = json_utils::parse(req.body);
    } catch (const std::exception&) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"invalid JSON\"}");
        r.set_content_type("application/json");
        return r;
    }

    if (!j.contains("path") || !j["path"].is_string()) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"missing 'path' field\"}");
        r.set_content_type("application/json");
        return r;
    }
    std::string path = j["path"].get<std::string>();

    // Write command file to bridge dir — the guest agent reads it via ExtFS
    auto& cfg = config::EmulatorConfig::instance();
    if (!cfg.bridge_enabled || cfg.bridge_dir.empty()) {
        return Response::json("{\"success\": false, \"error\": \"bridge not enabled (use --bridge)\"}");
    }

    bool open_doc = j.contains("open") && j["open"].is_boolean() && j["open"].get<bool>();

    bridge_remove_file(cfg.bridge_dir, "_bridge_result");
    bridge_write_file(cfg.bridge_dir, "_bridge_cmd", (open_doc ? "OPEN " : "LAUNCH ") + path);

    for (int i = 0; i < 100; i++) {  // 10 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::string result;
        if (bridge_read_file(cfg.bridge_dir, "_bridge_result", result)) {
            bridge_remove_file(cfg.bridge_dir, "_bridge_result");
            int mac_err = std::atoi(result.c_str());
            if (mac_err != 0) {
                std::ostringstream json;
                json << "{\"success\": false, \"error_code\": " << mac_err
                     << ", \"message\": \"launch failed (Mac OS error " << mac_err << ")\"}";
                return Response::json(json.str());
            }
            return Response::json("{\"success\": true, \"error_code\": 0, \"message\": \"launched\"}");
        }

        if (!bridge_has_file(cfg.bridge_dir, "_bridge_cmd")) {
            return Response::json("{\"success\": true, \"error_code\": 0, \"message\": \"launched\"}");
        }
    }

    bridge_remove_file(cfg.bridge_dir, "_bridge_cmd");
    return Response::json("{\"success\": false, \"error\": \"timeout waiting for bridge agent\"}");
}

// POST /api/script — generic 'misc'/'dosc' ("do script") to any classic-Mac
// scripting host. Body:
//   { "creator": "<4-char OSType>", "script": "<utf-8 source>" }
// Or, for non-ASCII bodies that need exact MacRoman bytes:
//   { "creator": "<4-char OSType>", "script_b64": "<base64 of mac-roman bytes>" }
//
// Examples:
//   { "creator": "McPL", "script": "print 'hi from perl';\n" }      → MacPerl
//   { "creator": "LAND", "script": "msg(\"hi\")" }                  → Frontier UserLand
//   { "creator": "MPS ", "script": "Echo hi" }                      → MPW Shell
//   { "creator": "ToyS", "script": "say \"hi\"" }                   → AppleScript editor
//
// The host is responsible for any language-specific quirks (e.g. MacPerl's
// \r-vs-\n eval bug). BridgeAgent just delivers the source verbatim.
Response APIRouter::handle_script(const Request& req) {
    nlohmann::json j;
    try {
        j = json_utils::parse(req.body);
    } catch (const std::exception&) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"invalid JSON\"}");
        r.set_content_type("application/json");
        return r;
    }

    if (!j.contains("creator") || !j["creator"].is_string()) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"missing 'creator' field (4-char OSType)\"}");
        r.set_content_type("application/json");
        return r;
    }
    std::string creator = j["creator"].get<std::string>();
    if (creator.size() != 4) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"'creator' must be exactly 4 characters\"}");
        r.set_content_type("application/json");
        return r;
    }

    // Resolve script body — either inline UTF-8 string or base64 bytes.
    std::string script;
    if (j.contains("script") && j["script"].is_string()) {
        script = j["script"].get<std::string>();
    } else if (j.contains("script_b64") && j["script_b64"].is_string()) {
        const std::string& b64 = j["script_b64"].get_ref<const std::string&>();
        static const int8_t b64_tab[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
            52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
            41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        };
        int buf_val = 0, buf_bits = 0;
        script.reserve(b64.size() * 3 / 4);
        for (char c : b64) {
            if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
            int v = b64_tab[(uint8_t)c];
            if (v < 0) {
                Response r; r.set_status(400);
                r.set_body("{\"error\": \"invalid base64 in 'script_b64'\"}");
                r.set_content_type("application/json");
                return r;
            }
            buf_val = (buf_val << 6) | v;
            buf_bits += 6;
            if (buf_bits >= 8) {
                buf_bits -= 8;
                script.push_back((char)((buf_val >> buf_bits) & 0xff));
            }
        }
    } else {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"missing 'script' or 'script_b64' field\"}");
        r.set_content_type("application/json");
        return r;
    }

    // 1 MB cap matches the BridgeAgent's NewPtr budget plus the AE Manager's
    // practical limit on dosc payloads.
    if (script.size() > 1024 * 1024) {
        return Response::json(
            "{\"success\": false, \"error\": \"script too large (>1MB)\"}");
    }

    auto& cfg = config::EmulatorConfig::instance();
    if (!cfg.bridge_enabled || cfg.bridge_dir.empty()) {
        return Response::json(
            "{\"success\": false, \"error\": \"bridge not enabled (use --bridge)\"}");
    }

    // Body in BR_FILE_SCRIPT, command in BR_FILE_CMD. Order matters — the agent
    // can fire as soon as it sees _bridge_cmd, so the script body must be
    // visible first.
    bridge_write_file(cfg.bridge_dir, "_bridge_script", script);
    return bridge_command(("SCRIPT " + creator).c_str(), 100);
}

Response APIRouter::handle_quit(const Request& req) {
    (void)req;
    return bridge_command("QUIT", 30);
}

// POST /api/clipboard — write raw MacRoman bytes (base64-encoded in body)
// into the guest's scrap via BridgeAgent. Browser does the UTF-8↔MacRoman
// translation; the host just pipes bytes through.
Response APIRouter::handle_clipboard_set(const Request& req) {
    nlohmann::json j;
    try {
        j = json_utils::parse(req.body);
    } catch (const std::exception&) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"invalid JSON\"}");
        r.set_content_type("application/json");
        return r;
    }
    if (!j.contains("text_b64") || !j["text_b64"].is_string()) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"missing 'text_b64' field\"}");
        r.set_content_type("application/json");
        return r;
    }

    // base64 decode
    const std::string& b64 = j["text_b64"].get_ref<const std::string&>();
    static const int8_t b64_tab[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    };
    std::string bytes;
    bytes.reserve(b64.size() * 3 / 4);
    int buf_val = 0, buf_bits = 0;
    for (char c : b64) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = b64_tab[(uint8_t)c];
        if (v < 0) {
            Response r; r.set_status(400);
            r.set_body("{\"error\": \"invalid base64\"}");
            r.set_content_type("application/json");
            return r;
        }
        buf_val = (buf_val << 6) | v;
        buf_bits += 6;
        if (buf_bits >= 8) {
            buf_bits -= 8;
            bytes.push_back((char)((buf_val >> buf_bits) & 0xff));
        }
    }
    if (bytes.size() > 65536) {
        return Response::json(
            "{\"success\": false, \"error\": \"clipboard too large (>64KB)\"}");
    }

    auto& cfg = config::EmulatorConfig::instance();
    if (!cfg.bridge_enabled || cfg.bridge_dir.empty()) {
        return Response::json(
            "{\"success\": false, \"error\": \"bridge not enabled (use --bridge)\"}");
    }

    bridge_write_file(cfg.bridge_dir, "_bridge_clipboard", bytes);
    return bridge_command("SET_CLIPBOARD", 30);
}

Response APIRouter::handle_wait(const Request& req) {
    nlohmann::json j;
    try {
        j = json_utils::parse(req.body);
    } catch (const std::exception&) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"invalid JSON\"}");
        r.set_content_type("application/json");
        return r;
    }

    if (!j.contains("condition") || !j["condition"].is_string()) {
        Response r; r.set_status(400);
        r.set_body("{\"error\": \"missing 'condition' field\"}");
        r.set_content_type("application/json");
        return r;
    }
    std::string condition = j["condition"].get<std::string>();

    // Parse timeout (default 30s)
    int timeout_s = 30;
    if (j.contains("timeout") && j["timeout"].is_number()) {
        timeout_s = j["timeout"].get<int>();
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
            if (ctx_->subprocess) {
                std::shared_lock<std::shared_mutex> shm_lock(g_ipc_shm_mutex);
                const IPCBuffer* buf = ctx_->subprocess->ipc_client()->shm();
                app = buf ? buf->cur_app_name : "";
            } else {
                auto result = command_bridge_read(CmdType::GET_APP_NAME);
                app = result.data;
            }
            if (app == cond_value) {
                return Response::json("{\"ok\": true, \"app\": \"" + storage::json_escape(app) + "\"}");
            }
        } else if (cond_type == "boot") {
            std::string phase;
            bool reached;
            if (ctx_->subprocess) {
                std::shared_lock<std::shared_mutex> shm_lock(g_ipc_shm_mutex);
                const IPCBuffer* buf = ctx_->subprocess->ipc_client()->shm();
                phase = buf ? buf->boot_phase : "pre-reset";
                reached = boot_progress_phase_reached_by_name(phase.c_str(), cond_value.c_str());
            } else {
                phase = boot_progress_phase();
                reached = boot_progress_phase_reached(cond_value.c_str());
            }
            if (reached) {
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
