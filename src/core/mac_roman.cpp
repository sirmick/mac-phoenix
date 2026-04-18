/*
 * mac_roman.cpp — MacRoman ↔ UTF-8.
 *
 * Table is the System 7.5+ Apple MacRoman mapping (cf. Unicode Consortium
 * VENDORS/APPLE/ROMAN.TXT). 0x00–0x7F is identity ASCII; 0x80–0xFF maps
 * to the codepoints below. 0xDB is € (post-1998 update, not the older
 * generic-currency glyph).
 */

#include "mac_roman.h"

#include <array>
#include <algorithm>
#include <cstdint>

namespace {

constexpr std::array<uint32_t, 128> kHighToCp = {
	/* 0x80 */ 0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
	/* 0x88 */ 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
	/* 0x90 */ 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
	/* 0x98 */ 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
	/* 0xA0 */ 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
	/* 0xA8 */ 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
	/* 0xB0 */ 0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
	/* 0xB8 */ 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
	/* 0xC0 */ 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
	/* 0xC8 */ 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
	/* 0xD0 */ 0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
	/* 0xD8 */ 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
	/* 0xE0 */ 0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
	/* 0xE8 */ 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
	/* 0xF0 */ 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
	/* 0xF8 */ 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

struct CpToByte { uint32_t cp; uint8_t b; };

/* Sorted-by-codepoint reverse map; built once at first call. */
const std::array<CpToByte, 128>& reverse_map() {
	static const auto map = []{
		std::array<CpToByte, 128> m{};
		for (int i = 0; i < 128; i++) m[i] = { kHighToCp[i], static_cast<uint8_t>(0x80 + i) };
		std::sort(m.begin(), m.end(),
			[](const CpToByte& a, const CpToByte& b){ return a.cp < b.cp; });
		return m;
	}();
	return map;
}

/* UTF-8 encode a single codepoint (≤ U+10FFFF). Returns bytes written. */
int encode_utf8(uint32_t cp, char out[4]) {
	if (cp < 0x80) {
		out[0] = static_cast<char>(cp);
		return 1;
	}
	if (cp < 0x800) {
		out[0] = static_cast<char>(0xC0 | (cp >> 6));
		out[1] = static_cast<char>(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = static_cast<char>(0xE0 | (cp >> 12));
		out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out[2] = static_cast<char>(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = static_cast<char>(0xF0 | (cp >> 18));
	out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
	out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
	out[3] = static_cast<char>(0x80 | (cp & 0x3F));
	return 4;
}

/* Decode one codepoint at &in[i]; advance i. Malformed sequences yield U+FFFD. */
uint32_t decode_utf8(std::string_view in, size_t& i) {
	uint8_t b0 = static_cast<uint8_t>(in[i++]);
	if (b0 < 0x80) return b0;

	auto cont = [&]() -> int {
		if (i >= in.size()) return -1;
		uint8_t c = static_cast<uint8_t>(in[i]);
		if ((c & 0xC0) != 0x80) return -1;
		i++;
		return c & 0x3F;
	};

	if ((b0 & 0xE0) == 0xC0) {
		int c1 = cont();
		if (c1 < 0) return 0xFFFD;
		return ((b0 & 0x1F) << 6) | c1;
	}
	if ((b0 & 0xF0) == 0xE0) {
		int c1 = cont(), c2 = cont();
		if (c1 < 0 || c2 < 0) return 0xFFFD;
		return ((b0 & 0x0F) << 12) | (c1 << 6) | c2;
	}
	if ((b0 & 0xF8) == 0xF0) {
		int c1 = cont(), c2 = cont(), c3 = cont();
		if (c1 < 0 || c2 < 0 || c3 < 0) return 0xFFFD;
		return ((b0 & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3;
	}
	return 0xFFFD;
}

}  // namespace

std::string mac_roman_to_utf8(std::string_view in) {
	std::string out;
	out.reserve(in.size());
	char buf[4];
	for (uint8_t b : in) {
		if (b == '\r') { out.push_back('\n'); continue; }
		if (b < 0x80) { out.push_back(static_cast<char>(b)); continue; }
		int n = encode_utf8(kHighToCp[b - 0x80], buf);
		out.append(buf, n);
	}
	return out;
}

std::string utf8_to_mac_roman(std::string_view in) {
	std::string out;
	out.reserve(in.size());
	const auto& rmap = reverse_map();
	size_t i = 0;
	while (i < in.size()) {
		uint32_t cp = decode_utf8(in, i);
		if (cp == '\n') { out.push_back('\r'); continue; }
		if (cp < 0x80) { out.push_back(static_cast<char>(cp)); continue; }
		auto it = std::lower_bound(rmap.begin(), rmap.end(), cp,
			[](const CpToByte& e, uint32_t v){ return e.cp < v; });
		out.push_back((it != rmap.end() && it->cp == cp)
			? static_cast<char>(it->b) : '?');
	}
	return out;
}
