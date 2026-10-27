/* OpenTransportGlue.c — Open Transport's public calls that the libraries
 * don't export themselves (OpenTransport.yaml, OpenTptInternet.yaml): each
 * calls the library's ...Priv routine with the application's client
 * record, the state OT keeps for it, in the application's globals.
 *
 * PowerPC code imports the ...Priv routines from OT's CFM libraries. 68k
 * code reaches them through the Shared Library Manager (ASLM): the
 * application gets a TLibraryManager of its own from ASLM's root manager,
 * then each routine by its function set and its index there
 * (TLibraryManager::GetFunctionPointer, the ASLM SDK's LibraryManager.h;
 * Apple's ASLM has no names for OT's sets); the wrappers generated for the
 * `aslm:` entries call __OTFunction.
 *
 * What a record holds before InitOpenTransportPriv was seen from outside:
 * what Apple's glue hands OT (Executor logging it, and Mac OS 9's OT read
 * by a probe), and what OT does with it. */

#include "Multiverse.h"

#include <stdlib.h>

/* What InitOpenTransport passes for an application (InitOpenTransportPriv's
   first argument). */
enum { kOTInitForApplication = 3 };

#if TARGET_CPU_PPC

/* The record: 36 bytes, OT's but for words 2-4, which point at storage of
   the client's own after it (a long, two longs and a routine descriptor),
   and the byte at 32, set. The descriptor is the client's way out: a
   routine that closes it, which OT calls itself when it has to. */
static struct
{
    UInt32 words[8];
    UInt8 flag;
    UInt8 pad[3];
    UInt32 a;
    UInt32 b[2];
    RoutineDescriptor close;
} client_record;
#define CLIENT ((OTClientContextPtr)&client_record)

static pascal long close_client(void)
{
    CloseOpenTransportPriv(CLIENT);
    return 0;
}

pascal OSStatus InitOpenTransport(void)
{
    RoutineDescriptor *rd = &client_record.close;

    rd->goMixedModeTrap = MIXED_MODE_TRAP;
    rd->version = 7;
    rd->routineCount = 0; /* one record */
    rd->routineRecords[0].procInfo = 0x30; /* pascal, a long result */
    rd->routineRecords[0].ISA = kPowerPCISA;
    rd->routineRecords[0].routineFlags = 4; /* kUseNativeISA */
    rd->routineRecords[0].procDescriptor = (ProcPtr)close_client;
    client_record.words[2] = (UInt32)&client_record.a;
    client_record.words[3] = (UInt32)client_record.b;
    client_record.words[4] = (UInt32)rd;
    client_record.flag = 1;
    return InitOpenTransportPriv(kOTInitForApplication, CLIENT);
}

pascal void CloseOpenTransport(void)
{
    CloseOpenTransportPriv(CLIENT);
}

#else /* 68k */

/* The record: 36 bytes, then the two the client keeps for ASLM, SLMGlobal
   and its TLibraryManager, which words 3 and 2 point at; word 7 and the
   byte at 32 set. */
static struct
{
    UInt32 words[7];
    UInt32 flags;
    UInt8 flag;
    UInt8 pad[3];
    void *slm;
    void *manager;
} client_record;
#define CLIENT ((OTClientContextPtr)&client_record)

/* A slot of an object's vtable (SC's layout, LibraryManager.h). */
#define SLOT(obj, off) (*(void **)(*(char **)(obj) + (off)))

/* SLMGlobal (ExpandMem+$10C): +$10 the root manager, whose vtable's +$9C
   makes a client's TLibraryManager and +$A0 frees it. A TLibraryManager's
   LoadClass is slot 16 ($40), GetFunctionPointer by index (1-based) slot
   20 ($50). */
typedef OSErr (*MakeManager)(void *root, void **manager, void *frame, unsigned long pool,
                             long zone, long memory, long nonApplication);
typedef Boolean (*FreeManager)(void *root, void **manager, long nonApplication);
typedef OSErr (*LoadClass)(void *manager, const char *id, long loadAll);
typedef void *(*GetFunctionPointer)(void *manager, const char *set, unsigned long index,
                                    OSErr *err);

/* Bumped with each manager: the wrappers look their routines up again. */
long __OTGeneration = 1;

static void *root_manager(void)
{
    char *em = *(char **)0x2B6;
    client_record.slm = em ? *(void **)(em + 0x10C) : NULL;
    return client_record.slm ? *(void **)((char *)client_record.slm + 0x10) : NULL;
}

static OSStatus make_manager(void)
{
    /* What Apple's glue points the frame argument at; ASLM's business. */
    long frame[5] = { 0x400, 0, 1, 0, 1 };
    void *root = root_manager();
    if(!root)
        return kOTNotFoundErr;
    __OTGeneration++;
    return ((MakeManager)SLOT(root, 0x9C))(root, &client_record.manager, frame, 0x400,
                                          3 /* kApplicZone */, 1 /* kNormalMemory */, 0);
}

static void free_manager(void)
{
    void *root = root_manager();
    if(root && client_record.manager)
        ((FreeManager)SLOT(root, 0xA0))(root, &client_record.manager, 0);
    client_record.manager = NULL;
    __OTGeneration++;
}

void *__OTFunction(const char *set, unsigned long index)
{
    void *m = client_record.manager;
    OSErr err = noErr;
    if(!m)
        return NULL;
    if(((LoadClass)SLOT(m, 0x40))(m, set, 0) != noErr)
        return NULL;
    return ((GetFunctionPointer)SLOT(m, 0x50))(m, set, index, &err);
}

static Boolean closed = true;

static void close_at_exit(void)
{
    if(!closed)
        CloseOpenTransport();
}

pascal OSStatus InitOpenTransport(void)
{
    static Boolean registered;
    long response;
    OSStatus err;

    /* Asked first, as Apple's glue does: OT knows its next manager is a
       client's. */
    if(Gestalt(gestaltOpenTpt, &response) || !(response & gestaltOpenTptPresent))
        return kOTNotFoundErr;
    if(!client_record.manager && (err = make_manager()) != noErr)
        return err;
    client_record.words[2] = (UInt32)&client_record.manager;
    client_record.words[3] = (UInt32)&client_record.slm;
    client_record.flags = 0x01000100;
    client_record.flag = 1;
    if(!registered)
    {
        registered = true;
        atexit(close_at_exit);
    }
    err = InitOpenTransportPriv(kOTInitForApplication, CLIENT);
    closed = err != noErr;
    return err;
}

pascal void CloseOpenTransport(void)
{
    CloseOpenTransportPriv(CLIENT);
    closed = true;
    free_manager();
}

#endif

pascal OSStatus OTRegisterAsClient(OTClientName name, OTNotifyProcPtr proc)
{
    return OTRegisterAsClientPriv(name, proc, CLIENT);
}

pascal OSStatus OTUnregisterAsClient(void)
{
    return OTUnregisterAsClientPriv(CLIENT);
}

void *OTAllocMem(OTByteCount size)
{
    return OTAllocMemPriv(size, CLIENT);
}

void OTFreeMem(void *mem)
{
    OTFreeMemPriv(mem, CLIENT);
}

pascal EndpointRef OTOpenEndpoint(OTConfiguration *config, OTOpenFlags oflag,
                                  TEndpointInfo *info, OSStatus *err)
{
    return OTOpenEndpointPriv(config, oflag, info, err, CLIENT);
}

pascal OSStatus OTAsyncOpenEndpoint(OTConfiguration *config, OTOpenFlags oflag,
                                    TEndpointInfo *info, OTNotifyProcPtr proc,
                                    void *contextPtr)
{
    return OTAsyncOpenEndpointPriv(config, oflag, info, proc, contextPtr, CLIENT);
}

pascal OSStatus OTCloseProvider(ProviderRef ref)
{
    return OTCloseProviderPriv(ref, CLIENT);
}

pascal void *OTAlloc(EndpointRef ref, OTStructType structType, UInt32 fields,
                     OSStatus *err)
{
    return OTAllocPriv(ref, structType, fields, err, CLIENT);
}

/* A task's owner is not the record but what OT put in its first word at
   InitOpenTransport. */
pascal SInt32 OTCreateDeferredTask(OTProcessProcPtr proc, void *arg)
{
    return OTCreateDeferredTaskPriv(proc, arg, (void *)client_record.words[0]);
}

pascal SInt32 OTCreateSystemTask(OTProcessProcPtr proc, void *arg)
{
    return OTCreateSystemTaskPriv(proc, arg, (void *)client_record.words[0]);
}

pascal InetSvcRef OTOpenInternetServices(OTConfiguration *cfig, OTOpenFlags oflag,
                                         OSStatus *err)
{
    return OTOpenInternetServicesPriv(cfig, oflag, err, CLIENT);
}

pascal OSStatus OTAsyncOpenInternetServices(OTConfiguration *cfig, OTOpenFlags oflag,
                                            OTNotifyProcPtr proc, void *contextPtr)
{
    return OTAsyncOpenInternetServicesPriv(cfig, oflag, proc, contextPtr, CLIENT);
}
