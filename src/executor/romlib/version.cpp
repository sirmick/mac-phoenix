/* Copyright 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <rsys/version.h>
#include <rsys/gestalt.h>
#include <prefs/prefs.h>

#include <ResourceMgr.h>
#include <MemoryMgr.h>
#include <OSUtil.h>

using namespace Executor;

#if !defined(EXECUTOR_VERSION)
#define EXECUTOR_VERSION ""
#endif

/* A simple version number like "1.99q" */
const char ROMlib_executor_version[] = EXECUTOR_VERSION;

/* A descriptive string like "Executor 1.99q DEMO" */
const char *ROMlib_executor_full_name = "Executor 2000 " EXECUTOR_VERSION;

#define UPDATE_RES(typ, id, fmt, args...)               \
    do {                                                \
        int len;                                        \
        char str[1024];                                 \
        GUEST<INTEGER> save_map;                        \
        Handle h;                                       \
        OSType _typ;                                    \
                                                        \
        _typ = (typ);                                   \
        len = snprintf(str, sizeof str, fmt, args);     \
        save_map = LM(CurMap);                              \
        LM(CurMap) = LM(SysMap);                                \
        h = C_Get1Resource(_typ, (id));                 \
        LM(CurMap) = save_map;                              \
        LoadResource(h);                                \
        HUnlock(h); /* safe to do -- app not running */ \
        SetHandleSize(h, len);                          \
        memcpy(*h, str, len);                     \
        if(_typ == "STR "_4)                        \
            *(char *)*h = len - 1;                \
        else if(_typ == "vers"_4)                   \
            ((char *)*h)[12] = len - 13;          \
    } while(0)

#define UPDATE_VERS(major, minor, rev, vers, fmt, args...)                                                       \
    do {                                                                                                         \
        int _major;                                                                                              \
        int _minor;                                                                                              \
        int _rev;                                                                                                \
                                                                                                                 \
        _major = (major);                                                                                        \
        _minor = (minor);                                                                                        \
        _rev = (rev);                                                                                            \
                                                                                                                 \
        UPDATE_RES("vers"_4, vers, "%c%c\x80%c%c%c\x5%d.%d.%d" fmt, _major, ((_minor << 4) | _rev), 0, 0, 0, \
                   _major, _minor, _rev, args);                                                                  \
    } while(0)

void
ROMlib_set_system_version(uint32_t version)
{
    static uint32_t old_version = -1;

    if(version != old_version)
    {
        int major, minor, rev;
        enum
        {
            MINOR_MASK = 0xF,
            REV_MASK = 0xF
        };

        system_version = version;
        LM(SysVersion) = version;

        major = version >> 8;
        minor = (version >> 4) & MINOR_MASK;
        rev = (version)&REV_MASK;
        gestalt_set_system_version(version);

        UPDATE_RES("STR "_4, 0, "XMacintosh System Software, "
                                    "version %d.%d",
                   major, minor);

        UPDATE_VERS(major, minor, rev, 1, "X%d.%d.%d",
                    major, minor, rev);

        UPDATE_VERS(major, minor, rev, 2, "XSystem %d.%d Version %d.%d.%d",
                    major, minor, major, minor, rev);

        old_version = version;
    }
}
