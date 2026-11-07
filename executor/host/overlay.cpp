/*
 * overlay.cpp - a disk image and its overlay file (overlay.h), as
 * executor/server/overlay.go reads and writes them.
 */
#include "overlay.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifndef O_BINARY
#define O_BINARY 0
#endif

namespace executor_host {

namespace {

constexpr char MAGIC[4] = { 'X', 'O', 'V', 'L' };
constexpr uint32_t FORMAT = 1;
constexpr uint32_t HEAD = 12;
constexpr uint32_t KIND_BLOCK = 1, KIND_META = 2, KIND_COMMIT = 3;
constexpr size_t MAX_DIRTY = 4096;  /* blocks gathered before they go out: 16 MB */

void put32(std::vector<uint8_t> &v, uint32_t x)
{
    for(int i = 0; i < 4; i++)
        v.push_back(uint8_t(x >> (8 * i)));
}

uint32_t get32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24;
}

void put_head(std::vector<uint8_t> &v, uint32_t kind, uint32_t a, uint32_t b)
{
    put32(v, kind);
    put32(v, a);
    put32(v, b);
}

bool read_all(int fd, void *buf, size_t n, uint64_t at)
{
    auto *p = static_cast<uint8_t *>(buf);
    while(n)
    {
        ssize_t got = ::pread(fd, p, n, (off_t)at);
        if(got < 0 && errno == EINTR)
            continue;
        if(got <= 0)
            return false;
        p += got, at += got, n -= got;
    }
    return true;
}

bool write_all(int fd, const void *buf, size_t n, uint64_t at)
{
    auto *p = static_cast<const uint8_t *>(buf);
    while(n)
    {
        ssize_t put = ::pwrite(fd, p, n, (off_t)at);
        if(put < 0 && errno == EINTR)
            continue;
        if(put <= 0)
            return false;
        p += put, at += put, n -= put;
    }
    return true;
}

int sync_fd(int fd)
{
#ifdef _WIN32
    return 0;
#else
    return ::fsync(fd);
#endif
}

/* The two keys of the meta record's JSON object, as overlay.go writes it
   ({"name":"...","size":N}); others ignored */
void parse_meta(const std::string &json, std::string *name, uint64_t *size)
{
    size_t at = json.find("\"size\"");
    if(at != std::string::npos && (at = json.find(':', at)) != std::string::npos)
        *size = strtoull(json.c_str() + at + 1, nullptr, 10);
    at = json.find("\"name\"");
    if(at != std::string::npos && (at = json.find('"', json.find(':', at))) != std::string::npos)
    {
        std::string out;
        for(size_t i = at + 1; i < json.size() && json[i] != '"'; i++)
        {
            if(json[i] == '\\' && i + 1 < json.size())
                i++;
            out += json[i];
        }
        *name = out;
    }
}

std::string json_escape(const std::string &s)
{
    std::string out;
    for(char c : s)
    {
        if(c == '"' || c == '\\')
            out += '\\';
        if((unsigned char)c < 0x20)
            continue;
        out += c;
    }
    return out;
}

}

std::unique_ptr<Overlay> Overlay::open(const std::string &image, const std::string &overlay, bool writable,
                                       std::string *error)
{
    std::unique_ptr<Overlay> o(new Overlay);
    o->base_ = ::open(image.c_str(), O_RDONLY | O_BINARY);
    if(o->base_ < 0)
    {
        if(error)
            *error = image + ": " + strerror(errno);
        return nullptr;
    }
    struct stat st;
    if(fstat(o->base_, &st) == 0)
        o->baseSize_ = (uint64_t)st.st_size;
    o->path_ = overlay;
    o->writable_ = writable;
    o->metaName_ = image.substr(image.find_last_of('/') + 1);
    if(!o->load(error))
        return nullptr;
    return o;
}

Overlay::~Overlay()
{
    sync();
    if(file_ >= 0)
        ::close(file_);
    if(base_ >= 0)
        ::close(base_);
}

/* The overlay file as overlay.go's load reads it */
bool Overlay::load(std::string *error)
{
    file_ = ::open(path_.c_str(), O_RDWR | O_BINARY);
    if(file_ < 0)
    {
        if(errno == ENOENT)
            return true;  /* none yet */
        if(error)
            *error = path_ + ": " + strerror(errno);
        return false;
    }
    struct stat st;
    fstat(file_, &st);
    uint64_t size = (uint64_t)st.st_size;
    uint8_t head[8];
    if(!read_all(file_, head, 8, 0) || memcmp(head, MAGIC, 4) != 0 || get32(head + 4) != FORMAT)
    {
        if(error)
            *error = path_ + ": not an overlay of format 1";
        ::close(file_);
        file_ = -1;
        return false;
    }
    struct Pending
    {
        uint32_t block;
        At at;
    };
    std::vector<Pending> pending;
    std::string meta;
    bool haveMeta = false;
    uint64_t at = 8, good = 8;
    uint8_t h[HEAD];
    while(at + HEAD <= size && read_all(file_, h, HEAD, at))
    {
        uint32_t kind = get32(h), a = get32(h + 4), b = get32(h + 8);
        at += HEAD;
        bool bad = false;
        if(kind == KIND_BLOCK)
        {
            if(at + BLOCK > size)
                bad = true;
            else
            {
                pending.push_back({ a, { b, at } });
                at += BLOCK;
            }
        }
        else if(kind == KIND_META)
        {
            if(at + a > size || a > (1u << 16))
                bad = true;
            else
            {
                meta.assign(a, '\0');
                read_all(file_, meta.data(), a, at);
                haveMeta = true;
                at += a;
            }
        }
        else if(kind == KIND_COMMIT)
        {
            if(b != reset_)
            {
                dead_ += index_.size();
                index_.clear();
                reset_ = b;
            }
            for(const Pending &p : pending)
            {
                if(index_.count(p.block))
                    dead_++;
                index_[p.block] = p.at;
            }
            pending.clear();
            if(haveMeta)
            {
                parse_meta(meta, &metaName_, &metaSize_);
                haveMeta = false;
            }
            version_ = a;
            good = at;
        }
        else
            bad = true;
        if(bad)
            break;
    }
    /* a write cut short: cut off, as overlay.go's load does, a locked
       mount's too (the file is open read-write either way; only what no
       commit vouches for goes) */
    if(good < size)
        (void)!ftruncate(file_, (off_t)good);
    end_ = good;
    return true;
}

bool Overlay::create()
{
    if(file_ >= 0)
        return true;
    file_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0644);
    if(file_ < 0)
        return false;
    std::vector<uint8_t> head(MAGIC, MAGIC + 4);
    put32(head, FORMAT);
    if(!write_all(file_, head.data(), head.size(), 0))
    {
        ::close(file_);
        file_ = -1;
        ::unlink(path_.c_str());
        return false;
    }
    end_ = head.size();
    return true;
}

uint64_t Overlay::size()
{
    return metaSize_ ? metaSize_ : baseSize_;
}

/* Block B as the Mac sees it: changed and not out yet, in the overlay, or
   the image's (zeros past its end) */
bool Overlay::readBlock(uint32_t b, uint8_t *out)
{
    auto d = dirty_.find(b);
    if(d != dirty_.end())
    {
        memcpy(out, d->second.data(), BLOCK);
        return true;
    }
    auto it = index_.find(b);
    if(it != index_.end())
        return read_all(file_, out, BLOCK, it->second.offset);
    uint64_t at = (uint64_t)b * BLOCK;
    memset(out, 0, BLOCK);
    if(at >= baseSize_)
        return true;
    return read_all(base_, out, std::min<uint64_t>(BLOCK, baseSize_ - at), at);
}

ssize_t Overlay::pread(void *buf, size_t n, uint64_t offset)
{
    uint64_t total = size();
    if(offset >= total)
        return 0;
    n = (size_t)std::min<uint64_t>(n, total - offset);
    auto *p = static_cast<uint8_t *>(buf);
    size_t done = 0;
    while(done < n)
    {
        uint64_t at = offset + done;
        uint32_t b = uint32_t(at / BLOCK), in = uint32_t(at % BLOCK);
        size_t len = std::min<size_t>(BLOCK - in, n - done);
        if(in == 0 && len == BLOCK && !dirty_.count(b) && !index_.count(b))
        {
            /* a run of the image's own blocks in one read */
            size_t run = BLOCK;
            while(done + run + BLOCK <= n && !dirty_.count(b + run / BLOCK) && !index_.count(b + run / BLOCK))
                run += BLOCK;
            uint64_t avail = at < baseSize_ ? baseSize_ - at : 0;
            size_t fromBase = (size_t)std::min<uint64_t>(run, avail);
            if(fromBase && !read_all(base_, p + done, fromBase, at))
                return -1;
            memset(p + done + fromBase, 0, run - fromBase);
            done += run;
            continue;
        }
        uint8_t block[BLOCK];
        if(!readBlock(b, block))
            return -1;
        memcpy(p + done, block + in, len);
        done += len;
    }
    return (ssize_t)n;
}

ssize_t Overlay::pwrite(const void *buf, size_t n, uint64_t offset)
{
    if(!writable_ || offset + n > size())
    {
        errno = writable_ ? ENOSPC : EBADF;
        return -1;
    }
    auto *p = static_cast<const uint8_t *>(buf);
    size_t done = 0;
    while(done < n)
    {
        uint64_t at = offset + done;
        uint32_t b = uint32_t(at / BLOCK), in = uint32_t(at % BLOCK);
        size_t len = std::min<size_t>(BLOCK - in, n - done);
        auto d = dirty_.find(b);
        if(d == dirty_.end())
        {
            std::array<uint8_t, BLOCK> block;
            if(len < BLOCK && !readBlock(b, block.data()))
                return -1;
            d = dirty_.emplace(b, block).first;
        }
        memcpy(d->second.data() + in, p + done, len);
        done += len;
    }
    if(dirty_.size() >= MAX_DIRTY && !sync())
        return -1;
    return (ssize_t)n;
}

/* The changes gathered, out as one version (overlay.go's commit) */
bool Overlay::sync()
{
    if(dirty_.empty())
        return true;
    if(!create())
    {
        fprintf(stderr, "[overlay] %s: can't make it: %s\n", path_.c_str(), strerror(errno));
        return false;
    }
    uint32_t v = version_ + 1;
    std::vector<uint8_t> buf;
    buf.reserve(dirty_.size() * (HEAD + BLOCK) + 256);
    if(end_ == 8)  /* a new file: what it's the overlay of */
    {
        std::string meta = "{\"name\":\"" + json_escape(metaName_) + "\",\"size\":" + std::to_string(size()) + "}";
        put_head(buf, KIND_META, (uint32_t)meta.size(), 0);
        buf.insert(buf.end(), meta.begin(), meta.end());
    }
    std::map<uint32_t, uint64_t> places;
    for(const auto &[b, data] : dirty_)
    {
        put_head(buf, KIND_BLOCK, b, v);
        places[b] = end_ + buf.size();
        buf.insert(buf.end(), data.begin(), data.end());
    }
    put_head(buf, KIND_COMMIT, v, reset_);
    if(!write_all(file_, buf.data(), buf.size(), end_) || sync_fd(file_) != 0)
    {
        fprintf(stderr, "[overlay] %s: write failed: %s\n", path_.c_str(), strerror(errno));
        (void)!ftruncate(file_, (off_t)end_);
        return false;
    }
    for(const auto &[b, off] : places)
    {
        if(index_.count(b))
            dead_++;
        index_[b] = { v, off };
    }
    if(!metaSize_)
        metaSize_ = baseSize_;
    end_ += buf.size();
    version_ = v;
    dirty_.clear();
    maybeCompact();
    return true;
}

/* Written again with the live blocks only, when superseded ones are most
   of it (overlay.go's maybeCompact) */
void Overlay::maybeCompact()
{
    if(file_ < 0 || dead_ < 256 || dead_ < index_.size())
        return;
    std::string tmp = path_ + ".tmp";
    int f = ::open(tmp.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if(f < 0)
        return;
    std::vector<uint8_t> head(MAGIC, MAGIC + 4);
    put32(head, FORMAT);
    std::string meta = "{\"name\":\"" + json_escape(metaName_) + "\",\"size\":" + std::to_string(size()) + "}";
    put_head(head, KIND_META, (uint32_t)meta.size(), 0);
    head.insert(head.end(), meta.begin(), meta.end());
    bool ok = write_all(f, head.data(), head.size(), 0);
    uint64_t at = head.size();
    std::map<uint32_t, At> index;
    std::vector<uint8_t> rec(HEAD + BLOCK);
    for(const auto &[b, e] : index_)
    {
        if(!ok)
            break;
        rec.clear();
        put_head(rec, KIND_BLOCK, b, e.version);
        rec.resize(HEAD + BLOCK);
        ok = read_all(file_, rec.data() + HEAD, BLOCK, e.offset) && write_all(f, rec.data(), rec.size(), at);
        index[b] = { e.version, at + HEAD };
        at += rec.size();
    }
    std::vector<uint8_t> c;
    put_head(c, KIND_COMMIT, version_, reset_);
    ok = ok && write_all(f, c.data(), c.size(), at) && sync_fd(f) == 0;
    at += c.size();
    if(!ok || ::rename(tmp.c_str(), path_.c_str()) != 0)
    {
        ::close(f);
        ::unlink(tmp.c_str());
        return;
    }
    ::close(file_);
    file_ = f;
    end_ = at;
    index_ = std::move(index);
    dead_ = 0;
}

}  // namespace executor_host
