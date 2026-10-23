/* hostdns.cpp — DNS queries on the host's resolver (res_query), for the
 * Toolbox's resolvers that ask more than names and addresses: MacTCP's
 * 'dnrp' (HInfo, MXInfo) and Open Transport's Internet services
 * (OTInetSysInfo, OTInetMailExchange, OTInetQuery). The answer is parsed
 * here (RFC 1035): the header, the questions skipped, then the resource
 * records of the three sections, names in record data expanded. */

#include <rsys/hostdns.h>

#include <arpa/nameser.h>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>

#include <algorithm>

using namespace Executor;

namespace
{

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

} // anonymous namespace

int Executor::host_dns_query(const std::string &name, int cls, int type, std::vector<HostRR> &rrs)
{
    std::vector<uint8_t> m(8192);
    int n = res_query(name.c_str(), cls, type, m.data(), m.size());
    if(n < 0)
    {
        switch(h_errno)
        {
            case HOST_NOT_FOUND:
                return HostDNSNoName;
            case NO_DATA:
                return HostDNSOk; /* the name is there, no such records */
            case TRY_AGAIN:
                return HostDNSTimeout;
            default:
                return HostDNSNoName;
        }
    }
    m.resize(std::min<int>(n, m.size()));
    if(m.size() < 12)
        return HostDNSBadAnswer;
    unsigned counts[4];
    for(int i = 0; i < 4; i++)
        counts[i] = (m[4 + 2 * i] << 8) | m[5 + 2 * i];
    size_t off = 12;
    std::string qname;
    for(unsigned i = 0; i < counts[0]; i++)
        if(!read_dns_name(m, off, qname) || (off += 4) > m.size())
            return HostDNSBadAnswer;
    for(int section = 1; section <= 3; section++)
        for(unsigned i = 0; i < counts[section]; i++)
        {
            HostRR rr;
            rr.section = section;
            if(!read_dns_name(m, off, rr.name) || off + 10 > m.size())
                return HostDNSBadAnswer;
            rr.type = (m[off] << 8) | m[off + 1];
            rr.cls = (m[off + 2] << 8) | m[off + 3];
            rr.ttl = ((uint32_t)m[off + 4] << 24) | (m[off + 5] << 16) | (m[off + 6] << 8) | m[off + 7];
            size_t len = (m[off + 8] << 8) | m[off + 9];
            off += 10;
            if(off + len > m.size())
                return HostDNSBadAnswer;
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
    return HostDNSOk;
}

