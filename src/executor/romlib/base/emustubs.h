#if !defined(_RSYS_EMUSTUBS_H_)
#define _RSYS_EMUSTUBS_H_

/*
 * Copyright 1995, 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 */

#include <ExMacTypes.h>
#include <DialogMgr.h>
#include <MemoryMgr.h>

#define MODULE_NAME base_emustubs
#include <base/api-module.h>

namespace Executor
{
struct adbop_t
{
    GUEST_STRUCT;
    GUEST<Ptr> buffer;
    GUEST<ProcPtr> proc;
    GUEST<Ptr> data;
};

typedef struct comm_toolbox_dispatch_args
{
    GUEST_STRUCT;
    GUEST<int16_t> selector;

    union {
        struct
        {
            GUEST<int16_t> n_items;
            GUEST<DialogPtr> dp;
        } shorten_args;
        struct
        {
            GUEST<DITLMethod> method;
            GUEST<Handle> new_items_h;
            GUEST<DialogPtr> dp;
        } append_args;
        struct
        {
            GUEST<DialogPtr> dp;
        } count_args;
        struct
        {
            GUEST<QElemPtr> qp;
        } crm_args;
    } args;
} comm_toolbox_dispatch_args_t;

typedef void *voidptr;

struct initzonehiddenargs_t
{
    GUEST_STRUCT;
    GUEST<voidptr> startPtr;
    GUEST<voidptr> limitPtr;
    GUEST<INTEGER> cMoreMasters;
    GUEST<GrowZoneUPP> pGrowZone;
};

extern void ROMlib_reset_bad_trap_addresses(void);

RAW_68K_TRAP(Unimplemented, 0xA89F);
RAW_68K_FUNCTION(bad_trap_unimplemented);

RAW_68K_TRAP(Chain, 0xA9F3);
RAW_68K_TRAP(InitZone68K, 0xA019);
RAW_68K_TRAP(PostEvent, 0xA02F);
RAW_68K_TRAP(SlotManager, 0xA06E);
RAW_68K_TRAP(EqualString, 0xA03C);
RAW_68K_TRAP(DrvrInstall, 0xA03D);
RAW_68K_TRAP(DrvrRemove, 0xA03E);
RAW_68K_TRAP(RelString, 0xA050);
RAW_68K_TRAP(UpperString, 0xA054);
RAW_68K_TRAP(ADBOp, 0xA07C);
RAW_68K_TRAP(Microseconds, 0xA193);
//RAW_68K_TRAP(Gestalt, 0xA1AD);
RAW_68K_TRAP(LoadSeg, 0xA9F0);
RAW_68K_TRAP(CommToolboxDispatch, 0xA08B);
RAW_68K_TRAP(modeswitch, 0xAAFE);
RAW_68K_TRAP(WackyQD32Trap, 0xAB03);
RAW_68K_FUNCTION(Key1Trans);
RAW_68K_FUNCTION(Key2Trans);
RAW_68K_TRAP(Fix2X, 0xA843);
RAW_68K_TRAP(Frac2X, 0xA845);
RAW_68K_TRAP(SCSIDispatch, 0xA815);
RAW_68K_TRAP(IMVI_LowerText, 0xA056);
RAW_68K_TRAP(GetDefaultStartup, 0xA07D);
RAW_68K_TRAP(SetDefaultStartup, 0xA07E);
RAW_68K_TRAP(GetVideoDefault, 0xA080);
RAW_68K_TRAP(SetVideoDefault, 0xA081);
RAW_68K_TRAP(GetOSDefault, 0xA084);
RAW_68K_TRAP(SetOSDefault, 0xA083);
RAW_68K_TRAP(IMVI_ReadXPRam, 0xA051);
RAW_68K_TRAP(IMVI_PPC, 0xA0DD);

static_assert(sizeof(adbop_t) == 12);
static_assert(sizeof(comm_toolbox_dispatch_args_t) == 12);
static_assert(sizeof(initzonehiddenargs_t) == 14);
}
#endif
