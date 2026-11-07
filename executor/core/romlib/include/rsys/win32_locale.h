#pragma once
/*
 * win32_locale.h - newlocale() and uselocale(), which the C library code
 * uses to convert numbers in the "C" locale, on Windows (MacPhoenix):
 * libc++ declares locale_t and newlocale() there (newlocale not exported
 * from its DLL) but no uselocale() (Windows' per-thread locales work
 * differently). No-ops do: a Windows process stays in the "C"
 * locale (the C runtime starts in it and nothing here, Qt included, calls
 * setlocale).
 */
#ifdef _WIN32
#include <clocale>
#include <locale>

#define newlocale(mask, name, base) locale_t()
inline locale_t uselocale(locale_t l) { return l; }
#endif
