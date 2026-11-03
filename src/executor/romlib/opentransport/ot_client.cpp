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
#include <mman/mman.h>
#include <rsys/process.h>

#include <map>

using namespace Executor;
using namespace Executor::ot;

namespace Executor::ot
{

std::map<uint32_t, Client> clients; /* by client record */

/* OTAllocMem's blocks, by address: the client record they were taken for
   (0 none given), given back when that client closes. */
std::map<uint32_t, uint32_t> client_blocks;

void release_client_memory(uint32_t record, uint32_t lo = 0, uint32_t hi = 0)
{
    for(auto it = client_blocks.begin(); it != client_blocks.end();)
    {
        if(it->second == record)
        {
            /* not what was in the quitting process's partition: gone */
            if(it->first < lo || it->first >= hi)
                DisposePtr((Ptr)SYN68K_TO_US(it->first));
            it = client_blocks.erase(it);
        }
        else
            ++it;
    }
}

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
    release_client_memory(record);
    clients.erase(record);
    return 0;
}

/* void OTExitPatchPriv(OTClientContextPtr): OT 1.1.1's PowerPC glue
   (Hotline Connect 1.9's) gives OT, in the client record, a way out that
   calls this with the record as the process quits: the client closed, as
   by CloseOpenTransport. Executor closes a quitting process's clients
   itself (ROMlib_ot_process_exit); a client closed already is let be. */
uint32_t exit_patch_priv(const Call &c)
{
    if(clients.count(c.l(0)))
        return close_priv(c);
    return 0;
}

/* OSStatus OTSetMemoryLimits(size_t growSize, size_t maxSize): how far
   OT may grow the client's memory pool. Executor's OT takes the client's
   memory as it goes (OTAllocMem), so there is no pool to limit:
   accepted. Hotline Connect 1.9 asks for (128K, 0) after
   InitOpenTransport and ignores the answer. */
uint32_t set_memory_limits(const Call &c)
{
    OT_LOG("OTSetMemoryLimits(%u, %u)\n", c.l(0), c.l(1));
    return kOTNoError;
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

/* void *OTAllocMemPriv(size_t, OTClientRecord *): OT's client pool is
   not the application's heap: what it hands out leaves FreeMem as it was.
   REALbasic's runtime (GameBox Classic on 8.6) takes up to 100 128K
   network buffers with OTAllocMem while FreeMem stays over 256K; from the
   heap that left the program nothing. Here from temporary memory (the
   Process Manager's zone), the heap when that is full; given back when
   the client closes or its process quits. */
uint32_t alloc_mem_priv(const Call &c)
{
    Ptr p = nullptr;
    if(ROMlib_pm_zone)
    {
        TheZoneGuard guard(ROMlib_pm_zone);
        p = NewPtr(c.l(0));
    }
    if(!p)
        p = NewPtr(c.l(0));
    if(p)
        client_blocks[guest_addr(p)] = c.l(1);
    return guest_addr(p);
}

/* void OTFreeMemPriv(void *, OTClientRecord *), and void OTFreeMem(void *):
   OT 1.1.1's OTClientUtilLib export, which REALbasic's RBOTLib imports */
uint32_t free_mem_priv(const Call &c)
{
    if(Ptr p = c.p<char>(0))
    {
        client_blocks.erase(guest_addr(p));
        DisposePtr(p);
    }
    return 0;
}

} // namespace Executor::ot

void Executor::ot::close_clients_in(uint32_t lo, uint32_t hi)
{
    for(auto it = clients.begin(); it != clients.end();)
    {
        if(it->first >= lo && it->first < hi)
        {
            OT_LOG("client $%08x: its process quit\n", it->first);
            close_client_providers(it->first);
            release_client_memory(it->first, lo, hi);
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
