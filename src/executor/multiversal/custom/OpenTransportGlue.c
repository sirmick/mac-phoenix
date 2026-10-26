/* OpenTransportGlue.c — Open Transport's public calls that the libraries
 * don't export themselves (OpenTransport.yaml, OpenTptInternet.yaml): each
 * calls the library's ...Priv routine with the application's client
 * record, the state OT keeps for it, in the application's globals.
 *
 * PowerPC only so far; on 68k the same calls bind through the Shared
 * Library Manager, glue still to be written. */

#include "Multiverse.h"

#if TARGET_CPU_PPC

/* The record, as InitOpenTransportPriv expects it (seen from outside: what
   Apple's glue hands Mac OS 9's OT, and what OT does with it): 36 bytes, OT's
   but for words 2-4, which point at storage of the client's own after it (a
   long, two longs and a routine descriptor), and the byte at 32, set. The
   descriptor is the client's way out: a routine that closes it, which OT
   calls itself when it has to. */
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

/* What InitOpenTransport passes for an application (InitOpenTransportPriv's
   first argument). */
enum { kOTInitForApplication = 3 };

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

#endif
