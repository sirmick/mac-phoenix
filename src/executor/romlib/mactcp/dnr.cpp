/* dnr.cpp — Executor's MacTCP resolver, the 'dnrp' code resource.
 *
 * MacTCP applications link Apple's DNR glue (dnr.c), which looks for the
 * MacTCP control panel, loads its 'dnrp' resource (or any 'dnrp' in the
 * resource chain) and calls it with C conventions:
 *
 *     OSErr (*dnr)(long selector, ...)
 *
 * Apple's 'dnrp' asks name servers itself over .IPP's UDP, configured by
 * MacTCP's control panel and Prep file.  Executor answers from the host's
 * resolver instead: without a 'dnrp' in the chain, GetIndResource hands
 * out this one, a JMP to a callback.  A lookup that has to wait runs on a
 * host thread; StrToAddr returns cacheFault, and the result procedure
 * runs at interrupt time (the network interrupt, mactcp.cpp), as
 * MacTCP's did from its UDP completion.  Answers are cached for the
 * session, and a cached name is answered at once, without the result
 * procedure, as MacTCP did.  HInfo and MXInfo ask the host's name servers
 * (net/hostdns.cpp) the same way.
 */

#include <base/common.h>
#include <base/cpu.h>
#include <MacTCP.h>
#include <MemoryMgr.h>
#include <error/error.h>
#include <rsys/hostdns.h>
#include <rsys/mactcp.h>

#include <arpa/nameser.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace Executor;

static_assert(sizeof(hostInfo) == 276, "hostInfo ABI");
static_assert(offsetof(hostInfo, addr) == 260, "hostInfo ABI");

namespace
{

/* AddressXlation.h selectors. */
enum : int32_t
{
    OPENRESOLVER = 1,
    CLOSERESOLVER = 2,
    STRTOADDR = 3,
    ADDRTOSTR = 4,
    ENUMCACHE = 5,
    ADDRTONAME = 6,
    HINFO = 7,
    MXINFO = 8,
};

/* What a lookup answers: addresses (StrToAddr, AddrToName), or a host
   information or mail exchanger record (HInfo, MXInfo), which go in the
   returnRec's rdata where hostInfo has its addresses (MacTCP Programmer's
   Guide: HInfoRec cpuType[30], osType[30]; MXRec preference, exchange[255]). */
enum class Kind : uint8_t
{
    Addrs,
    HInfo,
    MX,
};

struct Answer
{
    OSErr err = noErr;
    Kind kind = Kind::Addrs;
    std::string cname;
    std::vector<uint32_t> addrs;
    std::string cpu, os;  /* HInfo */
    uint16_t preference = 0; /* MX */
    std::string exchange;
};

struct Lookup
{
    /* guest addresses: the hostInfo, result procedure and its data */
    uint32_t info, proc, user;
    std::string key; /* the name, or "#a.b.c.d" for a reverse lookup */
    Answer answer;
};

std::mutex done_mutex;
std::vector<std::unique_ptr<Lookup>> done;
std::map<std::string, Answer> cache; /* emulator thread only */

bool log_enabled()
{
    return ERROR_ENABLED_P(ERROR_MACTCP_LOG);
}

std::string dotted(uint32_t a)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%u.%u.%u.%u", a >> 24, (a >> 16) & 0xFF,
             (a >> 8) & 0xFF, a & 0xFF);
    return buf;
}

OSErr map_gai(int e)
{
    switch(e)
    {
        case 0:
            return noErr;
        case EAI_NONAME:
#if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
        case EAI_NODATA:
#endif
            return authNameErr; /* the name does not exist */
        case EAI_AGAIN:
            return noAnsErr;
        case EAI_MEMORY:
            return outOfMemory;
        default:
            return dnrErr;
    }
}

Answer resolve(const std::string &name)
{
    Answer a;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    struct addrinfo *res = nullptr;
    a.err = map_gai(getaddrinfo(name.c_str(), nullptr, &hints, &res));
    if(a.err)
        return a;
    a.cname = res->ai_canonname ? res->ai_canonname : name;
    for(auto *ai = res; ai && a.addrs.size() < 4; ai = ai->ai_next)
    {
        uint32_t ip = ntohl(((struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr);
        if(std::find(a.addrs.begin(), a.addrs.end(), ip) == a.addrs.end())
            a.addrs.push_back(ip);
    }
    freeaddrinfo(res);
    return a;
}

Answer reverse(uint32_t ip)
{
    Answer a;
    struct sockaddr_in sin = {};
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(ip);
    char host[NI_MAXHOST];
    a.err = map_gai(getnameinfo((struct sockaddr *)&sin, sizeof sin, host,
                                sizeof host, nullptr, 0, NI_NAMEREQD));
    if(!a.err)
        a.cname = host;
    a.addrs.push_back(ip);
    return a;
}

void put_string(uint32_t at, const std::string &str, uint32_t size)
{
    for(uint32_t i = 0; i < size; i++)
        WRITEUB(at + i, i + 1 < size && i < str.size() ? str[i] : 0);
}

void fill(uint32_t info_addr, const Answer &a)
{
    hostInfo *info = (hostInfo *)SYN68K_TO_US(info_addr);
    info->rtnCode = a.err;
    std::memset(info->cname, 0, sizeof info->cname);
    std::strncpy((char *)info->cname, a.cname.c_str(), sizeof info->cname - 1);
    switch(a.kind)
    {
        case Kind::Addrs:
            for(int i = 0; i < 4; i++)
                info->addr[i] = i < (int)a.addrs.size() ? a.addrs[i] : 0;
            break;
        case Kind::HInfo:
            put_string(info_addr + 260, a.cpu, 30);
            put_string(info_addr + 290, a.os, 30);
            break;
        case Kind::MX:
            WRITEUW(info_addr + 260, a.preference);
            put_string(info_addr + 262, a.exchange, 255);
            break;
    }
}

/* HInfo and MXInfo: one record from the host's name servers. */
Answer records(const std::string &name, Kind kind)
{
    Answer a;
    a.kind = kind;
    a.cname = name;
    std::vector<HostRR> rrs;
    int type = kind == Kind::HInfo ? ns_t_hinfo : ns_t_mx;
    switch(host_dns_query(name, ns_c_in, type, rrs))
    {
        case HostDNSOk:
            break;
        case HostDNSNoName:
            a.err = authNameErr;
            return a;
        default:
            a.err = noAnsErr;
            return a;
    }
    bool found = false;
    for(auto &rr : rrs)
    {
        if(rr.section != 1 || rr.type != type)
            continue;
        const auto &d = rr.rdata;
        if(kind == Kind::HInfo && !d.empty() && 1u + d[0] < d.size())
        {
            size_t l1 = d[0], l2 = d[1 + l1];
            if(2 + l1 + l2 > d.size())
                continue;
            a.cpu.assign((const char *)&d[1], l1);
            a.os.assign((const char *)&d[2 + l1], l2);
            found = true;
            break;
        }
        if(kind == Kind::MX && d.size() >= 3)
        {
            uint16_t pref = (d[0] << 8) | d[1];
            if(!found || pref < a.preference)
            {
                a.preference = pref;
                a.exchange = (const char *)&d[2];
            }
            found = true;
        }
    }
    if(!found)
        a.err = noAnsErr;
    return a;
}

std::string guest_cstring(uint32_t addr)
{
    std::string s;
    if(!addr)
        return s;
    const char *p = (const char *)SYN68K_TO_US(addr);
    for(int i = 0; i < 255 && p[i]; i++)
        s += p[i];
    return s;
}

/* Start a lookup on a host thread, or answer it now from the cache.
 * The answer is filled in and noErr returned when it is known at once;
 * else the hostInfo says cacheFault until the result procedure runs. */
OSErr start(std::unique_ptr<Lookup> l, bool is_reverse, uint32_t ip, Kind kind = Kind::Addrs)
{
    auto hit = cache.find(l->key);
    if(hit != cache.end())
    {
        fill(l->info, hit->second);
        return hit->second.err;
    }
    if(!l->proc)
        return noResultProc;
    ((hostInfo *)SYN68K_TO_US(l->info))->rtnCode = cacheFault;
    std::string name = l->key.substr(l->key.find(':') + 1);
    std::thread([l = std::move(l), is_reverse, ip, name, kind]() mutable {
        l->answer = kind != Kind::Addrs ? records(name, kind)
                    : is_reverse      ? reverse(ip)
                                      : resolve(name);
        {
            std::lock_guard<std::mutex> lock(done_mutex);
            done.push_back(std::move(l));
        }
        ROMlib_mactcp_wake();
    }).detach();
    return cacheFault;
}

OSErr str_to_addr(uint32_t name_addr, uint32_t info, uint32_t proc,
                  uint32_t user)
{
    std::string name = guest_cstring(name_addr);
    while(!name.empty() && (name.back() == '.' || isspace((unsigned char)name.back())))
        name.pop_back();
    if(log_enabled())
        fprintf(stderr, "[dnr] StrToAddr \"%s\"\n", name.c_str());
    if(!info)
        return paramErr;
    if(name.empty())
        return nameSyntaxErr;

    /* A dotted address is its own answer. */
    struct in_addr in;
    if(inet_pton(AF_INET, name.c_str(), &in) == 1)
    {
        Answer a;
        a.cname = name;
        a.addrs.push_back(ntohl(in.s_addr));
        fill(info, a);
        return noErr;
    }
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    auto l = std::make_unique<Lookup>();
    l->info = info;
    l->proc = proc;
    l->user = user;
    l->key = name;
    return start(std::move(l), false, 0);
}

OSErr addr_to_name(uint32_t ip, uint32_t info, uint32_t proc, uint32_t user)
{
    if(log_enabled())
        fprintf(stderr, "[dnr] AddrToName %s\n", dotted(ip).c_str());
    if(!info)
        return paramErr;
    auto l = std::make_unique<Lookup>();
    l->info = info;
    l->proc = proc;
    l->user = user;
    l->key = "#" + dotted(ip);
    return start(std::move(l), true, ip);
}

/* OSErr HInfo / MXInfo(char *hostName, struct returnRec *, ResultProc2Ptr,
   char *userDataPtr): as StrToAddr, the record in the returnRec. */
OSErr record_lookup(Kind kind, uint32_t name_addr, uint32_t info, uint32_t proc, uint32_t user)
{
    std::string name = guest_cstring(name_addr);
    while(!name.empty() && (name.back() == '.' || isspace((unsigned char)name.back())))
        name.pop_back();
    if(log_enabled())
        fprintf(stderr, "[dnr] %s \"%s\"\n", kind == Kind::HInfo ? "HInfo" : "MXInfo", name.c_str());
    if(!info)
        return paramErr;
    if(name.empty())
        return nameSyntaxErr;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    auto l = std::make_unique<Lookup>();
    l->info = info;
    l->proc = proc;
    l->user = user;
    l->key = (kind == Kind::HInfo ? "hinfo:" : "mx:") + name;
    return start(std::move(l), false, 0, kind);
}

/* The 'dnrp' entry: C conventions, the caller pops the arguments. */
syn68k_addr_t dnr_entry(syn68k_addr_t, void *)
{
    uint32_t sp = EM_A7;
    auto arg = [sp](int i) -> uint32_t { return READUL(sp + 4 + 4 * i); };
    int32_t selector = (int32_t)arg(0);
    OSErr err;
    switch(selector)
    {
        case OPENRESOLVER:
        case CLOSERESOLVER:
            err = noErr;
            break;
        case STRTOADDR:
            err = str_to_addr(arg(1), arg(2), arg(3), arg(4));
            break;
        case ADDRTOSTR:
        {
            std::string s = dotted(arg(1));
            if(uint32_t out = arg(2))
                std::memcpy(SYN68K_TO_US(out), s.c_str(), s.size() + 1);
            err = noErr;
            break;
        }
        case ENUMCACHE:
            err = noErr; /* nothing to enumerate the caller could use */
            break;
        case ADDRTONAME:
            err = addr_to_name(arg(1), arg(2), arg(3), arg(4));
            break;
        case HINFO:
            err = record_lookup(Kind::HInfo, arg(1), arg(2), arg(3), arg(4));
            break;
        case MXINFO:
            err = record_lookup(Kind::MX, arg(1), arg(2), arg(3), arg(4));
            break;
        default:
            err = paramErr;
            break;
    }
    if(log_enabled())
        fprintf(stderr, "[dnr] selector %d -> %d\n", selector, err);
    EM_D0 = (uint32_t)(int32_t)err;
    return POPADDR();
}

} /* anonymous namespace */

Handle Executor::ROMlib_dnr_resource()
{
    static syn68k_addr_t entry;
    if(!entry)
        entry = callback_install(dnr_entry, nullptr);
    Handle h = NewHandle(8);
    if(!h)
        return nullptr;
    HLock(h);
    uint8_t *p = (uint8_t *)*h;
    p[0] = 0x4E; /* JMP abs.L entry */
    p[1] = 0xF9;
    p[2] = entry >> 24;
    p[3] = entry >> 16;
    p[4] = entry >> 8;
    p[5] = entry;
    p[6] = 0x4E; /* NOP */
    p[7] = 0x71;
    ROMlib_destroy_blocks(US_TO_SYN68K(*h), 8, false);
    if(log_enabled())
        fprintf(stderr, "[dnr] Executor's 'dnrp' handed out\n");
    return h;
}

void Executor::ROMlib_dnr_pump()
{
    std::vector<std::unique_ptr<Lookup>> ready;
    {
        std::lock_guard<std::mutex> lock(done_mutex);
        ready.swap(done);
    }
    for(auto &l : ready)
    {
        if(log_enabled())
            fprintf(stderr, "[dnr] %s -> %d %s (%zu addresses)\n",
                    l->key.c_str(), l->answer.err, l->answer.cname.c_str(),
                    l->answer.addrs.size());
        if(l->answer.err != noAnsErr)
            cache[l->key] = l->answer;
        fill(l->info, l->answer);
        /* pascal void (*)(struct hostInfo *, char *userDataPtr) */
        uint32_t saved_a7 = EM_A7;
        EM_A7 -= 4;
        WRITEUL(EM_A7, l->info);
        EM_A7 -= 4;
        WRITEUL(EM_A7, l->user);
        execute68K(l->proc);
        EM_A7 = saved_a7;
    }
}
