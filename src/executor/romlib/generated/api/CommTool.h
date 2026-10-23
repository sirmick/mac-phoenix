/*****************************************************************************
 *                                                                           *
 *                                 CommTool.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    QElemPtr, INTEGER, LONGINT, OSErr, StringHandle,        *
 *                   Handle, QHdrPtr                                         *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME CommTool
#include <base/api-module.h>


namespace Executor {

struct CRMRec{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> crmVersion;GUEST<LONGINT> crmPrivate;GUEST<INTEGER> crmReserved;GUEST<LONGINT> crmDeviceType;GUEST<LONGINT> crmDeviceID;GUEST<LONGINT> crmAttributes;GUEST<LONGINT> crmStatus;GUEST<LONGINT> crmRefCon;};static_assert(sizeof(CRMRec) == 34);void describeStruct(const CRMRec&, std::ostream&);

typedef CRMRec *CRMRecPtr;

typedef OSErr CRMErr;

enum {crmGenericError = -1,
crmNoErr = 0,
};

enum {curCRMVersion = 1,
};

enum {crmType = 9,
};

enum {crmRecVersion = 1,
};

enum {curCRMSerRecVer = 0,
};

enum {crmSerialDevice = 1,
};

struct CRMSerialRecord{GUEST_STRUCT;GUEST<INTEGER> version;GUEST<StringHandle> inputDriverName;GUEST<StringHandle> outputDriverName;GUEST<StringHandle> name;GUEST<Handle> deviceIcon;GUEST<LONGINT> ratedSpeed;GUEST<LONGINT> maxSpeed;GUEST<LONGINT> reserved;};static_assert(sizeof(CRMSerialRecord) == 30);void describeStruct(const CRMSerialRecord&, std::ostream&);

typedef CRMSerialRecord *CRMSerialPtr;

INTEGER CRMGetCRMVersion();

QHdrPtr CRMGetHeader();

void CRMInstall(QElemPtr );

OSErr CRMRemove(QElemPtr );

QElemPtr CRMSearch(QElemPtr );

CRMErr InitCRM();

} /* namespace Executor */