/*
 * CfgTest.c -- Open Transport's configuration database (OTLib$CfigLib),
 * the private calls the TCP/IP control panel makes, called directly: what
 * Apple's OT 1.1.1 answers (the Quadra, Mac OS 7.6.1) and what Executor's
 * does (romlib/opentransport/ot_config.cpp; docs/executor/OpenTransport.md,
 * "The configuration database").
 *
 * The calls have no public declarations: their arguments were worked out
 * from what the panel passes. 68k code reaches them by index in their
 * function set through the Shared Library Manager, as the panel's stubs do
 * (__OTFunction, custom/OpenTransportGlue.c); PowerPC code imports them
 * from OTConfigLib (looked up by name here). Each is a C routine.
 *
 * One line per check, "PASS name" or "FAIL name: why"; "# ..." lines
 * note an answer; then "---", in Host:cfg_results.txt (the guest suite's
 * harness: tests/test_guest_suite.sh --results cfg_results.txt).
 * Built with Retro68 by build-retro68.sh.
 */

#include <Types.h>
#include <Files.h>
#include <Folders.h>
#include <Resources.h>
#include <TextUtils.h>
#include <OpenTransport.h>
#include <stdio.h>
#include <string.h>

#if defined(__powerc) || defined(__POWERPC__) || defined(powerc)
#include <CodeFragments.h>

/* PowerPC code imports them from OTConfigLib, by name. */
static const char *const names[] = {
    "", "OTCreateConfigurationsDB", "OTOpenConfigurationsDB", "OTCloseConfigurationsDB",
    "OTCreateAConfiguration", "OTCopyAConfiguration", "OTDeleteAConfiguration",
    "OTGetConfigsCount", "OTGetIndexedConfigID", "OTGetActiveConfigID",
    "TCPCheckChangeConfigurationConsequences", "TCPChangeConfiguration", "OTSetActiveConfigID",
    "OTGetConfigName", "OTSetConfigName", "OTGetConfigID", "OTGetAttributesCount",
    "OTGetIndexedAttributeID", "OTSetAttributeValueHandle", "OTGetAttributeSize",
    "OTGetAttributeValueHandle", "OTSetAttributeValue", "OTGetAttributeValue",
};

static void *config_function(unsigned long index)
{
    static ConnectionID lib;
    Str255 name, err;
    Ptr main, addr = NULL;
    SymClass cls;

    if(!lib && GetSharedLibrary("\pOTConfigLib", kPowerPCArch, kLoadLib, &lib, &main, err))
        return NULL;
    name[0] = strlen(names[index]);
    memcpy(name + 1, names[index], name[0]);
    FindSymbol(lib, name, &addr, &cls);
    return addr;
}
#define F(type, index) ((type)config_function(index))
#else
/* 68k code reaches them by index in their function set through the
   Shared Library Manager, as the panel's stubs do. */
void *__OTFunction(const char *set, unsigned long index);
#define F(type, index) ((type)__OTFunction("OTLib$CfigLib", index))
#endif

typedef OSStatus (*OpenDB)(const FSSpec *, long perm, UInt32 *db);
typedef OSStatus (*CloseDB)(UInt32 *db);
typedef OSStatus (*CreateConfig)(UInt32 db, UInt32 *config);
typedef OSStatus (*CopyConfig)(UInt32 from, UInt32 fromConfig, UInt32 to, UInt32 toConfig,
                               ConstStringPtr name);
typedef OSStatus (*DbConfig)(UInt32 db, UInt32 config); /* delete, set active */
typedef UInt32 (*DbCount)(UInt32 db);                   /* count, active */
typedef UInt32 (*IndexedConfig)(UInt32 db, UInt32 index);
typedef unsigned char (*Consequences)(UInt32 db, UInt32 config);
typedef void (*ConfigName)(UInt32 db, UInt32 config, StringPtr name);
typedef void (*SetName)(UInt32 db, UInt32 config, ConstStringPtr name);
typedef UInt32 (*ConfigID)(UInt32 db, ConstStringPtr name);
typedef UInt32 (*AttributeSize)(UInt32 db, UInt32 config, OSType type);
typedef OSStatus (*AttributeValue)(UInt32 db, UInt32 config, OSType type, void *buf, UInt32 size);

static int failures;

static void pass(const char *name) { printf("PASS %s\n", name); }

static void fail(const char *name, const char *why, long code)
{
    printf("FAIL %s: %s (%ld)\n", name, why, code);
    failures++;
}

static void check(const char *name, int ok, const char *why, long code)
{
    if(ok)
        pass(name);
    else
        fail(name, why, code);
}

static const char *cstr(ConstStringPtr p)
{
    static char s[256];
    memcpy(s, p + 1, p[0]);
    s[p[0]] = 0;
    return s;
}

static Boolean is_res_file(UInt32 db)
{
    if(db == 0 || db > 0x7FFF)
        return false;
    GetResFileAttrs((short)db);
    return ResError() == noErr;
}

static UInt32 prefs_active;

/* The TCP/IP Preferences, read only. */
static void test_read(const FSSpec *prefs)
{
    UInt32 db = 0, count, active, first, none, id, size;
    OSStatus err;
    Str255 name;
    unsigned char buf[128], rawbuf[128];
    Handle raw;
    long rawsize;
    short saved = CurResFile();

    err = F(OpenDB, 2)(prefs, fsRdPerm, &db);
    printf("# open: err %ld db %lu\n", (long)err, (unsigned long)db);
    if(err)
    {
        fail("open", "OTOpenConfigurationsDB", err);
        return;
    }
    pass("open");
    /* The panel keeps a database in a word and passes its own resource
       file's refnum for one. */
    check("dbref_is_refnum", is_res_file(db), "not an open resource file's refnum", db);

    count = F(DbCount, 7)(db);
    active = F(DbCount, 9)(db);
    first = F(IndexedConfig, 8)(db, 1);
    none = F(IndexedConfig, 8)(db, 0);
    printf("# count %lu active %lu indexed(1) %lu indexed(0) %lu\n", (unsigned long)count,
           (unsigned long)active, (unsigned long)first, (unsigned long)none);
    check("count", count >= 1, "no configuration", count);
    check("indexed_from_1", first == active && none == 0, "index 1 is not the first", first);
    prefs_active = active;

    /* No result to speak of: PowerPC's r3 is left as it was. */
    name[0] = 0;
    F(ConfigName, 13)(db, active, name);
    printf("# name \"%s\"\n", cstr(name));
    check("name", name[0] > 0, "OTGetConfigName", 0);

    id = F(ConfigID, 15)(db, name);
    check("config_id", id == active, "OTGetConfigID of its name", id);
    /* "Default" in other case: */
    id = F(ConfigID, 15)(db, "\pDEFAULT");
    printf("# config_id(\"DEFAULT\") %lu\n", (unsigned long)id);
    id = F(ConfigID, 15)(db, "\pNo Such Configuration");
    check("config_id_missing", id == 0, "a configuration that isn't there", id);

    /* The resource itself, copied first: the database's calls release
       it (Apple's OT does). */
    UseResFile((short)db);
    raw = Get1Resource('iitf', (short)active);
    UseResFile(saved);
    rawsize = raw ? GetHandleSize(raw) : 0;
    if(raw)
        memcpy(rawbuf, *raw, rawsize < sizeof rawbuf ? rawsize : sizeof rawbuf);
    size = F(AttributeSize, 19)(db, active, 'iitf');
    check("attribute_size", raw && size == rawsize, "OTGetAttributeSize 'iitf'", size);
    memset(buf, 0xEE, sizeof buf);
    if(size > sizeof buf)
        size = sizeof buf;
    err = F(AttributeValue, 22)(db, active, 'iitf', buf, size);
    check("attribute_value", err == noErr && raw && memcmp(buf, rawbuf, size) == 0,
          "OTGetAttributeValue 'iitf' isn't the resource", err);
    /* An attribute the configuration hasn't: noErr, the buffer untouched. */
    memset(buf, 0xEE, sizeof buf);
    err = F(AttributeValue, 22)(db, active, 'zzzz', buf, 4);
    size = F(AttributeSize, 19)(db, active, 'zzzz');
    check("attribute_missing", err == noErr && size == 0 && buf[0] == 0xEE,
          "an attribute that isn't there", err);

    printf("# consequences of the active configuration: %d\n", (int)F(Consequences, 10)(db, active));

    /* An OSErr (a word), and the reference set to -1. */
    err = F(CloseDB, 3)(&db);
    check("close", (short)err == noErr && db == 0xFFFFFFFF, "OTCloseConfigurationsDB", db);
}

/* A database of the test's own, written: a resource file the test makes
   and opens itself, as the panel does to export a configuration. */
static void test_write(const FSSpec *prefs)
{
    FSSpec spec;
    short ref;
    UInt32 src = 0, a = 0, b = 0, id;
    OSStatus err;
    Str255 name;
    char buf[8];

    FSMakeFSSpec(0, 0, "\pHost:CfgTest DB", &spec);
    FSpDelete(&spec);
    FSpCreateResFile(&spec, 'ztcp', 'pref', smSystemScript);
    ref = FSpOpenResFile(&spec, fsRdWrPerm);
    if(ref == -1)
    {
        fail("scratch", "FSpOpenResFile", ResError());
        return;
    }
    if(F(OpenDB, 2)(prefs, fsRdPerm, &src) != noErr)
    {
        fail("scratch", "OTOpenConfigurationsDB", 0);
        CloseResFile(ref);
        return;
    }

    err = F(CreateConfig, 4)(ref, &a);
    printf("# create: err %ld config %lu, count %lu\n", (long)err, (unsigned long)a,
           (unsigned long)F(DbCount, 7)(ref));
    check("create", err == noErr && a != 0, "OTCreateAConfiguration", err);

    err = F(CopyConfig, 5)(src, prefs_active, ref, a, "\pProbe");
    check("copy", err == noErr, "OTCopyAConfiguration", err);
    name[0] = 0;
    F(ConfigName, 13)(ref, a, name);
    check("copy_name", EqualString(name, "\pProbe", true, true), cstr(name), 0);
    check("copy_attributes", F(AttributeSize, 19)(ref, a, 'iitf') == F(AttributeSize, 19)(src, prefs_active, 'iitf'),
          "'iitf' not copied", F(AttributeSize, 19)(ref, a, 'iitf'));

    F(SetName, 14)(ref, a, "\pRenamed");
    name[0] = 0;
    F(ConfigName, 13)(ref, a, name);
    /* No result to speak of: Apple's leaves 1 in D0, for a configuration
       that isn't there too. */
    check("rename", EqualString(name, "\pRenamed", true, true), cstr(name), 0);
    F(SetName, 14)(ref, 999, "\pNobody");
    check("rename_missing", F(ConfigID, 15)(ref, "\pNobody") == 0 && F(DbCount, 7)(ref) == 1,
          "renaming a configuration that isn't there made one", 0);
    check("config_id_renamed", F(ConfigID, 15)(ref, "\pRenamed") == a, "OTGetConfigID", 0);

    err = F(AttributeValue, 21)(ref, a, 'test', "abc", 3);
    memset(buf, 0, sizeof buf);
    check("set_attribute", err == noErr && F(AttributeSize, 19)(ref, a, 'test') == 3
          && F(AttributeValue, 22)(ref, a, 'test', buf, 3) == noErr && memcmp(buf, "abc", 3) == 0,
          "OTSetAttributeValue", err);
    err = F(AttributeValue, 21)(ref, a, 'test', NULL, 0);
    printf("# set size 0: err %ld, size now %lu\n", (long)err, (unsigned long)F(AttributeSize, 19)(ref, a, 'test'));
    check("set_attribute_empty", err == noErr && F(AttributeSize, 19)(ref, a, 'test') == 0,
          "size 0 should leave none", err);

    err = F(CreateConfig, 4)(ref, &b);
    printf("# second create: err %ld config %lu\n", (long)err, (unsigned long)b);
    check("create_second", err == noErr && b != 0 && b != a, "OTCreateAConfiguration", err);
    err = F(DbConfig, 12)(ref, b);
    id = F(DbCount, 9)(ref);
    check("set_active", err == noErr && id == b, "OTSetActiveConfigID", id);
    err = F(DbConfig, 6)(ref, b);
    id = F(DbCount, 7)(ref);
    printf("# delete: err %ld, count now %lu, active %lu\n", (long)err, (unsigned long)id,
           (unsigned long)F(DbCount, 9)(ref));
    check("delete", err == noErr && F(ConfigID, 15)(ref, "\pRenamed") == a && F(IndexedConfig, 8)(ref, 2) == 0,
          "OTDeleteAConfiguration", err);

    F(CloseDB, 3)(&src);
    CloseResFile(ref);
    FSpDelete(&spec);
}

int main(void)
{
    FSSpec prefs;
    short vref;
    long dir;
    OSStatus err;

    freopen("Host:cfg_results.txt", "w", stdout);
    err = InitOpenTransport();
    if(err)
        fail("init", "InitOpenTransport", err);
    else if(FindFolder(kOnSystemDisk, kPreferencesFolderType, kDontCreateFolder, &vref, &dir)
            || FSMakeFSSpec(vref, dir, "\pTCP/IP Preferences", &prefs))
        fail("prefs", "no TCP/IP Preferences", 0);
    else
    {
        test_read(&prefs);
        fflush(stdout);
        test_write(&prefs);
    }
    if(!err)
        CloseOpenTransport();
    printf("DONE %d\n---\n", failures);
    fclose(stdout);
    return 0;
}
