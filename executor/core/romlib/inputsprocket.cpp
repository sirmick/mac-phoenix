/* MacPhoenix: InputSprocket (multiversal/defs/InputSprocket.yaml), from the
   Game Sprockets documentation (InputSprocket.h 1.7). InputSprocketLib as
   on a Mac with InputSprocket installed and no devices: nothing to
   extract or configure, virtual elements that stay idle (buttons up, axes
   centred, no events). A game sees InputSprocket there and reads the
   keyboard itself, as it does when the user hasn't assigned it a device:
   Slithereens and Mars Rising import InputSprocketLib and failed to
   launch without it (M1.md #15). */

#include <base/common.h>
#include <InputSprocket.h>
#include <MemoryMgr.h>
#include <mman/mman.h>

#include <algorithm>

using namespace Executor;

namespace
{
/* A virtual element or element list: guest memory the reference points
   to, marked so a stray reference is told apart. A virtual element keeps
   its kind (its idle state) and the data size NewVirtual asked for. */
struct isp_object
{
    GUEST<OSType> magic;
    GUEST<OSType> kind;
    GUEST<UInt32> dataSize;
};

const OSType element_magic = "ISpE"_4, list_magic = "ISpL"_4;

isp_object *object(void *ref, OSType magic)
{
    isp_object *o = (isp_object *)ref;
    return o && o->magic == magic ? o : nullptr;
}

ISpElementReference new_element(OSType kind, UInt32 dataSize)
{
    isp_object *o = (isp_object *)NewPtrClear(sizeof(isp_object) + dataSize);
    if(!o)
        return nullptr;
    o->magic = element_magic;
    o->kind = kind;
    o->dataSize = dataSize;
    return (ISpElementReference)o;
}

ISpElementListReference new_list()
{
    isp_object *o = (isp_object *)NewPtrClear(sizeof(isp_object));
    if(!o)
        return nullptr;
    o->magic = list_magic;
    return (ISpElementListReference)o;
}

void no_event(ISpElementEventPtr event, UInt32 bufSize, Boolean *wasEvent)
{
    if(event && bufSize)
        memset(event, 0, std::min<UInt32>(bufSize, sizeof(ISpElementEvent)));
    if(wasEvent)
        *wasEvent = false;
}
}

NumVersion Executor::C_ISpGetVersion()
{
    NumVersion v;
    v.majorRev = 1;
    v.minorAndBugRev = 0x70; /* 1.7 */
    v.stage = 0x80;          /* final */
    v.nonRelRev = 0;
    return v;
}

OSStatus Executor::C_ISpStartup()
{
    return noErr;
}

OSStatus Executor::C_ISpShutdown()
{
    return noErr;
}

OSStatus Executor::C_ISpTickle()
{
    return noErr;
}

/* The needs' elements were made by NewVirtualFromNeeds; with no device
   there is nothing to bind them to. */
OSStatus Executor::C_ISpInit(UInt32 count, ISpNeed *needs, GUEST<ISpElementReference> *inReferences,
                             OSType appCreatorCode, OSType subCreatorCode, UInt32 flags,
                             INTEGER setListResourceId, UInt32 reserved)
{
    return noErr;
}

/* No device to configure: back at once, as the dialog's Cancel. */
OSStatus Executor::C_ISpConfigure(ISpEventProcPtr inEventProcPtr, void *inReserved)
{
    return noErr;
}

OSStatus Executor::C_ISpStop()
{
    return noErr;
}

OSStatus Executor::C_ISpSuspend()
{
    return noErr;
}

OSStatus Executor::C_ISpResume()
{
    return noErr;
}

OSStatus Executor::C_ISpDevices_Extract(UInt32 inBufferCount, GUEST<UInt32> *outCount,
                                        GUEST<ISpDeviceReference> *buffer)
{
    if(outCount)
        *outCount = 0;
    return noErr;
}

OSStatus Executor::C_ISpDevices_ExtractByClass(ISpDeviceClass inClass, UInt32 inBufferCount,
                                               GUEST<UInt32> *outCount, GUEST<ISpDeviceReference> *buffer)
{
    if(outCount)
        *outCount = 0;
    return noErr;
}

OSStatus Executor::C_ISpDevices_ActivateClass(ISpDeviceClass inClass)
{
    return noErr;
}

OSStatus Executor::C_ISpDevices_DeactivateClass(ISpDeviceClass inClass)
{
    return noErr;
}

OSStatus Executor::C_ISpDevices_Activate(UInt32 inDeviceCount, GUEST<ISpDeviceReference> *inDevicesToActivate)
{
    return inDeviceCount ? paramErr : noErr; /* there are none to name */
}

OSStatus Executor::C_ISpDevices_Deactivate(UInt32 inDeviceCount, GUEST<ISpDeviceReference> *inDevicesToDeactivate)
{
    return inDeviceCount ? paramErr : noErr;
}

OSStatus Executor::C_ISpElement_NewVirtual(UInt32 dataSize, GUEST<ISpElementReference> *outElement, UInt32 flags)
{
    if(!outElement)
        return paramErr;
    ISpElementReference e = new_element(kISpElementKind_Virtual, dataSize);
    *outElement = e;
    return e ? noErr : memFullErr;
}

OSStatus Executor::C_ISpElement_NewVirtualFromNeeds(UInt32 count, ISpNeed *needs,
                                                    GUEST<ISpElementReference> *outElements, UInt32 flags)
{
    if(count && (!needs || !outElements))
        return paramErr;
    for(UInt32 i = 0; i < count; i++)
    {
        /* a movement's complex state: two axes and a direction, 12 bytes */
        OSType kind = needs[i].theKind;
        ISpElementReference e = new_element(kind, kind == kISpElementKind_Movement ? 12 : 4);
        if(!e)
        {
            C_ISpElement_DisposeVirtual(i, outElements);
            return memFullErr;
        }
        outElements[i] = e;
    }
    return noErr;
}

OSStatus Executor::C_ISpElement_DisposeVirtual(UInt32 count, GUEST<ISpElementReference> *inElements)
{
    for(UInt32 i = 0; inElements && i < count; i++)
        if(isp_object *o = object(inElements[i], element_magic))
        {
            o->magic = 0;
            DisposePtr((Ptr)o);
        }
    return noErr;
}

/* Idle: a button up, a pad idle, an axis centred, a delta none. */
OSStatus Executor::C_ISpElement_GetSimpleState(ISpElementReference inElement, GUEST<UInt32> *outState)
{
    isp_object *o = object(inElement, element_magic);
    if(!o || !outState)
        return paramErr;
    *outState = o->kind == kISpElementKind_Axis ? (UInt32)kISpAxisMiddle : 0;
    return noErr;
}

OSStatus Executor::C_ISpElement_GetComplexState(ISpElementReference inElement, UInt32 bufferSize, void *outState)
{
    isp_object *o = object(inElement, element_magic);
    if(!o || !outState)
        return paramErr;
    if(bufferSize < o->dataSize)
        return kISpBufferToSmallErr;
    memset(outState, 0, o->dataSize);
    return noErr;
}

OSStatus Executor::C_ISpElement_GetNextEvent(ISpElementReference inElement, UInt32 bufSize,
                                             ISpElementEventPtr event, Boolean *wasEvent)
{
    no_event(event, bufSize, wasEvent);
    return object(inElement, element_magic) ? noErr : paramErr;
}

OSStatus Executor::C_ISpElement_Flush(ISpElementReference inElement)
{
    return object(inElement, element_magic) ? noErr : paramErr;
}

OSStatus Executor::C_ISpElementList_New(UInt32 inCount, GUEST<ISpElementReference> *inElements,
                                        GUEST<ISpElementListReference> *outElementList, UInt32 flags)
{
    if(!outElementList)
        return paramErr;
    ISpElementListReference l = new_list();
    *outElementList = l;
    return l ? noErr : memFullErr;
}

OSStatus Executor::C_ISpElementList_Dispose(ISpElementListReference inElementList)
{
    isp_object *o = object(inElementList, list_magic);
    if(!o)
        return paramErr;
    o->magic = 0;
    DisposePtr((Ptr)o);
    return noErr;
}

/* The system's list of every device element: none. One for the whole
   session, in the System heap. */
OSStatus Executor::C_ISpGetGlobalElementList(GUEST<ISpElementListReference> *outElementList)
{
    static ISpElementListReference global;
    if(!outElementList)
        return paramErr;
    if(!global)
    {
        TheZoneGuard guard(LM(SysZone));
        global = new_list();
    }
    *outElementList = global;
    return global ? noErr : memFullErr;
}

OSStatus Executor::C_ISpElementList_AddElements(ISpElementListReference inElementList, UInt32 refCon,
                                                UInt32 count, GUEST<ISpElementReference> *newElements)
{
    return object(inElementList, list_magic) ? noErr : paramErr;
}

OSStatus Executor::C_ISpElementList_RemoveElements(ISpElementListReference inElementList, UInt32 count,
                                                   GUEST<ISpElementReference> *oldElement)
{
    return object(inElementList, list_magic) ? noErr : paramErr;
}

OSStatus Executor::C_ISpElementList_GetNextEvent(ISpElementListReference inElementList, UInt32 bufSize,
                                                 ISpElementEventPtr event, Boolean *wasEvent)
{
    no_event(event, bufSize, wasEvent);
    return object(inElementList, list_magic) ? noErr : paramErr;
}

OSStatus Executor::C_ISpElementList_Flush(ISpElementListReference inElementList)
{
    return object(inElementList, list_magic) ? noErr : paramErr;
}
