/*
 * bridge_fs.h - In-memory filesystem for the automation bridge
 *
 * Stores files in host memory (std::map). The guest accesses them through
 * ExtFS, which routes to these functions instead of Unix I/O when the
 * bridge volume is active.
 *
 * Host-side API handlers read/write files directly via get_file/put_file.
 * Guest-side INIT reads/writes via standard Mac File Manager calls,
 * which ExtFS translates into these functions.
 */
#ifndef BRIDGE_FS_H
#define BRIDGE_FS_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <mutex>

class BridgeFS {
public:
    // File I/O (called from ExtFS dispatch)
    int open(const std::string& name, bool write);
    int close(int fd);
    ssize_t read(int fd, void* buf, size_t count);
    ssize_t write(int fd, const void* buf, size_t count);
    int64_t seek(int fd, int64_t offset, int whence);  // SEEK_SET/CUR/END
    int64_t get_eof(int fd);
    int set_eof(int fd, int64_t size);
    bool exists(const std::string& name);
    int create(const std::string& name);
    int remove(const std::string& name);

    // Host-side API (for /api/files and bridge command/result)
    bool get_file(const std::string& name, std::string& out);
    void put_file(const std::string& name, const std::string& data);
    bool has_file(const std::string& name);
    void remove_file(const std::string& name);

    // List files (for directory enumeration)
    std::vector<std::string> list_files();

private:
    struct FileData {
        std::vector<uint8_t> data;
    };

    struct OpenFile {
        std::string name;
        int64_t pos = 0;
        bool writable = false;
    };

    std::mutex mutex_;
    std::map<std::string, FileData> files_;
    std::map<int, OpenFile> open_files_;
    int next_fd_ = 1000;  // start high to avoid collision with real fds
};

// Global instance (created when --bridge is set)
extern BridgeFS* g_bridge_fs;

#endif // BRIDGE_FS_H
