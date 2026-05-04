/*
 * Static File Handler Module
 *
 * Implementation of static file serving
 */

#include "static_files.h"
#include "../drivers/video/encoders/codec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
           path == "/codec-fallback.js" ||
           path == "/upscaler.js" ||
           path == "/styles.css" ||
           path == "/favicon.png" ||
           path == "/Apple.svg" ||
           path == "/Motorola.svg" ||
           path == "/PowerPC.svg" ||
           path == "/rom_database.json";
}

Response StaticFileHandler::serve(const std::string& path) {
    std::string file_path = map_path_to_file(path);
    if (file_path.empty()) {
        return Response::not_found();
    }

    // Read file in binary mode so PNG/SVG bytes pass through verbatim.
    std::ifstream file(file_path, std::ios::binary);
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
    } else if (path == "/codec-fallback.js") {
        return root_dir_ + "/codec-fallback.js";
    } else if (path == "/upscaler.js") {
        return root_dir_ + "/upscaler.js";
    } else if (path == "/styles.css") {
        return root_dir_ + "/styles.css";
    } else if (path == "/favicon.png") {
        return root_dir_ + "/favicon.png";
    } else if (path == "/Apple.svg") {
        return root_dir_ + "/Apple.svg";
    } else if (path == "/Motorola.svg") {
        return root_dir_ + "/Motorola.svg";
    } else if (path == "/PowerPC.svg") {
        return root_dir_ + "/PowerPC.svg";
    } else if (path == "/rom_database.json") {
        return root_dir_ + "/rom_database.json";
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
    } else if (path.find(".png") != std::string::npos) {
        return "image/png";
    } else if (path.find(".json") != std::string::npos) {
        return "application/json";
    }
    return "text/plain";
}

std::string StaticFileHandler::inject_config_template(const std::string& html) const {
    if (!config_) return html;

    // Build JSON from EmulatorConfig (client expects these keys)
    QJsonObject j;
    j["codec"] = QString::fromStdString(config_->codec);
    j["mousemode"] = QString::fromStdString(config_->mousemode);
    j["screen"] = QString::fromStdString(config_->screen_string());
    // Signaling rides the same port as HTTP via /ws upgrade — no separate port.
    j["debug_connection"] = config_->debug_connection;
    j["debug_mode_switch"] = config_->debug_mode_switch;
    j["debug_perf"] = config_->debug_perf;

    // Client compat keys
    j["webcodec"] = QString::fromStdString(config_->codec);
    j["resolution"] = QString::fromStdString(config_->screen_string());

    // Codec availability (so client doesn't need a separate /api/codecs fetch)
    QJsonArray codecs;
    codecs.append(QJsonObject{{"id", "png"},  {"name", "PNG"},   {"available", true}});
    codecs.append(QJsonObject{{"id", "h264"}, {"name", "H.264"}, {"available", codec_available(CodecType::H264)}});
    codecs.append(QJsonObject{{"id", "vp9"},  {"name", "VP9"},   {"available", codec_available(CodecType::VP9)}});
    codecs.append(QJsonObject{{"id", "webp"}, {"name", "WebP"},  {"available", codec_available(CodecType::WEBP)}});
    j["codecs"] = codecs;

    std::string config_json = QJsonDocument(j).toJson(QJsonDocument::Indented).toStdString();

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

    // Replace {{SELECTED_*}} placeholders for codec and mouse dropdowns
    auto replace_all = [&result](const std::string& from, const std::string& to) {
        size_t p = 0;
        while ((p = result.find(from, p)) != std::string::npos) {
            result.replace(p, from.length(), to);
            p += to.length();
        }
    };

    replace_all("{{SELECTED_PNG}}",  config_->codec == "png"  ? "selected" : "");
    replace_all("{{SELECTED_H264}}", (config_->codec == "h264" && codec_available(CodecType::H264)) ? "selected" : "");
    replace_all("{{SELECTED_VP9}}",  (config_->codec == "vp9"  && codec_available(CodecType::VP9))  ? "selected" : "");
    replace_all("{{SELECTED_WEBP}}", (config_->codec == "webp" && codec_available(CodecType::WEBP)) ? "selected" : "");
    replace_all("{{SELECTED_RELATIVE}}", config_->mousemode == "relative" ? "selected" : "");
    replace_all("{{SELECTED_ABSOLUTE}}", config_->mousemode == "absolute" ? "selected" : "");

    return result;
}

} // namespace http
