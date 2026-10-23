/*****************************************************************************
 *                                                                           *
 *                                   MPW.h                                   *
 *                                   =====                                   *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    LONGINT, OSType, INTEGER                                *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME MPW
#include <base/api-module.h>


namespace Executor {

struct MPWFile;

using MPWFileProcPtr = UPP<LONGINT(MPWFile *file), callconv::CCall>;


using MPWQuitProcPtr = UPP<void(), callconv::CCall>;


using MPWAccessProcPtr = UPP<LONGINT(char *name, LONGINT op, MPWFile *file), callconv::CCall>;


using MPWIOCtlProcPtr = UPP<LONGINT(MPWFile *file, LONGINT cmd, void *param), callconv::CCall>;


struct fsysTable{GUEST_STRUCT;GUEST<OSType> magic;GUEST<MPWAccessProcPtr> access;GUEST<MPWFileProcPtr> close;GUEST<MPWFileProcPtr> read;GUEST<MPWFileProcPtr> write;GUEST<MPWIOCtlProcPtr> ioctl;};void describeStruct(const fsysTable&, std::ostream&);

struct MPWFile{GUEST_STRUCT;GUEST<INTEGER> flags;GUEST<INTEGER> err;GUEST<fsysTable*> functions;GUEST<LONGINT> cookie;GUEST<LONGINT> count;GUEST<void*> buffer;};void describeStruct(const MPWFile&, std::ostream&);

struct MPWSeekParamBlock{GUEST_STRUCT;GUEST<LONGINT> whence;GUEST<LONGINT> offset;};void describeStruct(const MPWSeekParamBlock&, std::ostream&);

struct devtable{GUEST_STRUCT;fsysTable fsys;fsysTable econ;fsysTable syst;};void describeStruct(const devtable&, std::ostream&);

struct PgmInfo2{GUEST_STRUCT;GUEST<INTEGER> magic2;GUEST<LONGINT> argc;GUEST<GUEST<char*>*> argv;GUEST<GUEST<char*>*> envp;GUEST<LONGINT> exitCode;GUEST<LONGINT> x;GUEST<LONGINT> y;GUEST<INTEGER> tableSize;GUEST<MPWFile*> ioptr;GUEST<devtable*> devptr;};void describeStruct(const PgmInfo2&, std::ostream&);

struct PgmInfo1{GUEST_STRUCT;GUEST<OSType> magic;GUEST<PgmInfo2*> pgmInfo2;};static_assert(sizeof(PgmInfo1) == 8);void describeStruct(const PgmInfo1&, std::ostream&);

const LowMemGlobal<PgmInfo1 *> MacPgm{ 0x316 };
inline PgmInfo1 * LMGetMacPgm() { return LM(MacPgm); }
inline void LMSetMacPgm(PgmInfo1 *val) { LM(MacPgm) = val; }
NOTRAP_FUNCTION2(LMGetMacPgm);
NOTRAP_FUNCTION2(LMSetMacPgm);


} /* namespace Executor */