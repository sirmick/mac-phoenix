/* MacPhoenix: the Collection Manager (Collections.yaml), from Inside
 * Macintosh: QuickDraw GX Environment and Utilities ("Collection Manager";
 * docs/executor/CollectionManager.md) and checked against Apple's on Mac OS
 * 9.0.4 by CollectionTest (tests/guest/collections):
 *
 * - Items are kept in tag order, IDs (signed) in order within a tag: an
 *   item's index, and a tag's, is its place in that order, from 1.
 * - Adding an item whose tag and ID exist replaces its data and keeps its
 *   attributes; a new item gets the collection's default attributes
 *   ($40000000, persistent, to start with).
 * - The lock bit protects an item's data only: replacing it is
 *   collectionItemLockedErr; removing, purging, emptying and changing its
 *   attributes (unlocking it too) are not refused.
 * - A size given to the Get routines is how much to copy at most; the
 *   item's own size is returned.
 * - The exception procedure, if any, is called with each error, and what
 *   it returns is the routine's result.
 * - Flattening writes the persistent items ('flac' in Collections.r):
 *   version $00010000, the default attributes, the count, per item its tag,
 *   ID, attributes and the offset of its data in the data block (0 for an
 *   empty item, which has no entry there), the data block's size, then the
 *   block, each entry its size and data padded to even length.
 *   Unflattening reads the items through their offsets, into a collection
 *   emptied first, and takes the default attributes from the stream.
 *
 * A collection is a block of the system heap, standing for Executor's
 * record of it. */

#include <base/common.h>
#include <Collections.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <FileMgr.h>
#include <mman/mman.h>
#include <base/functions.impl.h>
#include <algorithm>
#include <map>
#include <vector>

using namespace Executor;

namespace
{
constexpr uint32_t lockBit = 0x80000000;
constexpr uint32_t persistenceBit = 0x40000000;
constexpr uint32_t flattenedVersion = 0x00010000;

struct Item
{
    uint32_t tag;
    int32_t id;
    uint32_t attributes;
    std::vector<uint8_t> data;
};

struct CollectionRecord
{
    std::vector<Item> items; /* by tag, then ID */
    uint32_t defaultAttributes = persistenceBit;
    int32_t owners = 1;
    CollectionExceptionUPP exceptionProc = nullptr;
};

std::map<Collection, CollectionRecord> collections;

CollectionRecord *record(Collection c)
{
    auto it = collections.find(c);
    return it == collections.end() ? nullptr : &it->second;
}

bool before(const Item& item, uint32_t tag, int32_t id)
{
    return item.tag < tag || (item.tag == tag && item.id < id);
}

Item *find(CollectionRecord& r, uint32_t tag, int32_t id, int32_t *index = nullptr)
{
    auto it = std::lower_bound(r.items.begin(), r.items.end(), std::make_pair(tag, id),
        [](const Item& item, const std::pair<uint32_t, int32_t>& key) {
            return before(item, key.first, key.second);
        });
    if(it == r.items.end() || it->tag != tag || it->id != id)
        return nullptr;
    if(index)
        *index = it - r.items.begin() + 1;
    return &*it;
}

Item *indexed(CollectionRecord& r, int32_t index)
{
    return index >= 1 && index <= (int32_t)r.items.size() ? &r.items[index - 1] : nullptr;
}

/* The whichItem'th item with tag (from 1), or nil. */
Item *tagged(CollectionRecord& r, uint32_t tag, int32_t whichItem, int32_t *index = nullptr)
{
    int32_t n = 0;
    for(size_t i = 0; i < r.items.size(); i++)
        if(r.items[i].tag == tag && ++n == whichItem)
        {
            if(index)
                *index = i + 1;
            return &r.items[i];
        }
    return nullptr;
}

std::vector<uint32_t> tags(const CollectionRecord& r)
{
    std::vector<uint32_t> out;
    for(const Item& item : r.items)
        if(out.empty() || out.back() != item.tag)
            out.push_back(item.tag);
    return out;
}

/* An error, through the collection's exception procedure. */
OSErr fail(Collection c, OSErr err)
{
    CollectionRecord *r = record(c);
    if(r && r->exceptionProc)
        return r->exceptionProc(c, err);
    return err;
}

void setData(Item& item, int32_t size, const void *data)
{
    item.data.assign(size, 0);
    if(size > 0 && data)
        memcpy(item.data.data(), data, size);
}

OSErr add(Collection c, uint32_t tag, int32_t id, int32_t size, const void *data)
{
    CollectionRecord *r = record(c);
    if(!r || size < 0)
        return fail(c, paramErr);
    if(Item *item = find(*r, tag, id))
    {
        if(item->attributes & lockBit)
            return fail(c, collectionItemLockedErr);
        setData(*item, size, data);
        return noErr;
    }
    Item item { tag, id, r->defaultAttributes, {} };
    setData(item, size, data);
    auto at = std::lower_bound(r->items.begin(), r->items.end(), item,
        [](const Item& a, const Item& b) { return before(a, b.tag, b.id); });
    r->items.insert(at, std::move(item));
    return noErr;
}

/* An item's data to the caller: at most *size bytes if a size is given. */
void copyOut(const Item& item, GUEST<LONGINT> *size, void *data)
{
    int32_t n = item.data.size();
    if(data)
    {
        int32_t want = size ? std::min<int32_t>(*size, n) : n;
        if(want > 0)
            memcpy(data, item.data.data(), want);
    }
    if(size)
        *size = n;
}

OSErr copyToHandle(Collection c, const Item& item, Handle h)
{
    SetHandleSize(h, item.data.size());
    if(OSErr err = MemError())
        return fail(c, err);
    if(!item.data.empty())
        memcpy(*h, item.data.data(), item.data.size());
    return noErr;
}

void put32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(v >> 24);
    out.push_back(v >> 16);
    out.push_back(v >> 8);
    out.push_back(v);
}

uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

std::vector<uint8_t> flatten(const CollectionRecord& r, uint32_t which, uint32_t matching)
{
    std::vector<const Item *> items;
    for(const Item& item : r.items)
        if((item.attributes & persistenceBit) && (item.attributes & which) == (matching & which))
            items.push_back(&item);

    std::vector<uint8_t> block;
    std::vector<uint32_t> offsets;
    for(const Item *item : items)
    {
        if(item->data.empty())
        {
            offsets.push_back(0);
            continue;
        }
        put32(block, item->data.size());
        offsets.push_back(block.size());
        block.insert(block.end(), item->data.begin(), item->data.end());
        if(block.size() & 1)
            block.push_back(0);
    }

    std::vector<uint8_t> out;
    put32(out, flattenedVersion);
    put32(out, r.defaultAttributes);
    put32(out, items.size());
    for(size_t i = 0; i < items.size(); i++)
    {
        put32(out, items[i]->tag);
        put32(out, items[i]->id);
        put32(out, items[i]->attributes);
        put32(out, offsets[i]);
    }
    put32(out, block.size());
    out.insert(out.end(), block.begin(), block.end());
    return out;
}

/* The stream through the caller's procedure, from a block of guest
   memory. */
OSErr giveStream(Collection c, const std::vector<uint8_t>& stream, CollectionFlattenUPP proc,
                 void *refCon)
{
    if(stream.empty())
        return noErr;
    Ptr buffer = NewPtr(stream.size());
    if(!buffer)
        return fail(c, memFullErr);
    memcpy(buffer, stream.data(), stream.size());
    OSErr err = proc(stream.size(), buffer, refCon);
    DisposePtr(buffer);
    return err ? fail(c, err) : noErr;
}

/* The items of a flattened collection into c, emptied first. `read` gives
   the next n bytes (false: the stream's error, in err). */
template<typename Read>
OSErr unflatten(Collection c, Read read)
{
    CollectionRecord *r = record(c);
    if(!r)
        return fail(c, paramErr);
    std::vector<uint8_t> header(12);
    OSErr err = noErr;
    if(!read(header.data(), 12, err))
        return fail(c, err);
    if(get32(&header[0]) != flattenedVersion)
        return fail(c, collectionVersionErr);
    uint32_t count = get32(&header[8]);
    if(count > 0x100000)
        return fail(c, paramErr);
    std::vector<uint8_t> table(count * 16 + 4);
    if(!read(table.data(), table.size(), err))
        return fail(c, err);
    uint32_t blockSize = get32(&table[count * 16]);
    if(blockSize > 0x4000000)
        return fail(c, paramErr);
    std::vector<uint8_t> block(blockSize);
    if(blockSize && !read(block.data(), blockSize, err))
        return fail(c, err);

    std::vector<Item> items;
    for(uint32_t i = 0; i < count; i++)
    {
        const uint8_t *e = &table[i * 16];
        Item item { get32(e), (int32_t)get32(e + 4), get32(e + 8), {} };
        uint32_t offset = get32(e + 12);
        if(offset >= 4)
        {
            if(offset > blockSize)
                return fail(c, paramErr);
            uint32_t size = get32(&block[offset - 4]);
            if(size > blockSize - offset)
                return fail(c, paramErr);
            item.data.assign(block.begin() + offset, block.begin() + offset + size);
        }
        items.push_back(std::move(item));
    }
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return before(a, b.tag, b.id); });
    r->items = std::move(items);
    r->defaultAttributes = get32(&header[4]);
    return noErr;
}
}

Collection Executor::C_NewCollection()
{
    Ptr p;
    {
        TheZoneGuard guard(LM(SysZone));
        p = NewPtrClear(4);
    }
    if(!p)
        return nullptr;
    Collection c = (Collection)p;
    collections[c] = CollectionRecord();
    return c;
}

void Executor::C_DisposeCollection(Collection c)
{
    CollectionRecord *r = record(c);
    if(!r || --r->owners > 0)
        return;
    collections.erase(c);
    DisposePtr((Ptr)c);
}

Collection Executor::C_CloneCollection(Collection c)
{
    if(CollectionRecord *r = record(c))
        r->owners++;
    return c;
}

LONGINT Executor::C_CountCollectionOwners(Collection c)
{
    CollectionRecord *r = record(c);
    return r ? r->owners : 0;
}

Collection Executor::C_CopyCollection(Collection srcCollection, Collection dstCollection)
{
    CollectionRecord *src = record(srcCollection);
    if(!src)
        return nullptr;
    if(!dstCollection)
        dstCollection = C_NewCollection();
    CollectionRecord *dst = record(dstCollection);
    if(!dst)
        return nullptr;
    if(dst != src)
    {
        dst->items = src->items;
        dst->defaultAttributes = src->defaultAttributes;
    }
    return dstCollection;
}

LONGINT Executor::C_GetCollectionDefaultAttributes(Collection c)
{
    CollectionRecord *r = record(c);
    return r ? r->defaultAttributes : 0;
}

void Executor::C_SetCollectionDefaultAttributes(Collection c, LONGINT whichAttributes, LONGINT newAttributes)
{
    if(CollectionRecord *r = record(c))
        r->defaultAttributes = (r->defaultAttributes & ~whichAttributes) | (newAttributes & whichAttributes);
}

LONGINT Executor::C_CountCollectionItems(Collection c)
{
    CollectionRecord *r = record(c);
    return r ? r->items.size() : 0;
}

OSErr Executor::C_AddCollectionItem(Collection c, CollectionTag tag, LONGINT id, LONGINT itemSize,
                                    const void *itemData)
{
    return add(c, tag, id, itemSize, itemData);
}

OSErr Executor::C_GetCollectionItem(Collection c, CollectionTag tag, LONGINT id,
                                    GUEST<LONGINT> *itemSize, void *itemData)
{
    CollectionRecord *r = record(c);
    Item *item = r ? find(*r, tag, id) : nullptr;
    if(!item)
        return fail(c, collectionItemNotFoundErr);
    copyOut(*item, itemSize, itemData);
    return noErr;
}

OSErr Executor::C_RemoveCollectionItem(Collection c, CollectionTag tag, LONGINT id)
{
    CollectionRecord *r = record(c);
    int32_t index;
    if(!r || !find(*r, tag, id, &index))
        return fail(c, collectionItemNotFoundErr);
    r->items.erase(r->items.begin() + index - 1);
    return noErr;
}

OSErr Executor::C_SetCollectionItemInfo(Collection c, CollectionTag tag, LONGINT id,
                                        LONGINT whichAttributes, LONGINT newAttributes)
{
    CollectionRecord *r = record(c);
    Item *item = r ? find(*r, tag, id) : nullptr;
    if(!item)
        return fail(c, collectionItemNotFoundErr);
    item->attributes = (item->attributes & ~whichAttributes) | (newAttributes & whichAttributes);
    return noErr;
}

OSErr Executor::C_GetCollectionItemInfo(Collection c, CollectionTag tag, LONGINT id,
                                        GUEST<LONGINT> *index, GUEST<LONGINT> *itemSize,
                                        GUEST<LONGINT> *attributes)
{
    CollectionRecord *r = record(c);
    int32_t i;
    Item *item = r ? find(*r, tag, id, &i) : nullptr;
    if(!item)
        return fail(c, collectionItemNotFoundErr);
    if(index)
        *index = i;
    if(itemSize)
        *itemSize = item->data.size();
    if(attributes)
        *attributes = item->attributes;
    return noErr;
}

OSErr Executor::C_ReplaceIndexedCollectionItem(Collection c, LONGINT index, LONGINT itemSize,
                                               const void *itemData)
{
    CollectionRecord *r = record(c);
    Item *item = r ? indexed(*r, index) : nullptr;
    if(!item)
        return fail(c, collectionIndexRangeErr);
    if(item->attributes & lockBit)
        return fail(c, collectionItemLockedErr);
    if(itemSize < 0)
        return fail(c, paramErr);
    setData(*item, itemSize, itemData);
    return noErr;
}

OSErr Executor::C_GetIndexedCollectionItem(Collection c, LONGINT index, GUEST<LONGINT> *itemSize,
                                           void *itemData)
{
    CollectionRecord *r = record(c);
    Item *item = r ? indexed(*r, index) : nullptr;
    if(!item)
        return fail(c, collectionIndexRangeErr);
    copyOut(*item, itemSize, itemData);
    return noErr;
}

OSErr Executor::C_RemoveIndexedCollectionItem(Collection c, LONGINT index)
{
    CollectionRecord *r = record(c);
    if(!r || !indexed(*r, index))
        return fail(c, collectionIndexRangeErr);
    r->items.erase(r->items.begin() + index - 1);
    return noErr;
}

OSErr Executor::C_SetIndexedCollectionItemInfo(Collection c, LONGINT index, LONGINT whichAttributes,
                                               LONGINT newAttributes)
{
    CollectionRecord *r = record(c);
    Item *item = r ? indexed(*r, index) : nullptr;
    if(!item)
        return fail(c, collectionIndexRangeErr);
    item->attributes = (item->attributes & ~whichAttributes) | (newAttributes & whichAttributes);
    return noErr;
}

OSErr Executor::C_GetIndexedCollectionItemInfo(Collection c, LONGINT index, GUEST<CollectionTag> *tag,
                                               GUEST<LONGINT> *id, GUEST<LONGINT> *itemSize,
                                               GUEST<LONGINT> *attributes)
{
    CollectionRecord *r = record(c);
    Item *item = r ? indexed(*r, index) : nullptr;
    if(!item)
        return fail(c, collectionIndexRangeErr);
    if(tag)
        *tag = item->tag;
    if(id)
        *id = item->id;
    if(itemSize)
        *itemSize = item->data.size();
    if(attributes)
        *attributes = item->attributes;
    return noErr;
}

Boolean Executor::C_CollectionTagExists(Collection c, CollectionTag tag)
{
    CollectionRecord *r = record(c);
    return r && tagged(*r, tag, 1);
}

LONGINT Executor::C_CountCollectionTags(Collection c)
{
    CollectionRecord *r = record(c);
    return r ? tags(*r).size() : 0;
}

OSErr Executor::C_GetIndexedCollectionTag(Collection c, LONGINT tagIndex, GUEST<CollectionTag> *tag)
{
    CollectionRecord *r = record(c);
    if(!r)
        return fail(c, collectionIndexRangeErr);
    std::vector<uint32_t> all = tags(*r);
    if(tagIndex < 1 || tagIndex > (int32_t)all.size())
        return fail(c, collectionIndexRangeErr);
    if(tag)
        *tag = all[tagIndex - 1];
    return noErr;
}

LONGINT Executor::C_CountTaggedCollectionItems(Collection c, CollectionTag tag)
{
    CollectionRecord *r = record(c);
    if(!r)
        return 0;
    return std::count_if(r->items.begin(), r->items.end(),
                         [tag](const Item& item) { return item.tag == tag; });
}

OSErr Executor::C_GetTaggedCollectionItem(Collection c, CollectionTag tag, LONGINT whichItem,
                                          GUEST<LONGINT> *itemSize, void *itemData)
{
    CollectionRecord *r = record(c);
    Item *item = r ? tagged(*r, tag, whichItem) : nullptr;
    if(!item)
        return fail(c, collectionIndexRangeErr);
    copyOut(*item, itemSize, itemData);
    return noErr;
}

OSErr Executor::C_GetTaggedCollectionItemInfo(Collection c, CollectionTag tag, LONGINT whichItem,
                                              GUEST<LONGINT> *id, GUEST<LONGINT> *index,
                                              GUEST<LONGINT> *itemSize, GUEST<LONGINT> *attributes)
{
    CollectionRecord *r = record(c);
    int32_t i;
    Item *item = r ? tagged(*r, tag, whichItem, &i) : nullptr;
    if(!item)
        return fail(c, collectionIndexRangeErr);
    if(id)
        *id = item->id;
    if(index)
        *index = i;
    if(itemSize)
        *itemSize = item->data.size();
    if(attributes)
        *attributes = item->attributes;
    return noErr;
}

void Executor::C_PurgeCollection(Collection c, LONGINT whichAttributes, LONGINT matchingAttributes)
{
    CollectionRecord *r = record(c);
    if(!r)
        return;
    uint32_t which = whichAttributes, matching = matchingAttributes & whichAttributes;
    r->items.erase(std::remove_if(r->items.begin(), r->items.end(),
        [&](const Item& item) { return (item.attributes & which) == matching; }), r->items.end());
}

void Executor::C_PurgeCollectionTag(Collection c, CollectionTag tag)
{
    CollectionRecord *r = record(c);
    if(!r)
        return;
    r->items.erase(std::remove_if(r->items.begin(), r->items.end(),
        [tag](const Item& item) { return item.tag == tag; }), r->items.end());
}

void Executor::C_EmptyCollection(Collection c)
{
    if(CollectionRecord *r = record(c))
        r->items.clear();
}

OSErr Executor::C_FlattenCollection(Collection c, CollectionFlattenUPP flattenProc, void *refCon)
{
    CollectionRecord *r = record(c);
    if(!r)
        return fail(c, paramErr);
    return giveStream(c, flatten(*r, 0, 0), flattenProc, refCon);
}

OSErr Executor::C_FlattenPartialCollection(Collection c, CollectionFlattenUPP flattenProc, void *refCon,
                                           LONGINT whichAttributes, LONGINT matchingAttributes)
{
    CollectionRecord *r = record(c);
    if(!r)
        return fail(c, paramErr);
    return giveStream(c, flatten(*r, whichAttributes, matchingAttributes), flattenProc, refCon);
}

OSErr Executor::C_UnflattenCollection(Collection c, CollectionFlattenUPP flattenProc, void *refCon)
{
    return unflatten(c, [&](uint8_t *out, uint32_t n, OSErr& err) {
        Ptr buffer = NewPtr(n);
        if(!buffer)
        {
            err = memFullErr;
            return false;
        }
        err = flattenProc(n, buffer, refCon);
        if(!err)
            memcpy(out, buffer, n);
        DisposePtr(buffer);
        return err == noErr;
    });
}

CollectionExceptionUPP Executor::C_GetCollectionExceptionProc(Collection c)
{
    CollectionRecord *r = record(c);
    return r ? r->exceptionProc : nullptr;
}

void Executor::C_SetCollectionExceptionProc(Collection c, CollectionExceptionUPP exceptionProc)
{
    if(CollectionRecord *r = record(c))
        r->exceptionProc = exceptionProc;
}

/* A 'cltn' resource (Collections.r): the count, then per item its tag,
   ID, attributes and data (a word of size, the bytes, padded to even). */
Collection Executor::C_GetNewCollection(INTEGER collectionID)
{
    Handle h = GetResource("cltn"_4, collectionID);
    if(!h)
        return nullptr;
    Collection c = C_NewCollection();
    if(!c)
        return nullptr;
    int32_t size = GetHandleSize(h);
    const uint8_t *p = (const uint8_t *)*h;
    int32_t at = 4;
    uint32_t count = size >= 4 ? get32(p) : 0;
    for(uint32_t i = 0; i < count && at + 14 <= size; i++)
    {
        uint32_t tag = get32(p + at), attributes = get32(p + at + 8);
        int32_t id = get32(p + at + 4);
        int32_t n = p[at + 12] << 8 | p[at + 13];
        at += 14;
        if(at + n > size)
            break;
        add(c, tag, id, n, p + at);
        if(Item *item = find(*record(c), tag, id))
            item->attributes = attributes;
        at += n + (n & 1);
    }
    return c;
}

OSErr Executor::C_AddCollectionItemHdl(Collection aCollection, CollectionTag tag, LONGINT id,
                                       Handle itemData)
{
    if(!itemData)
        return add(aCollection, tag, id, 0, nullptr);
    return add(aCollection, tag, id, GetHandleSize(itemData), *itemData);
}

OSErr Executor::C_GetCollectionItemHdl(Collection aCollection, CollectionTag tag, LONGINT id,
                                       Handle itemData)
{
    CollectionRecord *r = record(aCollection);
    Item *item = r ? find(*r, tag, id) : nullptr;
    if(!item)
        return fail(aCollection, collectionItemNotFoundErr);
    return copyToHandle(aCollection, *item, itemData);
}

OSErr Executor::C_ReplaceIndexedCollectionItemHdl(Collection aCollection, LONGINT index, Handle itemData)
{
    if(!itemData)
        return C_ReplaceIndexedCollectionItem(aCollection, index, 0, nullptr);
    return C_ReplaceIndexedCollectionItem(aCollection, index, GetHandleSize(itemData), *itemData);
}

OSErr Executor::C_GetIndexedCollectionItemHdl(Collection aCollection, LONGINT index, Handle itemData)
{
    CollectionRecord *r = record(aCollection);
    Item *item = r ? indexed(*r, index) : nullptr;
    if(!item)
        return fail(aCollection, collectionIndexRangeErr);
    return copyToHandle(aCollection, *item, itemData);
}

OSErr Executor::C_FlattenCollectionToHdl(Collection aCollection, Handle flattened)
{
    CollectionRecord *r = record(aCollection);
    if(!r)
        return fail(aCollection, paramErr);
    std::vector<uint8_t> stream = flatten(*r, 0, 0);
    SetHandleSize(flattened, stream.size());
    if(OSErr err = MemError())
        return fail(aCollection, err);
    memcpy(*flattened, stream.data(), stream.size());
    return noErr;
}

OSErr Executor::C_UnflattenCollectionFromHdl(Collection aCollection, Handle flattened)
{
    uint32_t at = 0, size = GetHandleSize(flattened);
    return unflatten(aCollection, [&](uint8_t *out, uint32_t n, OSErr& err) {
        if(n > size - at)
        {
            err = eofErr;
            return false;
        }
        memcpy(out, (uint8_t *)*flattened + at, n);
        at += n;
        return true;
    });
}
