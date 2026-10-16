/* ot_utils.cpp — Open Transport's utility functions: memory and strings,
 * time stamps, the LIFO and list helpers ("OTClnt$CmnShared"), and
 * configurations ("OTClnt$LclUtils").
 *
 * All C entries but the configuration ones (pascal). An OTTimeStamp
 * (UnsignedWide) holds microseconds of the host's monotonic clock; the
 * functions that take them back agree on that, which is all OT promises
 * ("tick numbers"). */

#include "ot_internal.h"

#include <MemoryMgr.h>

#include <chrono>
#include <cstring>

using namespace Executor;
using namespace Executor::ot;

namespace
{

/* ---- memory and strings ---------------------------------------------- */

uint32_t memcpy_(const Call &c)
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
uint32_t memset_(const Call &c)
{
    if(c.l(2))
        std::memset(c.p(0), c.l(1) & 0xFF, c.l(2));
    return 0;
}

/* Boolean OTMemcmp: true when the bytes are the same. */
uint32_t memcmp_(const Call &c)
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

uint32_t strcat_(const Call &c)
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

const char cmn[] = "OTClnt$CmnShared";
const char utils[] = "OTClnt$LclUtils";

const Def table[] = {
    { cmn, "OTMemcpy", Conv::C, "LLL", 'V', memcpy_ },
    { cmn, "OTMemmove", Conv::C, "LLL", 'V', memcpy_ },
    { cmn, "OTMemzero", Conv::C, "LL", 'V', memzero },
    { cmn, "OTMemset", Conv::C, "LLL", 'V', memset_ },
    { cmn, "OTMemcmp", Conv::C, "LLL", 'B', memcmp_ },
    { cmn, "OTStrLength", Conv::C, "L", 'L', strlength },
    { cmn, "OTStrCopy", Conv::C, "LL", 'V', strcopy },
    { cmn, "OTStrCat", Conv::C, "LL", 'V', strcat_ },
    { cmn, "OTStrEqual", Conv::C, "LL", 'B', strequal },

    { cmn, "OTGetTimeStamp", Conv::C, "L", 'V', get_time_stamp },
    { cmn, "OTSubtractTimeStamps", Conv::C, "LLL", 'L', subtract_time_stamps },
    { cmn, "OTTimeStampInMilliseconds", Conv::C, "L", 'L', stamp_in_ms },
    { cmn, "OTTimeStampInMicroseconds", Conv::C, "L", 'L', stamp_in_us },
    { cmn, "OTElapsedMilliseconds", Conv::C, "L", 'L', elapsed_ms },
    { cmn, "OTElapsedMicroseconds", Conv::C, "L", 'L', elapsed_us },
    { cmn, "OTGetClockTimeInSecs", Conv::C, "", 'L', clock_secs },

    { cmn, "OTLIFOEnqueue", Conv::C, "LL", 'V', lifo_enqueue },
    { cmn, "OTLIFODequeue", Conv::C, "L", 'L', lifo_dequeue },
    { cmn, "OTLIFOStealList", Conv::C, "L", 'L', lifo_steal },
    { cmn, "OTReverseList", Conv::C, "L", 'L', reverse_list },
    { cmn, "OTAddFirst", Conv::C, "LL", 'V', add_first },
    { cmn, "OTAddLast", Conv::C, "LL", 'V', add_last },
    { cmn, "OTRemoveFirst", Conv::C, "L", 'L', remove_first },
    { cmn, "OTRemoveLast", Conv::C, "L", 'L', remove_last },
    { cmn, "OTGetFirst", Conv::C, "L", 'L', get_first },
    { cmn, "OTGetLast", Conv::C, "L", 'L', get_last },
    { cmn, "OTIsInList", Conv::C, "LL", 'B', is_in_list },
    { cmn, "OTRemoveLink", Conv::C, "LL", 'B', remove_link },
    { cmn, "OTGetIndexedLink", Conv::C, "LL", 'L', get_indexed_link },

    { utils, "OTCREATECONFIGURATION", Conv::Pascal, "L", 'L', create_configuration },
    { utils, "OTCLONECONFIGURATION", Conv::Pascal, "L", 'L', clone_configuration },
    { utils, "OTDESTROYCONFIGURATION", Conv::Pascal, "L", 'V', destroy_configuration },
};

} // anonymous namespace

void Executor::ot::register_utils()
{
    register_table(table);
}

/* Configuration paths for the providers (ot_provider.cpp). */
namespace Executor
{
namespace ot
{
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
