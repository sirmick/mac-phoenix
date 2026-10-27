/* ot_utils.cpp — Open Transport's utility functions: memory and strings,
 * time stamps, the LIFO and list helpers, atomic operations and port
 * references ("OTClnt$CmnShared"), and ports and
 * configurations ("OTClnt$LclUtils").
 *
 * All C entries but the configuration ones (pascal). An OTTimeStamp
 * (UnsignedWide) holds microseconds of the host's monotonic clock; the
 * functions that take them back agree on that, which is all OT promises
 * ("tick numbers"). */

#include "ot_provider.h"

#include <MemoryMgr.h>

#include <chrono>
#include <cstring>

using namespace Executor;
using namespace Executor::ot;

namespace Executor::ot
{

/* ---- memory and strings ---------------------------------------------- */

uint32_t ot_memcpy(const Call &c)
{
    if(c.l(2))
        std::memmove(c.p(0), c.p(1), c.l(2)); /* OT's tolerates overlap */
    return 0;
}

uint32_t memzero(const Call &c)
{
    if(c.l(1))
        std::memset(c.p(0), 0, c.l(1));
    return 0;
}

/* void OTMemset(void *dest, uchar_p toSet, size_t nBytes) */
uint32_t ot_memset(const Call &c)
{
    if(c.l(2))
        std::memset(c.p(0), c.l(1) & 0xFF, c.l(2));
    return 0;
}

/* Boolean OTMemcmp: true when the bytes are the same. */
uint32_t ot_memcmp(const Call &c)
{
    uint32_t n = c.l(2);
    return !n || std::memcmp(c.p(0), c.p(1), n) == 0;
}

uint32_t strlength(const Call &c)
{
    return guest_cstring(c.l(0), 0xFFFF).size();
}

uint32_t strcopy(const Call &c)
{
    std::string s = guest_cstring(c.l(1), 0xFFFF);
    std::memcpy(c.p(0), s.c_str(), s.size() + 1);
    return 0;
}

uint32_t ot_strcat(const Call &c)
{
    std::string a = guest_cstring(c.l(0), 0xFFFF);
    std::string b = guest_cstring(c.l(1), 0xFFFF);
    std::memcpy(c.p(0) + a.size(), b.c_str(), b.size() + 1);
    return 0;
}

uint32_t strequal(const Call &c)
{
    return guest_cstring(c.l(0), 0xFFFF) == guest_cstring(c.l(1), 0xFFFF);
}

/* ---- time ------------------------------------------------------------- */

using Clock = std::chrono::steady_clock;
const Clock::time_point epoch = Clock::now();

uint64_t now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - epoch).count();
}

uint64_t read_stamp(uint32_t addr)
{
    return ((uint64_t)READUL(addr) << 32) | READUL(addr + 4);
}

void write_stamp(uint32_t addr, uint64_t v)
{
    WRITEUL(addr, (uint32_t)(v >> 32));
    WRITEUL(addr + 4, (uint32_t)v);
}

uint32_t get_time_stamp(const Call &c)
{
    write_stamp(c.l(0), now_us());
    return 0;
}

/* OTTimeStamp *OTSubtractTimeStamps(result, start, end): end - start */
uint32_t subtract_time_stamps(const Call &c)
{
    write_stamp(c.l(0), read_stamp(c.l(2)) - read_stamp(c.l(1)));
    return c.l(0);
}

uint32_t stamp_in_ms(const Call &c)
{
    return (uint32_t)(read_stamp(c.l(0)) / 1000);
}

uint32_t stamp_in_us(const Call &c)
{
    return (uint32_t)read_stamp(c.l(0));
}

uint32_t elapsed_ms(const Call &c)
{
    return (uint32_t)((now_us() - read_stamp(c.l(0))) / 1000);
}

uint32_t elapsed_us(const Call &c)
{
    return (uint32_t)(now_us() - read_stamp(c.l(0)));
}

uint32_t clock_secs(const Call &)
{
    return (uint32_t)(now_us() / 1000000);
}

/* ---- LIFOs and lists --------------------------------------------------
   OTLIFO and OTList are both { OTLink *fHead }; an OTLink is { OTLink
   *fNext }. Executor runs one 68k thread at a time and calls these with
   no interrupt pending, so plain reads and writes are atomic enough. */

uint32_t lifo_enqueue(const Call &c)
{
    uint32_t list = c.l(0), link = c.l(1);
    WRITEUL(link, READUL(list));
    WRITEUL(list, link);
    return 0;
}

uint32_t lifo_dequeue(const Call &c)
{
    uint32_t list = c.l(0);
    uint32_t link = READUL(list);
    if(link)
        WRITEUL(list, READUL(link));
    return link;
}

uint32_t lifo_steal(const Call &c)
{
    uint32_t list = c.l(0);
    uint32_t head = READUL(list);
    WRITEUL(list, 0);
    return head;
}

/* OTLink *OTReverseList(OTLink *) */
uint32_t reverse_list(const Call &c)
{
    uint32_t prev = 0, link = c.l(0);
    while(link)
    {
        uint32_t next = READUL(link);
        WRITEUL(link, prev);
        prev = link;
        link = next;
    }
    return prev;
}

uint32_t add_first(const Call &c)
{
    return lifo_enqueue(c);
}

uint32_t add_last(const Call &c)
{
    uint32_t list = c.l(0), link = c.l(1);
    WRITEUL(link, 0);
    uint32_t at = list; /* the field that points at the next link */
    while(READUL(at))
        at = READUL(at);
    WRITEUL(at, link);
    return 0;
}

uint32_t remove_first(const Call &c)
{
    return lifo_dequeue(c);
}

uint32_t remove_last(const Call &c)
{
    uint32_t list = c.l(0);
    uint32_t at = list;
    if(!READUL(at))
        return 0;
    while(READUL(READUL(at)))
        at = READUL(at);
    uint32_t last = READUL(at);
    WRITEUL(at, 0);
    return last;
}

uint32_t get_first(const Call &c)
{
    return READUL(c.l(0));
}

uint32_t get_last(const Call &c)
{
    uint32_t link = READUL(c.l(0));
    while(link && READUL(link))
        link = READUL(link);
    return link;
}

uint32_t is_in_list(const Call &c)
{
    for(uint32_t link = READUL(c.l(0)); link; link = READUL(link))
        if(link == c.l(1))
            return 1;
    return 0;
}

/* Boolean OTRemoveLink(OTList *, OTLink *) */
uint32_t remove_link(const Call &c)
{
    uint32_t at = c.l(0), want = c.l(1);
    for(uint32_t link = READUL(at); link; at = link, link = READUL(link))
        if(link == want)
        {
            WRITEUL(at, READUL(link));
            return 1;
        }
    return 0;
}

/* OTLink *OTGetIndexedLink(OTList *, size_t index): 0 is the first. */
uint32_t get_indexed_link(const Call &c)
{
    uint32_t link = READUL(c.l(0));
    for(uint32_t i = c.l(1); link && i; i--)
        link = READUL(link);
    return link;
}

/* void OTEnqueue(void **listHead, void *object, size_t linkOffset) */
uint32_t enqueue(const Call &c)
{
    uint32_t head = c.l(0), obj = c.l(1);
    WRITEUL(obj + c.l(2), READUL(head));
    WRITEUL(head, obj);
    return 0;
}

/* void *OTDequeue(void **listHead, size_t linkOffset) */
uint32_t dequeue(const Call &c)
{
    uint32_t head = c.l(0);
    uint32_t obj = READUL(head);
    if(obj)
        WRITEUL(head, READUL(obj + c.l(1)));
    return obj;
}

/* Boolean (*OTListSearchProcPtr)(const void *ref, OTLink *), C. */
bool search_matches(uint32_t proc, uint32_t ref, uint32_t link, bool ppc)
{
    if(ppc)
        return (callPPC(proc, { ref, link }) & 0xFF) != 0;
    uint32_t saved_a7 = EM_A7;
    EM_A7 -= 8;
    WRITEUL(EM_A7, ref);
    WRITEUL(EM_A7 + 4, link);
    execute68K(proc);
    EM_A7 = saved_a7;
    return (EM_D0 & 0xFF) != 0;
}

/* OTLink *OTFindLink(OTList *, OTListSearchProcPtr, const void *ref) */
uint32_t find_link(const Call &c)
{
    for(uint32_t link = READUL(c.l(0)); link; link = READUL(link))
        if(search_matches(c.l(1), c.l(2), link, c.ppc()))
            return link;
    return 0;
}

uint32_t find_and_remove_link(const Call &c)
{
    uint32_t at = c.l(0);
    for(uint32_t link = READUL(at); link; at = link, link = READUL(link))
        if(search_matches(c.l(1), c.l(2), link, c.ppc()))
        {
            WRITEUL(at, READUL(link));
            return link;
        }
    return 0;
}

/* ---- atomic operations --------------------------------------------------
   SC compiles these inline (BSET, CAS); other compilers call them. One 68k
   instruction stream at a time, and no interrupt inside a call, so plain
   reads and writes are atomic. Bits are numbered as BSET.B numbers them
   (0 the low bit, modulo 8). */

uint32_t atomic_set_bit(const Call &c)
{
    uint32_t a = c.l(0);
    uint8_t m = 1 << (c.l(1) & 7), v = READUB(a);
    WRITEUB(a, v | m);
    return (v & m) != 0;
}

uint32_t atomic_clear_bit(const Call &c)
{
    uint32_t a = c.l(0);
    uint8_t m = 1 << (c.l(1) & 7), v = READUB(a);
    WRITEUB(a, v & ~m);
    return (v & m) != 0;
}

uint32_t atomic_test_bit(const Call &c)
{
    return (READUB(c.l(0)) & (1 << (c.l(1) & 7))) != 0;
}

/* Boolean OTCompareAndSwapNN(old, new, location): true if it swapped. */
uint32_t cas32(const Call &c)
{
    if(READUL(c.l(2)) != c.l(0))
        return 0;
    WRITEUL(c.l(2), c.l(1));
    return 1;
}

uint32_t cas16(const Call &c)
{
    if(READUW(c.l(2)) != (uint16_t)c.l(0))
        return 0;
    WRITEUW(c.l(2), c.l(1));
    return 1;
}

uint32_t cas8(const Call &c)
{
    if(READUB(c.l(2)) != (uint8_t)c.l(0))
        return 0;
    WRITEUB(c.l(2), c.l(1));
    return 1;
}

/* SIntNN OTAtomicAddNN(SInt32 delta, SIntNN *): the new value. */
uint32_t atomic_add32(const Call &c)
{
    uint32_t v = READUL(c.l(1)) + c.l(0);
    WRITEUL(c.l(1), v);
    return v;
}

uint32_t atomic_add16(const Call &c)
{
    int16_t v = (int16_t)(READUW(c.l(1)) + c.l(0));
    WRITEUW(c.l(1), v);
    return (uint32_t)(int32_t)v;
}

uint32_t atomic_add8(const Call &c)
{
    int8_t v = (int8_t)(READUB(c.l(1)) + c.l(0));
    WRITEUB(c.l(1), v);
    return (uint32_t)(int32_t)v;
}

/* ---- port references ----------------------------------------------------
   An OTPortRef packs the bus type (4 bits), the device type (10), the slot
   and the "other" number (8 each); the functions below are the only ones
   that look inside. */

/* pascal OTPortRef OTCreatePortRef(UInt8 bus, UInt16 dev, UInt16 slot,
   UInt16 other): a pascal byte argument is a word, the value in its high
   byte. */
uint32_t create_port_ref(const Call &c)
{
    return make_port_ref(c.byte(0), c.w(1), c.w(2), c.w(3));
}

uint32_t device_type_of(const Call &c)
{
    return (c.l(0) >> 16) & 0x3FF;
}

uint32_t bus_type_of(const Call &c)
{
    return (c.l(0) >> 26) & 0xF;
}

/* UInt16 OTGetSlotFromPortRef(OTPortRef, UInt16 *other) */
uint32_t slot_of(const Call &c)
{
    if(c.l(1))
        WRITEUW(c.l(1), c.l(0) & 0xFF);
    return (c.l(0) >> 8) & 0xFF;
}

/* ---- configurations ---------------------------------------------------
   An OTConfiguration is opaque to clients: a block in the current heap,
   'OTCf' and the path. Opening a provider takes it (and disposes of it),
   as OT's does. */

constexpr uint32_t CONFIG_TAG = 0x4F544366; /* 'OTCf' */

uint32_t make_config(const std::string &path)
{
    Ptr p = NewPtr(4 + path.size() + 1);
    if(!p)
        return 0;
    uint32_t a = guest_addr(p);
    WRITEUL(a, CONFIG_TAG);
    std::memcpy(p + 4, path.c_str(), path.size() + 1);
    return a;
}

/* pascal OTConfiguration *OTCreateConfiguration(const char *path) */
uint32_t create_configuration(const Call &c)
{
    std::string path = guest_cstring(c.l(0));
    OT_LOG("OTCreateConfiguration \"%s\"\n", path.c_str());
    return make_config(path);
}

uint32_t clone_configuration(const Call &c)
{
    uint32_t cfg = c.l(0);
    if(!cfg || READUL(cfg) != CONFIG_TAG)
        return 0;
    return make_config(guest_cstring(cfg + 4));
}

uint32_t destroy_configuration(const Call &c)
{
    uint32_t cfg = c.l(0);
    if(cfg && READUL(cfg) == CONFIG_TAG)
    {
        WRITEUL(cfg, 0);
        DisposePtr((Ptr)SYN68K_TO_US(cfg));
    }
    return 0;
}

/* ---- ports ---------------------------------------------------------------
   One: built-in Ethernet, "enet", which the host's network stands behind.
   OTPortRecord (OpenTransport.h): fRef, fPortFlags, fInfoFlags,
   fCapabilities, fNumChildPorts, fChildPorts, fPortName[36],
   fModuleName[32], fSlotID[8], fResourceInfo[32], fReserved[164]. */

constexpr uint32_t PORT_RECORD_SIZE = 296;

void fill_port_record(uint32_t rec)
{
    for(uint32_t i = 0; i < PORT_RECORD_SIZE; i++)
        WRITEUB(rec + i, 0);
    WRITEUL(rec + 0, enet_port_ref());
    WRITEUL(rec + 4, 0x00000001); /* kOTPortIsActive */
    WRITEUL(rec + 8, 0x00000001 | 0x00004000); /* kOTPortIsDLPI, kOTPortIsSystemRegistered */
    std::memcpy(SYN68K_TO_US(rec + 24), "enet", 5); /* fPortName */
    std::memcpy(SYN68K_TO_US(rec + 60), "enet", 5); /* fModuleName */
}

/* pascal Boolean OTGetIndexedPort(OTPortRecord *, size_t index) */
uint32_t get_indexed_port(const Call &c)
{
    if(c.l(1) != 0 || !c.l(0))
        return 0;
    fill_port_record(c.l(0));
    return 1;
}

/* pascal Boolean OTFindPort(OTPortRecord *, const char *name) */
uint32_t find_port(const Call &c)
{
    if(!c.l(0) || guest_cstring(c.l(1), 64) != "enet")
        return 0;
    fill_port_record(c.l(0));
    return 1;
}

/* pascal Boolean OTFindPortByRef(OTPortRecord *, OTPortRef) */
uint32_t find_port_by_ref(const Call &c)
{
    if(!c.l(0) || c.l(1) != enet_port_ref())
        return 0;
    fill_port_record(c.l(0));
    return 1;
}

} // namespace Executor::ot

/* Configuration paths for the providers (ot_provider.cpp). */
namespace Executor
{
namespace ot
{
uint32_t make_port_ref(unsigned bus, unsigned device, unsigned slot, unsigned other)
{
    return ((bus & 0xF) << 26) | ((device & 0x3FF) << 16) | ((slot & 0xFF) << 8) | (other & 0xFF);
}

/* Built-in Ethernet: kOTMotherboardBus, kOTEthernetDevice (OpenTptLinks.h). */
uint32_t enet_port_ref()
{
    return make_port_ref(1, 10, 0, 0);
}

bool config_path(uint32_t cfg, std::string &path)
{
    if(cfg == 0xFFFFFFFD) /* kDefaultInternetServicesPath */
    {
        path = "dnr";
        return true;
    }
    if(!cfg || READUL(cfg) != CONFIG_TAG)
        return false;
    path = guest_cstring(cfg + 4);
    return true;
}

void dispose_config(uint32_t cfg)
{
    if(cfg && cfg != 0xFFFFFFFD && READUL(cfg) == CONFIG_TAG)
    {
        WRITEUL(cfg, 0);
        DisposePtr((Ptr)SYN68K_TO_US(cfg));
    }
}
}
}
