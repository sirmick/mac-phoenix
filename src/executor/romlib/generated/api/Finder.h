/*****************************************************************************
 *                                                                           *
 *                                  Finder.h                                 *
 *                                  ========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    QElemPtr, INTEGER, Ptr, OSErr, StringPtr, LONGINT,      *
 *                   SignedByte, OSType, Boolean                             *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME Finder
#include <base/api-module.h>


namespace Executor {

struct DTPBRec{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> ioTrap;GUEST<Ptr> ioCmdAddr;GUEST<ProcPtr> ioCompletion;GUEST<OSErr> ioResult;GUEST<StringPtr> ioNamePtr;GUEST<INTEGER> ioVRefNum;GUEST<INTEGER> ioDTRefNum;GUEST<INTEGER> ioIndex;GUEST<LONGINT> ioTagInfo;GUEST<Ptr> ioDTBuffer;GUEST<LONGINT> ioDTReqCount;GUEST<LONGINT> ioDTActCount;SignedByte filler1;SignedByte ioIconType;GUEST<INTEGER> filler2;GUEST<LONGINT> ioDirID;GUEST<OSType> ioFileCreator;GUEST<OSType> ioFileType;GUEST<LONGINT> ioFiller3;GUEST<LONGINT> ioDTLgLen;GUEST<LONGINT> ioDTPyLen;GUEST<INTEGER> ioFiller4[14];GUEST<LONGINT> ioAPPLParID;};static_assert(sizeof(DTPBRec) == 104);void describeStruct(const DTPBRec&, std::ostream&);

typedef DTPBRec *DTPBRecPtr;

typedef DTPBRec *DTPBPtr;

EXTERN_DISPATCHER_TRAP(FSDispatch, 0xA060, D0W);
OSErr PBDTGetPath(DTPBPtr dtp);REGISTER_SUBTRAP2(PBDTGetPath, 0xA060, 0x0020, FSDispatch, D0(A0));


OSErr PBDTCloseDown(DTPBPtr dtp);REGISTER_SUBTRAP2(PBDTCloseDown, 0xA060, 0x0021, FSDispatch, D0(A0));


OSErr PBDTAddIcon(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTAddIcon, DTPBPtr, 0xA260, 0x0022, FSDispatch);

OSErr PBDTGetIcon(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTGetIcon, DTPBPtr, 0xA260, 0x0023, FSDispatch);

OSErr PBDTGetIconInfo(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTGetIconInfo, DTPBPtr, 0xA260, 0x0024, FSDispatch);

OSErr PBDTAddAPPL(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTAddAPPL, DTPBPtr, 0xA260, 0x0025, FSDispatch);

OSErr PBDTRemoveAPPL(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTRemoveAPPL, DTPBPtr, 0xA260, 0x0026, FSDispatch);

OSErr PBDTGetAPPL(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTGetAPPL, DTPBPtr, 0xA260, 0x0027, FSDispatch);

OSErr PBDTSetComment(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTSetComment, DTPBPtr, 0xA260, 0x0028, FSDispatch);

OSErr PBDTRemoveComment(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTRemoveComment, DTPBPtr, 0xA260, 0x0029, FSDispatch);

OSErr PBDTGetComment(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTGetComment, DTPBPtr, 0xA260, 0x002A, FSDispatch);

OSErr PBDTFlush(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTFlush, DTPBPtr, 0xA260, 0x002B, FSDispatch);

OSErr PBDTReset(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTReset, DTPBPtr, 0xA260, 0x002C, FSDispatch);

OSErr PBDTGetInfo(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTGetInfo, DTPBPtr, 0xA260, 0x002D, FSDispatch);

OSErr PBDTOpenInform(DTPBPtr dtp);REGISTER_SUBTRAP2(PBDTOpenInform, 0xA060, 0x002E, FSDispatch, D0(A0));


OSErr PBDTDelete(DTPBPtr dtp, Boolean async);FILE_SUBTRAP(PBDTDelete, DTPBPtr, 0xA260, 0x002F, FSDispatch);

} /* namespace Executor */