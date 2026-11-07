/* Copyright 1994, 1995, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* AppleEvents implementation */

#include <base/common.h>

#include <EventMgr.h>
#include <NotifyMgr.h>
#include <AppleEvents.h>
#include <MemoryMgr.h>
#include <ProcessMgr.h>
#include <OSEvent.h>
#include <OSUtil.h>
#include <ProcessMgr.h>
#include <AliasMgr.h>

#include <mman/mman.h>
#include <appleevent/apple_events.h>
#include <commandline/flags.h>
#include <base/functions.impl.h>
#include <base/traps.h>

namespace Executor
{
bool send_application_open_aevt_p;
}
using namespace Executor;

/* dispatching apple events */

/* MacPhoenix: the application's table, then the system's; in each the
   exact entry, then the wildcard ones (8.6's Script Editor installs only
   '****' '****' and gets its 'oapp' there) */
static OSErr find_handler(AEEventClass event_class, AEEventID event_id,
                          GUEST<AEEventHandlerUPP> *hdlr_s, GUEST<int32_t> *refcon_s)
{
    OSErr err = errAEHandlerNotFound;
    for(bool system_p : { false, true })
        for(int i = 0; i < 4 && err != noErr; i++)
            err = AEGetEventHandler(i & 2 ? typeWildCard : event_class, i & 1 ? typeWildCard : event_id,
                                    hdlr_s, refcon_s, system_p);
    return err;
}

/* MacPhoenix: AESend to kCurrentProcess, as 8.6's Apple Event Manager
   does it (difftest aevt, G3): the handler is called at once, before
   AESend returns, with kAEDirectCall as the event's source. Waiting for
   the reply, the handler gets one to fill: 'aevt' 'ansr' with the
   event's return ID; an error it answers (-1708 when there is no
   handler) goes in the reply as 'errn' and is AESend's result. Without
   a reply, the reply is a null descriptor. BBEdit Lite 6.1 makes its
   untitled window by sending itself 'core' 'crel' and waiting. */
static OSErr send_direct(AppleEvent *evt, AppleEvent *reply, bool want_reply)
{
    GUEST<AEEventClass> class_s;
    GUEST<AEEventID> id_s;
    GUEST<DescType> dummy_type;
    GUEST<Size> dummy_size;
    OSErr err = AEGetAttributePtr(evt, keyEventClassAttr, typeType, &dummy_type, &class_s,
                                  sizeof class_s, &dummy_size);
    if(err == noErr)
        err = AEGetAttributePtr(evt, keyEventIDAttr, typeType, &dummy_type, &id_s, sizeof id_s,
                                &dummy_size);
    if(err != noErr)
        return err;
    AEEventClass event_class = class_s;
    AEEventID event_id = id_s;

    AppleEvent *copy = (AppleEvent *)alloca(sizeof *copy);
    err = AEDuplicateDesc(evt, copy);
    if(err != noErr)
        return err;
    GUEST<int16_t> direct_s = kAEDirectCall;
    AEPutAttributePtr(copy, keyEventSourceAttr, typeShortInteger, &direct_s, sizeof direct_s);

    AppleEvent *answer = (AppleEvent *)alloca(sizeof *answer);
    DESC_TYPE(answer) = typeNull;
    DESC_DATA(answer) = nullptr;
    if(want_reply)
    {
        GUEST<int32_t> rtid_s = 0;
        GUEST<DescType> type;
        GUEST<Size> size;
        AEGetAttributePtr(copy, keyReturnIDAttr, typeLongInteger, &type, &rtid_s, sizeof rtid_s, &size);
        ProcessSerialNumber self = { 0, kCurrentProcess };
        AEAddressDesc *target = (AEAddressDesc *)alloca(sizeof *target);
        AECreateDesc(typeProcessSerialNumber, (Ptr)&self, sizeof self, target);
        err = AECreateAppleEvent(kCoreEventClass, kAEAnswer, target, 0, kAnyTransactionID, answer);
        AEDisposeDesc(target);
        if(err != noErr)
        {
            AEDisposeDesc(copy);
            return err;
        }
        AEPutAttributePtr(answer, keyReturnIDAttr, typeLongInteger, &rtid_s, sizeof rtid_s);
        AEPutAttributePtr(answer, keyEventSourceAttr, typeShortInteger, &direct_s, sizeof direct_s);
    }

    GUEST<AEEventHandlerUPP> hdlr_s;
    GUEST<int32_t> refcon_s;
    OSErr retval;
    if(find_handler(event_class, event_id, &hdlr_s, &refcon_s) != noErr)
        retval = errAEEventNotHandled;
    else
    {
        AEEventHandlerUPP hdlr = hdlr_s;
        retval = hdlr(copy, answer, (int32_t)refcon_s);
    }
    AEDisposeDesc(copy);

    if(want_reply && retval != noErr)
    {
        GUEST<Size> size;
        GUEST<DescType> type;
        if(AESizeOfParam(answer, keyErrorNumber, &type, &size) != noErr)
        {
            GUEST<int32_t> errn_s = retval;
            AEPutParamPtr(answer, keyErrorNumber, typeLongInteger, &errn_s, sizeof errn_s);
        }
    }
    if(reply)
        *reply = *answer;
    else
        AEDisposeDesc(answer);
    return retval;
}

OSErr Executor::C_AEProcessAppleEvent(EventRecord *evtrec)
{
    GUEST<AEEventID> event_id_s;
    GUEST<AEEventClass> event_class_s;
    GUEST<DescType> dummy_type;
    GUEST<Size> dummy_size;
    GUEST<TargetID> dummy_target_id;

    AppleEvent *evt = (AppleEvent *)alloca(sizeof *evt);

    GUEST<AEEventHandlerUPP> hdlr_s;
    AEEventHandlerUPP hdlr;
    Handle evt_data;
    GUEST<Size> evt_data_size;
    GUEST<int32_t> refcon_s;
    GUEST<int32_t> dummy_refcon;

    OSErr err;
    OSErr retval;

    /* our current buffer is empty */
    evt_data_size = 0;
    err = AcceptHighLevelEvent(&dummy_target_id,
                               &dummy_refcon,
                               nullptr, &evt_data_size);
    if(err != bufferIsSmall)
        AE_RETURN_ERROR(errAEEventNotHandled);

    evt_data = NewHandle(evt_data_size);
    if(MemError() != noErr)
        AE_RETURN_ERROR(MemError());

    {
        HLockGuard guard(evt_data);
        err = AcceptHighLevelEvent(&dummy_target_id,
                                   &dummy_refcon,
                                   *evt_data, &evt_data_size);
    }

    if(err != noErr)
    {
        DisposeHandle(evt_data);
        AE_RETURN_ERROR(errAEEventNotHandled);
    }

    DESC_TYPE(evt) = typeAppleEvent;
    DESC_DATA(evt) = evt_data;

    /* MacPhoenix: a message in Apple's wire format ('aevt', version 1.1,
       attributes, ';;;;', parameters: keyword, type, size, data) -- what
       the Process Manager and Finder post (launch parameters, 'amis') and
       Apple's own PACK 8 sends. Rebuild it as one of ours. Parameters that
       are lists or records aren't decoded yet. */
    if(evt_data_size >= 12 && *(GUEST<OSType> *)*evt_data == "aevt"_4)
    {
        /* its address is the sender, a TargetID, as 8.6's Apple Event
           Manager gives it (difftest aevt: the launch's 'oapp') */
        AEAddressDesc *target = (AEAddressDesc *)alloca(sizeof *target);
        AECreateDesc(typeTargetID, (Ptr)&dummy_target_id, sizeof dummy_target_id, target);
        AppleEvent *rebuilt = (AppleEvent *)alloca(sizeof *rebuilt);
        GUEST<uint32_t> id;
        memcpy(&id, &evtrec->where, sizeof id);
        err = AECreateAppleEvent(evtrec->message, (uint32_t)id, target, 0, kAnyTransactionID,
                                 rebuilt);
        AEDisposeDesc(target);
        if(err != noErr)
        {
            DisposeHandle(evt_data);
            AE_RETURN_ERROR(err);
        }
        HLockGuard guard(evt_data);
        const uint8_t *m = (const uint8_t *)*evt_data;
        int32_t size = evt_data_size, off = 8;
        bool params = false;
        while(off + 4 <= size)
        {
            uint32_t key = (m[off] << 24) | (m[off + 1] << 16) | (m[off + 2] << 8) | m[off + 3];
            if(key == ";;;;"_4)
            {
                params = true;
                off += 4;
                continue;
            }
            if(off + 12 > size)
                break;
            uint32_t type = (m[off + 4] << 24) | (m[off + 5] << 16) | (m[off + 6] << 8) | m[off + 7];
            int32_t len = (m[off + 8] << 24) | (m[off + 9] << 16) | (m[off + 10] << 8) | m[off + 11];
            if(len < 0 || off + 12 + len > size)
                break;
            if(params && type != "list"_4 && type != "reco"_4)
                AEPutParamPtr(rebuilt, key, type, (Ptr)(m + off + 12), len);
            off += 12 + ((len + 1) & ~1);
        }
        DisposeHandle(evt_data);
        *evt = *rebuilt;
    }

    err = AEGetAttributePtr(evt, keyEventClassAttr,
                            typeType, &dummy_type,
                            (Ptr)&event_class_s, sizeof event_class_s,
                            &dummy_size);
    if(err != noErr)
    {
        AEDisposeDesc(evt);
        AE_RETURN_ERROR(err);
    }
    AEEventClass event_class = event_class_s;

    err = AEGetAttributePtr(evt, keyEventIDAttr,
                            typeType, &dummy_type,
                            (Ptr)&event_id_s, sizeof event_id_s, &dummy_size);
    if(err != noErr)
    {
        AEDisposeDesc(evt);
        AE_RETURN_ERROR(err);
    }
    AEEventID event_id = event_id_s;

    /* MacPhoenix: what the Apple Event Manager records about a received
       event (difftest aevt, G3): its source, kAESameProcess or
       kAELocalProcess (the launch's 'oapp' comes from the system
       process), and the sender as the original address, a TargetID.
       AcceptHighLevelEvent gives the sender's serial number as the
       session ID. */
    {
        ProcessSerialNumber self;
        GetCurrentProcess(&self);
        GUEST<int16_t> source_s = dummy_target_id.sessionID == (int32_t)self.lowLongOfPSN
                                      ? kAESameProcess
                                      : kAELocalProcess;
        AEPutAttributePtr(evt, keyEventSourceAttr, typeShortInteger, &source_s, sizeof source_s);
        AEPutAttributePtr(evt, keyOriginalAddressAttr, typeTargetID, &dummy_target_id,
                          sizeof dummy_target_id);
    }

    err = find_handler(event_class, event_id, &hdlr_s, &refcon_s);
    if(err != noErr)
    {
        AEDisposeDesc(evt);
        AE_RETURN_ERROR(err);
    }
    hdlr = hdlr_s;
    int32_t refcon = refcon_s;

    {
        /* MacPhoenix: the reply is a null descriptor for an event that
           wants none (8.6, difftest aevt: the launch's 'oapp'). No sender
           here waits for a reply (AESend refuses kAEWaitReply and
           kAEQueueReply), so every event is one of those. */
        AppleEvent *reply = (AppleEvent *)alloca(sizeof *reply);
        DESC_TYPE(reply) = typeNull;
        DESC_DATA(reply) = nullptr;

        retval = hdlr(evt, reply, refcon);
        AEDisposeDesc(reply);
    }

    AEDisposeDesc(evt);
    AE_RETURN_ERROR(retval);
}

OSErr Executor::C_AESend(AppleEvent *evt, AppleEvent *reply,
                         AESendMode send_mode, AESendPriority send_priority,
                         int32_t timeout, IdleUPP idle_proc,
                         EventFilterUPP filter_proc)
{
    AEDesc *target = (AEDesc *)alloca(sizeof *target);
    GUEST<DescType> target_type_s, dummy_type;
    GUEST<Size> target_size_s, dummy_size;

    ProcessSerialNumber *target_psn = (ProcessSerialNumber *)alloca(sizeof *target_psn);
    ProcessSerialNumber *current_psn = (ProcessSerialNumber *)alloca(sizeof *current_psn);

    AEEventID event_id;
    GUEST<AEEventClass> event_class;

    OSErr err;

    GetCurrentProcess(current_psn);
    err = AEGetAttributePtr(evt, keyAddressAttr,
                            typeWildCard, &target_type_s,
                            (Ptr)target_psn, sizeof *target_psn, &target_size_s);
    if(err != noErr)
        /* ### not sure what error we should return here */
        AE_RETURN_ERROR(errAEEventNotHandled);

    DescType target_type = target_type_s;
    Size target_size = target_size_s;

    /* MacPhoenix: addressed by signature, to the process that has it, as
       by its serial number (Office 98's First Run installer sends 'MVis'
       'MVai' to the Microsoft Office First Run it just launched, 'VIS3');
       none running is connectionInvalid, as before. Several with
       the signature: the first in GetNextProcess order (not measured on the
       G3). */
    if(target_type == typeApplSignature && target_size == sizeof(OSType))
    {
        OSType signature = *(GUEST<OSType> *)target_psn;
        ProcessSerialNumber psn = { 0, kNoProcess };
        ProcessInfoRec info = {};
        bool found = false;
        while(!found && GetNextProcess(&psn) == noErr)
        {
            info.processInfoLength = sizeof info;
            info.processName = nullptr;
            info.processAppSpec = nullptr;
            found = GetProcessInformation(&psn, &info) == noErr
                && info.processSignature == signature;
        }
        if(!found)
            AE_RETURN_ERROR(invalidConnection);
        *target_psn = psn;
        target_type = keyProcessSerialNumber;
        target_size = sizeof *target_psn;
    }

    /* MacPhoenix: no reply until one comes (8.6: null after a send
       without one) */
    if(reply)
    {
        DESC_TYPE(reply) = typeNull;
        DESC_DATA(reply) = nullptr;
    }

    err = AEGetAttributePtr(evt, keyEventClassAttr,
                            typeType, &dummy_type,
                            (Ptr)&event_class, sizeof event_class,
                            &dummy_size);
    if(err != noErr)
        AE_RETURN_ERROR(err);

    err = AEGetAttributePtr(evt, keyEventIDAttr,
                            typeType, &dummy_type,
                            (Ptr)&event_id, sizeof event_id, &dummy_size);
    if(err != noErr)
        AE_RETURN_ERROR(err);

    /* MacPhoenix: an event to be recorded only (MacPerl sends its typing
       as 'setd' so): not executed, and no recording here */
    if(send_mode & kAEDontExecute)
        AE_RETURN_ERROR(noErr);

    if(target_type == keyProcessSerialNumber
       && target_size == sizeof *target_psn
       && target_psn->highLongOfPSN == 0 && target_psn->lowLongOfPSN == kCurrentProcess
       && (send_mode & _kAEReplyMask) != kAEQueueReply)
        AE_RETURN_ERROR(send_direct(evt, reply, (send_mode & _kAEReplyMask) == kAEWaitReply));

    if(target_type == keyProcessSerialNumber
       && target_size == sizeof *target_psn)
    {
        Boolean same_process_p;

        err = SameProcess(target_psn, current_psn, &same_process_p);
        if(err != noErr)
            AE_RETURN_ERROR(errAEEventNotHandled);

        /* MacPhoenix: to another process too (Executor runs several now):
           the event is posted to it by serial number, as to ourselves, and
           its AEProcessAppleEvent takes it from there. No reply comes back
           to a waiting sender yet. */
        {
            if((send_mode & _kAEReplyMask) == kAEWaitReply)
            {
                /* ### ignoring timeout/receipt requests */
                warning_unimplemented("target kCurrentProcess && kAEWaitReply");
                AE_RETURN_ERROR(errAEEventNotHandled);
            }

            if((send_mode & _kAEReplyMask) == kAEQueueReply)
            {
                warning_unimplemented("can't handle reply requests yet");
                AE_RETURN_ERROR(errAEEventNotHandled);
            }

            {
                EventRecord evt_rec;
                Handle desc_data;
                int desc_data_size;
                GUEST<Point> bogo_event_id;

                desc_data = DESC_DATA(evt);
                desc_data_size = GetHandleSize(desc_data);

                evt_rec.what = kHighLevelEvent;
                evt_rec.message = event_class;

                *(uint32_t *)&bogo_event_id = event_id;
                evt_rec.where = bogo_event_id;

                {
                    HLockGuard guard(desc_data);
                    if(same_process_p)
                        err = PostHighLevelEvent(&evt_rec,
                                                 /* #### i dunno */
                                                 nullptr, -1,
                                                 *desc_data, desc_data_size,
                                                 -1);
                    else
                        err = PostHighLevelEvent(&evt_rec, (Ptr)target_psn, -1,
                                                 *desc_data, desc_data_size,
                                                 0x8000 /* receiverIDisPSN */);
                }

                AE_RETURN_ERROR(err);
            }
        }
    }
    else
    {
        /* target is not a psn, we can't deal with anything else at this
	 time */
        if(target_type == typeApplSignature)
        {
            warning_unimplemented("returning invalidConnection");
            AE_RETURN_ERROR(invalidConnection);
        }

        warning_unimplemented("bad AE target, not ProcessSerialNumber");
        AE_RETURN_ERROR((send_mode & _kAEReplyMask) == kAEWaitReply
                            ? errAEUnknownAddressType
                            : noErr);
    }
}

OSErr Executor::C_AESuspendTheCurrentEvent(AppleEvent *evt)
{
    warning_unimplemented("");
    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AEResumeTheCurrentEvent(
    AppleEvent *evt, AppleEvent *reply, AEEventHandlerUPP dispatcher,
    int32_t refcon)
{
    warning_unimplemented("");
    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AEGetTheCurrentEvent(AppleEvent *return_evt)
{
    warning_unimplemented("");
    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AESetTheCurrentEvent(AppleEvent *evt)
{
    warning_unimplemented("");
    AE_RETURN_ERROR(noErr);
}

/* array spew */

OSErr Executor::C_AEGetArray(AEDescList *list, AEArrayType array_type,
                             AEArrayDataPointer array_ptr, Size max_size,
                             GUEST<DescType> *return_item_type,
                             GUEST<Size> *return_item_size,
                             GUEST<int32_t> *return_item_count)
{
    warning_unimplemented("");
    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AEPutArray(AEDescList *list, AEArrayType type,
                             AEArrayDataPointer array_data, DescType item_type,
                             Size item_size, int32_t item_count)
{
    warning_unimplemented("");
    AE_RETURN_ERROR(noErr);
}

/* user interaction settings */

AEInteractionAllowed interaction_level = kAEInteractWithSelf;

OSErr Executor::C_AESetInteractionAllowed(AEInteractionAllowed level)
{
    /* do nothing; since all levels are a superset of
     kAEInteractWithSelf, which is the only interaction currently
     available */

    interaction_level = level;
    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AEGetInteractionAllowed(AEInteractionAllowed *level_out)
{
    *level_out = interaction_level;
    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AEInteractWithUser(int32_t timeout, NMRecPtr nm_req,
                                     IdleUPP idle_proc)
{
    warning_unimplemented("");
    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AEResetTimer(AppleEvent *evt)
{
    warning_unimplemented("");
    AE_RETURN_ERROR(noErr);
}

/* suspending and resuming AE handling */

/* type coersion */

OSErr Executor::C_AECoercePtr(DescType data_type, const void *data_ptr, Size data_size,
                              DescType result_type, AEDesc *desc_out)
{
    Ptr data = (Ptr)data_ptr;
    GUEST<AECoerceDescUPP> coercion_hdlr_s;
    GUEST<int32_t> refcon_s;
    Boolean is_desc_hdlr_p;
    OSErr err;

    if(result_type == typeWildCard
       || result_type == data_type)
    {
        AECreateDesc(data_type, data, data_size, desc_out);
        AE_RETURN_ERROR(noErr);
    }

    err = AEGetCoercionHandler(data_type, result_type,
                               &coercion_hdlr_s, &refcon_s,
                               &is_desc_hdlr_p,
                               false);
    if(err != noErr)
    {
        err = AEGetCoercionHandler(data_type, result_type,
                                   &coercion_hdlr_s, &refcon_s,
                                   &is_desc_hdlr_p,
                                   true);
        if(err != noErr)
            AE_RETURN_ERROR(errAECoercionFail);
    }

    /* swap things to a normal state */
    AECoerceDescUPP coercion_hdlr = coercion_hdlr_s;
    int32_t refcon = refcon_s;

    if(is_desc_hdlr_p)
    {
        descriptor_t *desc = (descriptor_t *)alloca(sizeof *desc);

        err = AECreateDesc(data_type, data, data_size, desc);
        if(err != noErr)
            return memFullErr;

        err = coercion_hdlr(desc, result_type, refcon, desc_out);

        AEDisposeDesc(desc);
    }
    else
    {
        AECoercePtrUPP ptr_coercion_hdlr((void *)coercion_hdlr);
        err = ptr_coercion_hdlr(data_type, data, data_size, result_type,
                                refcon, desc_out);
    }

    if(err != noErr)
        AE_RETURN_ERROR(errAECoercionFail);

    AE_RETURN_ERROR(noErr);
}

#define AEGetParamDesc AEGetParamDesc

/* MacPhoenix: 'aevt' to 'appa', a launch's AppParameters as the Process
   Manager takes them (LaunchApplication's launchAppParameters):
   {EventRecord theMsgEvent, eventRefCon, messageLength, message}, the
   message the event's data as AESend posts it to another process. Any
   event: Netscape's User Profile Manager launches Communicator with a
   'MOSS'/'prfl' that has no direct object (the old coercion read only an
   'odoc''s list of files, into a format nothing reads any more). */
static OSErr
parse_evt(const AppleEvent *evtp, AEDesc *desc_out)
{
    GUEST<OSType> event_class, event_id;
    GUEST<DescType> type;
    GUEST<Size> size;
    OSErr err = AEGetAttributePtr((AppleEvent *)evtp, keyEventClassAttr, typeType, &type,
                                  (Ptr)&event_class, sizeof event_class, &size);
    if(err == noErr)
        err = AEGetAttributePtr((AppleEvent *)evtp, keyEventIDAttr, typeType, &type,
                                (Ptr)&event_id, sizeof event_id, &size);
    if(err != noErr)
        return err;

    Handle data = DESC_DATA(evtp);
    int32_t length = data ? GetHandleSize(data) : 0;
    enum { header = 16 + 4 + 4 }; /* EventRecord, refcon, length */
    Handle h = NewHandleClear(header + length);
    if(!h)
        return MemError();
    EventRecord *evt = (EventRecord *)*h;
    evt->what = kHighLevelEvent;
    evt->message = (OSType)event_class;
    memcpy(&evt->where, &event_id, sizeof event_id);
    *(GUEST<int32_t> *)(*h + 20) = length;
    if(length)
        memcpy(*h + header, *data, length);
    desc_out->descriptorType = "appa"_4;
    desc_out->dataHandle = h;
    return noErr;
}

/* MacPhoenix: the Apple Event Manager's own coercions between the integer
   types, after the installed handlers: 'shor' to 'long', and 'long' to
   'shor' when the value fits (8.6, difftest aevt: an 'esrc' read as a
   long; a return ID read as a short is errAECoercionFail). */
static bool coerce_integer(AEDesc *desc, DescType result_type, AEDesc *desc_out)
{
    DescType from = DESC_TYPE(desc);
    Handle h = DESC_DATA(desc);
    if(!h)
        return false;
    Size size = GetHandleSize(h);
    int32_t v;
    if(from == typeShortInteger && size == 2)
        v = *(GUEST<int16_t> *)*h;
    else if(from == typeLongInteger && size == 4)
        v = *(GUEST<int32_t> *)*h;
    else
        return false;
    if(result_type == typeLongInteger)
    {
        GUEST<int32_t> out = v;
        return AECreateDesc(typeLongInteger, &out, sizeof out, desc_out) == noErr;
    }
    if(result_type == typeShortInteger && v >= INT16_MIN && v <= INT16_MAX)
    {
        GUEST<int16_t> out = v;
        return AECreateDesc(typeShortInteger, &out, sizeof out, desc_out) == noErr;
    }
    return false;
}

/* MacPhoenix: a list of one item is that item, coerced (aevt
   coerce.list.*; Navigation Services' reply, its selection coerced to
   'fss ' by BBEdit Lite 6.1's Save). Two items or none don't coerce. */
static bool coerce_single_item_list(AEDesc *desc, DescType result_type, AEDesc *desc_out)
{
    GUEST<LONGINT> count;
    GUEST<AEKeyword> keyword;
    if(DESC_TYPE(desc) != typeAEList || result_type == typeAERecord
       || AECountItems(desc, &count) != noErr || count != 1)
        return false;
    return AEGetNthDesc(desc, 1, result_type, &keyword, desc_out) == noErr;
}

/* MacPhoenix: a record is any type its holder asks for, its data as it
   is, and a descriptor whose data is a record is a record again (aevt
   coerce.reco.*, coerce.obj.reco: an object specifier is a record coerced
   to 'obj ', and the Object Support Library coerces it back to read its
   keys; BBEdit Lite 6.1's 'save' handler failed with -1700). Not between
   two other types ('obj ' to 'abcd'), nor from data that is no record. */
/* MacPhoenix: any other descriptor asked for as a list is a list of it
   (aevt coerce.TEXT.list; MacPerl's 'dosc' handler takes its script so). */
static bool coerce_to_list(AEDesc *desc, DescType result_type, AEDesc *desc_out)
{
    if(result_type != typeAEList || DESC_TYPE(desc) == typeAEList || DESC_TYPE(desc) == typeNull)
        return false;
    if(AECreateList(nullptr, 0, false, desc_out) != noErr)
        return false;
    if(AEPutDesc(desc_out, 0, desc) != noErr)
    {
        AEDisposeDesc(desc_out);
        return false;
    }
    return true;
}

/* A record's items as they are inside another descriptor (a record
   parameter of another type, 'obj ', taken out of an event): the count,
   a long, then key, type, size and data (padded to even) each, to the
   end: Executor's own record without its first 16 header bytes. */
static bool nested_record_p(Handle h)
{
    Size size = GetHandleSize(h);
    if(size < 8)
        return false;
    const uint8_t *p = (const uint8_t *)*h;
    auto be32 = [](const uint8_t *q) { return (uint32_t)q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3]; };
    uint32_t count = be32(p);
    Size at = 8;
    for(uint32_t i = 0; i < count; i++)
    {
        if(at + 12 > size)
            return false;
        uint32_t n = be32(p + at + 8);
        at += 12 + n + (n & 1);
    }
    return at == size;
}

static bool coerce_record(AEDesc *desc, DescType result_type, AEDesc *desc_out)
{
    Handle h = DESC_DATA(desc);
    if(!h)
        return false;
    constexpr Size prefix = offsetof(list_header_t, param_count);
    bool header_p = GetHandleSize(h) >= (Size)sizeof(list_header_t) && ATTRIBUTE_COUNT(h) == typeAERecord;
    if(DESC_TYPE(desc) != typeAERecord && !(result_type == typeAERecord && (header_p || nested_record_p(h))))
        return false;
    if(header_p || DESC_TYPE(desc) == typeAERecord)
    {
        if(AEDuplicateDesc(desc, desc_out) != noErr)
            return false;
        desc_out->descriptorType = result_type;
        return true;
    }
    /* the nested layout given its header (AECreateList's) */
    list_header_t header;
    memset(&header, 0, sizeof header);
    header.param_offset = sizeof header;
    header.attribute_count = typeAERecord;
    if(AECreateDesc(typeAERecord, &header, prefix, desc_out) != noErr)
        return false;
    Size n = GetHandleSize(h);
    SetHandleSize(DESC_DATA(desc_out), prefix + n);
    if(LM(MemErr) != noErr)
    {
        AEDisposeDesc(desc_out);
        return false;
    }
    memcpy(*DESC_DATA(desc_out) + prefix, *h, n);
    return true;
}

/* MacPhoenix: the Apple Event Manager's own coercions between an alias and
   an FSSpec, after the installed handlers (8.6, difftest aevt coerce): the
   Finder gives an 'odoc's documents as aliases and applications ask for
   'fss ' (SimpleText and BBEdit Lite 6.1 opened nothing). */
static bool coerce_alias(AEDesc *desc, DescType result_type, AEDesc *desc_out)
{
    DescType from = DESC_TYPE(desc);
    Handle h = DESC_DATA(desc);
    if(!h)
        return false;
    if(from == typeAlias && result_type == typeFSS)
    {
        GUEST<Handle> alias = h;
        if(HandToHand(&alias) != noErr)
            return false;
        FSSpec spec;
        Boolean was_aliased;
        OSErr err = ResolveAlias(nullptr, (AliasHandle)(Handle)alias, &spec, &was_aliased);
        DisposeHandle(alias);
        return err == noErr && AECreateDesc(typeFSS, &spec, sizeof spec, desc_out) == noErr;
    }
    if(from == typeFSS && result_type == typeAlias && GetHandleSize(h) >= (Size)sizeof(FSSpec))
    {
        FSSpec spec;
        memcpy(&spec, *h, sizeof spec);
        GUEST<AliasHandle> alias;
        if(NewAlias(nullptr, &spec, &alias) != noErr || !alias)
            return false;
        bool ok;
        {
            HLockGuard guard((Handle)(AliasHandle)alias);
            ok = AECreateDesc(typeAlias, *(Handle)(AliasHandle)alias,
                              GetHandleSize((Handle)(AliasHandle)alias), desc_out) == noErr;
        }
        DisposeHandle((Handle)(AliasHandle)alias);
        return ok;
    }
    return false;
}

OSErr Executor::C_AECoerceDesc(AEDesc *desc, DescType result_type,
                               AEDesc *desc_out)
{
    GUEST<AECoerceDescUPP> coercion_hdlr_s;
    GUEST<int32_t> refcon_s;
    AECoerceDescUPP coercion_hdlr;
    int32_t refcon;
    GUEST<Boolean> is_desc_hdlr_p = false;
    DescType desc_type;
    OSErr err;

    desc_type = DESC_TYPE(desc);

    if(desc_type == "aevt"_4 && result_type == "appa"_4)
    {
        err = parse_evt(desc, desc_out);
        AE_RETURN_ERROR(err);
    }

    if(result_type == typeWildCard
       || result_type == desc_type)
    {
        AEDuplicateDesc(desc, desc_out);
        AE_RETURN_ERROR(noErr);
    }

    err = AEGetCoercionHandler(desc_type, result_type,
                               &coercion_hdlr_s, &refcon_s,
                               &is_desc_hdlr_p,
                               false);
    if(err != noErr)
    {
        err = AEGetCoercionHandler(desc_type, result_type,
                                   &coercion_hdlr_s, &refcon_s,
                                   &is_desc_hdlr_p,
                                   true);
        if(err != noErr)
        {
            if(coerce_integer(desc, result_type, desc_out)
               || coerce_alias(desc, result_type, desc_out)
               || coerce_single_item_list(desc, result_type, desc_out)
               || coerce_record(desc, result_type, desc_out)
               || coerce_to_list(desc, result_type, desc_out))
                AE_RETURN_ERROR(noErr);
            goto fail;
        }
    }

    /* swap things to a normal state */
    coercion_hdlr = coercion_hdlr_s;
    refcon = refcon_s;

    if(is_desc_hdlr_p)
    {
        err = coercion_hdlr(desc, result_type, refcon, desc_out);
    }
    else
    {
        Handle desc_data;

        desc_data = DESC_DATA(desc);

        {
            HLockGuard guard(desc_data);
            AECoercePtrUPP ptr_coercion_hdlr((void *)coercion_hdlr);
            err = ptr_coercion_hdlr(desc_type, *desc_data,
                                    GetHandleSize(desc_data),
                                    result_type, refcon, desc_out);
        }
    }

    if(err != noErr)
        goto fail;

    AE_RETURN_ERROR(noErr);

fail:
    desc_out->descriptorType = typeNull;
    desc_out->dataHandle = nullptr;
    AE_RETURN_ERROR(errAECoercionFail);
}

/* MacPhoenix: 7.5.5's PACK 8 has no AEManagerInfo (its routine table ends
   at $3E), so the call is paramErr on a real System of that age; Disk Copy
   6.3.3 takes that and gives up on noErr with a version of 0. */
OSErr Executor::C_AEManagerInfo(AEKeyword keyWord, GUEST<LONGINT> *resultp)
{
    (void)keyWord;
    if(resultp)
        *resultp = 0;
    return paramErr;
}

/* stubs added by Cliff after reading documentation on Apple's web site;
   MacPhoenix: the accessor table, AECallObjectAccessor, the callbacks,
   AEDisposeToken and AEResolve are in AE_hdlr.cpp */

