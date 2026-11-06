/* Copyright 1999 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>

#include <CommTool.h>
#include <MemoryMgr.h>
#include <OSUtil.h>

#include <util/string.h>
#include <mman/mman.h>

using namespace Executor;

INTEGER
Executor::CRMGetCRMVersion(void)
{
    INTEGER retval;

    retval = curCRMVersion;
    return retval;
}

static QHdr commtool_head;

QHdrPtr
Executor::CRMGetHeader(void)
{
    QHdrPtr retval;

    retval = &commtool_head;
    return retval;
}

void
Executor::CRMInstall(QElemPtr qp)
{
    if(!commtool_head.qTail)
        ((CRMRecPtr)qp)->crmDeviceID = 1;
    else
    {
        CRMRecPtr prevp;

        prevp = (CRMRecPtr)commtool_head.qTail;
        ((CRMRecPtr)qp)->crmDeviceID = prevp->crmDeviceID + 1;
    }

    Enqueue(qp, &commtool_head);
}

OSErr
Executor::CRMRemove(QElemPtr qp)
{
    OSErr retval;

    retval = Dequeue(qp, &commtool_head);
    return retval;
}

QElemPtr
Executor::CRMSearch(QElemPtr qp)
{
    QElemPtr retval;
    CRMRecPtr p;
    LONGINT min;

    min = ((CRMRecPtr)qp)->crmDeviceID + 1;

    for(p = (CRMRecPtr)commtool_head.qHead;
        p && p->crmDeviceID < min;
        p = (CRMRecPtr)p->qLink)
        ;
    retval = (QElemPtr)p;
    return retval;
}

static CRMErr
serial_insert(const char *input, const char *output, const char *name)
{
    CRMErr retval;
    CRMSerialPtr p;

    p = (CRMSerialPtr)NewPtrSys(sizeof *p);
    if(!p)
        retval = MemError();
    else
    {
        CRMRecPtr qp;

        qp = (CRMRecPtr)NewPtrSys(sizeof *qp);
        if(!qp)
            retval = MemError();
        else
        {
            p->version = curCRMSerRecVer;
            p->inputDriverName = stringhandle_from_c_string(input);
            p->outputDriverName = stringhandle_from_c_string(output);
            p->name = stringhandle_from_c_string(name);
            p->deviceIcon = nullptr;
            p->ratedSpeed = 19200;
            p->maxSpeed = 57600;
            p->reserved = 0;
            qp->qLink = nullptr;
            qp->qType = crmType;
            qp->crmVersion = crmRecVersion;
            qp->crmPrivate = 0;
            qp->crmReserved = 0;
            qp->crmDeviceType = crmSerialDevice;
            qp->crmDeviceID = 0;
            qp->crmAttributes = guest_cast<LONGINT>(p);
            qp->crmStatus = 0;
            qp->crmRefCon = 0;
            CRMInstall((QElemPtr)qp);
            retval = noErr;
        }
    }
    return retval;
}

CRMErr
Executor::InitCRM()
{
    static bool beenhere;
    CRMErr retval;

    if(beenhere)
        retval = noErr;
    else
    {
        TheZoneGuard guard(LM(SysZone));
        retval = serial_insert(".AIn", ".AOut", "COM1");
        if(retval == noErr)
            retval = serial_insert(".BIn", ".BOut", "COM2");
        beenhere = true;
    }
    return retval;
}
