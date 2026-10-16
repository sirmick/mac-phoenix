/* ot_inet.cpp — Open Transport's Internet services ("ot:inet$LclInetClnt"):
 * the resolver provider (OTOpenInternetServices), name and address lookups
 * on the host's resolver, the address helpers, and the interface's
 * configuration as the host has it.
 *
 * A synchronous provider looks a name up there and then. An asynchronous
 * one answers noErr at once and looks it up on a host thread; the result
 * goes into the caller's InetHostInfo and T_DNRSTRINGTOADDRCOMPLETE (or
 * T_DNRADDRTONAMECOMPLETE) reaches its notifier at interrupt time, the
 * cookie the caller's buffer, as OT's. */

#include "ot_provider.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
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

/* Lookups finished on host threads, for the pump. */
struct Done
{
    uint32_t ref;
    bool reverse;
    uint32_t buf; /* InetHostInfo, or the InetDomainName */
    Answer answer;
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
        OT_LOG("lookup done for %08x: %d \"%s\" (%zu addresses)\n", d.ref, d.answer.err,
               d.answer.name.c_str(), d.answer.addrs.size());
        if(d.reverse)
        {
            fill_name(d.buf, d.answer.name);
            post(*p, T_DNRADDRTONAMECOMPLETE, d.answer.err, d.buf);
        }
        else
        {
            fill_hostinfo(d.buf, d.answer);
            post(*p, T_DNRSTRINGTOADDRCOMPLETE, d.answer.err, d.buf);
        }
    }
}

bool hooked = false;

void start_lookup(Provider &p, bool rev, std::string name, uint32_t ip, uint32_t buf)
{
    if(!hooked)
    {
        add_pump_hook(deliver);
        hooked = true;
    }
    uint32_t ref = p.ref;
    std::thread([=]() {
        Done d{ ref, rev, buf, rev ? reverse(ip) : resolve(name) };
        {
            std::lock_guard<std::mutex> lock(done_mutex);
            done.push_back(std::move(d));
        }
        wake();
    }).detach();
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
};

} // anonymous namespace

void Executor::ot::register_inet()
{
    register_table(table);
}
