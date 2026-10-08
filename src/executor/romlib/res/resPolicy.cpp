/* MacPhoenix: choose Apple's or Executor's version of System resources.
 *
 * With Apple's System file as the system resource map, its 68k code
 * resources (MBDF, WDEF, PACK...) replace Executor's native stubs and may
 * call traps Executor doesn't have. A small policy table, consulted when
 * a resource is loaded from the System map, serves Executor's own copy
 * instead. The resource map stays Apple's: only the bytes behind the
 * handle change.
 *
 * Table: res/resource-policy.txt (embedded), replaced by
 * <data_dir>/resource-policy.txt when that file exists.
 */
#include <base/common.h>
#include <res/resource.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <file/file.h>
#include <mman/mman.h>

#include <cmrc/cmrc.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

CMRC_DECLARE(resources);

using namespace Executor;

namespace
{
using Key = std::pair<uint32_t, int>;   // (type, id); id INT32_MIN = any

std::map<Key, bool> policy;             // true = executor
std::map<Key, std::vector<uint8_t>> builtin;
std::once_flag loaded;

uint32_t be32(const uint8_t *p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
uint16_t be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }

void parse_policy(std::istream& in)
{
    policy.clear();
    std::string line;
    while(std::getline(in, line))
    {
        auto hash = line.find('#');
        if(hash != std::string::npos)
            line.resize(hash);
        if(line.size() < 4)
            continue;
        // The type is exactly four characters and may contain a space.
        std::string type = line.substr(0, 4);
        std::istringstream rest(line.substr(4));
        std::string id, source;
        if(!(rest >> id >> source))
            continue;
        uint32_t t = be32((const uint8_t *)type.data());
        int i = id == "*" ? INT32_MIN : std::stoi(id);
        policy[{ t, i }] = source == "executor";
    }
}

/* Executor's built-in System file: AppleDouble, resource fork entry 2. */
void parse_builtin()
{
    auto efs = cmrc::resources::get_filesystem();
    auto f = efs.open("System.ad");
    const uint8_t *ad = (const uint8_t *)f.begin();
    size_t adlen = f.size();
    const uint8_t *fork = nullptr;
    size_t forklen = 0;
    for(int i = 0, n = be16(ad + 24); i < n; i++)
    {
        const uint8_t *e = ad + 26 + 12 * i;
        if(be32(e) == 2 && be32(e + 4) + be32(e + 8) <= adlen)
            fork = ad + be32(e + 4), forklen = be32(e + 8);
    }
    if(!fork || forklen < 16)
        return;
    uint32_t data_off = be32(fork), map_off = be32(fork + 4);
    const uint8_t *m = fork + map_off;
    const uint8_t *tl = m + be16(m + 24);
    for(int i = 0, nt = (be16(tl) + 1) & 0xFFFF; i < nt; i++)
    {
        const uint8_t *te = tl + 2 + 8 * i;
        uint32_t type = be32(te);
        int count = be16(te + 4) + 1;
        const uint8_t *refs = tl + be16(te + 6);
        for(int j = 0; j < count; j++)
        {
            const uint8_t *r = refs + 12 * j;
            int id = (int16_t)be16(r);
            uint32_t doff = be32(r + 4) & 0xFFFFFF;
            const uint8_t *d = fork + data_off + doff;
            uint32_t len = be32(d);
            builtin[{ type, id }].assign(d + 4, d + 4 + len);
        }
    }
}

void load()
{
    std::string override_path = (fs::path(ROMlib_system_folder_path()).parent_path()
                                 / "resource-policy.txt").string();
    std::ifstream user(override_path);
    if(user)
    {
        parse_policy(user);
        fprintf(stderr, "[Executor] resource policy: %s (%zu entries)\n",
                override_path.c_str(), policy.size());
    }
    else
    {
        auto efs = cmrc::resources::get_filesystem();
        auto f = efs.open("resource-policy.txt");
        std::istringstream in(std::string(f.begin(), f.end()));
        parse_policy(in);
    }
    parse_builtin();
}

/* The type of a ref entry: find the type-list entry whose refs hold it. */
ResType type_of(resmaphand map, resref *rr)
{
    INTEGER i, j;
    typref *tr;
    resref *r;
    WALKTANDR(map, i, tr, j, r)
        if(r == rr)
            return tr->rtyp;
    EWALKTANDR(tr, r)
    return 0;
}
} // namespace

/* Returns a loaded handle when the policy serves Executor's version of
   this System-map resource, nullptr to load Apple's bytes as usual. */
Handle Executor::ROMlib_policy_load(resmaphand map, resref *rr)
{
    if((*map)->resfn != LM(SysMap) || !LM(ResLoad))
        return nullptr;
    std::call_once(loaded, load);

    uint32_t type = type_of(map, rr);
    int id = rr->rid;
    auto p = policy.find({ type, id });
    if(p == policy.end())
        p = policy.find({ type, INT32_MIN });
    if(p == policy.end() || !p->second)
        return nullptr;
    auto b = builtin.find({ type, id });
    if(b == builtin.end())
        return nullptr;   // Executor has no copy: fall back to Apple's

    const std::vector<uint8_t>& data = b->second;
    TheZoneGuard guard((rr->ratr & resSysHeap) ? LM(SysZone)
                                               : (GUEST<THz>)HandleZone((Handle)map));
    Handle h = rr->rhand;
    if(h)
        ReallocateHandle(h, data.size());
    else
        h = NewHandle(data.size());
    if(!h || MemError() != noErr)
        return nullptr;
    rr->rhand = h;
    memcpy(*h, data.data(), data.size());
    return h;
}
