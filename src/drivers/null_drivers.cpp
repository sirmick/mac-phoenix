/*
 *  null_drivers.cpp - Null/stub implementations for misc drivers
 *
 *  Consolidates dummy implementations from:
 *    - clip_dummy.cpp (clipboard)
 *    - prefs_dummy.cpp (preferences)
 *    - prefs_editor_dummy.cpp (preferences UI)
 *    - user_strings_dummy.cpp (localization)
 *    - xpram_dummy.cpp (parameter RAM)
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "sysdeps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "clip.h"
#include "prefs_editor.h"
#include "user_strings.h"
#include "xpram.h"
#include "macos_util.h"

#define DEBUG 0
#include "debug.h"


// ============================================================
// CLIPBOARD (clip_dummy.cpp)
// ============================================================

void ClipInit(void)
{
}

void ClipExit(void)
{
}

void GetScrap(void **handle, uint32 type, int32 offset)
{
	D(bug("GetScrap handle %p, type %08x, offset %d\n", handle, type, offset));
}

void ZeroScrap()
{
	D(bug("ZeroScrap\n"));
}

void PutScrap(uint32 type, void *scrap, int32 length)
{
	D(bug("PutScrap type %08lx, data %08lx, length %ld\n", type, scrap, length));
	if (length <= 0)
		return;

	switch (type) {
		case FOURCC('T','E','X','T'):
			D(bug(" clipping TEXT\n"));
			break;
	}
}


// ============================================================
// PREFERENCES EDITOR (prefs_editor_dummy.cpp)
// ============================================================

bool PrefsEditor(void)
{
	return true;
}


// ============================================================
// USER STRINGS (user_strings_dummy.cpp)
// ============================================================

// Platform-specific string definitions
user_string_def platform_strings[] = {
	{-1, NULL}	// End marker
};

const char *GetString(int num)
{
	// First search for platform-specific string
	int i = 0;
	while (platform_strings[i].num >= 0) {
		if (platform_strings[i].num == num)
			return platform_strings[i].str;
		i++;
	}

	// Not found, search for common string
	i = 0;
	while (common_strings[i].num >= 0) {
		if (common_strings[i].num == num)
			return common_strings[i].str;
		i++;
	}
	return NULL;
}


// ============================================================
// XPRAM (xpram_dummy.cpp)
// ============================================================

// XPRAM file name and path
const char XPRAM_FILE_NAME[] = "BasiliskII_XPRAM";

void LoadXPRAM(const char *vmdir)
{
	FILE *f = fopen(XPRAM_FILE_NAME, "rb");
	if (f != NULL) {
		fread(XPRAM, 256, 1, f);
		fclose(f);
	}
}

void SaveXPRAM(void)
{
	FILE *f = fopen(XPRAM_FILE_NAME, "wb");
	if (f != NULL) {
		fwrite(XPRAM, 256, 1, f);
		fclose(f);
	}
}

