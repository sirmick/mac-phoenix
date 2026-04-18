/*
 * mac_roman.h — MacRoman ↔ UTF-8 codec for clipboard text exchange.
 *
 * Both directions translate line endings: Mac '\r' ↔ host '\n'.
 * Apple logo (MacRoman 0xF0) round-trips through Apple's PUA U+F8FF.
 * Unmappable UTF-8 codepoints become '?' (0x3F) in the MacRoman output.
 */

#ifndef MAC_ROMAN_H
#define MAC_ROMAN_H

#include <string>
#include <string_view>

std::string mac_roman_to_utf8(std::string_view in);
std::string utf8_to_mac_roman(std::string_view in);

#endif
