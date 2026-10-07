/* Copyright 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>

#include <OSEvent.h>
#include <MemoryMgr.h>
#include <osevent/osevent.h>
#include <base/functions.impl.h>

using namespace Executor;

typedef struct hle_q_elt
{
    struct hle_q_elt *next;
    HighLevelEventMsgPtr hle_msg;
} hle_q_elt_t;

static hle_q_elt_t *hle_q;

/* event q element currently being processed */
static HighLevelEventMsgPtr current_hle_msg;

void Executor::hle_init(void)
{
    hle_q = nullptr;
    current_hle_msg = nullptr;
}

void Executor::hle_reinit(void)
{
    hle_q = nullptr;
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
    if(hle_q)
    {
        hle_q_elt_t *t;

        if(current_hle_msg != nullptr)
        {
            warning_unexpected("current_hle_msg != """);
            hle_reset();
        }

        current_hle_msg = hle_q->hle_msg;
        *evt = current_hle_msg->theMsgEvent;

        t = hle_q;
        if(remflag)
        {
            hle_q = hle_q->next;
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

    /* #### *sender_id_return = ...; */
    *refcon_return = current_hle_msg->userRefCon;

    if(*msg_buf_length_return < current_hle_msg->msgLength)
        retval = bufferIsSmall;

    if(retval == noErr)
        memcpy(msg_buf, guest_cast<Ptr>(current_hle_msg->theMsgEvent.when),
               current_hle_msg->msgLength);

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
    hle_msg->theMsgEvent.modifiers = -1;

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
    retval = noErr;

done:
    return retval;
}
