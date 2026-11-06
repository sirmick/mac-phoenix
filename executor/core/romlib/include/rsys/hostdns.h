#pragma once

/* hostdns.h — DNS queries on the host's resolver (net/hostdns.cpp). */

#include <cstdint>
#include <string>
#include <vector>

namespace Executor
{

struct HostRR
{
    std::string name;
    uint16_t type = 0, cls = 0;
    uint32_t ttl = 0;
    int section = 0; /* 1 answer, 2 authority, 3 additional */
    std::vector<uint8_t> rdata; /* names in it expanded (CNAME, NS, PTR, MX) */
};

enum
{
    HostDNSOk = 0,
    HostDNSNoName,    /* no such name */
    HostDNSTimeout,   /* no answer from the name servers */
    HostDNSBadAnswer, /* an answer that doesn't parse */
};

/* res_query(name, cls, type), the records appended to rrs; HostDNSOk also
   when the name exists without such records (rrs then empty). */
int host_dns_query(const std::string &name, int cls, int type, std::vector<HostRR> &rrs);

}
