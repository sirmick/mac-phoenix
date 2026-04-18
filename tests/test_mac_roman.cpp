/*
 * test_mac_roman — unit test for the MacRoman ↔ UTF-8 codec.
 * Standalone: compiles mac_roman.cpp directly, no core dependency.
 */

#include "mac_roman.h"

#include <cassert>
#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool ok, const char* what, const std::string& got, const std::string& want) {
	if (ok) {
		printf("  ok    %s\n", what);
		return;
	}
	failures++;
	printf("  FAIL  %s\n        got : ", what);
	for (unsigned char c : got)  printf("%02x ", c);
	printf("\n        want: ");
	for (unsigned char c : want) printf("%02x ", c);
	printf("\n");
}

static void check_eq(const std::string& got, const std::string& want, const char* what) {
	check(got == want, what, got, want);
}

int main() {
	printf("MacRoman ↔ UTF-8 codec\n");

	/* ASCII round-trip */
	check_eq(mac_roman_to_utf8("hello world"), "hello world", "ascii to utf8");
	check_eq(utf8_to_mac_roman("hello world"), "hello world", "ascii to macroman");

	/* Newline swap both ways */
	check_eq(mac_roman_to_utf8("a\rb\rc"), "a\nb\nc", "CR to LF");
	check_eq(utf8_to_mac_roman("a\nb\nc"), "a\rb\rc", "LF to CR");

	/* MacRoman 0xCA = NBSP (U+00A0) */
	check_eq(mac_roman_to_utf8("\xCA"), "\xC2\xA0", "0xCA → NBSP");
	check_eq(utf8_to_mac_roman("\xC2\xA0"), "\xCA", "NBSP → 0xCA");

	/* Curly quotes 0xD2-D5 → U+201C/D and U+2018/9 */
	check_eq(mac_roman_to_utf8("\xD2hi\xD3"), "\xE2\x80\x9Chi\xE2\x80\x9D", "curly double quotes");
	check_eq(utf8_to_mac_roman("\xE2\x80\x9Chi\xE2\x80\x9D"), "\xD2hi\xD3", "curly double quotes back");

	/* En/em dash 0xD0/D1 */
	check_eq(mac_roman_to_utf8("\xD0-\xD1"), "\xE2\x80\x93-\xE2\x80\x94", "en/em dash");

	/* Apple logo 0xF0 ↔ U+F8FF (Apple PUA) — round trip via our table */
	std::string apple_mr = "\xF0";
	std::string apple_u8 = mac_roman_to_utf8(apple_mr);
	check_eq(apple_u8, "\xEF\xA3\xBF", "apple logo to PUA");
	check_eq(utf8_to_mac_roman(apple_u8), apple_mr, "apple logo round-trip");

	/* € sign 0xDB → U+20AC (System 7.5+ mapping) */
	check_eq(mac_roman_to_utf8("\xDB"), "\xE2\x82\xAC", "0xDB → euro");
	check_eq(utf8_to_mac_roman("\xE2\x82\xAC"), "\xDB", "euro → 0xDB");

	/* Unmappable UTF-8 (emoji, here U+1F600) → '?' */
	check_eq(utf8_to_mac_roman("\xF0\x9F\x98\x80"), "?", "emoji → ?");

	/* Full round-trip: every MacRoman byte maps somewhere and comes back */
	std::string all_mr;
	all_mr.reserve(255);
	for (int b = 0; b < 256; b++) {
		if (b == '\r' || b == '\n') continue;  /* line endings swap, won't round-trip */
		all_mr.push_back(static_cast<char>(b));
	}
	std::string all_u8 = mac_roman_to_utf8(all_mr);
	std::string all_back = utf8_to_mac_roman(all_u8);
	check_eq(all_back, all_mr, "all 254 bytes round-trip");

	if (failures == 0) {
		printf("PASS\n");
		return 0;
	}
	printf("FAIL: %d failure(s)\n", failures);
	return 1;
}
