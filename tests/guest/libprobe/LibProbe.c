/*
 * LibProbe.c -- which PowerPC shared libraries a system provides, and where
 * their code lives: for each library name, whether the Code Fragment
 * Manager has it already (kFindLib), whether it loads (kReferenceCFrag),
 * its number of exports, and where its first exported routine's code is:
 * ROM, the System heap, or elsewhere (a file loaded into the application
 * heap or temporary memory). Run on Mac OS 8.6/9.0.4 on the G3 with
 * extensions off, it says which libraries are the system's own
 * (docs/executor/MacOS8-9.md). Writes Host:libprobe.txt, one line per
 * library, then "---". Built with Retro68 by build-retro68.sh.
 */

#include <Types.h>
#include <Memory.h>
#include <CodeFragments.h>
#include <Gestalt.h>
#include <stdio.h>
#include <string.h>

static const char *const libs[] = {
    /* what the 8.6 and 9.0.4 Finders import */
    "InterfaceLib", "PrivateInterfaceLib", "AppearanceLib", "WindowsLib", "ControlsLib",
    "MenusLib", "MacDialogsLib", "DialogsLib", "DragLib", "IconServicesLib", "Translation",
    "ThreadsLib", "ObjectSupportLib", "FontManager", "FontObjects",
    "ToolboxUtils", "DesktopItemsLib", "NameRegistryLib", "PCCard", "StdCLib", "MathLib",
    "NavigationLib", "ContextualMenu", "FileSharingLib", "FoldersLib", "ColorPickerLib",
    "AppleGuideGlueLib", "TextCommon", "UnicodeConverter",
    /* the System file's 'nlib's and other common ones */
    "SoundLib", "QuickdrawLib", "Math64Lib", "VideoServicesLib", "DriverServicesLib",
    "DriverLoaderLib", "CursorDevicesLib", "UTCUtils", "StringCompare", "PPCToolbox",
    "DebugLib", "CFragManager", "AppleScriptLib", "OTClientLib", "OTUtilityLib",
    "InternetConfigLib", "QuickTimeLib", "ATSUnicodeLib", "LocalesLib", "CarbonLib",
    "TextEncodingConverter", "ThreadsLibrary", "AppearanceLibrary", "ControlStripLib",
    "Help Manager", "HelpLib", "ScrapLib", "MenusLibrary", "ProcessesLib", "FileManagerLib",
    "CarbonAccessors.o", "NumberFormatting", "CollectionsLib", "ComponentLib",
    /* last: it hangs GetSharedLibrary on the G3 (8.6 and 9.0.4) */
    "Memory Manager",
};

static unsigned long rom_lo, sys_lo, sys_hi;

static const char *where(unsigned long a)
{
    if(a >= rom_lo)
        return "ROM";
    if(a >= sys_lo && a < sys_hi)
        return "System heap";
    return "elsewhere";
}

int main(void)
{
    int i;
    long sysv = 0, mach = 0;
    THz sz = SystemZone();

    freopen("Host:libprobe.txt", "w", stdout);
    Gestalt('sysv', &sysv);
    Gestalt('mach', &mach);
    rom_lo = 0xFF800000UL;
    sys_lo = (unsigned long)sz;
    sys_hi = (unsigned long)sz->bkLim;
    printf("# System %lx, machine %ld, System heap %08lx-%08lx\n", sysv, mach, sys_lo, sys_hi);
    for(i = 0; i < (int)(sizeof libs / sizeof libs[0]); i++)
    {
        Str255 name, err, sym;
        ConnectionID conn = 0;
        Ptr main = NULL, addr = NULL;
        SymClass cls = 0;
        long count = 0, k;
        OSErr found, loaded;
        unsigned long code = 0;

        printf("%-24s ", libs[i]);
        fflush(stdout);
        name[0] = strlen(libs[i]);
        memcpy(name + 1, libs[i], name[0]);
        found = GetSharedLibrary(name, kPowerPCArch, kFindLib, &conn, &main, err);
        loaded = GetSharedLibrary(name, kPowerPCArch, kReferenceCFrag, &conn, &main, err);
        if(loaded == noErr)
        {
            CountSymbols(conn, &count);
            for(k = 0; k < count; k++)
                if(GetIndSymbol(conn, k, sym, &addr, &cls) == noErr && cls == 2 /* kTVectorCFragSymbol */)
                {
                    code = *(unsigned long *)addr;
                    break;
                }
            if(!code && count > 0 && GetIndSymbol(conn, 0, sym, &addr, &cls) == noErr)
                code = (unsigned long)addr;
            printf("found %-5d load %-5d exports %4ld code %08lx %s", found, loaded,
                   count, code, code ? where(code) : "-");
            if(code)
                printf(" (%.*s)", sym[0], sym + 1);
            printf("\n");
            if(found != noErr)
                CloseConnection(&conn);
        }
        else
            printf("found %-5d load %-5d\n", found, loaded);
        fflush(stdout);
    }
    printf("---\n");
    fclose(stdout);
    return 0;
}
