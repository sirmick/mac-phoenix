/* MacPhoenix: Executor's Image Compression Manager (icm.cpp) on its own,
   without Apple's codecs: image descriptions (color tables, extensions),
   descriptions of pixel maps, codecs that aren't there. The codecs
   themselves are tests/guest/qticm's (ctest qticm_executor_761). */
#include "gtest/gtest.h"
#include "compat.h"

#include <CQuickDraw.h>
#include <ImageCompression.h>
#include <MemoryMgr.h>
#include <QuickDraw.h>

#include <cstring>

using namespace Executor;

namespace
{
ImageDescriptionHandle new_desc()
{
    ImageDescriptionHandle d = (ImageDescriptionHandle)NewHandleClear(sizeof(ImageDescription));
    (*d)->idSize = sizeof(ImageDescription);
    (*d)->cType = "raw "_4;
    (*d)->depth = 8;
    (*d)->clutID = -1;
    return d;
}

Handle bytes(const char *s)
{
    Handle h = NewHandle(strlen(s));
    memcpy(*h, s, strlen(s));
    return h;
}

std::string text(Handle h)
{
    return std::string((const char *)*h, GetHandleSize(h));
}
}

TEST(ICM, ImageDescriptionExtensions)
{
    ImageDescriptionHandle d = new_desc();
    Handle a = bytes("hello"), b = bytes("wx");
    EXPECT_EQ(noErr, C_AddImageDescriptionExtension(d, a, "abcd"_4));
    EXPECT_EQ(noErr, C_AddImageDescriptionExtension(d, b, "efgh"_4));
    EXPECT_EQ(noErr, C_AddImageDescriptionExtension(d, a, "abcd"_4));
    EXPECT_EQ(86 + 13 + 10 + 13, (*d)->idSize);
    GUEST<LONGINT> n;
    EXPECT_EQ(noErr, C_CountImageDescriptionExtensionType(d, "abcd"_4, &n));
    EXPECT_EQ(2, n);
    EXPECT_EQ(noErr, C_CountImageDescriptionExtensionType(d, 0, &n));
    EXPECT_EQ(3, n);
    GUEST<Handle> got;
    EXPECT_EQ(noErr, C_GetImageDescriptionExtension(d, &got, "efgh"_4, 1));
    EXPECT_EQ("wx", text(got));
    DisposeHandle(got);
    EXPECT_EQ(codecExtensionNotFoundErr, C_GetImageDescriptionExtension(d, &got, "efgh"_4, 2));
    GUEST<LONGINT> type = 0;
    EXPECT_EQ(noErr, C_GetNextImageDescriptionExtensionType(d, &type));
    EXPECT_EQ("abcd"_4, (uint32_t)type);
    EXPECT_EQ(noErr, C_GetNextImageDescriptionExtensionType(d, &type));
    EXPECT_EQ("efgh"_4, (uint32_t)type);
    EXPECT_EQ(noErr, C_GetNextImageDescriptionExtensionType(d, &type));
    EXPECT_EQ(0, type);
    EXPECT_EQ(noErr, C_RemoveImageDescriptionExtension(d, "abcd"_4, 1));
    EXPECT_EQ(noErr, C_CountImageDescriptionExtensionType(d, "abcd"_4, &n));
    EXPECT_EQ(1, n);
    EXPECT_EQ(86 + 10 + 13, (*d)->idSize);
    EXPECT_EQ(86 + 10 + 13, GetHandleSize((Handle)d));
    DisposeHandle(a);
    DisposeHandle(b);
    DisposeHandle((Handle)d);
}

/* The color table goes between the fixed part and the extensions */
TEST(ICM, ImageDescriptionCTable)
{
    ImageDescriptionHandle d = new_desc();
    Handle e = bytes("data");
    EXPECT_EQ(noErr, C_AddImageDescriptionExtension(d, e, "ext "_4));
    CTabHandle ct = (CTabHandle)NewHandleClear(8 + 2 * 8);
    (*ct)->ctSize = 1;
    (*ct)->ctTable[1].rgb.red = 0xFFFF;
    EXPECT_EQ(noErr, C_SetImageDescriptionCTable(d, ct));
    EXPECT_EQ(0, (*d)->clutID);
    EXPECT_EQ(86 + 24 + 12, (*d)->idSize);
    GUEST<CTabHandle> got;
    EXPECT_EQ(noErr, C_GetImageDescriptionCTable(d, &got));
    ASSERT_NE(nullptr, (CTabHandle)got);
    EXPECT_EQ(1, (*got)->ctSize);
    EXPECT_EQ(0xFFFF, (*got)->ctTable[1].rgb.red);
    DisposeHandle((Handle)(CTabHandle)got);
    GUEST<Handle> ext;
    EXPECT_EQ(noErr, C_GetImageDescriptionExtension(d, &ext, "ext "_4, 1));
    EXPECT_EQ("data", text(ext));
    DisposeHandle(ext);
    /* and out again */
    EXPECT_EQ(noErr, C_SetImageDescriptionCTable(d, nullptr));
    EXPECT_EQ(-1, (*d)->clutID);
    EXPECT_EQ(86 + 12, (*d)->idSize);
    EXPECT_EQ(noErr, C_GetImageDescriptionExtension(d, &ext, "ext "_4, 1));
    EXPECT_EQ("data", text(ext));
    DisposeHandle(ext);
    DisposeHandle((Handle)ct);
    DisposeHandle(e);
    DisposeHandle((Handle)d);
}

TEST(ICM, DescriptionForPixMap)
{
    PixMapHandle pm = NewPixMap();
    (*pm)->bounds.top = 10;
    (*pm)->bounds.left = 20;
    (*pm)->bounds.bottom = 58;
    (*pm)->bounds.right = 84;
    (*pm)->pixelSize = 32;
    GUEST<ImageDescriptionHandle> d;
    EXPECT_EQ(noErr, C_MakeImageDescriptionForPixMap(pm, &d));
    ASSERT_NE(nullptr, (ImageDescriptionHandle)d);
    EXPECT_EQ("raw "_4, (uint32_t)(*d)->cType);
    EXPECT_EQ(64, (*d)->width);
    EXPECT_EQ(48, (*d)->height);
    EXPECT_EQ(32, (*d)->depth);
    EXPECT_EQ(-1, (*d)->clutID);
    DisposeHandle((Handle)(ImageDescriptionHandle)d);
    DisposePixMap(pm);
}

TEST(ICM, NoCodec)
{
    GUEST<CompressorComponent> c;
    GUEST<DecompressorComponent> dc;
    EXPECT_EQ(noCodecErr, C_FindCodec("none"_4, nullptr, &c, &dc));
    EXPECT_EQ(nullptr, (Component)c);
    CodecInfo info;
    EXPECT_EQ(noCodecErr, C_GetCodecInfo(&info, "none"_4, nullptr));
    GUEST<LONGINT> v = 0;
    EXPECT_EQ(noErr, C_CodecManagerVersion(&v));
    EXPECT_EQ(18, v);
}
