#pragma once
/* image.h -- where an HFS+ reader reads from: a disk image (or device) at
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
    virtual uint64_t size() const = 0;
};

/* A file descriptor, read with pread. */
class FdImageReader : public ImageReader
{
public:
    /* owns: close the fd with the reader. */
    FdImageReader(int fd, bool owns);
    ~FdImageReader() override;
    static std::shared_ptr<FdImageReader> open(const std::string& path, std::string *error = nullptr);

    bool read(uint64_t offset, void *buf, size_t n) override;
    uint64_t size() const override { return size_; }

private:
    int fd_;
    bool owns_;
    uint64_t size_ = 0;
};

inline uint16_t be16(const uint8_t *p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
inline uint64_t be64(const uint8_t *p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }

} // namespace Executor::hfsplus
