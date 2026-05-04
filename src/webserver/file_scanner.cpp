/*
 * File Scanner Module
 *
 * Scans storage directories for ROM files, disk images, and CD-ROMs
 * Provides file metadata including size and ROM checksums
 */

#include "file_scanner.h"
#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <sstream>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>

namespace storage {

// Helper: Check if filename has one of the given extensions
static bool has_extension(const std::string& filename, const std::vector<std::string>& extensions) {
    size_t dot = filename.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = filename.substr(dot);
    for (auto& c : ext) c = tolower(c);
    for (const auto& e : extensions) {
        if (ext == e) return true;
    }
    return false;
}

// Helper: Read first 4 bytes of ROM file as checksum (big-endian)
static uint32_t read_rom_checksum(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return 0;
    uint8_t buf[4];
    if (fread(buf, 1, 4, f) != 4) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

// Helper: Calculate MD5 hash of entire file
static std::string calculate_md5(const std::string& path) {
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) return "";

    QCryptographicHash hash(QCryptographicHash::Md5);
    if (!hash.addData(&file)) return "";

    return hash.result().toHex().toStdString();
}

// Recursive directory scanning
static void scan_directory_recursive(const std::string& base_dir, const std::string& relative_path,
                                     const std::vector<std::string>& extensions, bool read_checksums,
                                     std::vector<FileInfo>& files) {
    std::string current_dir = relative_path.empty() ? base_dir : base_dir + "/" + relative_path;

    QDir dir(QString::fromStdString(current_dir));
    if (!dir.exists()) {
        fprintf(stderr, "[Storage] Failed to open directory: %s (errno=%d)\n", current_dir.c_str(), errno);
        return;
    }

    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo& info : entries) {
        const std::string name = info.fileName().toStdString();
        if (name.empty() || name[0] == '.') continue;

        const std::string full_path = current_dir + "/" + name;
        const std::string rel_name = relative_path.empty() ? name : relative_path + "/" + name;

        if (info.isDir()) {
            scan_directory_recursive(base_dir, rel_name, extensions, read_checksums, files);
        } else if (info.isFile()) {
            if (has_extension(name, extensions)) {
                FileInfo fi;
                fi.name = rel_name;
                fi.size = info.size();
                fi.checksum = 0;
                fi.has_checksum = false;
                fi.md5 = "";

                if (read_checksums) {
                    fi.checksum = read_rom_checksum(full_path);
                    fi.has_checksum = true;
                    fi.md5 = calculate_md5(full_path);
                }

                files.push_back(fi);
            }
        }
    }
}

// Public: Scan directory for files with given extensions
std::vector<FileInfo> scan_directory(const std::string& directory,
                                     const std::vector<std::string>& extensions,
                                     bool read_checksums, bool recursive) {
    std::vector<FileInfo> files;

    if (recursive) {
        scan_directory_recursive(directory, "", extensions, read_checksums, files);
    } else {
        QDir dir(QString::fromStdString(directory));
        if (!dir.exists()) return files;

        const QFileInfoList entries = dir.entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot);

        for (const QFileInfo& info : entries) {
            const std::string name = info.fileName().toStdString();
            if (name.empty() || name[0] == '.') continue;

            if (has_extension(name, extensions)) {
                FileInfo fi;
                fi.name = name;
                fi.size = info.size();
                fi.checksum = 0;
                fi.has_checksum = false;
                fi.md5 = "";

                const std::string full_path = directory + "/" + name;
                if (read_checksums) {
                    fi.checksum = read_rom_checksum(full_path);
                    fi.has_checksum = true;
                    fi.md5 = calculate_md5(full_path);
                }

                files.push_back(fi);
            }
        }
    }

    std::sort(files.begin(), files.end(), [](const FileInfo& a, const FileInfo& b) {
        return a.name < b.name;
    });
    return files;
}

// Public: Scan storage directories and build JSON inventory
std::string get_storage_json(const std::string& roms_path, const std::string& images_path) {
    fprintf(stderr, "[Storage] Scanning ROMs directory: %s\n", roms_path.c_str());
    auto roms = scan_directory(roms_path, {".rom"}, true, true);
    fprintf(stderr, "[Storage] Found %zu ROM(s)\n", roms.size());

    fprintf(stderr, "[Storage] Scanning disk images directory: %s\n", images_path.c_str());
    auto disks = scan_directory(images_path, {".img", ".dsk", ".hfv", ".toast"});
    fprintf(stderr, "[Storage] Found %zu disk image(s)\n", disks.size());

    auto cdroms = scan_directory(images_path, {".iso"});
    fprintf(stderr, "[Storage] Found %zu CD-ROM(s)\n", cdroms.size());

    // Note: Client handles deduplication of known ROMs by MD5
    std::ostringstream json;
    json << "{\n";
    json << "  \"romsPath\": \"" << json_escape(roms_path) << "\",\n";
    json << "  \"imagesPath\": \"" << json_escape(images_path) << "\",\n";
    json << "  \"roms\": [";
    for (size_t i = 0; i < roms.size(); i++) {
        if (i > 0) json << ", ";
        json << "{\"name\": \"" << json_escape(roms[i].name) << "\", \"size\": " << roms[i].size;
        char checksum_hex[16];
        snprintf(checksum_hex, sizeof(checksum_hex), "%08x", roms[i].checksum);
        json << ", \"checksum\": \"" << checksum_hex << "\"";
        json << ", \"md5\": \"" << roms[i].md5 << "\"}";
    }
    json << "],\n";
    json << "  \"disks\": [";
    for (size_t i = 0; i < disks.size(); i++) {
        if (i > 0) json << ", ";
        json << "{\"name\": \"" << json_escape(disks[i].name) << "\", \"size\": " << disks[i].size << "}";
    }
    json << "],\n";
    json << "  \"cdroms\": [";
    for (size_t i = 0; i < cdroms.size(); i++) {
        if (i > 0) json << ", ";
        json << "{\"name\": \"" << json_escape(cdroms[i].name) << "\", \"size\": " << cdroms[i].size << "}";
    }
    json << "]\n";
    json << "}";

    return json.str();
}

// Helper: Escape string for JSON. Handles all control bytes (< 0x20) so
// raw Mac strings (filenames, window titles, app names) can be safely
// interpolated into JSON responses without producing invalid output.
std::string json_escape(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    result += buf;
                } else {
                    result += static_cast<char>(c);
                }
                break;
        }
    }
    return result;
}

} // namespace storage
