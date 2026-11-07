/* Disk images through the front end's opener (hfs/image_io.h): an HFS
   floppy mounted and written with the image in memory, the file on disk
   untouched; HFS+'s reader on an opener's descriptor; and no opener, the
   POSIX file as before. */
#include "gtest/gtest.h"

#include "compat.h"

#include <FileMgr.h>
#include <file/file.h>
#include <hfs/hfs.h>
#include <hfs/image_io.h>
#include <hfs/plus/image.h>
#include <rsys/filesystem.h>

#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Executor;

namespace
{
/* An image in memory */
class MemoryFile : public image_io::File
{
public:
    explicit MemoryFile(std::vector<uint8_t> &bytes) : bytes_(bytes) {}
    ssize_t pread(void *buf, size_t n, uint64_t offset) override
    {
        if(offset >= bytes_.size())
            return 0;
        n = std::min<uint64_t>(n, bytes_.size() - offset);
        memcpy(buf, bytes_.data() + offset, n);
        return n;
    }
    ssize_t pwrite(const void *buf, size_t n, uint64_t offset) override
    {
        if(offset + n > bytes_.size())
            return -1;
        memcpy(bytes_.data() + offset, buf, n);
        writes++;
        return n;
    }
    uint64_t size() override { return bytes_.size(); }
    bool sync() override
    {
        syncs++;
        return true;
    }
    static inline int writes = 0, syncs = 0;

private:
    std::vector<uint8_t> &bytes_;
};

std::vector<uint8_t> read_file(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

StringPtr pname(const std::string &s)
{
    static Str255 buf; /* guest-addressable */
    buf[0] = (uint8_t)s.size();
    memcpy(buf + 1, s.data(), s.size());
    return buf;
}

struct OpenerGone
{
    ~OpenerGone() { image_io::set_opener(nullptr); }
};
}

#ifdef EXECUTOR_HFS_BLANK
TEST(ImageIO, HFSWritesGoToTheOpenersFile)
{
    OpenerGone gone;
    std::string copy = (fs::temp_directory_path() / fs::unique_path("imageio-%%%%-%%%%.img")).string();
    std::error_code ec;
    fs::copy_file(EXECUTOR_HFS_BLANK, copy, ec);
    ASSERT_FALSE(ec);
    std::vector<uint8_t> original = read_file(copy), memory = original;
    int opened = 0;
    image_io::set_opener([&](const std::string &path, bool writable) -> std::unique_ptr<image_io::File> {
        if(path != copy)
            return nullptr;
        opened++;
        EXPECT_TRUE(writable);
        return std::make_unique<MemoryFile>(memory);
    });
    MemoryFile::writes = MemoryFile::syncs = 0;

    GUEST<LONGINT> mess;
    ROMlib_openharddisk(copy.c_str(), &mess);
    EXPECT_GE(opened, 1);
    HParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioNamePtr = pname("Blank:");
    pb.volumeParam.ioVolIndex = -1;
    ASSERT_EQ(noErr, PBHGetVInfo(&pb, false));
    short vref = pb.volumeParam.ioVRefNum;

    ASSERT_EQ(noErr, HCreate(vref, 2, pname("Kept apart"), "TEXT"_4, "ttxt"_4));
    GUEST<INTEGER> ref;
    ASSERT_EQ(noErr, HOpenDF(vref, 2, pname("Kept apart"), fsRdWrPerm, &ref));
    char data[] = "written through image_io";
    GUEST<LONGINT> count = sizeof data;
    EXPECT_EQ(noErr, FSWrite(ref, &count, (Ptr)data));
    EXPECT_EQ(noErr, FSClose(ref));
    EXPECT_EQ(noErr, FlushVol(nullptr, vref));

    EXPECT_GT(MemoryFile::writes, 0);
    EXPECT_GT(MemoryFile::syncs, 0);
    EXPECT_NE(original, memory);
    EXPECT_EQ(original, read_file(copy));   /* the image itself as it was */
    auto found = std::search(memory.begin(), memory.end(), data, data + sizeof data - 1);
    EXPECT_NE(memory.end(), found);

    ParamBlockRec upb;
    memset(&upb, 0, sizeof upb);
    upb.volumeParam.ioVRefNum = vref;
    EXPECT_EQ(noErr, hfsPBUnmountVol(&upb, true));
    fs::remove(copy, ec);
}
#endif

TEST(ImageIO, HFSPlusReaderOnAnOpenersFile)
{
    OpenerGone gone;
    std::vector<uint8_t> memory(64 * 1024, 0xA5);
    image_io::set_opener([&](const std::string &path, bool) -> std::unique_ptr<image_io::File> {
        return path == "/no/such/image" ? std::make_unique<MemoryFile>(memory) : nullptr;
    });
    int fd = image_io::open("/no/such/image", O_RDWR);
    ASSERT_GE(fd, 0);
    EXPECT_TRUE(image_io::is_virtual(fd));
    {
        hfsplus::FdImageReader r(fd, false, true);
        EXPECT_EQ(memory.size(), r.size());
        uint8_t buf[16];
        ASSERT_TRUE(r.read(100, buf, sizeof buf));
        EXPECT_EQ(0xA5, buf[0]);
        ASSERT_TRUE(r.write(4096, "abc", 3));
        ASSERT_TRUE(r.zero(8192, 4096));   /* no hole punched in an opener's file */
    }
    EXPECT_EQ('a', memory[4096]);
    EXPECT_EQ(0, memory[8192]);
    EXPECT_EQ(0xA5, memory[8192 + 4096]);
    EXPECT_EQ(0, image_io::close(fd));
    EXPECT_FALSE(image_io::is_virtual(fd));
}

TEST(ImageIO, WithoutAnOpenerAFileIsPOSIX)
{
    std::string path = (fs::temp_directory_path() / fs::unique_path("imageio-%%%%-%%%%.bin")).string();
    {
        std::ofstream out(path, std::ios::binary);
        out << std::string(1000, 'x');
    }
    int fd = image_io::open(path.c_str(), O_RDWR);
    ASSERT_GE(fd, 0);
    EXPECT_FALSE(image_io::is_virtual(fd));
    uint64_t size;
    bool regular;
    ASSERT_TRUE(image_io::stat(fd, &size, &regular));
    EXPECT_EQ(1000u, size);
    EXPECT_TRUE(regular);
    EXPECT_EQ(2, image_io::pwrite(fd, "ok", 2, 10));
    char buf[4] = {};
    EXPECT_EQ(4, image_io::pread(fd, buf, 4, 9));
    EXPECT_EQ(0, memcmp(buf, "xokx", 4));
    EXPECT_EQ(0, image_io::fsync(fd));
    EXPECT_EQ(0, image_io::close(fd));
    std::error_code ec;
    fs::remove(path, ec);
}
