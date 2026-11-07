/* executor/host's overlay (host/overlay.h): overlays executor/server
   wrote read back (fixtures/overlay, made by overlay.go's own put: two
   versions, the second superseding a block; and a replace, a reset), and
   its own written, cut short, compacted and read again, the image never
   written. */
#include "gtest/gtest.h"

#include "overlay.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using executor_host::Overlay;

namespace
{
constexpr uint32_t B = Overlay::BLOCK;

std::vector<uint8_t> read_file(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

void write_file(const fs::path &p, const std::vector<uint8_t> &bytes)
{
    std::ofstream out(p, std::ios::binary);
    out.write((const char *)bytes.data(), bytes.size());
}

class OverlayTest : public testing::Test
{
protected:
    fs::path dir;
    fs::path image;
    std::vector<uint8_t> base;   // the image: block n is all 'a' + n

    void SetUp() override
    {
        dir = fs::temp_directory_path() / ("overlay-test-" + std::to_string(std::random_device()()));
        fs::create_directories(dir);
        image = dir / "fixture.img";
        base.resize(16 * B);
        for(size_t i = 0; i < base.size(); i++)
            base[i] = uint8_t('a' + i / B);
        write_file(image, base);
    }
    void TearDown() override { fs::remove_all(dir); }

    std::vector<uint8_t> contents(Overlay &o)
    {
        std::vector<uint8_t> out(o.size());
        EXPECT_EQ((ssize_t)out.size(), o.pread(out.data(), out.size(), 0));
        return out;
    }
    std::vector<uint8_t> expect(std::map<uint32_t, uint8_t> blocks)
    {
        std::vector<uint8_t> out = base;
        for(auto [b, c] : blocks)
            std::fill(out.begin() + b * B, out.begin() + (b + 1) * B, c);
        return out;
    }
    fs::path fixture(const std::string &name)
    {
        fs::path to = dir / (name + ".overlay");
        fs::copy_file(fs::path(EXECUTOR_SOURCE_DIR) / "tests/fixtures/overlay" / (name + ".overlay"), to);
        return to;
    }
};
}

TEST_F(OverlayTest, ServerOverlayReadsBack)
{
    std::string err;
    auto o = Overlay::open(image, fixture("plain"), false, &err);
    ASSERT_TRUE(o) << err;
    EXPECT_EQ(2u, o->version());
    EXPECT_EQ(3u, o->blocks());
    EXPECT_EQ(65536u, o->size());
    EXPECT_EQ(expect({ { 0, 'A' }, { 3, 'C' }, { 10, 'D' } }), contents(*o));
    EXPECT_EQ(base, read_file(image));
}

TEST_F(OverlayTest, ServerResetDropsTheBlocksBefore)
{
    std::string err;
    auto o = Overlay::open(image, fixture("reset"), false, &err);
    ASSERT_TRUE(o) << err;
    EXPECT_EQ(2u, o->version());
    EXPECT_EQ(expect({ { 5, 'E' } }), contents(*o));
}

TEST_F(OverlayTest, WritesOnAServerOverlay)
{
    fs::path ov = fixture("plain");
    {
        auto o = Overlay::open(image, ov, true);
        ASSERT_TRUE(o);
        std::vector<uint8_t> z(B, 'Z');
        ASSERT_EQ((ssize_t)B, o->pwrite(z.data(), B, 3 * B));
        ASSERT_TRUE(o->sync());
        EXPECT_EQ(3u, o->version());
    }
    auto o = Overlay::open(image, ov, false);
    EXPECT_EQ(expect({ { 0, 'A' }, { 3, 'Z' }, { 10, 'D' } }), contents(*o));
}

TEST_F(OverlayTest, RoundTripCutShortAndCompacted)
{
    fs::path ov = dir / "new.overlay";
    std::vector<uint8_t> model = base;
    {
        auto o = Overlay::open(image, ov, true);
        ASSERT_TRUE(o);
        EXPECT_FALSE(fs::exists(ov));  /* nothing written yet */
        /* across block edges, unaligned */
        std::string s(3 * B, 'x');
        for(size_t i = 0; i < s.size(); i++)
            s[i] = char('0' + i % 10);
        ASSERT_EQ((ssize_t)s.size(), o->pwrite(s.data(), s.size(), B + 100));
        std::copy(s.begin(), s.end(), model.begin() + B + 100);
        /* read back before it's out */
        EXPECT_EQ(model, contents(*o));
        ASSERT_TRUE(o->sync());
        /* past the end: refused */
        EXPECT_EQ(-1, o->pwrite("no", 2, o->size() - 1));
    }
    EXPECT_EQ(base, read_file(image));
    {
        auto o = Overlay::open(image, ov, false);
        EXPECT_EQ(model, contents(*o));
        EXPECT_EQ(1u, o->version());
    }

    /* a write cut short: what follows the last commit goes */
    auto good = fs::file_size(ov);
    {
        std::ofstream out(ov, std::ios::binary | std::ios::app);
        uint8_t head[12] = { 1, 0, 0, 0, 7, 0, 0, 0, 2, 0, 0, 0 };
        out.write((const char *)head, sizeof head);
        out << std::string(1000, 'q');
    }
    {
        /* a locked mount's open cuts it off too, as overlay.go's load */
        auto o = Overlay::open(image, ov, false);
        EXPECT_EQ(model, contents(*o));
        EXPECT_EQ(good, fs::file_size(ov));
    }

    /* the same block written over and over: superseded copies go */
    {
        auto o = Overlay::open(image, ov, true);
        for(int i = 0; i < 300; i++)
        {
            uint8_t c = uint8_t(i);
            ASSERT_EQ(1, o->pwrite(&c, 1, 7 * B));
            ASSERT_TRUE(o->sync());
        }
        model[7 * B] = uint8_t(299);
        EXPECT_EQ(model, contents(*o));
        /* compacted at 256 superseded (overlay.go's threshold), 300 written */
        EXPECT_LT(fs::file_size(ov), 64 * (12 + B));
    }
    auto o = Overlay::open(image, ov, false);
    EXPECT_EQ(model, contents(*o));
    EXPECT_EQ(301u, o->version());
    EXPECT_EQ(base, read_file(image));
}

TEST_F(OverlayTest, NotAnOverlay)
{
    fs::path ov = dir / "junk.overlay";
    write_file(ov, std::vector<uint8_t>(100, 'j'));
    std::string err;
    EXPECT_FALSE(Overlay::open(image, ov, true, &err));
    EXPECT_NE(std::string::npos, err.find("not an overlay"));
}
