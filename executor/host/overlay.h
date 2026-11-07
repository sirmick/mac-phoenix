#pragma once
/*
 * overlay.h - a disk image with the Mac's changes kept apart: an
 * image_io::File (core/romlib/hfs/image_io.h) over the image, read-only,
 * and an overlay file of its changed 4 KB blocks, the format
 * executor/server/overlay.go keeps for the tab, so either reads the other's:
 *
 *   "XOVL" u32 format (1), then records of a 12-byte head (kind, a, b; u32 LE):
 *     block   a = block number, b = its version; 4096 bytes follow
 *     meta    a = length; that much JSON follows: { name, size }
 *     commit  a = the version, b = the reset version after it
 *
 * Records count once a commit follows them (what follows the last commit,
 * a write cut short, is cut off); a commit whose reset differs from the one
 * before drops the blocks before it (a Revert, a replace). Writes gather in
 * memory and go out at sync() (the File Manager's flush, fsync; 16 MB at
 * most), one version each; the file is written again without superseded
 * blocks when they are most of it. A native front end sets an opener that
 * makes these (frontends/desktop).
 */
#include <hfs/image_io.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace executor_host {

class Overlay : public Executor::image_io::File
{
public:
    /* IMAGE (never written) with the changes in OVERLAY (made at the first
       sync with something to write). Null and ERROR: the image can't be
       read, or OVERLAY isn't one. */
    static std::unique_ptr<Overlay> open(const std::string &image, const std::string &overlay, bool writable,
                                         std::string *error = nullptr);
    ~Overlay() override;

    ssize_t pread(void *buf, size_t n, uint64_t offset) override;
    ssize_t pwrite(const void *buf, size_t n, uint64_t offset) override;
    uint64_t size() override;
    bool sync() override;

    /* The committed version and the blocks it has (tests, logs) */
    uint32_t version() const { return version_; }
    size_t blocks() const { return index_.size(); }

    static constexpr uint32_t BLOCK = 4096;

private:
    Overlay() = default;
    bool load(std::string *error);
    bool create();
    bool readBlock(uint32_t b, uint8_t *out);
    void maybeCompact();

    int base_ = -1, file_ = -1;
    uint64_t baseSize_ = 0, metaSize_ = 0;
    std::string metaName_;
    std::string path_;
    bool writable_ = false;
    uint64_t end_ = 0;           // the file's committed length
    uint32_t version_ = 0, reset_ = 0;
    size_t dead_ = 0;            // superseded blocks in the file
    struct At
    {
        uint32_t version;
        uint64_t offset;         // of its data in the file
    };
    std::map<uint32_t, At> index_;
    std::map<uint32_t, std::array<uint8_t, BLOCK>> dirty_;
};

}  // namespace executor_host
