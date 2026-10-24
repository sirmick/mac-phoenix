#pragma once
/* unicode.h -- HFS+ names: TN1150's case-insensitive order
 * (FastUnicodeCompare), and the MacRoman names the classic File Manager
 * sees, as Mac OS 8.1-9 make them (checked on 9.0.4:
 * tests/guest/hfsplus/oracle-9.0.4). */
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Executor::hfsplus
{

/* TN1150's FastUnicodeCompare: <0, 0, >0. Case-insensitive, ignorable
   characters skipped. */
int fastUnicodeCompare(const char16_t *a, size_t alen, const char16_t *b, size_t blen);
inline int fastUnicodeCompare(const std::u16string& a, const std::u16string& b)
{
    return fastUnicodeCompare(a.data(), a.size(), b.data(), b.size());
}

/* HFSX's binary order. */
int binaryCompare(const char16_t *a, size_t alen, const char16_t *b, size_t blen);

/* A MacRoman name as HFS+ stores it: decomposed. */
std::u16string macRomanToUnicode(const std::string& macRoman);

/* An HFS+ name in MacRoman, composed again; characters with no MacRoman
   form become '?' and *lossy is set. A name made on Mac OS X can hold ':'
   (its '/'): it comes back as '/', as Mac OS 9 shows it. */
std::string unicodeToMacRoman(const std::u16string& name, bool *lossy = nullptr);

/* The name the classic (31-character, MacRoman) File Manager gives an
   item, as Mac OS 9.0.4 does: the MacRoman name when it has one of 31
   characters or fewer; else the name cut so that "#" + the CNID in hex
   (upper case) + the extension (from the last '.') fits 31. */
std::string classicName(const std::u16string& name, uint32_t cnid);

/* If macRoman is such a shortened name, the CNID in it. */
std::optional<uint32_t> cnidInClassicName(const std::string& macRoman);

/* UTF-8, for tools and logs. */
std::string toUTF8(const std::u16string& s);

} // namespace Executor::hfsplus
