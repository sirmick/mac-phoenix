/*
 * File Scanner Module
 *
 * Scans storage directories for ROM files, disk images, and CD-ROMs
 * Provides file metadata including size and ROM checksums
 */

#include "file_scanner.h"
#include "../core/mac_roman.h"
#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <sstream>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t* p) { return (uint32_t)be16(p) << 16 | be16(p + 2); }

// What a disk image's volume header says: its name, and whether it has a
// System to boot (a blessed System Folder on HFS/HFS+; boot blocks on MFS,
// which has no folders). Raw volumes, Apple partition maps (the first HFS
// partition) and DiskCopy 4.2 images.
struct VolumeInfo {
    bool system = false;
    std::string name;   // UTF-8; empty on HFS+ (the name is in the catalog)
};

static VolumeInfo probe_volume(const std::string& path) {
    VolumeInfo v;
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly)) return v;
    auto read_at = [&](qint64 off, uint8_t* buf, qint64 n) {
        return f.seek(off) && f.read(reinterpret_cast<char*>(buf), n) == n;
    };
    uint8_t blk[512];
    qint64 base = 0;
    if (!read_at(0, blk, sizeof(blk))) return v;
    if (be16(blk) == 0x4552) {                          // 'ER': partition map
        for (uint32_t i = 1; i < 64; i++) {
            if (!read_at((qint64)i * 512, blk, sizeof(blk)) || be16(blk) != 0x504D) return v;
            if (!memcmp(blk + 48, "Apple_HFS", 10)) { base = (qint64)be32(blk + 8) * 512; break; }
            if (i >= be32(blk + 4)) return v;
        }
        if (!base) return v;
    } else if (blk[0] < 64 && be16(blk + 0x52) == 0x0100) {   // DiskCopy 4.2 header
        base = 84;
    }
    uint8_t boot[2], mdb[512];
    if (!read_at(base, boot, 2) || !read_at(base + 1024, mdb, sizeof(mdb))) return v;
    switch (be16(mdb)) {
    case 0x4244:                                        // 'BD': HFS (drFndrInfo[0], drVN)
        v.system = be32(mdb + 92) != 0;
        v.name = mac_roman_to_utf8(std::string_view(reinterpret_cast<char*>(mdb + 37), std::min<int>(mdb[36], 27)));
        break;
    case 0x482B: case 0x4858:                           // 'H+' / 'HX': finderInfo[0]
        v.system = be32(mdb + 80) != 0;
        break;
    case 0xD2D7:                                        // MFS (drVN)
        v.system = be16(boot) == 0x4C4B;                // 'LK'
        v.name = mac_roman_to_utf8(std::string_view(reinterpret_cast<char*>(mdb + 37), std::min<int>(mdb[36], 27)));
        break;
    }
    return v;
}

// A disk's entry in the storage inventory: its volume header, and its record
// beside it (<name>.json: "title", "tags", "note"; provisioning/build.py
// writes them for the images it makes).
static std::string disk_json(const std::string& images_path, const FileInfo& fi) {
    const std::string path = images_path + "/" + fi.name;
    VolumeInfo vol = probe_volume(path);
    std::ostringstream json;
    json << "{\"name\": \"" << json_escape(fi.name) << "\", \"size\": " << fi.size
         << ", \"system\": " << (vol.system ? "true" : "false");
    if (!vol.name.empty()) json << ", \"volume\": \"" << json_escape(vol.name) << "\"";
    QFile rf(QString::fromStdString(path + ".json"));
    if (rf.open(QIODevice::ReadOnly)) {
        QJsonObject rec = QJsonDocument::fromJson(rf.readAll()).object();
        for (const char* key : {"title", "note"}) {
            QString val = rec.value(key).toString();
            if (!val.isEmpty()) json << ", \"" << key << "\": \"" << json_escape(val.toStdString()) << "\"";
        }
        if (vol.name.empty() && rec.value("label").isString())
            json << ", \"volume\": \"" << json_escape(rec.value("label").toString().toStdString()) << "\"";
        QJsonArray tags = rec.value("tags").toArray();
        if (!tags.isEmpty()) {
            json << ", \"tags\": [";
            for (int i = 0; i < tags.size(); i++)
                json << (i ? ", " : "") << "\"" << json_escape(tags[i].toString().toStdString()) << "\"";
            json << "]";
        }
    }
    json << "}";
    return json.str();
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
        json << disk_json(images_path, disks[i]);
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
