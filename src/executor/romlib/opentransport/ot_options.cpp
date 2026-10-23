/* ot_options.cpp — Open Transport's options on host socket options
 * (OTOptionManagement, OTNextOption, OTFindOption; "OTClnt$LclClnt").
 *
 * An option buffer is a run of TOptions (OpenTransport.h: len, level,
 * name, status, then the value; len counts the 16-byte header), each
 * starting on a 4-byte boundary. OTOptionManagement takes a TOptMgmt
 * (TNetbuf opt, OTFlags flags) and answers in another:
 *   T_NEGOTIATE  set each option; the answer has what it is now
 *   T_CHECK      say whether each could be set
 *   T_DEFAULT    the default values
 *   T_CURRENT    the current values
 * each option's status T_SUCCESS, T_FAILURE, T_READONLY or T_NOTSUPPORT,
 * the answer's flags the worst of them. Name T_ALLOPT with T_DEFAULT or
 * T_CURRENT asks for every option of its level.
 *
 * A TCP endpoint makes its socket at OTBind or OTConnect; options set
 * before then are kept and applied when the socket is made.
 */

#include <rsys/hostnet.h>
#include "ot_provider.h"

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstring>

using namespace Executor;
using namespace Executor::ot;

namespace
{

enum : uint32_t
{
    XTI_GENERIC = 0xFFFF,
    INET_IP = 0x0,
    INET_TCP = 0x06,
    INET_UDP = 0x11,

    T_NEGOTIATE = 0x004,
    T_CHECK = 0x008,
    T_DEFAULT = 0x010,
    T_CURRENT = 0x080,

    T_SUCCESS = 0x020,
    T_FAILURE = 0x040,
    T_PARTSUCCESS = 0x100,
    T_READONLY = 0x200,
    T_NOTSUPPORT = 0x400,

    T_ALLOPT = 0,
    T_YES = 1,
    T_NO = 0,
};

/* How a value is laid out. */
enum class Shape : uint8_t
{
    Int,      /* a 4-byte number (or 1 or 2 bytes, as the client sends it) */
    Linger,   /* t_linger: l_onoff, l_linger (seconds) */
    KeepAlive,/* t_kpalive: kp_onoff, kp_timeout (minutes) */
    Mreq,     /* TIPAddMulticast: group address, interface address */
};

struct Opt
{
    uint32_t level, name;
    int sol, so;      /* setsockopt's; so < 0: accepted, nothing to set */
    Shape shape;
    uint32_t dflt;    /* T_DEFAULT's value */
    bool tcp_only, udp_only;
    bool write_only;  /* nothing to read back (multicast joins) */
};

const Opt opts[] = {
    { XTI_GENERIC, 0x0001, 0, -1, Shape::Int, 0, false, false, false },                 /* XTI_DEBUG */
    { XTI_GENERIC, 0x0080, SOL_SOCKET, SO_LINGER, Shape::Linger, 0, true, false, false },/* XTI_LINGER */
    { XTI_GENERIC, 0x1001, SOL_SOCKET, SO_SNDBUF, Shape::Int, 8192, false, false, false },
    { XTI_GENERIC, 0x1002, SOL_SOCKET, SO_RCVBUF, Shape::Int, 8192, false, false, false },
    { XTI_GENERIC, 0x1003, SOL_SOCKET, -1, Shape::Int, 1, false, false, false },         /* XTI_SNDLOWAT */
    { XTI_GENERIC, 0x1004, SOL_SOCKET, SO_RCVLOWAT, Shape::Int, 1, false, false, false },
    { INET_TCP, 0x01, IPPROTO_TCP, TCP_NODELAY, Shape::Int, T_YES, true, false, false }, /* on here */
    { INET_TCP, 0x02, IPPROTO_TCP, TCP_MAXSEG, Shape::Int, 536, true, false, false },
    { INET_TCP, 0x08, SOL_SOCKET, SO_KEEPALIVE, Shape::KeepAlive, T_NO, true, false, false },
    { INET_IP, 0x02, IPPROTO_IP, IP_TOS, Shape::Int, 0, false, false, false },
    { INET_IP, 0x03, IPPROTO_IP, IP_TTL, Shape::Int, 64, false, false, false },
    { INET_IP, 0x04, SOL_SOCKET, SO_REUSEADDR, Shape::Int, T_NO, false, false, false },
    { INET_IP, 0x10, SOL_SOCKET, SO_DONTROUTE, Shape::Int, T_NO, false, false, false },
    { INET_IP, 0x20, SOL_SOCKET, SO_BROADCAST, Shape::Int, T_NO, false, true, false },
    { INET_IP, 0x1011, IPPROTO_IP, IP_MULTICAST_TTL, Shape::Int, 1, false, true, false },
    { INET_IP, 0x1012, IPPROTO_IP, IP_MULTICAST_LOOP, Shape::Int, T_YES, false, true, false },
    { INET_IP, 0x1013, IPPROTO_IP, IP_ADD_MEMBERSHIP, Shape::Mreq, 0, false, true, true },
    { INET_IP, 0x1014, IPPROTO_IP, IP_DROP_MEMBERSHIP, Shape::Mreq, 0, false, true, true },
    { INET_UDP, 0x0600, 0, -1, Shape::Int, T_YES, false, true, false },                 /* UDP_CHECKSUM */
};

const Opt *find_opt(uint32_t level, uint32_t name)
{
    for(const auto &o : opts)
        if(o.level == level && o.name == name)
            return &o;
    return nullptr;
}

bool applies(const Opt &o, const Provider &p)
{
    return !(o.tcp_only && p.kind != Kind::TCP) && !(o.udp_only && p.kind != Kind::UDP);
}

uint32_t key(uint32_t level, uint32_t name)
{
    return (level << 16) | (name & 0xFFFF);
}

/* An option's value as the client sent it (big-endian, 1, 2 or 4 bytes
   for a number). */
uint32_t read_int(const std::vector<uint8_t> &v)
{
    switch(v.size())
    {
        case 1:
            return v[0];
        case 2:
            return (v[0] << 8) | v[1];
        default:
            return v.size() >= 4 ? ((uint32_t)v[0] << 24) | (v[1] << 16) | (v[2] << 8) | v[3] : 0;
    }
}

uint32_t be32(const std::vector<uint8_t> &v, size_t at)
{
    return at + 4 <= v.size()
               ? ((uint32_t)v[at] << 24) | (v[at + 1] << 16) | (v[at + 2] << 8) | v[at + 3]
               : 0;
}

void put32(std::vector<uint8_t> &v, size_t at, uint32_t x)
{
    if(v.size() < at + 4)
        v.resize(at + 4);
    v[at] = x >> 24;
    v[at + 1] = x >> 16;
    v[at + 2] = x >> 8;
    v[at + 3] = x;
}

/* Value of the size the client used (at least 4 bytes for a number). */
void put_int(std::vector<uint8_t> &v, uint32_t x)
{
    switch(v.size())
    {
        case 1:
            v[0] = x;
            break;
        case 2:
            v[0] = x >> 8;
            v[1] = x;
            break;
        default:
            v.resize(std::max<size_t>(v.size(), 4));
            put32(v, 0, x);
    }
}

/* Set one option on a socket. */
bool set_on_socket(int fd, const Opt &o, const std::vector<uint8_t> &v)
{
    if(o.so < 0)
        return true;
    switch(o.shape)
    {
        case Shape::Int:
        {
            int x = (int)read_int(v);
            return hostnet::setsockopt(fd, o.sol, o.so, &x, sizeof x) == 0;
        }
        case Shape::Linger:
        {
            struct linger lg = { be32(v, 0) == T_YES, (int)be32(v, 4) };
            return hostnet::setsockopt(fd, o.sol, o.so, &lg, sizeof lg) == 0;
        }
        case Shape::KeepAlive:
        {
            int on = be32(v, 0) == T_YES;
            if(hostnet::setsockopt(fd, o.sol, o.so, &on, sizeof on) != 0)
                return false;
#ifdef TCP_KEEPIDLE
            int secs = (int)be32(v, 4) * 60;
            if(on && secs > 0)
                hostnet::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &secs, sizeof secs);
#endif
            return true;
        }
        case Shape::Mreq:
        {
            struct ip_mreq mr = {};
            mr.imr_multiaddr.s_addr = htonl(be32(v, 0));
            mr.imr_interface.s_addr = htonl(be32(v, 4));
            return hostnet::setsockopt(fd, o.sol, o.so, &mr, sizeof mr) == 0;
        }
    }
    return false;
}

/* The current value, from the socket (or what was kept for it, or the
   default). */
void current(const Provider &p, const Opt &o, std::vector<uint8_t> &v)
{
    auto kept = p.pending_opts.find(key(o.level, o.name));
    if(p.fd < 0 || o.so < 0 || o.write_only)
    {
        if(kept != p.pending_opts.end())
            v = kept->second;
        else if(o.shape == Shape::Int)
            put_int(v, o.dflt);
        else
            v.assign(8, 0);
        return;
    }
    switch(o.shape)
    {
        case Shape::Int:
        {
            int x = 0;
            socklen_t len = sizeof x;
            hostnet::getsockopt(p.fd, o.sol, o.so, &x, &len);
#ifdef __linux__
            if(o.so == SO_SNDBUF || o.so == SO_RCVBUF)
                x /= 2; /* Linux reports twice what was set */
#endif
            if(o.so == TCP_NODELAY || o.so == SO_REUSEADDR || o.so == SO_BROADCAST
               || o.so == SO_DONTROUTE)
                x = x ? T_YES : T_NO;
            put_int(v, x);
            break;
        }
        case Shape::Linger:
        {
            struct linger lg = {};
            socklen_t len = sizeof lg;
            hostnet::getsockopt(p.fd, o.sol, o.so, &lg, &len);
            v.assign(8, 0);
            put32(v, 0, lg.l_onoff ? T_YES : T_NO);
            put32(v, 4, lg.l_linger);
            break;
        }
        case Shape::KeepAlive:
        {
            int on = 0;
            socklen_t len = sizeof on;
            hostnet::getsockopt(p.fd, o.sol, o.so, &on, &len);
            v.assign(8, 0);
            put32(v, 0, on ? T_YES : T_NO);
            uint32_t mins = kept != p.pending_opts.end() ? be32(kept->second, 4) : 120;
#ifdef TCP_KEEPIDLE
            int secs = 0;
            len = sizeof secs;
            if(hostnet::getsockopt(p.fd, IPPROTO_TCP, TCP_KEEPIDLE, &secs, &len) == 0 && secs)
                mins = secs / 60;
#endif
            put32(v, 4, mins);
            break;
        }
        case Shape::Mreq:
            break;
    }
}

struct Option
{
    uint32_t level, name, status;
    std::vector<uint8_t> value;
};

/* The options in a buffer; false if one is malformed. */
bool parse(uint32_t buf, uint32_t len, std::vector<Option> &out)
{
    uint32_t at = 0;
    while(at + 16 <= len)
    {
        uint32_t olen = READUL(buf + at);
        if(olen < 16 || at + olen > len)
            return false;
        Option o{ READUL(buf + at + 4), READUL(buf + at + 8), 0, {} };
        for(uint32_t i = 16; i < olen; i++)
            o.value.push_back(READUB(buf + at + i));
        out.push_back(std::move(o));
        at += (olen + 3) & ~3u;
    }
    return at >= len || len - at < 16;
}

int rank(uint32_t status)
{
    switch(status)
    {
        case T_SUCCESS:
            return 0;
        case T_PARTSUCCESS:
            return 1;
        case T_FAILURE:
            return 2;
        case T_READONLY:
            return 3;
        default:
            return 4; /* T_NOTSUPPORT */
    }
}

} // anonymous namespace

/* OSStatus OTOptionManagement(ref, TOptMgmt *req, TOptMgmt *ret) */
uint32_t Executor::ot::option_management(Provider &p, uint32_t req, uint32_t ret)
{
    if(!req)
        return kOTBadOptionErr;
    uint32_t flags = READUL(req + 12);
    uint32_t inbuf = READUL(req + 8), inlen = READUL(req + 4);
    std::vector<Option> in;
    if(inlen && (!inbuf || !parse(inbuf, inlen, in)))
        return kOTBadOptionErr;

    /* T_ALLOPT: every option of the level. */
    std::vector<Option> work;
    for(auto &o : in)
    {
        if(o.name == T_ALLOPT && (flags == T_CURRENT || flags == T_DEFAULT))
        {
            for(const auto &d : opts)
                if(d.level == o.level && applies(d, p) && !d.write_only)
                    work.push_back({ d.level, d.name, 0, {} });
        }
        else
            work.push_back(o);
    }

    uint32_t worst = T_SUCCESS;
    for(auto &o : work)
    {
        const Opt *d = find_opt(o.level, o.name);
        if(!d || !applies(*d, p))
        {
            o.status = T_NOTSUPPORT;
        }
        else if(flags == T_NEGOTIATE)
        {
            bool ok = true;
            if(p.fd >= 0)
                ok = set_on_socket(p.fd, *d, o.value);
            if(ok)
                p.pending_opts[key(o.level, o.name)] = o.value;
            o.status = ok ? T_SUCCESS : T_FAILURE;
            if(!d->write_only)
                current(p, *d, o.value);
        }
        else if(flags == T_CHECK)
            o.status = T_SUCCESS;
        else if(flags == T_DEFAULT)
        {
            if(d->shape == Shape::Int)
                put_int(o.value, d->dflt);
            else
                o.value.assign(8, 0);
            o.status = T_SUCCESS;
        }
        else if(flags == T_CURRENT)
        {
            current(p, *d, o.value);
            o.status = T_SUCCESS;
        }
        else
            return kOTBadFlagErr;
        if(rank(o.status) > rank(worst))
            worst = o.status;
        OT_LOG("option %08x level $%x name $%x: status $%x\n", p.ref, o.level, o.name, o.status);
    }

    if(ret)
    {
        uint32_t outbuf = READUL(ret + 8), max = READUL(ret), at = 0;
        int32_t err = kOTNoError;
        for(auto &o : work)
        {
            uint32_t olen = 16 + o.value.size();
            uint32_t padded = (olen + 3) & ~3u;
            if(!outbuf || at + olen > max)
            {
                err = kOTBufferOverflowErr;
                break;
            }
            WRITEUL(outbuf + at, olen);
            WRITEUL(outbuf + at + 4, o.level);
            WRITEUL(outbuf + at + 8, o.name);
            WRITEUL(outbuf + at + 12, o.status);
            for(size_t i = 0; i < o.value.size(); i++)
                WRITEUB(outbuf + at + 16 + i, o.value[i]);
            for(uint32_t i = olen; i < padded && at + i < max; i++)
                WRITEUB(outbuf + at + i, 0);
            at += std::min(padded, max - at);
        }
        WRITEUL(ret + 4, at);
        WRITEUL(ret + 12, worst);
        if(err)
            return err;
    }
    return kOTNoError;
}

/* The options kept for a socket that wasn't there yet, now that it is. */
void Executor::ot::apply_pending_options(Provider &p)
{
    if(p.fd < 0)
        return;
    for(auto &[k, v] : p.pending_opts)
        if(const Opt *d = find_opt(k >> 16, k & 0xFFFF))
            if(applies(*d, p) && !set_on_socket(p.fd, *d, v))
                OT_LOG("option %08x level $%x name $%x: could not set\n", p.ref, d->level,
                       d->name);
}

bool Executor::ot::option_was_set(const Provider &p, uint32_t level, uint32_t name)
{
    return p.pending_opts.count(key(level, name)) != 0;
}

/* pascal OSStatus OTNextOption(UInt8 *buffer, UInt32 buflen, TOption **prev):
   *prev nil for the first; nil again after the last. */
uint32_t Executor::ot::next_option(const Call &c)
{
    uint32_t buf = c.l(0), len = c.l(1), prevp = c.l(2);
    if(!buf || !prevp)
        return kOTBadOptionErr;
    uint32_t prev = READUL(prevp), at;
    if(!prev)
        at = 0;
    else
    {
        uint32_t plen = READUL(prev);
        if(prev < buf || plen < 16)
            return kOTBadOptionErr;
        at = prev - buf + ((plen + 3) & ~3u);
    }
    if(at + 16 > len)
    {
        WRITEUL(prevp, 0);
        return kOTNoError;
    }
    uint32_t olen = READUL(buf + at);
    if(olen < 16 || at + olen > len)
        return kOTBadOptionErr;
    WRITEUL(prevp, buf + at);
    return kOTNoError;
}

/* pascal TOption *OTFindOption(UInt8 *buffer, UInt32 buflen, OTXTILevel,
   OTXTIName) */
uint32_t Executor::ot::find_option(const Call &c)
{
    uint32_t buf = c.l(0), len = c.l(1), level = c.l(2), name = c.l(3);
    for(uint32_t at = 0; buf && at + 16 <= len;)
    {
        uint32_t olen = READUL(buf + at);
        if(olen < 16 || at + olen > len)
            return 0;
        if(READUL(buf + at + 4) == level && READUL(buf + at + 8) == name)
            return buf + at;
        at += (olen + 3) & ~3u;
    }
    return 0;
}
