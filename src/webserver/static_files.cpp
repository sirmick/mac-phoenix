/*
 * Static File Handler Module
 *
 * Implementation of static file serving
 */

#include "static_files.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

namespace http {

StaticFileHandler::StaticFileHandler(const std::string& root_dir, const config::EmulatorConfig* config)
    : root_dir_(root_dir), config_(config)
{}

bool StaticFileHandler::handles(const std::string& path) const {
    // Handle root paths and known static files
    return path == "/" ||
           path == "/index.html" ||
           path == "/client.js" ||
           path == "/styles.css" ||
           path == "/Apple.svg" ||
           path == "/Motorola.svg" ||
           path == "/PowerPC.svg";
}

Response StaticFileHandler::serve(const std::string& path) {
    std::string file_path = map_path_to_file(path);
    if (file_path.empty()) {
        return Response::not_found();
    }

    // Read file
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return Response::not_found();
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    // Template injection for index.html: embed config JSON to eliminate race conditions
    if (path == "/" || path == "/index.html") {
        content = inject_config_template(content);
    }

    // Build response
    Response resp;
    resp.set_content_type(get_content_type(path));

    // Don't cache — files change during development and index.html has dynamic config
    resp.add_header("Cache-Control", "no-cache, no-store, must-revalidate");
    resp.add_header("Pragma", "no-cache");
    resp.add_header("Expires", "0");

    resp.set_body(content);
    return resp;
}

std::string StaticFileHandler::map_path_to_file(const std::string& path) const {
    if (path == "/" || path == "/index.html") {
        return root_dir_ + "/index.html";
    } else if (path == "/client.js") {
        return root_dir_ + "/client.js";
    } else if (path == "/styles.css") {
        return root_dir_ + "/styles.css";
    } else if (path == "/Apple.svg") {
        return root_dir_ + "/Apple.svg";
    } else if (path == "/Motorola.svg") {
        return root_dir_ + "/Motorola.svg";
    } else if (path == "/PowerPC.svg") {
        return root_dir_ + "/PowerPC.svg";
    }
    return "";
}

std::string StaticFileHandler::get_content_type(const std::string& path) const {
    if (path.find(".html") != std::string::npos || path == "/") {
        return "text/html";
    } else if (path.find(".js") != std::string::npos) {
        return "application/javascript";
    } else if (path.find(".css") != std::string::npos) {
        return "text/css";
    } else if (path.find(".svg") != std::string::npos) {
        return "image/svg+xml";
    }
    return "text/plain";
}

std::string StaticFileHandler::inject_config_template(const std::string& html) const {
    if (!config_) return html;

    // Build JSON from EmulatorConfig (client expects these keys)
    nlohmann::json j;
    j["codec"] = config_->codec;
    j["mousemode"] = config_->mousemode;
    j["screen"] = config_->screen_string();
    j["debug_connection"] = config_->debug_connection;
    j["debug_mode_switch"] = config_->debug_mode_switch;
    j["debug_perf"] = config_->debug_perf;

    // Client compat keys
    j["webcodec"] = config_->codec;
    j["resolution"] = config_->screen_string();

    std::string config_json = j.dump(2);

    // Replace {{CONFIG_JSON}} placeholder
    std::string result = html;
    const std::string placeholder = "{{CONFIG_JSON}}";
    size_t pos = result.find(placeholder);

    if (pos != std::string::npos) {
        result.replace(pos, placeholder.length(), config_json);
        fprintf(stderr, "[HTTP] Injected config into index.html (codec=%s, mousemode=%s)\n",
                config_->codec.c_str(), config_->mousemode.c_str());
    } else {
        fprintf(stderr, "[HTTP] Warning: {{CONFIG_JSON}} placeholder not found in index.html\n");
    }

    return result;
}

} // namespace http
