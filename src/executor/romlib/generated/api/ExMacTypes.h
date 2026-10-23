/*****************************************************************************
 *                                                                           *
 *                                 MacTypes.h                                *
 *                                 ==========                                *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>

#define MODULE_NAME MacTypes
#include <base/api-module.h>


namespace Executor {

typedef int16_t INTEGER;

typedef int32_t LONGINT;

typedef uint32_t ULONGINT;

typedef int8_t Boolean;

// very important not to use this as char
typedef int16_t CharParameter;

typedef int8_t SignedByte;

typedef uint8_t Byte;

typedef char *Ptr;

typedef GUEST<Ptr> *Handle;

typedef int8_t Boolean;

typedef int8_t SInt8;

typedef uint8_t UInt8;

typedef int16_t SInt16;

typedef uint16_t UInt16;

typedef int32_t SInt32;

typedef uint32_t UInt32;

typedef Byte Str15[16];

typedef Byte Str31[32];

typedef Byte Str32[33];

typedef Byte Str63[64];

typedef Byte Str255[256];

typedef Byte *StringPtr;

typedef const uint8_t *ConstStringPtr;

typedef ConstStringPtr ConstStr255Param;

typedef ConstStringPtr ConstStr63Param;

typedef ConstStringPtr ConstStr31Param;

typedef ConstStringPtr ConstStr16Param;

typedef GUEST<StringPtr> *StringHandle;

struct RoutineDescriptor;

typedef RoutineDescriptor *UniversalProcPtr;

using VoidUPP = UPP<void()>;


typedef LONGINT Fixed;

typedef LONGINT Fract;

// SmallFract represnts values between 0 and 65535
typedef uint16_t SmallFract;

enum {MaxSmallFract = 65535,
};

typedef double Extended;

typedef LONGINT Size;

typedef INTEGER OSErr;

BEGIN_EXECUTOR_ONLY
class OSErrorException : public std::runtime_error
{
public:
    OSErr code;

    OSErrorException(OSErr err) : std::runtime_error("oserror"), code(err) {}
};
END_EXECUTOR_ONLY


typedef LONGINT OSType;

typedef LONGINT ResType;

BEGIN_EXECUTOR_ONLY
constexpr OSType operator"" _4(const char*x, size_t n)
{
    assert(n == 4);
    return (uint8_t(x[0]) << 24) | (uint8_t(x[1]) << 16) | (uint8_t(x[2]) << 8) | uint8_t(x[3]);
}
END_EXECUTOR_ONLY


union QElem;

typedef QElem *QElemPtr;

struct QHdr{GUEST_STRUCT;GUEST<INTEGER> qFlags;GUEST<QElemPtr> qHead;GUEST<QElemPtr> qTail;};static_assert(sizeof(QHdr) == 10);void describeStruct(const QHdr&, std::ostream&);

typedef QHdr *QHdrPtr;

enum {noErr = 0,
paramErr = -50,
};

struct Rect{GUEST_STRUCT;GUEST<INTEGER> top;GUEST<INTEGER> left;GUEST<INTEGER> bottom;GUEST<INTEGER> right;};static_assert(sizeof(Rect) == 8);void describeStruct(const Rect&, std::ostream&);

typedef Rect *RectPtr;

BEGIN_EXECUTOR_ONLY
inline short RECT_WIDTH(const Rect *r)
{
    return r->right - r->left;
}
inline short RECT_HEIGHT(const Rect *r)
{
    return r->bottom - r->top;
}
END_EXECUTOR_ONLY


// from IntlUtil.h
typedef INTEGER ScriptCode;

typedef INTEGER LangCode;

} /* namespace Executor */