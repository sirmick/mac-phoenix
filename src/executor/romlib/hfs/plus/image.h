#pragma once
/* image.h -- where HFS+ volumes are read from and written to: a disk image (or device) at
 * 64-bit byte offsets. In the browser the image is a WORKERFS/OVERLAYFS
 * file; pread reaches it the same way. Host code only: no guest types, so
 * the File Manager's volume, the System sideload and tools share it. */
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Executor::hfsplus
{

class ImageReader
{
public:
    virtual ~ImageReader() = default;
    /* All n bytes at offset, or false. */
    virtual bool read(uint64_t offset, void *buf, size_t n) = 0;
    /* All n bytes written at offset, or false (always, read-only). */
    virtual bool write(uint64_t offset, const void *buf, size_t n) { return false; }
    virtual bool writable() const { return false; }
    virtual void sync() {}
    virtual uint64_t size() const = 0;
};

/* A file descriptor, read with pread. */
class FdImageReader : public ImageReader
{
public:
    /* owns: close the fd with the reader; writable: write() may write */
    FdImageReader(int fd, bool owns, bool writable = false);
    ~FdImageReader() override;
    static std::shared_ptr<FdImageReader> open(const std::string& path, std::string *error = nullptr);

    bool read(uint64_t offset, void *buf, size_t n) override;
    bool write(uint64_t offset, const void *buf, size_t n) override;
    bool writable() const override { return writable_; }
    void sync() override;
    uint64_t size() const override { return size_; }

private:
    int fd_;
    bool owns_, writable_;
    uint64_t size_ = 0;
};

inline uint16_t be16(const uint8_t *p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
inline uint64_t be64(const uint8_t *p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }
inline void put16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
inline void put32(uint8_t *p, uint32_t v) { put16(p, uint16_t(v >> 16)); put16(p + 2, uint16_t(v)); }
inline void put64(uint8_t *p, uint64_t v) { put32(p, uint32_t(v >> 32)); put32(p + 4, uint32_t(v)); }

} // namespace Executor::hfsplus
