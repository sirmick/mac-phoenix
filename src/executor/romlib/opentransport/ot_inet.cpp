/* ot_inet.cpp — Open Transport's Internet services ("ot:inet$LclInetClnt"):
 * the resolver provider (OTOpenInternetServices), name and address lookups
 * on the host's resolver, the address helpers, and the interface's
 * configuration as the host has it.
 *
 * A synchronous provider looks a name up there and then. An asynchronous
 * one answers noErr at once and looks it up on a host thread; the result
 * goes into the caller's InetHostInfo and T_DNRSTRINGTOADDRCOMPLETE (or
 * T_DNRADDRTONAMECOMPLETE) reaches its notifier at interrupt time, the
 * cookie the caller's buffer, as OT's. The other queries (OTInetSysInfo's
 * HINFO, OTInetMailExchange's MX, OTInetQuery's any) go to the host's
 * name servers with res_query, and are answered the same two ways. */

#include "ot_provider.h"

#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

using namespace Executor;
using namespace Executor::ot;

namespace
{

constexpr uint32_t HOSTINFO_NAME = 0;     /* InetDomainName, 256 bytes */
constexpr uint32_t HOSTINFO_ADDRS = 256;  /* InetHost[10] */
constexpr int MAX_ADDRS = 10;

struct Answer
{
    int32_t err = kOTNoError;
    std::string name;
    std::vector<uint32_t> addrs;
};

int32_t map_gai(int e)
{
    switch(e)
    {
        case 0:
            return kOTNoError;
        case EAI_NONAME:
#if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
        case EAI_NODATA:
#endif
            return kOTBadNameErr;
        case EAI_AGAIN:
            return kETIMEDOUTErr;
        case EAI_MEMORY:
            return kOTOutOfMemoryErr;
        default:
            return kOTSysErrorErr;
    }
}

Answer resolve(std::string name)
{
    Answer a;
    while(!name.empty() && (name.back() == '.' || isspace((unsigned char)name.back())))
        name.pop_back();
    struct in_addr in;
    if(inet_pton(AF_INET, name.c_str(), &in) == 1)
    {
        a.name = name;
        a.addrs.push_back(ntohl(in.s_addr));
        return a;
    }
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    struct addrinfo *res = nullptr;
    a.err = map_gai(getaddrinfo(name.c_str(), nullptr, &hints, &res));
    if(a.err)
        return a;
    a.name = res->ai_canonname ? res->ai_canonname : name;
    for(auto *ai = res; ai && (int)a.addrs.size() < MAX_ADDRS; ai = ai->ai_next)
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
    a.err = map_gai(getnameinfo((struct sockaddr *)&sin, sizeof sin, host, sizeof host,
                                nullptr, 0, NI_NAMEREQD));
    if(!a.err)
        a.name = host;
    return a;
}

void fill_hostinfo(uint32_t hinfo, const Answer &a)
{
    if(!hinfo)
        return;
    std::string name = a.name.substr(0, 255);
    for(uint32_t i = 0; i < 256; i++)
        WRITEUB(hinfo + HOSTINFO_NAME + i, i < name.size() ? name[i] : 0);
    for(int i = 0; i < MAX_ADDRS; i++)
        WRITEUL(hinfo + HOSTINFO_ADDRS + 4 * i, i < (int)a.addrs.size() ? a.addrs[i] : 0);
}

void fill_name(uint32_t buf, const std::string &s)
{
    if(!buf)
        return;
    std::string name = s.substr(0, 255);
    for(uint32_t i = 0; i <= name.size(); i++)
        WRITEUB(buf + i, i < name.size() ? name[i] : 0);
}

/* ---- DNS queries ----------------------------------------------------------
   res_query's answer parsed here (RFC 1035): the header, the questions
   skipped, then the resource records of the three sections. */

struct RR
{
    std::string name;
    uint16_t type = 0, cls = 0;
    uint32_t ttl = 0;
    int section = 0; /* 1 answer, 2 authority, 3 additional */
    std::vector<uint8_t> rdata; /* names in it expanded (CNAME, NS, PTR, MX) */
};

/* A (possibly compressed) name at off; off moves past it. */
bool read_dns_name(const std::vector<uint8_t> &m, size_t &off, std::string &out)
{
    out.clear();
    size_t at = off;
    bool jumped = false;
    for(int guard = 0; guard < 128; guard++)
    {
        if(at >= m.size())
            return false;
        uint8_t len = m[at];
        if((len & 0xC0) == 0xC0)
        {
            if(at + 1 >= m.size())
                return false;
            if(!jumped)
                off = at + 2;
            jumped = true;
            at = ((len & 0x3F) << 8) | m[at + 1];
            continue;
        }
        if(len == 0)
        {
            if(!jumped)
                off = at + 1;
            return true;
        }
        if(at + 1 + len > m.size())
            return false;
        if(!out.empty())
            out += '.';
        out.append((const char *)&m[at + 1], len);
        at += 1 + len;
    }
    return false;
}

void put_name(std::vector<uint8_t> &v, const std::string &name)
{
    v.insert(v.end(), name.begin(), name.end());
    v.push_back(0);
}

int32_t dns_query(const std::string &name, int cls, int type, std::vector<RR> &rrs)
{
    std::vector<uint8_t> m(8192);
    int n = res_query(name.c_str(), cls, type, m.data(), m.size());
    if(n < 0)
    {
        switch(h_errno)
        {
            case HOST_NOT_FOUND:
                return kOTBadNameErr;
            case NO_DATA:
                return kOTNoError; /* the name is there, no such records */
            case TRY_AGAIN:
                return kETIMEDOUTErr;
            default:
                return kOTBadNameErr;
        }
    }
    m.resize(std::min<int>(n, m.size()));
    if(m.size() < 12)
        return kOTSysErrorErr;
    unsigned counts[4];
    for(int i = 0; i < 4; i++)
        counts[i] = (m[4 + 2 * i] << 8) | m[5 + 2 * i];
    size_t off = 12;
    std::string qname;
    for(unsigned i = 0; i < counts[0]; i++)
        if(!read_dns_name(m, off, qname) || (off += 4) > m.size())
            return kOTSysErrorErr;
    for(int section = 1; section <= 3; section++)
        for(unsigned i = 0; i < counts[section]; i++)
        {
            RR rr;
            rr.section = section;
            if(!read_dns_name(m, off, rr.name) || off + 10 > m.size())
                return kOTSysErrorErr;
            rr.type = (m[off] << 8) | m[off + 1];
            rr.cls = (m[off + 2] << 8) | m[off + 3];
            rr.ttl = ((uint32_t)m[off + 4] << 24) | (m[off + 5] << 16) | (m[off + 6] << 8) | m[off + 7];
            size_t len = (m[off + 8] << 8) | m[off + 9];
            off += 10;
            if(off + len > m.size())
                return kOTSysErrorErr;
            size_t r = off;
            std::string target;
            switch(rr.type)
            {
                case ns_t_cname:
                case ns_t_ns:
                case ns_t_ptr:
                    if(read_dns_name(m, r, target))
                        put_name(rr.rdata, target);
                    break;
                case ns_t_mx:
                    if(len >= 2)
                    {
                        rr.rdata = { m[off], m[off + 1] };
                        r += 2;
                        if(read_dns_name(m, r, target))
                            put_name(rr.rdata, target);
                    }
                    break;
                default:
                    rr.rdata.assign(m.begin() + off, m.begin() + off + len);
            }
            off += len;
            rrs.push_back(std::move(rr));
        }
    return kOTNoError;
}

/* ---- answers delivered later ---------------------------------------------
   An asynchronous provider's lookup runs on a host thread; its answer is
   written into the caller's memory at interrupt time and the completion
   event (cookie: the caller's buffer) goes to the notifier. */
struct Done
{
    uint32_t ref;
    uint32_t code;
    uint32_t cookie;
    std::function<int32_t()> fill; /* writes the answer; returns the result */
};
std::mutex done_mutex;
std::vector<Done> done;

void deliver()
{
    std::vector<Done> ready;
    {
        std::lock_guard<std::mutex> lock(done_mutex);
        ready.swap(done);
    }
    for(auto &d : ready)
    {
        Provider *p = find_provider(d.ref);
        if(!p)
            continue; /* closed before it answered */
        int32_t err = d.fill();
        OT_LOG("lookup done for %08x: event $%08x result %d\n", d.ref, d.code, err);
        post(*p, d.code, err, d.cookie);
    }
}

bool hooked = false;

/* Run work on a host thread; it returns the filler to run at interrupt
   time. */
void later(Provider &p, uint32_t code, uint32_t cookie,
           std::function<std::function<int32_t()>()> work)
{
    if(!hooked)
    {
        add_pump_hook(deliver);
        hooked = true;
    }
    uint32_t ref = p.ref;
    std::thread([=]() {
        Done d{ ref, code, cookie, work() };
        {
            std::lock_guard<std::mutex> lock(done_mutex);
            done.push_back(std::move(d));
        }
        wake();
    }).detach();
}

void start_lookup(Provider &p, bool rev, std::string name, uint32_t ip, uint32_t buf)
{
    later(p, rev ? T_DNRADDRTONAMECOMPLETE : T_DNRSTRINGTOADDRCOMPLETE, buf,
          [=]() -> std::function<int32_t()> {
              Answer a = rev ? reverse(ip) : resolve(name);
              return [=]() {
                  if(rev)
                      fill_name(buf, a.name);
                  else
                      fill_hostinfo(buf, a);
                  return a.err;
              };
          });
}

/* ---- the provider ----------------------------------------------------- */

int32_t open_services(uint32_t cfg, uint32_t client, uint32_t &ref)
{
    std::string path;
    if(!config_path(cfg, path))
        return kOTBadConfigurationErr;
    dispose_config(cfg);
    Provider &p = new_provider(client, Kind::Inet);
    ref = p.ref;
    OT_LOG("open Internet services \"%s\" -> %08x\n", path.c_str(), ref);
    return kOTNoError;
}

/* InetSvcRef OTOpenInternetServicesPriv(cfig, oflag, OSStatus *err, client) */
uint32_t open_priv(const Call &c)
{
    uint32_t ref = 0;
    int32_t err = open_services(c.l(0), c.l(3), ref);
    if(c.l(2))
        WRITEUL(c.l(2), (uint32_t)err);
    return ref;
}

/* OSStatus OTAsyncOpenInternetServicesPriv(cfig, oflag, proc, ctx, client) */
uint32_t async_open_priv(const Call &c)
{
    uint32_t ref = 0;
    int32_t err = open_services(c.l(0), c.l(4), ref);
    if(err)
        return err;
    Provider &p = *find_provider(ref);
    p.notifier = c.l(2);
    p.context = c.l(3);
    p.a5 = current_a5();
    p.async = true;
    post(p, T_OPENCOMPLETE, kOTNoError, ref);
    return kOTNoError;
}

/* pascal OSStatus OTInetStringToAddress(ref, char *name, InetHostInfo *) */
uint32_t string_to_address(const Call &c)
{
    Provider *p = find_provider(c.l(0));
    if(!p || p->kind != Kind::Inet)
        return kOTBadReferenceErr;
    std::string name = guest_cstring(c.l(1));
    OT_LOG("OTInetStringToAddress \"%s\"%s\n", name.c_str(), p->async ? " async" : "");
    if(p->async)
    {
        start_lookup(*p, false, name, 0, c.l(2));
        return kOTNoError;
    }
    Answer a = resolve(name);
    fill_hostinfo(c.l(2), a);
    return a.err;
}

/* pascal OSStatus OTInetAddressToName(ref, InetHost, InetDomainName) */
uint32_t address_to_name(const Call &c)
{
    Provider *p = find_provider(c.l(0));
    if(!p || p->kind != Kind::Inet)
        return kOTBadReferenceErr;
    if(p->async)
    {
        start_lookup(*p, true, "", c.l(1), c.l(2));
        return kOTNoError;
    }
    Answer a = reverse(c.l(1));
    fill_name(c.l(2), a.name);
    return a.err;
}

/* The answer for OTInetSysInfo: HINFO's two character strings. */
int32_t sysinfo_answer(const std::string &name, std::string &cpu, std::string &os)
{
    std::vector<RR> rrs;
    int32_t err = dns_query(name, ns_c_in, ns_t_hinfo, rrs);
    if(err)
        return err;
    for(auto &rr : rrs)
        if(rr.section == 1 && rr.type == ns_t_hinfo && !rr.rdata.empty())
        {
            const auto &d = rr.rdata;
            size_t l1 = d[0];
            if(1 + l1 >= d.size())
                break;
            cpu.assign((const char *)&d[1], l1);
            size_t l2 = d[1 + l1];
            if(2 + l1 + l2 > d.size())
                break;
            os.assign((const char *)&d[2 + l1], l2);
            return kOTNoError;
        }
    return kOTBadNameErr;
}

void fill_sysinfo(uint32_t info, const std::string &cpu, const std::string &os)
{
    if(!info)
        return;
    for(uint32_t i = 0; i < 32; i++)
    {
        WRITEUB(info + i, i < 31 && i < cpu.size() ? cpu[i] : 0);
        WRITEUB(info + 32 + i, i < 31 && i < os.size() ? os[i] : 0);
    }
}

/* pascal OSStatus OTInetSysInfo(ref, char *name, InetSysInfo *) */
uint32_t sys_info(const Call &c)
{
    Provider *p = find_provider(c.l(0));
    if(!p || p->kind != Kind::Inet)
        return kOTBadReferenceErr;
    std::string name = guest_cstring(c.l(1));
    uint32_t info = c.l(2);
    OT_LOG("OTInetSysInfo \"%s\"%s\n", name.c_str(), p->async ? " async" : "");
    if(p->async)
    {
        later(*p, T_DNRSYSINFOCOMPLETE, info, [=]() -> std::function<int32_t()> {
            std::string cpu, os;
            int32_t err = sysinfo_answer(name, cpu, os);
            return [=]() {
                if(!err)
                    fill_sysinfo(info, cpu, os);
                return err;
            };
        });
        return kOTNoError;
    }
    std::string cpu, os;
    int32_t err = sysinfo_answer(name, cpu, os);
    if(!err)
        fill_sysinfo(info, cpu, os);
    return err;
}

/* MX records, lowest preference first. */
using MXList = std::vector<std::pair<uint16_t, std::string>>;

int32_t mx_answer(const std::string &name, MXList &mx)
{
    std::vector<RR> rrs;
    int32_t err = dns_query(name, ns_c_in, ns_t_mx, rrs);
    if(err)
        return err;
    for(auto &rr : rrs)
        if(rr.section == 1 && rr.type == ns_t_mx && rr.rdata.size() >= 3)
            mx.emplace_back((rr.rdata[0] << 8) | rr.rdata[1],
                            std::string((const char *)&rr.rdata[2]));
    std::stable_sort(mx.begin(), mx.end(),
                     [](auto &a, auto &b) { return a.first < b.first; });
    return kOTNoError;
}

/* InetMailExchange: UInt16 preference, InetDomainName exchange (258 bytes). */
void fill_mx(uint32_t nump, uint32_t out, const MXList &mx)
{
    unsigned room = nump ? READUW(nump) : 0;
    unsigned n = std::min<size_t>(room, mx.size());
    for(unsigned i = 0; i < n && out; i++)
    {
        WRITEUW(out + 258 * i, mx[i].first);
        fill_name(out + 258 * i + 2, mx[i].second);
    }
    if(nump)
        WRITEUW(nump, n);
}

/* pascal OSStatus OTInetMailExchange(ref, char *name, UInt16 *num,
   InetMailExchange *) */
uint32_t mail_exchange(const Call &c)
{
    Provider *p = find_provider(c.l(0));
    if(!p || p->kind != Kind::Inet)
        return kOTBadReferenceErr;
    std::string name = guest_cstring(c.l(1));
    uint32_t nump = c.l(2), out = c.l(3);
    OT_LOG("OTInetMailExchange \"%s\"%s\n", name.c_str(), p->async ? " async" : "");
    if(p->async)
    {
        later(*p, T_DNRMAILEXCHANGECOMPLETE, out, [=]() -> std::function<int32_t()> {
            MXList mx;
            int32_t err = mx_answer(name, mx);
            return [=]() {
                if(!err)
                    fill_mx(nump, out, mx);
                else if(nump)
                    WRITEUW(nump, 0);
                return err;
            };
        });
        return kOTNoError;
    }
    MXList mx;
    int32_t err = mx_answer(name, mx);
    if(!err)
        fill_mx(nump, out, mx);
    else if(nump)
        WRITEUW(nump, 0);
    return err;
}

/* OTInetQuery's records into buf as DNSQueryInfo (qType, qClass, ttl,
   InetDomainName name, responseType: 1 answer, 2 authority, 3 additional,
   resourceLen, resourceData), each on a 4-byte boundary, and pointers to
   them in argv, nil after the last if there is room. Records that don't
   fit are left out. */
int32_t fill_query(const std::vector<RR> &rrs, uint32_t buf, uint32_t buflen, uint32_t argv,
                   uint32_t argvlen)
{
    uint32_t at = 0, n = 0;
    for(auto &rr : rrs)
    {
        uint32_t size = (268 + rr.rdata.size() + 3) & ~3u;
        if(at + size > buflen || n >= argvlen)
            break;
        uint32_t q = buf + at;
        WRITEUW(q, rr.type);
        WRITEUW(q + 2, rr.cls);
        WRITEUL(q + 4, rr.ttl);
        for(uint32_t i = 0; i < 256; i++)
            WRITEUB(q + 8 + i, i < 255 && i < rr.name.size() ? rr.name[i] : 0);
        WRITEUW(q + 264, rr.section);
        WRITEUW(q + 266, rr.rdata.size());
        for(size_t i = 0; i < rr.rdata.size(); i++)
            WRITEUB(q + 268 + i, rr.rdata[i]);
        WRITEUL(argv + 4 * n, q);
        n++;
        at += size;
    }
    if(n < argvlen)
        WRITEUL(argv + 4 * n, 0);
    return rrs.empty() ? kOTBadNameErr : kOTNoError;
}

/* pascal OSStatus OTInetQuery(ref, char *name, UInt16 qClass, UInt16 qType,
   char *buf, size_t buflen, void **argv, size_t argvlen, OTFlags) */
uint32_t query(const Call &c)
{
    Provider *p = find_provider(c.l(0));
    if(!p || p->kind != Kind::Inet)
        return kOTBadReferenceErr;
    std::string name = guest_cstring(c.l(1));
    int cls = c.w(2), type = c.w(3);
    uint32_t buf = c.l(4), buflen = c.l(5), argv = c.l(6), argvlen = c.l(7);
    OT_LOG("OTInetQuery \"%s\" class %d type %d%s\n", name.c_str(), cls, type,
           p->async ? " async" : "");
    if(!buf || !argv)
        return kOTBadReferenceErr;
    if(p->async)
    {
        later(*p, T_DNRQUERYCOMPLETE, buf, [=]() -> std::function<int32_t()> {
            auto rrs = std::make_shared<std::vector<RR>>();
            int32_t err = dns_query(name, cls, type, *rrs);
            return [=]() { return err ? err : fill_query(*rrs, buf, buflen, argv, argvlen); };
        });
        return kOTNoError;
    }
    std::vector<RR> rrs;
    int32_t err = dns_query(name, cls, type, rrs);
    return err ? err : fill_query(rrs, buf, buflen, argv, argvlen);
}

/* ---- address helpers -------------------------------------------------- */

/* pascal void OTInitInetAddress(InetAddress *, InetPort, InetHost) */
uint32_t init_inet_address(const Call &c)
{
    uint32_t a = c.l(0);
    if(!a)
        return 0;
    WRITEUW(a, 2); /* AF_INET */
    WRITEUW(a + 2, c.w(1));
    WRITEUL(a + 4, c.l(2));
    WRITEUL(a + 8, 0);
    WRITEUL(a + 12, 0);
    return 0;
}

/* pascal size_t OTInitDNSAddress(DNSAddress *, char *str) */
uint32_t init_dns_address(const Call &c)
{
    uint32_t a = c.l(0);
    std::string s = guest_cstring(c.l(1));
    WRITEUW(a, 42); /* AF_DNS */
    fill_name(a + 2, s);
    return 2 + s.size() + 1;
}

/* pascal OSStatus OTInetStringToHost(char *str, InetHost *host): dotted
   decimal only, as OT's. */
uint32_t string_to_host(const Call &c)
{
    std::string s = guest_cstring(c.l(0));
    struct in_addr in;
    if(inet_pton(AF_INET, s.c_str(), &in) != 1)
        return kOTBadNameErr;
    if(c.l(1))
        WRITEUL(c.l(1), ntohl(in.s_addr));
    return kOTNoError;
}

/* pascal void OTInetHostToString(InetHost, char *str) */
uint32_t host_to_string(const Call &c)
{
    uint32_t h = c.l(0);
    char buf[16];
    snprintf(buf, sizeof buf, "%u.%u.%u.%u", h >> 24, (h >> 16) & 0xFF, (h >> 8) & 0xFF,
             h & 0xFF);
    fill_name(c.l(1), buf);
    return 0;
}

/* ---- the interface ------------------------------------------------------
   The host's: the address it sends from by default, that interface's mask
   and broadcast address, the first name server in /etc/resolv.conf. */

uint32_t primary_address()
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0)
        return 0;
    struct sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(53);
    inet_pton(AF_INET, "192.0.2.1", &a.sin_addr); /* TEST-NET: nothing is sent */
    uint32_t ip = 0;
    if(connect(fd, (struct sockaddr *)&a, sizeof a) == 0)
    {
        socklen_t len = sizeof a;
        if(getsockname(fd, (struct sockaddr *)&a, &len) == 0)
            ip = ntohl(a.sin_addr.s_addr);
    }
    close(fd);
    return ip;
}

/* pascal OSStatus OTInetGetInterfaceInfo(InetInterfaceInfo *, SInt32) */
uint32_t get_interface_info(const Call &c)
{
    int32_t index = (int32_t)c.l(1);
    uint32_t info = c.l(0);
    if(index != -1 && index != 0)
        return kOTNotFoundErr;
    uint32_t ip = primary_address();
    if(!ip)
        ip = 0x7F000001;
    uint32_t mask = 0xFFFFFF00, bcast = (ip & mask) | ~mask;
    struct ifaddrs *ifs = nullptr;
    if(getifaddrs(&ifs) == 0)
    {
        for(auto *i = ifs; i; i = i->ifa_next)
            if(i->ifa_addr && i->ifa_addr->sa_family == AF_INET
               && ntohl(((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr) == ip)
            {
                if(i->ifa_netmask)
                    mask = ntohl(((struct sockaddr_in *)i->ifa_netmask)->sin_addr.s_addr);
                bcast = (ip & mask) | ~mask;
            }
        freeifaddrs(ifs);
    }
    uint32_t dns = 0;
    std::string domain;
    std::ifstream rc("/etc/resolv.conf");
    for(std::string line; std::getline(rc, line);)
    {
        std::istringstream ls(line);
        std::string key, val;
        ls >> key >> val;
        struct in_addr in;
        if(key == "nameserver" && !dns && inet_pton(AF_INET, val.c_str(), &in) == 1)
            dns = ntohl(in.s_addr);
        else if((key == "domain" || key == "search") && domain.empty())
            domain = val;
    }
    for(uint32_t i = 0; i < 4 * 5 + 2 + 2 + 4 + 4 + 8 + 256 + 256; i++)
        WRITEUB(info + i, 0);
    WRITEUL(info + 0, ip);
    WRITEUL(info + 4, mask);
    WRITEUL(info + 8, bcast);
    WRITEUL(info + 12, 0); /* the gateway: the host's own business */
    WRITEUL(info + 16, dns);
    WRITEUW(info + 20, 2); /* kInetInterfaceInfoVersion */
    WRITEUW(info + 22, 0); /* no hardware address */
    WRITEUL(info + 24, 0);
    WRITEUL(info + 28, 1500);
    fill_name(info + 40, domain);
    return kOTNoError;
}

const char inet[] = "ot:inet$LclInetClnt";

const Def table[] = {
    { inet, "OTOpenInternetServicesPriv", Conv::C, "LLLL", 'L', open_priv },
    { inet, "OTAsyncOpenInternetServicesPriv", Conv::C, "LLLLL", 'L', async_open_priv },
    { inet, "OTINETSTRINGTOADDRESS", Conv::Pascal, "LLL", 'L', string_to_address },
    { inet, "OTINETADDRESSTONAME", Conv::Pascal, "LLL", 'L', address_to_name },
    { inet, "OTINITINETADDRESS", Conv::Pascal, "LWL", 'V', init_inet_address },
    { inet, "OTINITDNSADDRESS", Conv::Pascal, "LL", 'L', init_dns_address },
    { inet, "OTINETSTRINGTOHOST", Conv::Pascal, "LL", 'L', string_to_host },
    { inet, "OTINETHOSTTOSTRING", Conv::Pascal, "LL", 'V', host_to_string },
    { inet, "OTINETGETINTERFACEINFO", Conv::Pascal, "LL", 'L', get_interface_info },
    { inet, "OTINETSYSINFO", Conv::Pascal, "LLL", 'L', sys_info },
    { inet, "OTINETMAILEXCHANGE", Conv::Pascal, "LLLL", 'L', mail_exchange },
    { inet, "OTINETQUERY", Conv::Pascal, "LLWWLLLLL", 'L', query },
};

} // anonymous namespace

int32_t Executor::ot::inet_resolve(const std::string &name, uint32_t &ip)
{
    Answer a = resolve(name);
    ip = a.addrs.empty() ? 0 : a.addrs[0];
    return a.err ? a.err : a.addrs.empty() ? kOTBadNameErr : kOTNoError;
}

void Executor::ot::register_inet()
{
    register_table(table);
}
