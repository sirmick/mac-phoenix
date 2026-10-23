/* ot_client.cpp — Open Transport's clients: InitOpenTransport,
 * CloseOpenTransport, OTRegisterAsClient and the client's memory.
 *
 * Each application has its own __gOTClientRecord (36 bytes in its
 * globals); the glue passes its address last to the ...Priv functions,
 * and it identifies the client here. InitOpenTransport's glue has already
 * asked Gestalt and started the Shared Library Manager (InitLibraryManager,
 * LoadClass "OTClnt$LclUtils") when InitOpenTransportPriv runs. */

#include "ot_internal.h"
#include "ot_client.h"

#include <MemoryMgr.h>

#include <map>

using namespace Executor;
using namespace Executor::ot;

namespace
{

std::map<uint32_t, Client> clients; /* by client record */

/* OSStatus InitOpenTransportPriv(UInt32 kind, OTClientRecord *) — kind 3
   for an application (InitOpenTransport), others for utilities only. */
uint32_t init_priv(const Call &c)
{
    uint32_t kind = c.l(0), record = c.l(1);
    Client &cl = clients[record];
    cl.record = record;
    cl.kind = kind;
    cl.inits++;
    OT_LOG("InitOpenTransport: client $%08x kind %u (%d)\n", record, kind, cl.inits);
    return kOTNoError;
}

/* void CloseOpenTransportPriv(OTClientRecord *) */
uint32_t close_priv(const Call &c)
{
    uint32_t record = c.l(0);
    OT_LOG("CloseOpenTransport: client $%08x\n", record);
    close_client_providers(record);
    clients.erase(record);
    return 0;
}

/* OSStatus OTRegisterAsClientPriv(OTClientName, OTNotifyProcPtr,
   OTClientRecord *) */
uint32_t register_priv(const Call &c)
{
    Client &cl = clients[c.l(2)];
    cl.record = c.l(2);
    cl.notifier = c.l(1);
    OT_LOG("OTRegisterAsClient \"%s\"\n", guest_cstring(c.l(0)).c_str());
    return kOTNoError;
}

/* OSStatus OTUnregisterAsClientPriv(OTClientRecord *) */
uint32_t unregister_priv(const Call &c)
{
    auto it = clients.find(c.l(0));
    if(it != clients.end())
        it->second.notifier = 0;
    return kOTNoError;
}

/* void *OTAllocMemPriv(size_t, OTClientRecord *): from the current heap,
   the application's (OT took it from the client's pool there). */
uint32_t alloc_mem_priv(const Call &c)
{
    Ptr p = NewPtr(c.l(0));
    return guest_addr(p);
}

/* void OTFreeMemPriv(void *, OTClientRecord *) */
uint32_t free_mem_priv(const Call &c)
{
    if(Ptr p = c.p<char>(0))
        DisposePtr(p);
    return 0;
}

const char utils[] = "OTClnt$LclUtils";

const Def table[] = {
    { utils, "InitOpenTransportPriv", Conv::C, "LL", 'L', init_priv },
    { utils, "CloseOpenTransportPriv", Conv::C, "L", 'V', close_priv },
    { utils, "OTRegisterAsClientPriv", Conv::C, "LLL", 'L', register_priv },
    { utils, "OTUnregisterAsClientPriv", Conv::C, "L", 'L', unregister_priv },
    { utils, "OTAllocMemPriv", Conv::C, "LL", 'L', alloc_mem_priv },
    { utils, "OTFreeMemPriv", Conv::C, "LL", 'V', free_mem_priv },
};

} // anonymous namespace

void Executor::ot::register_client()
{
    register_table(table);
}

void Executor::ot::close_clients_in(uint32_t lo, uint32_t hi)
{
    for(auto it = clients.begin(); it != clients.end();)
    {
        if(it->first >= lo && it->first < hi)
        {
            OT_LOG("client $%08x: its process quit\n", it->first);
            close_client_providers(it->first);
            it = clients.erase(it);
        }
        else
            ++it;
    }
}

Client *Executor::ot::find_client(uint32_t record)
{
    auto it = clients.find(record);
    return it == clients.end() ? nullptr : &it->second;
}
