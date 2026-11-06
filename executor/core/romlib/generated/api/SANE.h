/*****************************************************************************
 *                                                                           *
 *                                   SANE.h                                  *
 *                                   ======                                  *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    INTEGER, Byte, LONGINT                                  *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME SANE
#include <base/api-module.h>


namespace Executor {

// Big-endian 64 bit "comp" data type.  Note that this has a NaN value!
// typedef union {
//     struct
//     {
//         ULONGINT hi;
//         ULONGINT lo;
//     } hilo;
//     signed long long val;
// } comp_t;
struct comp_t{GUEST_STRUCT;GUEST<int64_t> val;};static_assert(sizeof(comp_t) == 8);void describeStruct(const comp_t&, std::ostream&);

// "Packed" IEEE 80 bit FP representation (zero field omitted).
struct x80_t{GUEST_STRUCT;GUEST<uint16_t> sgn_and_exp;GUEST<uint64_t> mantissa;};static_assert(sizeof(x80_t) == 10);void describeStruct(const x80_t&, std::ostream&);

// For backwards compatibility with old stuff.
typedef x80_t extended80;

typedef comp_t comp;

BEGIN_EXECUTOR_ONLY
/* Begin old stuff:
This only makes sense on the 68k. */
#if defined(mc68000)
END_EXECUTOR_ONLY


struct extended96{GUEST_STRUCT;GUEST<INTEGER> exp;GUEST<INTEGER> zero;GUEST<INTEGER> man[4];};void describeStruct(const extended96&, std::ostream&);

BEGIN_EXECUTOR_ONLY
#endif
END_EXECUTOR_ONLY


enum {SIGDIGLEN = 20,
};

struct Decimal{GUEST_STRUCT;uint8_t sgn;uint8_t unused_filler;GUEST<INTEGER> exp;uint8_t sig[SIGDIGLEN];};static_assert(sizeof(Decimal) == 24);void describeStruct(const Decimal&, std::ostream&);

typedef enum {FloatDecimal,
FixedDecimal = 256,
}toobigdecformstyle_t;

typedef INTEGER DecFormStyle;

enum {DECIMALTYPEMASK = 256,
};

typedef enum {SNaN = 1,
QNaN,
Infinite,
ZeroNum,
NormalNum,
DenormalNum,
}NumClass;

struct DecForm{GUEST_STRUCT;GUEST<DecFormStyle> style;GUEST<INTEGER> digits;};static_assert(sizeof(DecForm) == 4);void describeStruct(const DecForm&, std::ostream&);

typedef char *Decstr;

enum {FX_OPERAND = 0,
FD_OPERAND = 2048,
FS_OPERAND = 4096,
FC_OPERAND = 12288,
FI_OPERAND = 8192,
FL_OPERAND = 10240,
};

enum {Fx2X_OPCODE = 14,
};

enum {FI2X = FI_OPERAND | Fx2X_OPCODE,
};

DISPATCHER_TRAP(Pack4, 0xA9EB, StackWLookahead<0xFF>);


DISPATCHER_TRAP(Pack5, 0xA9EC, StackWMasked<0xFF>);


DISPATCHER_TRAP(Pack7, 0xA9EE, StackW);


void C_ROMlib_Fsetenv(GUEST<INTEGER> *dp, INTEGER sel);PASCAL_SUBTRAP(ROMlib_Fsetenv, 0xA9EB, 0x0001, Pack4);


void C_ROMlib_Fgetenv(GUEST<INTEGER> *dp, INTEGER sel);PASCAL_SUBTRAP(ROMlib_Fgetenv, 0xA9EB, 0x0003, Pack4);


void C_ROMlib_Fprocentry(GUEST<INTEGER> *dp, INTEGER sel);PASCAL_SUBTRAP(ROMlib_Fprocentry, 0xA9EB, 0x0017, Pack4);


void C_ROMlib_Fprocexit(GUEST<INTEGER> *dp, INTEGER sel);PASCAL_SUBTRAP(ROMlib_Fprocexit, 0xA9EB, 0x0019, Pack4);


void C_ROMlib_Ftestxcp(GUEST<INTEGER> *dp, INTEGER sel);PASCAL_SUBTRAP(ROMlib_Ftestxcp, 0xA9EB, 0x001B, Pack4);


void C_ROMlib_FsqrtX(GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_FsqrtX, 0xA9EB, 0x0012, Pack4);


void C_ROMlib_FscalbX(GUEST<INTEGER> *sp, GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_FscalbX, 0xA9EB, 0x0018, Pack4);


void C_ROMlib_FlogbX(GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_FlogbX, 0xA9EB, 0x001A, Pack4);


void C_ROMlib_FabsX(GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_FabsX, 0xA9EB, 0x000F, Pack4);


void C_ROMlib_FnegX(GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_FnegX, 0xA9EB, 0x000D, Pack4);


void C_ROMlib_FrintX(GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_FrintX, 0xA9EB, 0x0014, Pack4);


void C_ROMlib_FtintX(GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_FtintX, 0xA9EB, 0x0016, Pack4);


void C_ROMlib_Fcpysgnx(x80_t *sp, x80_t *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Fcpysgnx, 0xA9EB, 0x0011, Pack4);


void C_ROMlib_Faddx(void *sp, GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Faddx, 0xA9EB, 0x0000, Pack4);


void C_ROMlib_Fsubx(void *sp, GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Fsubx, 0xA9EB, 0x0002, Pack4);


void C_ROMlib_Fmulx(void *sp, GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Fmulx, 0xA9EB, 0x0004, Pack4);


void C_ROMlib_Fdivx(void *sp, GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Fdivx, 0xA9EB, 0x0006, Pack4);


void C_ROMlib_Fremx(void *sp, GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Fremx, 0xA9EB, 0x000C, Pack4);


void C_ROMlib_Fcmpx(void *sp, GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Fcmpx, 0xA9EB, 0x0008, Pack4);


void C_ROMlib_FcpXx(void *sp, GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_FcpXx, 0xA9EB, 0x000A, Pack4);


void C_ROMlib_FX2x(GUEST<extended80> *sp, void *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_FX2x, 0xA9EB, 0x0010, Pack4);


void C_ROMlib_Fx2X(void *sp, GUEST<extended80> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Fx2X, 0xA9EB, 0x000E, Pack4);


void C_ROMlib_Fx2dec(DecForm *sp2, void *sp, Decimal *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Fx2dec, 0xA9EB, 0x000B, Pack4);


void C_ROMlib_Fdec2x(Decimal *sp, void *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Fdec2x, 0xA9EB, 0x0009, Pack4);


void C_ROMlib_Fclassx(void *sp, GUEST<INTEGER> *dp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Fclassx, 0xA9EB, 0x001C, Pack4);


void C_ROMlib_FlnX(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_FlnX, 0xA9EC, 0x0000, Pack5);


void C_ROMlib_Flog2X(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_Flog2X, 0xA9EC, 0x0002, Pack5);


void C_ROMlib_Fln1X(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_Fln1X, 0xA9EC, 0x0004, Pack5);


void C_ROMlib_Flog21X(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_Flog21X, 0xA9EC, 0x0006, Pack5);


void C_ROMlib_FexpX(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_FexpX, 0xA9EC, 0x0008, Pack5);


void C_ROMlib_Fexp2X(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_Fexp2X, 0xA9EC, 0x000A, Pack5);


void C_ROMlib_Fexp1X(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_Fexp1X, 0xA9EC, 0x000C, Pack5);


void C_ROMlib_Fexp21X(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_Fexp21X, 0xA9EC, 0x000E, Pack5);


void C_ROMlib_Fxpwri(GUEST<INTEGER> *sp, GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_Fxpwri, 0xA9EC, 0x0010, Pack5);


void C_ROMlib_Fxpwry(GUEST<extended80> *sp, GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_Fxpwry, 0xA9EC, 0x0012, Pack5);


void C_ROMlib_Fcompound(GUEST<extended80> *sp2, GUEST<extended80> *sp, GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_Fcompound, 0xA9EC, 0x0014, Pack5);


void C_ROMlib_Fannuity(GUEST<extended80> *sp2, GUEST<extended80> *sp, GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_Fannuity, 0xA9EC, 0x0016, Pack5);


void C_ROMlib_FsinX(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_FsinX, 0xA9EC, 0x0018, Pack5);


void C_ROMlib_FcosX(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_FcosX, 0xA9EC, 0x001A, Pack5);


void C_ROMlib_FtanX(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_FtanX, 0xA9EC, 0x001C, Pack5);


void C_ROMlib_FatanX(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_FatanX, 0xA9EC, 0x001E, Pack5);


void C_ROMlib_FrandX(GUEST<extended80> *dp);PASCAL_SUBTRAP(ROMlib_FrandX, 0xA9EC, 0x0020, Pack5);


void C_ROMlib_Fdec2str(DecForm *sp2, Decimal *sp, Decstr dp);PASCAL_SUBTRAP(ROMlib_Fdec2str, 0xA9EE, 0x0003, Pack7);


void C_ROMlib_Fcstr2dec(Decstr sp2, GUEST<INTEGER> *sp, Decimal *dp2, Byte *dp);PASCAL_SUBTRAP(ROMlib_Fcstr2dec, 0xA9EE, 0x0004, Pack7);


void C_ROMlib_Fpstr2dec(Decstr sp2, GUEST<INTEGER> *sp, Decimal *dp2, Byte *dp);PASCAL_SUBTRAP(ROMlib_Fpstr2dec, 0xA9EE, 0x0002, Pack7);


void C_ROMlib_Fsethv(GUEST<LONGINT> *hvp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Fsethv, 0xA9EB, 0x0005, Pack4);


void C_ROMlib_Fgethv(GUEST<LONGINT> *hvp, uint16_t sel);PASCAL_SUBTRAP(ROMlib_Fgethv, 0xA9EB, 0x0007, Pack4);


void C_ROMlib_FnextX(uint8_t *x, uint8_t *y, uint16_t sel);PASCAL_SUBTRAP(ROMlib_FnextX, 0xA9EB, 0x0013, Pack4);


} /* namespace Executor */