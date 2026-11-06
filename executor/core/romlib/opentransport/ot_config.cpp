/* ot_config.cpp — Open Transport's configuration database
 * (OTLib$CfigLib): what the TCP/IP control panel reads and writes.
 *
 * The database is a resource file (the TCP/IP control panel's "TCP/IP
 * Preferences"): each configuration has an ID, its name is the 'cnam'
 * resource of that ID, and each of its attributes is a resource of that ID
 * whose type names the attribute ('iitf' the interface, 'idns', 'port',
 * 'prot' ...); 'ccfg' 1 holds the active configuration's ID. These calls
 * are private to Open Transport: their arguments were worked out from
 * what the control panel passes and checked against Apple's OT 1.1.1 on
 * the Quadra (docs/executor/OpenTransport.md, "The configuration
 * database"). A database reference is the resource file's refnum, as
 * in Apple's OT: the panel keeps it in a word, and reads the defaults in
 * its own file (the 'srvr' resources: which address fields a server
 * fills in for each user mode, link and method) by passing that file's
 * refnum, so any open resource file is a database. */

#include "ot_internal.h"
#include "ot_provider.h"

#include <FileMgr.h>
#include <MemoryMgr.h>
#include <OSUtil.h>
#include <ResourceMgr.h>
#include <mman/mman.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace Executor;
using namespace Executor::ot;

namespace Executor::ot
{

namespace
{
/* The refnum of an open resource file, from a reference; 0 if it is not
   one. */
INTEGER db_of(uint32_t ref)
{
    if(ref == 0 || ref > 0x7FFF)
        return 0;
    GetResFileAttrs((INTEGER)ref);
    return ResError() == noErr ? (INTEGER)ref : 0;
}
}

/* OSStatus OTOpenConfigurationsDB(const FSSpec *, perm, DBRef *) */
uint32_t cfg_open_db(const Call &c)
{
    FSSpec *spec = c.p<FSSpec>(0);
    int8_t perm = (int8_t)c.l(1);
    GUEST<uint32_t> *out = c.p<GUEST<uint32_t>>(2);
    INTEGER refnum = FSpOpenResFile(spec, perm);
    OSErr err = ResError();
    OT_LOG("OTOpenConfigurationsDB: perm %d -> refnum %d err %d\n", perm, refnum, err);
    if(refnum == -1)
        return err ? err : fnfErr;
    if(out)
        *out = (uint16_t)refnum;
    return noErr;
}

/* OSErr OTCloseConfigurationsDB(DBRef *): the reference is set to -1
   (Apple's OT 1.1.1, CfgTest). */
uint32_t cfg_close_db(const Call &c)
{
    GUEST<uint32_t> *ref = c.p<GUEST<uint32_t>>(0);
    INTEGER db = ref ? db_of(*ref) : 0;
    OT_LOG("OTCloseConfigurationsDB: %d %s\n", db, db ? "closed" : "not open");
    if(!db)
        return paramErr;
    CloseResFile(db);
    OSErr err = ResError();
    *ref = 0xFFFFFFFF;
    return err;
}

namespace
{
/* The attribute's resource in the database's file alone. */
Handle attribute(INTEGER db, uint32_t config, uint32_t type)
{
    GUEST<INTEGER> saved = LM(CurMap);
    UseResFile(db);
    Handle h = Get1Resource(type, (INTEGER)config);
    LM(CurMap) = saved;
    return h;
}
}

/* OSStatus OTGetAttributeValue(DBRef, configID, OSType attribute,
   void *buffer, size): the value's first size bytes; for an attribute the
   configuration doesn't have, noErr and the buffer as it was (Apple's OT
   1.1.1, CfgTest). */
uint32_t cfg_get_attribute_value(const Call &c)
{
    INTEGER db = db_of(c.l(0));
    uint32_t config = c.l(1), type = c.l(2), size = c.l(4);
    uint8_t *buffer = c.p(3);
    Handle h = db ? attribute(db, config, type) : nullptr;
    OT_LOG("OTGetAttributeValue: db %u config %u '%c%c%c%c' size %u -> %s\n", c.l(0), config,
           type >> 24, type >> 16, type >> 8, type, size, !db ? "no such database" : h ? "found" : "missing");
    if(!db)
        return paramErr;
    if(!h)
        return noErr;
    Size n = std::min<Size>(GetHandleSize(h), size);
    if(buffer)
        memcpy(buffer, *h, n);
    return noErr;
}

/* Boolean OTIsPortCompatibleWith(const OTPortRecord *, const char *protocol):
   whether a protocol stack ("tcp", "atlk") can run over the port. An
   Ethernet port (the device in its OTPortRef, OpenTptLinks.h) carries
   both; Executor has no other. */
uint32_t cfg_is_port_compatible(const Call &c)
{
    uint32_t rec = c.l(0);
    std::string protocol = guest_cstring(c.l(1), 16);
    if(!rec)
        return 0;
    unsigned device = (READUL(rec) >> 16) & 0x3FF;
    bool ok = device == 10 /* kOTEthernetDevice */ && (protocol == "tcp" || protocol == "atlk");
    OT_LOG("OTIsPortCompatibleWith: port $%08x \"%s\" -> %d\n", READUL(rec), protocol.c_str(), ok);
    return ok;
}

/* Boolean OTGetUserPortNameFromPortRef(OTPortRef, Str255 name): the name
   the user sees for the port, as a configuration's 'port' resource
   records it ("Ethernet"). */
uint32_t cfg_user_port_name(const Call &c)
{
    uint32_t ref = c.l(0);
    uint8_t *name = c.p(1);
    unsigned device = (ref >> 16) & 0x3FF;
    OT_LOG("OTGetUserPortNameFromPortRef: $%08x\n", ref);
    if(device != 10 /* kOTEthernetDevice */ || !name)
        return 0;
    static const char ethernet[] = "Ethernet";
    name[0] = sizeof ethernet - 1;
    std::memcpy(name + 1, ethernet, sizeof ethernet - 1);
    return 1;
}

/* UInt32 OTGetActiveConfigID(DBRef): the active configuration's ID, from
   the 'ccfg' resource (ID 1); 0 if there is none. */
uint32_t cfg_active_config(const Call &c)
{
    INTEGER db = db_of(c.l(0));
    if(!db)
        return 0;
    Handle h = attribute(db, 1, "ccfg"_4);
    uint32_t id = h && GetHandleSize(h) >= 2 ? (uint16_t)*(GUEST<INTEGER> *)*h : 0;
    OT_LOG("OTGetActiveConfigID: %u\n", id);
    return id;
}

/* UInt32 OTGetAttributeSize(DBRef, configID, OSType attribute): the value's
   size, 0 if the configuration has none. */
uint32_t cfg_attribute_size(const Call &c)
{
    INTEGER db = db_of(c.l(0));
    uint32_t config = c.l(1), type = c.l(2);
    Handle h = db ? attribute(db, config, type) : nullptr;
    uint32_t n = h ? GetHandleSize(h) : 0;
    OT_LOG("OTGetAttributeSize: config %u '%c%c%c%c' -> %u\n", config, type >> 24, type >> 16,
           type >> 8, type, n);
    return n;
}

/* OTPortRef OTGetDefaultPort(device type): the default port of a kind of
   device (OpenTptLinks.h); 0 if there is none. Executor's one Ethernet. */
uint32_t cfg_default_port(const Call &c)
{
    uint32_t device = c.l(0) & 0xFFFF;
    uint32_t ref = device == 10 /* kOTEthernetDevice */ ? enet_port_ref() : 0;
    OT_LOG("OTGetDefaultPort: device %u -> $%08x\n", device, ref);
    return ref;
}

namespace
{
/* Calls on the database's file alone. */
struct InDB
{
    GUEST<INTEGER> saved = LM(CurMap);
    explicit InDB(INTEGER db) { UseResFile(db); }
    ~InDB() { LM(CurMap) = saved; }
};
}

/* UInt32 OTGetConfigsCount(DBRef): how many configurations ('cnam'). */
uint32_t cfg_configs_count(const Call &c)
{
    INTEGER db = db_of(c.l(0));
    if(!db)
        return 0;
    InDB in(db);
    uint32_t n = Count1Resources("cnam"_4);
    OT_LOG("OTGetConfigsCount: %u\n", n);
    return n;
}

/* UInt32 OTGetIndexedConfigID(DBRef, index): the ID of the index-th
   configuration (from 1), 0 if there is none. */
uint32_t cfg_indexed_config(const Call &c)
{
    INTEGER db = db_of(c.l(0));
    uint32_t index = c.l(1);
    if(!db)
        return 0;
    InDB in(db);
    Handle h = Get1IndResource("cnam"_4, (INTEGER)index);
    GUEST<INTEGER> id;
    GUEST<ResType> type;
    Str255 name;
    uint32_t r = 0;
    if(h)
    {
        GetResInfo(h, &id, &type, name);
        r = (uint16_t)(INTEGER)id;
    }
    OT_LOG("OTGetIndexedConfigID: %u -> %u\n", index, r);
    return r;
}

/* OSStatus OTGetConfigName(DBRef, configID, Str255 name): its 'cnam'
   resource's name. */
uint32_t cfg_config_name(const Call &c)
{
    INTEGER db = db_of(c.l(0));
    uint32_t config = c.l(1);
    uint8_t *out = c.p(2);
    if(!db)
        return paramErr;
    Handle h = attribute(db, config, "cnam"_4);
    OT_LOG("OTGetConfigName: %u -> %s\n", config, h ? "found" : "missing");
    if(!h)
        return resNotFound;
    GUEST<INTEGER> id;
    GUEST<ResType> type;
    Str255 name;
    GetResInfo(h, &id, &type, name);
    if(out)
        std::memcpy(out, name, name[0] + 1);
    return noErr;
}

/* UInt32 OTGetConfigID(DBRef, ConstStr255 name): the ID of the
   configuration of that name, 0 if there is none. */
uint32_t cfg_config_id(const Call &c)
{
    INTEGER db = db_of(c.l(0));
    const uint8_t *want = c.p(1);
    if(!db || !want)
        return 0;
    InDB in(db);
    uint32_t r = 0;
    for(INTEGER i = 1, n = Count1Resources("cnam"_4); i <= n && !r; i++)
    {
        Handle h = Get1IndResource("cnam"_4, i);
        GUEST<INTEGER> id;
        GUEST<ResType> type;
        Str255 name;
        if(!h)
            continue;
        GetResInfo(h, &id, &type, name);
        if(EqualString(name, want, false, true))
            r = (uint16_t)(INTEGER)id;
    }
    OT_LOG("OTGetConfigID: \"%.*s\" -> %u\n", want[0], want + 1, r);
    return r;
}

namespace
{
/* Writes a resource of the database's file, made or replaced; size 0
   removes it. */
OSErr put_resource(INTEGER db, uint32_t type, INTEGER id, const void *data, Size size,
                   ConstStringPtr name = nullptr)
{
    InDB in(db);
    Handle h = Get1Resource(type, id);
    if(size == 0 && !name)
    {
        if(h)
        {
            RemoveResource(h);
            if(ResError() == noErr)
                DisposeHandle(h);
        }
        return noErr;
    }
    if(h)
    {
        SetHandleSize(h, size);
        if(MemError() != noErr)
            return MemError();
        if(size)
            std::memcpy(*h, data, size);
        if(name)
            SetResInfo(h, id, name);
        ChangedResource(h);
    }
    else
    {
        h = NewHandle(size);
        if(!h)
            return memFullErr;
        if(size)
            std::memcpy(*h, data, size);
        static const unsigned char none[1] = { 0 };
        AddResource(h, type, id, name ? name : none);
    }
    OSErr err = ResError();
    if(err == noErr)
        WriteResource(h);
    return err ? err : ResError();
}

/* Every resource of the configuration, its name ('cnam') too: (type,
   handle). Not the file's own: 'ccfg' 1 (the panel's temporary
   configuration has ID 1 too) and 'vers'. */
template<typename F>
void each_attribute(INTEGER db, INTEGER config, F f)
{
    InDB in(db);
    for(INTEGER t = 1, nt = Count1Types(); t <= nt; t++)
    {
        GUEST<ResType> type;
        Get1IndType(&type, t);
        if(type == "ccfg"_4 || type == "vers"_4)
            continue;
        Handle h = Get1Resource(type, config);
        if(h)
            f((uint32_t)(ResType)type, h);
    }
}
}

/* OSStatus OTSetAttributeValue(DBRef, configID, OSType attribute,
   const void *value, size): the value, replaced; size 0 (value nil)
   removes it (the panel does that for 'ipcp' and 'alis'). */
uint32_t cfg_set_attribute_value(const Call &c)
{
    INTEGER db = db_of(c.l(0));
    uint32_t config = c.l(1), type = c.l(2), size = c.l(4);
    const uint8_t *value = c.p(3);
    OT_LOG("OTSetAttributeValue: db %u config %u '%c%c%c%c' size %u\n", c.l(0), config, type >> 24,
           type >> 16, type >> 8, type, size);
    if(!db || (size && !value))
        return paramErr;
    return put_resource(db, type, (INTEGER)config, value, size);
}

/* OSStatus OTCreateAConfiguration(DBRef, UInt32 *configID): a new, empty
   configuration (a nameless 'cnam'), the lowest free ID from 128. */
uint32_t cfg_create(const Call &c)
{
    INTEGER db = db_of(c.l(0));
    GUEST<uint32_t> *out = c.p<GUEST<uint32_t>>(1);
    if(!db)
        return paramErr;
    INTEGER id = 128;
    {
        InDB in(db);
        while(Get1Resource("cnam"_4, id))
            id++;
    }
    static const unsigned char unnamed[1] = { 0 };
    OSErr err = put_resource(db, "cnam"_4, id, nullptr, 0, unnamed);
    OT_LOG("OTCreateAConfiguration: %d err %d\n", id, err);
    if(err == noErr && out)
        *out = (uint16_t)id;
    return err;
}

/* OSStatus OTCopyAConfiguration(DBRef from, UInt32 fromID, DBRef to,
   UInt32 toID, ConstStr255 name): every attribute of one configuration
   into another (made with OTCreateAConfiguration), which is given the
   name (nil: the original's). */
uint32_t cfg_copy(const Call &c)
{
    INTEGER src = db_of(c.l(0)), dst = db_of(c.l(2));
    INTEGER from = (INTEGER)c.l(1), to = (INTEGER)c.l(3);
    const uint8_t *name = c.p(4);
    if(!src || !dst)
        return paramErr;
    OSErr err = noErr;
    Str255 original = { 0 };
    each_attribute(src, from, [&](uint32_t type, Handle h) {
        if(type == "cnam"_4)
        {
            GUEST<INTEGER> id;
            GUEST<ResType> t;
            GetResInfo(h, &id, &t, original);
            return;
        }
        /* a copy: the destination may be the same file */
        GUEST<Handle> held = h;
        if(HandToHand(&held) != noErr)
        {
            err = memFullErr;
            return;
        }
        Handle copy = held;
        HLock(copy);
        if(err == noErr)
            err = put_resource(dst, type, to, *copy, GetHandleSize(copy));
        DisposeHandle(copy);
    });
    if(err == noErr)
        err = put_resource(dst, "cnam"_4, to, nullptr, 0, name ? name : original);
    OT_LOG("OTCopyAConfiguration: %d -> %d \"%.*s\" err %d\n", from, to,
           name ? name[0] : original[0], name ? name + 1 : original + 1, err);
    return err;
}

/* OSStatus OTDeleteAConfiguration(DBRef, configID): the configuration's
   attributes and name, gone. */
uint32_t cfg_delete(const Call &c)
{
    INTEGER db = db_of(c.l(0));
    INTEGER config = (INTEGER)c.l(1);
    if(!db)
        return paramErr;
    std::vector<uint32_t> types;
    each_attribute(db, config, [&](uint32_t type, Handle) { types.push_back(type); });
    for(uint32_t type : types)
        put_resource(db, type, config, nullptr, 0);
    OT_LOG("OTDeleteAConfiguration: %d (%zu attributes)\n", config, types.size());
    return noErr;
}

/* OTSetConfigName(DBRef, configID, ConstStr255 name): no result to speak
   of (Apple's OT 1.1.1 leaves 1 in D0, for a configuration that isn't
   there too: CfgTest); noErr here. */
uint32_t cfg_set_name(const Call &c)
{
    INTEGER db = db_of(c.l(0));
    INTEGER config = (INTEGER)c.l(1);
    const uint8_t *name = c.p(2);
    if(!db || !name)
        return paramErr;
    InDB in(db);
    Handle h = Get1Resource("cnam"_4, config);
    OT_LOG("OTSetConfigName: %d \"%.*s\" %s\n", config, name[0], name + 1, h ? "" : "(no such configuration)");
    if(!h)
        return resNotFound;
    SetResInfo(h, config, name);
    return ResError();
}

/* OSStatus OTSetActiveConfigID(DBRef, configID): 'ccfg' 1. */
uint32_t cfg_set_active(const Call &c)
{
    INTEGER db = db_of(c.l(0));
    GUEST<INTEGER> id = (INTEGER)c.l(1);
    OT_LOG("OTSetActiveConfigID: %d\n", (INTEGER)id);
    if(!db)
        return paramErr;
    return put_resource(db, "ccfg"_4, 1, &id, sizeof id);
}

/* Boolean TCPCheckChangeConfigurationConsequences(DBRef, configID):
   whether putting the configuration in force would interrupt TCP/IP
   services (the panel then warns "Saving the changes may interrupt any
   TCP/IP services currently established"). It asks before saving, with
   the edited configuration as a temporary one (ID 1, "TempConfig").
   Nothing is interrupted here: the configuration is stored, not applied
   (TCPChangeConfiguration). */
uint32_t cfg_tcp_check_consequences(const Call &c)
{
    OT_LOG("TCPCheckChangeConfigurationConsequences: db %u config %u -> none\n", c.l(0), c.l(1));
    return 0;
}

/* OSStatus TCPChangeConfiguration(DBRef, configID): put the configuration
   in force in the TCP/IP stack. Executor's networking is the host's: the
   configuration is stored (the user's choice: show and store), not
   applied. */
uint32_t cfg_tcp_change(const Call &c)
{
    OT_LOG("TCPChangeConfiguration: db %u config %u (stored, not applied)\n", c.l(0), c.l(1));
    return db_of(c.l(0)) ? noErr : paramErr;
}

} // namespace Executor::ot
