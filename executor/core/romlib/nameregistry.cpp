/* MacPhoenix: NameRegistryLib (multiversal/defs/NameRegistry.yaml), from
   Designing PCI Cards and Drivers for Power Macintosh Computers, chapter
   10. A Power Mac's Name Registry holds the device tree Open Firmware
   found; Executor has no devices to describe, so its registry is the root
   ("root") and its "Devices" entry, each with its "name" property, and
   nothing below. The answers, errors and edge cases included, are a G3's
   on 8.6 (a probe through CFM, 2026-11-03): iteration that runs out says
   done with noErr, a property that isn't there nrNotFoundErr, an ID from
   RegistryEntryIDInit nrInvalidNodeErr. TattleTech reads the registry on
   a Power Mac (Gestalt 'nreg', gestalt.cpp) and takes its resource file
   on that path only. */

#include <base/common.h>
#include <NameRegistry.h>
#include <MemoryMgr.h>

#include <cstring>
#include <string>
#include <vector>

using namespace Executor;

namespace
{
struct entry
{
    const char *name;
    int parent;
};

/* the root, then its child: an entry's path is its names from below the
   root, joined by colons ("Devices"); the root's is "root" */
const entry entries[] = {
    { "root", -1 },
    { "Devices", 0 },
};
const int nentries = std::size(entries);

/* an entry's ID: as the G3's, $00010000 and a number, then zeros; an ID
   from RegistryEntryIDInit is $FFFE0000 and zeros */
const uint32_t id_tag = 0x00010000, id_invalid = 0xFFFE0000, id_base = 0x00016D80;

int entry_of(const RegEntryID *id)
{
    if(!id || id->contents[0] != id_tag || id->contents[2] != 0 || id->contents[3] != 0)
        return -1;
    uint32_t n = id->contents[1] - id_base;
    return n % 0xC0 == 0 && n / 0xC0 < (uint32_t)nentries ? (int)(n / 0xC0) : -1;
}

void set_id(RegEntryID *id, int e)
{
    id->contents[0] = id_tag;
    id->contents[1] = id_base + 0xC0 * e;
    id->contents[2] = 0;
    id->contents[3] = 0;
}

std::string path_of(int e)
{
    if(e == 0)
        return "root";
    std::string p = entries[e].name;
    for(int a = entries[e].parent; a > 0; a = entries[a].parent)
        p = std::string(entries[a].name) + ":" + p;
    return p;
}

/* the one property an entry has: its name, with its zero */
bool property(int e, const char *name, const void **value, uint32_t *size)
{
    if(!name || strcmp(name, "name") != 0)
        return false;
    *value = entries[e].name;
    *size = strlen(entries[e].name) + 1;
    return true;
}

/* An iterator, in guest memory: the entry the last relationship started
   from, that relationship, how far along it, and the current entry (the
   last one found), where the next relationship starts. */
struct iterator
{
    GUEST<OSType> magic;
    GUEST<int32_t> base;
    GUEST<uint32_t> op;
    GUEST<int32_t> pos;
    GUEST<int32_t> current;
};
const OSType iterator_magic = "NReI"_4, prop_iterator_magic = "NReP"_4;

iterator *iter(RegEntryIter *cookie)
{
    iterator *it = cookie ? (iterator *)(void *)cookie->opaque : nullptr;
    return it && it->magic == iterator_magic ? it : nullptr;
}

void descendants(int e, std::vector<int> &out)
{
    for(int c = 0; c < nentries; c++)
        if(entries[c].parent == e)
        {
            out.push_back(c);
            descendants(c, out);
        }
}

/* the entries a relationship gives from base, in order */
std::vector<int> related(int base, uint32_t op)
{
    std::vector<int> out;
    switch(op)
    {
        case kRegIterRoot:
            out.push_back(0);
            break;
        case kRegIterParents:
            for(int a = entries[base].parent; a >= 0; a = entries[a].parent)
                out.push_back(a);
            break;
        case kRegIterChildren:
            for(int c = 0; c < nentries; c++)
                if(entries[c].parent == base)
                    out.push_back(c);
            break;
        case kRegIterDescendants:
            descendants(base, out);
            break;
        case kRegIterSibling:
            for(int c = 0; c < nentries; c++)
                if(c != base && entries[c].parent == entries[base].parent && entries[base].parent >= 0)
                    out.push_back(c);
            break;
    }
    return out;
}

/* the next entry (matching, for RegistryEntrySearch); done when there's
   none, noErr either way (an unknown relationship finds nothing) */
OSStatus next(RegEntryIter *cookie, RegEntryIterationOp relationship, RegEntryID *found,
              Boolean *done, const char *name, const void *value, uint32_t size)
{
    iterator *it = iter(cookie);
    if(!it)
        return paramErr;
    if(relationship != kRegIterContinue)
    {
        it->base = it->current;
        it->op = relationship;
        it->pos = 0;
    }
    std::vector<int> list = related(it->base, it->op);
    while(it->pos < (int32_t)list.size())
    {
        int e = list[it->pos];
        it->pos = it->pos + 1;
        const void *v;
        uint32_t s;
        if(name && !(property(e, name, &v, &s) && (!value || (s == size && memcmp(v, value, s) == 0))))
            continue;
        it->current = e;
        if(found)
            set_id(found, e);
        if(done)
            *done = false;
        return noErr;
    }
    if(done)
        *done = true;
    return noErr;
}
}

OSStatus Executor::C_RegistryEntryIDInit(RegEntryID *id)
{
    id->contents[0] = id_invalid;
    id->contents[1] = 0;
    id->contents[2] = 0;
    id->contents[3] = 0;
    return noErr;
}

Boolean Executor::C_RegistryEntryIDCompare(const RegEntryID *id1, const RegEntryID *id2)
{
    return memcmp(id1, id2, sizeof *id1) == 0;
}

OSStatus Executor::C_RegistryEntryIDCopy(const RegEntryID *src, RegEntryID *dst)
{
    memmove(dst, src, sizeof *dst);
    return noErr;
}

OSStatus Executor::C_RegistryEntryIDDispose(RegEntryID *id)
{
    memset(id, 0, sizeof *id);
    return noErr;
}

OSStatus Executor::C_RegistryEntryIterateCreate(RegEntryIter *cookie)
{
    iterator *it = (iterator *)NewPtrSysClear(sizeof(iterator));
    if(!it)
        return memFullErr;
    it->magic = iterator_magic;
    cookie->opaque = it;
    return noErr;
}

OSStatus Executor::C_RegistryEntryIterateDispose(RegEntryIter *cookie)
{
    if(iterator *it = iter(cookie))
    {
        it->magic = 0;
        DisposePtr((Ptr)it);
    }
    cookie->opaque = nullptr;
    return noErr;
}

/* an ID that isn't an entry is taken, as on the G3, and changes nothing */
OSStatus Executor::C_RegistryEntryIterateSet(RegEntryIter *cookie, const RegEntryID *startEntryID)
{
    iterator *it = iter(cookie);
    if(!it)
        return paramErr;
    int e = entry_of(startEntryID);
    if(e >= 0)
    {
        it->current = e;
        it->base = e;
        it->op = 0;
        it->pos = 0;
    }
    return noErr;
}

OSStatus Executor::C_RegistryEntryIterate(RegEntryIter *cookie, RegEntryIterationOp relationship,
                                          RegEntryID *foundEntry, Boolean *done)
{
    return next(cookie, relationship, foundEntry, done, nullptr, nullptr, 0);
}

OSStatus Executor::C_RegistryEntrySearch(RegEntryIter *cookie, RegEntryIterationOp relationship,
                                         RegEntryID *foundEntry, Boolean *done,
                                         const RegPropertyName *propertyName,
                                         const void *propertyValue, RegPropertyValueSize propertySize)
{
    return next(cookie, relationship, foundEntry, done, propertyName ? propertyName : "",
                propertyValue, propertySize);
}

OSStatus Executor::C_RegistryEntryToPathSize(const RegEntryID *entryID, GUEST<RegPathNameSize> *pathSize)
{
    int e = entry_of(entryID);
    if(e < 0)
    {
        *pathSize = 0;
        return nrInvalidNodeErr;
    }
    *pathSize = path_of(e).size() + 1;
    return noErr;
}

OSStatus Executor::C_RegistryCStrEntryToPath(const RegEntryID *entryID, RegCStrPathName *pathName,
                                             RegPathNameSize pathSize)
{
    int e = entry_of(entryID);
    if(e < 0)
        return nrInvalidNodeErr;
    std::string p = path_of(e);
    if(p.size() + 1 > pathSize)
        return nrPathBufferTooSmall;
    memcpy(pathName, p.c_str(), p.size() + 1);
    return noErr;
}

/* from the root, names separated by colons (case counts; "" is the root);
   from searchPointID, a path that starts with a colon, as on the G3 */
OSStatus Executor::C_RegistryCStrEntryLookup(const RegEntryID *searchPointID,
                                             const RegCStrPathName *pathName, RegEntryID *foundEntry)
{
    if(!pathName)
        return paramErr;
    int e = 0;
    const char *p = pathName;
    if(searchPointID)
    {
        e = entry_of(searchPointID);
        if(e < 0)
            return nrInvalidNodeErr;
        if(*p != ':')
            return paramErr;
        p++;
    }
    while(*p)
    {
        const char *end = strchr(p, ':');
        size_t len = end ? end - p : strlen(p);
        int c = 0;
        for(; c < nentries; c++)
            if(entries[c].parent == e && strlen(entries[c].name) == len
               && memcmp(entries[c].name, p, len) == 0)
                break;
        if(c == nentries)
            return nrPathNotFound;
        e = c;
        p += len;
        if(*p == ':')
            p++;
    }
    set_id(foundEntry, e);
    return noErr;
}

OSStatus Executor::C_RegistryPropertyGetSize(const RegEntryID *entryID, const RegPropertyName *propertyName,
                                             GUEST<RegPropertyValueSize> *propertySize)
{
    int e = entry_of(entryID);
    if(e < 0)
        return nrInvalidNodeErr;
    const void *v;
    uint32_t s;
    if(!property(e, propertyName, &v, &s))
        return nrNotFoundErr;
    *propertySize = s;
    return noErr;
}

/* a buffer too small gets what fits, noErr, and the value's whole size,
   as on the G3 */
OSStatus Executor::C_RegistryPropertyGet(const RegEntryID *entryID, const RegPropertyName *propertyName,
                                         void *propertyValue, GUEST<RegPropertyValueSize> *propertySize)
{
    int e = entry_of(entryID);
    const void *v;
    uint32_t s;
    if(e < 0)
    {
        *propertySize = 0;
        return nrInvalidNodeErr;
    }
    if(!property(e, propertyName, &v, &s))
    {
        *propertySize = 0;
        return nrNotFoundErr;
    }
    memcpy(propertyValue, v, std::min<uint32_t>(s, *propertySize));
    *propertySize = s;
    return noErr;
}

namespace
{
struct prop_iterator
{
    GUEST<OSType> magic;
    GUEST<int32_t> entry;
    GUEST<int32_t> pos;
};
}

OSStatus Executor::C_RegistryPropertyIterateCreate(const RegEntryID *entry, GUEST<RegPropertyIter> *cookie)
{
    int e = entry_of(entry);
    if(e < 0)
        return nrInvalidNodeErr;
    prop_iterator *it = (prop_iterator *)NewPtrSysClear(sizeof(prop_iterator));
    if(!it)
        return memFullErr;
    it->magic = prop_iterator_magic;
    it->entry = e;
    *cookie = it;
    return noErr;
}

OSStatus Executor::C_RegistryPropertyIterateDispose(GUEST<RegPropertyIter> *cookie)
{
    prop_iterator *it = (prop_iterator *)(void *)*cookie;
    if(it && it->magic == prop_iterator_magic)
    {
        it->magic = 0;
        DisposePtr((Ptr)it);
    }
    *cookie = nullptr;
    return noErr;
}

/* the entry's one property, "name", then done */
OSStatus Executor::C_RegistryPropertyIterate(GUEST<RegPropertyIter> *cookie, RegPropertyName *foundProperty,
                                             Boolean *done)
{
    prop_iterator *it = (prop_iterator *)(void *)*cookie;
    if(!it || it->magic != prop_iterator_magic)
        return paramErr;
    if(it->pos == 0)
    {
        strcpy(foundProperty, "name");
        it->pos = 1;
        *done = false;
    }
    else
        *done = true;
    return noErr;
}
