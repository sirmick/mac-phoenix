/* ot_atalk.cpp — Open Transport's AppleTalk client library (OTATalkClientLib)
 * as far as the TCP/IP control panel's PowerPC code imports it: Executor's
 * OT has no AppleTalk, so each call answers kOTNotSupportedErr, logged.
 * Answering the library at all keeps Apple's file out: it needs Apple's OT
 * kernel (OTNtvUtilLib), whose initialisation fails, and the panel then
 * says Open Transport isn't there. */

#include "ot_internal.h"

#include <base/traps.h>

#include <string>

using namespace Executor;
using namespace Executor::ot;

namespace Executor::ot
{

namespace
{
uint32_t no_appletalk(const char *name)
{
    traps::log_unimplemented(std::string("Open Transport ") + name
                             + " (OTATalkClientLib): no AppleTalk, answered kOTNotSupportedErr");
    return (uint32_t)(int32_t)kOTNotSupportedErr;
}
}

/* OSStatus OTAsyncOpenAppleTalkServicesPriv(OTConfiguration *, OTOpenFlags,
   OTNotifyProcPtr, void *context, client) */
uint32_t at_open_services(const Call &)
{
    return no_appletalk("OTAsyncOpenAppleTalkServicesPriv");
}

/* OSStatus OTATalkGetMyZone(ATSvcRef, TNetbuf *zone) */
uint32_t at_my_zone(const Call &)
{
    return no_appletalk("OTATalkGetMyZone");
}

/* OSStatus OTATalkGetZoneList(ATSvcRef, TNetbuf *zones) */
uint32_t at_zone_list(const Call &)
{
    return no_appletalk("OTATalkGetZoneList");
}

} // namespace Executor::ot
