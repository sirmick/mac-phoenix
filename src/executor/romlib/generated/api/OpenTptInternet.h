/*****************************************************************************
 *                                                                           *
 *                             OpenTptInternet.h                             *
 *                             =================                             *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h          UInt16, UInt32, UInt8, SInt32                     *
 *     OpenTransport.h     OTAddressType, OTConfiguration, OTOpenFlags,      *
 *                         OTClientContextPtr, OTNotifyProcPtr,              *
 *                         OTByteCount, OTFlags                              *
 *     DriverServices.h    OSStatus                                          *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "OpenTransport.h"
#include "DriverServices.h"

#define MODULE_NAME OpenTptInternet
#include <base/api-module.h>


namespace Executor {

typedef UInt16 InetPort;

typedef UInt32 InetHost;

typedef void *InetSvcRef;

enum {kOTAnyInetAddress = 0,
};

// IP_TOS precedence and type of service.
enum {T_ROUTINE = 0,
T_PRIORITY = 1,
T_IMMEDIATE = 2,
T_FLASH = 3,
T_OVERRIDEFLASH = 4,
T_CRITIC_ECP = 5,
T_INETCONTROL = 6,
T_NETCONTROL = 7,
T_NOTOS = 0,
T_LDELAY = 16,
T_HITHRPT = 8,
T_HIREL = 4,
};

struct TIPAddMulticast{GUEST_STRUCT;GUEST<InetHost> multicastGroupAddress;GUEST<InetHost> interfaceAddress;};static_assert(sizeof(TIPAddMulticast) == 8);void describeStruct(const TIPAddMulticast&, std::ostream&);

// The DNR's completion events.
enum {T_DNRSTRINGTOADDRCOMPLETE = 268435457,
T_DNRADDRTONAMECOMPLETE = 268435458,
T_DNRSYSINFOCOMPLETE = 268435459,
T_DNRMAILEXCHANGECOMPLETE = 268435460,
T_DNRQUERYCOMPLETE = 268435461,
};

struct InetAddress{GUEST_STRUCT;GUEST<OTAddressType> fAddressType; // AF_INET
GUEST<InetPort> fPort;GUEST<InetHost> fHost; // network byte order
UInt8 fUnused[8];};static_assert(sizeof(InetAddress) == 16);void describeStruct(const InetAddress&, std::ostream&);

enum {kMaxHostAddrs = 10,
kMaxSysStringLen = 32,
kMaxHostNameLen = 255,
};

typedef char InetDomainName[256];

struct InetHostInfo{GUEST_STRUCT;GUEST<InetDomainName> name;GUEST<InetHost> addrs[10];};static_assert(sizeof(InetHostInfo) == 296);void describeStruct(const InetHostInfo&, std::ostream&);

struct InetSysInfo{GUEST_STRUCT;char cpuType[32];char osType[32];};static_assert(sizeof(InetSysInfo) == 64);void describeStruct(const InetSysInfo&, std::ostream&);

struct InetMailExchange{GUEST_STRUCT;GUEST<UInt16> preference;GUEST<InetDomainName> exchange;};static_assert(sizeof(InetMailExchange) == 258);void describeStruct(const InetMailExchange&, std::ostream&);

struct DNSQueryInfo{GUEST_STRUCT;GUEST<UInt16> qType;GUEST<UInt16> qClass;GUEST<UInt32> ttl;GUEST<InetDomainName> name;GUEST<UInt16> responseType;GUEST<UInt16> resourceLen;char resourceData[4];};static_assert(sizeof(DNSQueryInfo) == 272);void describeStruct(const DNSQueryInfo&, std::ostream&);

struct DNSAddress{GUEST_STRUCT;GUEST<OTAddressType> fAddressType; // AF_DNS
GUEST<InetDomainName> fName;};static_assert(sizeof(DNSAddress) == 258);void describeStruct(const DNSAddress&, std::ostream&);

struct InetInterfaceInfo{GUEST_STRUCT;GUEST<InetHost> fAddress;GUEST<InetHost> fNetmask;GUEST<InetHost> fBroadcastAddr;GUEST<InetHost> fDefaultGatewayAddr;GUEST<InetHost> fDNSAddr;GUEST<UInt16> fVersion;GUEST<UInt16> fHWAddrLen;GUEST<UInt8*> fHWAddr;GUEST<UInt32> fIfMTU;GUEST<UInt8*> fReservedPtrs[2];GUEST<InetDomainName> fDomainName;UInt8 fReserved[256];};static_assert(sizeof(InetInterfaceInfo) == 552);void describeStruct(const InetInterfaceInfo&, std::ostream&);

enum {kDefaultInetInterface = -1,
kInetInterfaceInfoVersion = 2,
};

namespace ot { uint32_t open_priv(const Call &); }OT_FUNCTION(OTInetClientLib, OTOpenInternetServicesPriv, "ot:inet$LclInetClnt", "OTOpenInternetServicesPriv", false, "LLLL", 'L', open_priv);


namespace ot { uint32_t async_open_priv(const Call &); }OT_FUNCTION(OTInetClientLib, OTAsyncOpenInternetServicesPriv, "ot:inet$LclInetClnt", "OTAsyncOpenInternetServicesPriv", false, "LLLLL", 'L', async_open_priv);


namespace ot { uint32_t string_to_address(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetStringToAddress, "ot:inet$LclInetClnt", "OTINETSTRINGTOADDRESS", true, "LLL", 'L', string_to_address);


namespace ot { uint32_t address_to_name(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetAddressToName, "ot:inet$LclInetClnt", "OTINETADDRESSTONAME", true, "LLL", 'L', address_to_name);


namespace ot { uint32_t init_inet_address(const Call &); }OT_FUNCTION(OTInetClientLib, OTInitInetAddress, "ot:inet$LclInetClnt", "OTINITINETADDRESS", true, "LWL", 'V', init_inet_address);


namespace ot { uint32_t init_dns_address(const Call &); }OT_FUNCTION(OTInetClientLib, OTInitDNSAddress, "ot:inet$LclInetClnt", "OTINITDNSADDRESS", true, "LL", 'L', init_dns_address);


namespace ot { uint32_t string_to_host(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetStringToHost, "ot:inet$LclInetClnt", "OTINETSTRINGTOHOST", true, "LL", 'L', string_to_host);


namespace ot { uint32_t host_to_string(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetHostToString, "ot:inet$LclInetClnt", "OTINETHOSTTOSTRING", true, "LL", 'V', host_to_string);


namespace ot { uint32_t get_interface_info(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetGetInterfaceInfo, "ot:inet$LclInetClnt", "OTINETGETINTERFACEINFO", true, "LL", 'L', get_interface_info);


namespace ot { uint32_t sys_info(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetSysInfo, "ot:inet$LclInetClnt", "OTINETSYSINFO", true, "LLL", 'L', sys_info);


namespace ot { uint32_t mail_exchange(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetMailExchange, "ot:inet$LclInetClnt", "OTINETMAILEXCHANGE", true, "LLLL", 'L', mail_exchange);


namespace ot { uint32_t query(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetQuery, "ot:inet$LclInetClnt", "OTINETQUERY", true, "LLWWLLLLL", 'L', query);


} /* namespace Executor */