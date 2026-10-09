/* Copyright 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>

#include <OSEvent.h>
#include <MemoryMgr.h>
#include <osevent/osevent.h>
#include <rsys/process.h>
#include <ProcessMgr.h>
#include <mman/mman.h>
#include <AppleEvents.h>
#include <base/functions.impl.h>

#include <algorithm>

using namespace Executor;

/* MacPhoenix: one queue for the machine; each message is for one process
   (its receiver) and lives in the System heap, so it outlives a sender
   that quits and reaches a receiver that hasn't run yet. */
typedef struct hle_q_elt
{
    struct hle_q_elt *next;
    HighLevelEventMsgPtr hle_msg;
    ProcessSerialNumber to;
    ProcessSerialNumber from;
} hle_q_elt_t;

static hle_q_elt_t *hle_q;

/* event q element currently being processed, and its sender (per process) */
static HighLevelEventMsgPtr current_hle_msg;
static ProcessSerialNumber current_hle_from;

static bool for_current(const hle_q_elt_t *t)
{
    ProcessSerialNumber me;
    GetCurrentProcess(&me);
    return t->to.highLongOfPSN == me.highLongOfPSN && t->to.lowLongOfPSN == me.lowLongOfPSN;
}

bool Executor::ROMlib_hle_pending(const ProcessSerialNumber *psn)
{
    for(hle_q_elt_t *t = hle_q; t; t = t->next)
        if(t->to.highLongOfPSN == psn->highLongOfPSN && t->to.lowLongOfPSN == psn->lowLongOfPSN)
            return true;
    return false;
}

void Executor::ROMlib_hle_forget(const ProcessSerialNumber *psn)
{
    for(hle_q_elt_t **pp = &hle_q; *pp;)
    {
        hle_q_elt_t *t = *pp;
        if(t->to.highLongOfPSN == psn->highLongOfPSN && t->to.lowLongOfPSN == psn->lowLongOfPSN)
        {
            *pp = t->next;
            DisposePtr(guest_cast<Ptr>(t->hle_msg->theMsgEvent.when));
            DisposePtr((Ptr)t->hle_msg);
            DisposePtr((Ptr)t);
        }
        else
            pp = &t->next;
    }
}

void Executor::hle_init(void)
{
    hle_q = nullptr;
    current_hle_msg = nullptr;
    ROMlib_process_register_state(&current_hle_msg, sizeof current_hle_msg);
    ROMlib_process_register_state(&current_hle_from, sizeof current_hle_from);
    /* Whether this process still has its opening Apple event to get. */
    ROMlib_process_register_state(&send_application_open_aevt_p, sizeof send_application_open_aevt_p);
}

void Executor::hle_reinit(void)
{
    current_hle_msg = nullptr;
}

void Executor::hle_reset(void)
{
    if(current_hle_msg == nullptr)
        return;

    DisposePtr(guest_cast<Ptr>(current_hle_msg->theMsgEvent.when));
    DisposePtr((Ptr)current_hle_msg);

    current_hle_msg = nullptr;
}

bool Executor::hle_get_event(EventRecord *evt, bool remflag)
{
    for(hle_q_elt_t **pp = &hle_q; *pp; pp = &(*pp)->next)
    {
        hle_q_elt_t *t = *pp;
        if(!for_current(t))
            continue;

        if(current_hle_msg != nullptr)
        {
            warning_unexpected("current_hle_msg != \"\"");
            hle_reset();
        }

        current_hle_msg = t->hle_msg;
        current_hle_from = t->from;
        *evt = current_hle_msg->theMsgEvent;

        if(remflag)
        {
            *pp = t->next;
            DisposePtr((Ptr)t);
        }

        return true;
    }

    return false;
}

OSErr Executor::C_AcceptHighLevelEvent(TargetID *sender_id_return,
                                       GUEST<int32_t> *refcon_return,
                                       Ptr msg_buf,
                                       GUEST<int32_t> *msg_buf_length_return)
{
    OSErr retval = noErr;

    if(current_hle_msg == nullptr)
        return noOutstandingHLE;

    /* MacPhoenix: the sender is a process on this machine (no location;
       Finder ignores events from another machine), the receiver us. */
    if(sender_id_return)
    {
        memset(sender_id_return, 0, sizeof *sender_id_return);
        if(!ROMlib_process_port_name_of(&current_hle_from, &sender_id_return->name))
            ROMlib_process_port_name(&sender_id_return->name);
        ROMlib_process_port_name(&sender_id_return->recvrName);
    }
    *refcon_return = current_hle_msg->userRefCon;

    /* A buffer too small gets as much of the message as fits (Apple's
       Apple Event Manager reads the 'aevt' signature from a 4-byte one,
       then asks again with the length) and bufferIsSmall. */
    uint32_t room = *msg_buf_length_return;
    if(room < current_hle_msg->msgLength)
        retval = bufferIsSmall;

    if(msg_buf)
        memcpy(msg_buf, guest_cast<Ptr>(current_hle_msg->theMsgEvent.when),
               std::min<uint32_t>(room, current_hle_msg->msgLength));

    *msg_buf_length_return = current_hle_msg->msgLength;

    return retval;
}

Boolean Executor::C_GetSpecificHighLevelEvent(
    GetSpecificFilterUPP fn, Ptr data, GUEST<OSErr> *err_return)
{
    hle_q_elt_t *t, **prev;

    for(prev = &hle_q, t = hle_q; t; prev = &t->next, t = t->next)
    {
        Boolean evt_handled_p;

        if(!for_current(t))
            continue;

        evt_handled_p = fn(data, t->hle_msg, /* ##### target id */ nullptr);
        if(evt_handled_p)
        {
            *prev = t->next;
            return true;
        }
    }

    return false;
}

OSErr Executor::C_PostHighLevelEvent(EventRecord *evt, Ptr receiver_id,
                                     int32_t refcon, Ptr msg_buf,
                                     int32_t msg_length, int32_t post_options)
{
    HighLevelEventMsgPtr hle_msg;
    Ptr msg_buf_copy;
    hle_q_elt_t *t, *elt;
    OSErr retval;
    TheZoneGuard guard(LM(SysZone));

    /* The receiver: a process serial number or signature (others, and
       kCurrentProcess, mean the sender). */
    ProcessSerialNumber to;
    GetCurrentProcess(&to);
    if(receiver_id)
    {
        switch(post_options & 0xF000)
        {
            case 0x8000: /* receiverIDisPSN */
            {
                ProcessSerialNumber psn = *(ProcessSerialNumber *)receiver_id;
                if(!(psn.highLongOfPSN == 0 && psn.lowLongOfPSN == kCurrentProcess))
                    to = psn;
                break;
            }
            case 0x7000: /* receiverIDisSignature */
                ROMlib_process_with_signature(*(GUEST<OSType> *)receiver_id, &to);
                break;
        }
    }

    hle_msg = (HighLevelEventMsgPtr)NewPtr(sizeof *hle_msg);
    if(MemError() != noErr)
    {
        retval = MemError();
        goto done;
    }

    hle_msg->HighLevelEventMsgHeaderlength = 0;
    hle_msg->version = 0;
    hle_msg->reserved1 = -1;
    hle_msg->theMsgEvent = *evt;

    /* #### copy the message buffer? */
    msg_buf_copy = NewPtr(msg_length);
    if(MemError() != noErr)
    {
        retval = MemError();
        DisposePtr((Ptr)hle_msg);
        goto done;
    }
    memcpy(msg_buf_copy, msg_buf, msg_length);
    hle_msg->theMsgEvent.when = guest_cast<int32_t>(msg_buf_copy);
    /* MacPhoenix: was -1. Apple's Apple Event Manager (PACK 8) reads
       modifier bits of a high-level event: with bit 3 set it takes the
       message's first long for its size. Plain messages have none. */
    hle_msg->theMsgEvent.modifiers = 0;

    hle_msg->userRefCon = refcon;
    hle_msg->postingOptions = post_options;
    hle_msg->msgLength = msg_length;

    /* stick the new msg */
    elt = (hle_q_elt_t *)NewPtr(sizeof *t);
    if(MemError() != noErr)
    {
        retval = MemError();
        DisposePtr(guest_cast<Ptr>(hle_msg->theMsgEvent.when));
        DisposePtr((Ptr)hle_msg);
        goto done;
    }

    if(hle_q == nullptr)
        hle_q = elt;
    else
    {
        for(t = hle_q; t->next != nullptr; t = t->next)
            ;
        t->next = elt;
    }
    elt->next = nullptr;
    elt->hle_msg = hle_msg;
    elt->to = to;
    GetCurrentProcess(&elt->from);
    retval = noErr;

done:
    return retval;
}
