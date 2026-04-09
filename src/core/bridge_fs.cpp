/*
 * bridge_fs.cpp - In-memory filesystem for the automation bridge
 */
#include "bridge_fs.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <algorithm>

BridgeFS* g_bridge_fs = nullptr;

int BridgeFS::open(const std::string& name, bool write) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Auto-create on open-for-write
    if (write && files_.find(name) == files_.end())
        files_[name] = FileData{};

    if (files_.find(name) == files_.end())
        return -1;  // file not found

    int fd = next_fd_++;
    open_files_[fd] = {name, 0, write};
    return fd;
}

int BridgeFS::close(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    open_files_.erase(fd);
    return 0;
}

ssize_t BridgeFS::read(int fd, void* buf, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = open_files_.find(fd);
    if (it == open_files_.end()) return -1;

    auto& of = it->second;
    auto fit = files_.find(of.name);
    if (fit == files_.end()) return -1;

    auto& data = fit->second.data;
    int64_t avail = static_cast<int64_t>(data.size()) - of.pos;
    if (avail <= 0) return 0;

    size_t to_read = std::min(count, static_cast<size_t>(avail));
    memcpy(buf, data.data() + of.pos, to_read);
    of.pos += to_read;
    return static_cast<ssize_t>(to_read);
}

ssize_t BridgeFS::write(int fd, const void* buf, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = open_files_.find(fd);
    if (it == open_files_.end()) return -1;
    if (!it->second.writable) return -1;

    auto& of = it->second;
    auto& data = files_[of.name].data;

    // Extend if writing past end
    size_t needed = static_cast<size_t>(of.pos) + count;
    if (needed > data.size())
        data.resize(needed);

    memcpy(data.data() + of.pos, buf, count);
    of.pos += count;
    return static_cast<ssize_t>(count);
}

int64_t BridgeFS::seek(int fd, int64_t offset, int whence) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = open_files_.find(fd);
    if (it == open_files_.end()) return -1;

    auto& of = it->second;
    auto fit = files_.find(of.name);
    int64_t size = fit != files_.end() ? static_cast<int64_t>(fit->second.data.size()) : 0;

    switch (whence) {
        case 0: of.pos = offset; break;           // SEEK_SET
        case 1: of.pos += offset; break;           // SEEK_CUR
        case 2: of.pos = size + offset; break;     // SEEK_END
    }
    if (of.pos < 0) of.pos = 0;
    return of.pos;
}

int64_t BridgeFS::get_eof(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = open_files_.find(fd);
    if (it == open_files_.end()) return -1;
    auto fit = files_.find(it->second.name);
    if (fit == files_.end()) return 0;
    return static_cast<int64_t>(fit->second.data.size());
}

int BridgeFS::set_eof(int fd, int64_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = open_files_.find(fd);
    if (it == open_files_.end()) return -1;
    files_[it->second.name].data.resize(static_cast<size_t>(size));
    return 0;
}

bool BridgeFS::exists(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_.find(name) != files_.end();
}

int BridgeFS::create(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    files_[name] = FileData{};
    return 0;
}

int BridgeFS::remove(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    files_.erase(name);
    return 0;
}

// Host-side API

bool BridgeFS::get_file(const std::string& name, std::string& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(name);
    if (it == files_.end()) return false;
    out.assign(it->second.data.begin(), it->second.data.end());
    return true;
}

void BridgeFS::put_file(const std::string& name, const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    files_[name].data.assign(data.begin(), data.end());

    // Also write to the bridge dir on disk so ExtFS can find it via FSMakeFSSpec
    if (!bridge_dir_.empty()) {
        std::string path = bridge_dir_ + "/" + name;
        FILE* f = fopen(path.c_str(), "w");
        if (f) {
            fwrite(data.data(), 1, data.size(), f);
            fclose(f);
            sync();
        }
    }
}

bool BridgeFS::has_file(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_.find(name) != files_.end();
}

void BridgeFS::remove_file(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    files_.erase(name);
}

std::vector<std::string> BridgeFS::list_files() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    for (auto& kv : files_)
        result.push_back(kv.first);
    return result;
}
