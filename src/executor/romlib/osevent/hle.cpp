/* Copyright 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>

#include <OSEvent.h>
#include <ToolboxEvent.h>
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

/* MacPhoenix: a message is one block, as the Process Manager keeps it: the
   HighLevelEventMsg header, then msgLength bytes of message. Apple's Apple
   Event Manager reads the message there from its GetSpecificHighLevelEvent
   filter (to find the reply to the event it sent). */
static Ptr hle_data(HighLevelEventMsgPtr m)
{
    return (Ptr)m + sizeof(HighLevelEventMsg);
}

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
        /* The session to answer on (receiverIDisSessionID): the sender. */
        sender_id_return->sessionID = current_hle_from.lowLongOfPSN;
    }
    *refcon_return = current_hle_msg->userRefCon;

    /* A buffer too small gets as much of the message as fits (Apple's
       Apple Event Manager reads the 'aevt' signature from a 4-byte one,
       then asks again with the length) and bufferIsSmall. */
    uint32_t room = *msg_buf_length_return;
    if(room < current_hle_msg->msgLength)
        retval = bufferIsSmall;

    if(msg_buf)
        memcpy(msg_buf, hle_data(current_hle_msg),
               std::min<uint32_t>(room, current_hle_msg->msgLength));

    *msg_buf_length_return = current_hle_msg->msgLength;

    return retval;
}

Boolean Executor::C_GetSpecificHighLevelEvent(
    GetSpecificFilterUPP fn, Ptr data, GUEST<OSErr> *err_return)
{
    hle_q_elt_t *t, **prev;

    /* MacPhoenix: the filter gets the sender's TargetID (Apple's Apple
       Event Manager matches replies with it); it lives in the System heap
       so the filter, guest code, can read it. A message the filter takes
       leaves the queue. */
    TargetID *sender = (TargetID *)NewPtrSysClear(sizeof(TargetID));
    if(err_return)
        *err_return = noErr;
    Boolean taken = false;
    for(prev = &hle_q, t = hle_q; t; prev = &t->next, t = t->next)
    {
        if(!for_current(t))
            continue;

        if(sender)
        {
            memset(sender, 0, sizeof *sender);
            if(!ROMlib_process_port_name_of(&t->from, &sender->name))
                ROMlib_process_port_name(&sender->name);
            ROMlib_process_port_name(&sender->recvrName);
            sender->sessionID = t->from.lowLongOfPSN;
        }
        /* The filter accepts a message by calling AcceptHighLevelEvent
           (Apple's Apple Event Manager does, for its replies): that reads
           the message being offered. */
        HighLevelEventMsgPtr saved_msg = current_hle_msg;
        ProcessSerialNumber saved_from = current_hle_from;
        current_hle_msg = t->hle_msg;
        current_hle_from = t->from;
        bool took = fn(data, t->hle_msg, sender);
        current_hle_msg = saved_msg;
        current_hle_from = saved_from;
        if(took)
        {
            *prev = t->next;
            DisposePtr((Ptr)t->hle_msg);
            DisposePtr((Ptr)t);
            taken = true;
            break;
        }
    }
    if(sender)
        DisposePtr((Ptr)sender);
    return taken;
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
            case 0x6000: /* receiverIDisSessionID: receiverID is the
                            session ID itself (not a pointer to one; real
                            7.5.5 posts 'ansr' replies with $40000004 there):
                            the session a message came in on, which
                            AcceptHighLevelEvent gives as the sender's PSN.
                            The Apple Event Manager replies this way. */
                to.highLongOfPSN = 0;
                to.lowLongOfPSN = US_TO_SYN68K(receiver_id);
                break;
        }
    }

    hle_msg = (HighLevelEventMsgPtr)NewPtr(sizeof *hle_msg + std::max<int32_t>(msg_length, 0));
    if(MemError() != noErr)
    {
        retval = MemError();
        goto done;
    }

    hle_msg->HighLevelEventMsgHeaderlength = sizeof *hle_msg;
    hle_msg->version = 0;
    hle_msg->reserved1 = -1;
    hle_msg->theMsgEvent = *evt;
    /* MacPhoenix: it arrives as a high-level event whatever the poster
       left in 'what' (Apple's AESend leaves it unset). */
    hle_msg->theMsgEvent.what = kHighLevelEvent;

    msg_buf_copy = hle_data(hle_msg);
    if(msg_length > 0)
        memcpy(msg_buf_copy, msg_buf, msg_length);
    hle_msg->theMsgEvent.when = TickCount();
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
