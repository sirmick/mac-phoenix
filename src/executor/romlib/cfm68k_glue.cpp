/* MacPhoenix: InterfaceLib routines that are glue on a Mac rather than traps
 * of their own (docs/executor/CFM68K.md). Classic 68k applications get them
 * from MPW's Interface.o or inline trap words; code fragments import them
 * from InterfaceLib by name, and Executor's generated InterfaceLib exports
 * only routines that have a wrapper. Each is written from Inside Macintosh's
 * description of the routine; the trap-based ones run their trap through
 * the trap table, so an extension's patch sees the call. */

#include <vector>
#include <base/common.h>
#include <base/builtinlibs.h>
#include <base/functions.impl.h>
#include <base/traps.h>

#include <DialogMgr.h>
#include <FileMgr.h>
#include <FontMgr.h>
#include <ScriptMgr.h>
#include <IntlUtil.h>
#include <MemoryMgr.h>
#include <OSUtil.h>
#include <ThreadMgr.h>
#include <ToolboxEvent.h>
#include <ToolboxUtil.h>
#include <Components.h>
#include <AppleEvents.h>
#include <MixedMode.h>
#include <CodeFragments.h>
#include <mman/mman.h>
#include <rsys/cfm.h>

#include <cstring>

extern unsigned char XPRAM[8192];

using namespace Executor;

namespace Executor
{


/* A Pascal trap called as 68k code calls it: room for the result, then the
   arguments, the first deepest; D0 the selector of a dispatcher that takes
   it there. Through the trap table, so patches run. */
namespace {
struct PascalArg
{
    int size; /* 2 or 4 */
    uint32_t value;
};
}


uint32_t pascal_trap(uint16_t trap, uint32_t d0, int resultSize,
                     std::initializer_list<PascalArg> args)
{
    uint32_t save_a7 = EM_A7;
    if(resultSize)
    {
        EM_A7 -= resultSize;
        if(resultSize == 4)
            *ptr_from_longint<GUEST<uint32_t> *>(EM_A7) = 0;
        else
            *ptr_from_longint<GUEST<uint16_t> *>(EM_A7) = 0;
    }
    for(const PascalArg& a : args)
    {
        EM_A7 -= a.size;
        if(a.size == 4)
            *ptr_from_longint<GUEST<uint32_t> *>(EM_A7) = a.value;
        else
            *ptr_from_longint<GUEST<uint16_t> *>(EM_A7) = a.value;
    }
    EM_D0 = d0;
    EM_D1 = trap;
    uint32_t addr = (trap & TOOLBIT) ? tooltraptable[trap & 0x3FF] : ostraptable[trap & 0xFF];
    execute68K(addr);
    uint32_t result = 0;
    if(resultSize == 4)
        result = *ptr_from_longint<GUEST<uint32_t> *>(EM_A7);
    else if(resultSize == 2)
        result = *ptr_from_longint<GUEST<uint16_t> *>(EM_A7);
    EM_A7 = save_a7;
    return result;
}

/* A register-based OS trap: A0 the parameter block, D0 the selector, the
   trap word's flag bits (async, HFS) in D1. */
OSErr register_trap(uint16_t trap, uint32_t d0, void *pb)
{
    EM_A0 = US_TO_SYN68K(pb);
    EM_D0 = d0;
    EM_D1 = trap;
    execute68K(ostraptable[trap & 0xFF]);
    return (int16_t)EM_D0;
}

/* ---- strings ------------------------------------------------------------ */

StringPtr glue_c2pstr(char *s)
{
    size_t n = std::min<size_t>(strlen(s), 255);
    memmove(s + 1, s, n);
    s[0] = n;
    return (StringPtr)s;
}

char *glue_p2cstr(StringPtr s)
{
    size_t n = s[0];
    memmove(s, s + 1, n);
    s[n] = 0;
    return (char *)s;
}

void glue_c2pstrcpy(StringPtr dst, const char *src)
{
    size_t n = std::min<size_t>(strlen(src), 255);
    memmove(dst + 1, src, n);
    dst[0] = n;
}

void glue_p2cstrcpy(char *dst, ConstStringPtr src)
{
    size_t n = src[0];
    memmove(dst, src + 1, n);
    dst[n] = 0;
}

void glue_CopyPascalStringToC(ConstStringPtr src, char *dst)
{
    glue_p2cstrcpy(dst, src);
}

void glue_CopyCStringToPascal(const char *src, StringPtr dst)
{
    glue_c2pstrcpy(dst, src);
}

/* ---- the Operating System and International Utilities ------------------- */

/* The itl2 handle chooses the sorting rules; Executor has one set. */
INTEGER glue_IUEqualPString(ConstStringPtr a, ConstStringPtr b, Handle)
{
    return IUEqualString(a, b);
}

INTEGER glue_IUCompPString(ConstStringPtr a, ConstStringPtr b, Handle)
{
    return IUCompString(a, b);
}

/* A MachineLocation: 12 bytes, kept in parameter RAM $E4..$EF (Inside
   Macintosh: Operating System Utilities, "Reading and Setting the
   Geographic Location"). */
void glue_ReadLocation(Ptr loc)
{
    memcpy(loc, &XPRAM[0xE4], 12);
}

void glue_WriteLocation(Ptr loc)
{
    memcpy(&XPRAM[0xE4], loc, 12);
}

void glue_FlushInstructionCache()
{
    C_FlushCodeCache();
}

void glue_FlushDataCache()
{
}

/* ---- the Memory Manager -------------------------------------------------- */

THz glue_GetMyZone()
{
    return ApplicationZone();
}

Size glue_InlineGetHandleSize(Handle h)
{
    return GetHandleSize(h);
}

/* ---- fonts and events ---------------------------------------------------- */

INTEGER glue_GetSysFont()
{
    return LM(SysFontFam);
}

INTEGER glue_GetAppFont()
{
    return LM(ApFontID);
}

INTEGER glue_LMGetSysFontSize()
{
    return LM(SysFontSiz);
}

void glue_LMSetSysFontSize(INTEGER size)
{
    LM(SysFontSiz) = size;
}

INTEGER glue_GetSysDirection()
{
    return LM(TESysJust); /* GetSysDirection: TESysJust */
}

/* ---- the Dialog Manager --------------------------------------------------- */

/* ---- the File Manager ----------------------------------------------------- */

/* Guest memory for a parameter block or descriptor handed to the Toolbox:
   a patched trap gets its address, and host stacks aren't guest memory. */
namespace {
template<typename T>
struct GuestTemp
{
    T *p = (T *)NewPtrClear(sizeof(T));
    ~GuestTemp() { DisposePtr((Ptr)p); }
    T *operator->() { return p; }
};
}


OSErr glue_DirCreate(INTEGER vRefNum, LONGINT parentDirID, ConstStringPtr name,
                     GUEST<LONGINT> *createdDirID)
{
    GuestTemp<HParamBlockRec> pb;
    pb->fileParam.ioNamePtr = (StringPtr)name;
    pb->fileParam.ioVRefNum = vRefNum;
    pb->fileParam.ioDirID = parentDirID;
    OSErr err = PBDirCreate(pb.p, false);
    if(err == noErr && createdDirID)
        *createdDirID = pb->fileParam.ioDirID;
    return err;
}

/* HSetFInfo, HSetFLock, HRstFLock, CatMove (Inside Macintosh: Files, the
   high-level HFS routines): glue over their parameter-block calls. */
OSErr glue_HSetFInfo(INTEGER vRefNum, LONGINT dirID, ConstStringPtr name,
                     const FInfo *fndrInfo)
{
    GuestTemp<HParamBlockRec> pb;
    pb->fileParam.ioNamePtr = (StringPtr)name;
    pb->fileParam.ioVRefNum = vRefNum;
    pb->fileParam.ioFVersNum = 0;
    pb->fileParam.ioFDirIndex = 0;
    pb->fileParam.ioDirID = dirID;
    OSErr err = PBHGetFInfo(pb.p, false);
    if(err != noErr)
        return err;
    pb->fileParam.ioFlFndrInfo = *fndrInfo;
    pb->fileParam.ioDirID = dirID;      /* PBHGetFInfo put the file number there */
    return PBHSetFInfo(pb.p, false);
}

static OSErr hfs_lock(INTEGER vRefNum, LONGINT dirID, ConstStringPtr name, bool lock)
{
    GuestTemp<HParamBlockRec> pb;
    pb->fileParam.ioNamePtr = (StringPtr)name;
    pb->fileParam.ioVRefNum = vRefNum;
    pb->fileParam.ioFVersNum = 0;
    pb->fileParam.ioDirID = dirID;
    return lock ? PBHSetFLock(pb.p, false) : PBHRstFLock(pb.p, false);
}

OSErr glue_HSetFLock(INTEGER vRefNum, LONGINT dirID, ConstStringPtr name)
{
    return hfs_lock(vRefNum, dirID, name, true);
}

OSErr glue_HRstFLock(INTEGER vRefNum, LONGINT dirID, ConstStringPtr name)
{
    return hfs_lock(vRefNum, dirID, name, false);
}

OSErr glue_CatMove(INTEGER vRefNum, LONGINT dirID, ConstStringPtr oldName,
                   LONGINT newDirID, ConstStringPtr newName)
{
    GuestTemp<CMovePBRec> pb;
    pb->ioNamePtr = (StringPtr)oldName;
    pb->ioVRefNum = vRefNum;
    pb->ioNewName = (StringPtr)newName;
    pb->ioNewDirID = newDirID;
    pb->ioDirID = dirID;
    return PBCatMove(pb.p, false);
}

/* The volume an open file is on (Inside Macintosh: Files, glue over
   PBGetFCBInfo); Retro68's PowerPC runtime imports it. */
OSErr glue_GetVRefNum(INTEGER prn, GUEST<INTEGER> *vrn)
{
    GuestTemp<FCBPBRec> pb;
    pb->ioRefNum = prn;
    pb->ioFCBIndx = 0;
    OSErr err = PBGetFCBInfo(pb.p, false);
    if(err == noErr && vrn)
        *vrn = pb->ioFCBVRefNum;
    return err;
}

/* _HFSDispatch $A260, selector $39 (Inside Macintosh: Files, "Low-Level File
   Access Routines"). */
OSErr glue_PBHOpenRFDenySync(HParmBlkPtr pb)
{
    return register_trap(0xA260, 0x39, pb);
}

OSErr glue_PBHOpenRFDenyAsync(HParmBlkPtr pb)
{
    return register_trap(0xA260 | 0x400, 0x39, pb);
}

/* ---- the Thread Manager --------------------------------------------------- */

OSErr glue_YieldToAnyThread()
{
    return C_YieldToThread(kNoThreadID);
}

/* ---- the Object Support Library ------------------------------------------ */

/* AEPackObject's routines (Inside Macintosh: Interapplication Communication,
   "Creating Object Specifier Records"): an object specifier and its range,
   comparison and logical descriptors are AE records of their own types,
   built from the keyword-specified descriptors listed there. */
namespace {
struct KeyedDesc
{
    AEKeyword key;
    AEDesc *desc;
};
}


namespace {
struct KeyedType
{
    AEKeyword key;
    DescType type;
    uint32_t value;
};
}


OSErr build_record(DescType type, std::initializer_list<KeyedType> values,
                   std::initializer_list<KeyedDesc> descs, Boolean disposeInputs, AEDesc *result)
{
    GuestTemp<AEDesc> rec;
    GuestTemp<GUEST<uint32_t>> value;
    OSErr err = C_AECreateList(nullptr, 0, true, (GUEST<AEDescList> *)rec.p);
    for(const KeyedType& v : values)
        if(err == noErr)
        {
            *value.p = v.value;
            err = C_AEPutParamPtr((GUEST<AERecord> *)rec.p, v.key, v.type, value.p, 4);
        }
    for(const KeyedDesc& d : descs)
        if(err == noErr && d.desc)
            err = C_AEPutParamDesc((GUEST<AERecord> *)rec.p, d.key, d.desc);
    if(err == noErr)
    {
        if(C_AECoerceDesc(rec.p, type, result) == noErr)
            C_AEDisposeDesc(rec.p);
        else
        {
            /* The same data, as the record's new type. */
            *result = *rec.p;
            result->descriptorType = type;
        }
    }
    else
        C_AEDisposeDesc(rec.p);
    if(disposeInputs)
        for(const KeyedDesc& d : descs)
            if(d.desc)
                C_AEDisposeDesc(d.desc);
    return err;
}

OSErr glue_AEObjectInit()
{
    return noErr; /* Executor's object support is always there */
}

OSErr glue_CreateOffsetDescriptor(LONGINT offset, AEDesc *result)
{
    GuestTemp<GUEST<LONGINT>> value;
    *value.p = offset;
    return C_AECreateDesc("long"_4, value.p, 4, result);
}

OSErr glue_CreateCompDescriptor(DescType op, AEDesc *operand1, AEDesc *operand2,
                                Boolean disposeInputs, AEDesc *result)
{
    return build_record("cmpd"_4, { { "relo"_4, "enum"_4, (uint32_t)op } },
                        { { "obj1"_4, operand1 }, { "obj2"_4, operand2 } }, disposeInputs, result);
}

OSErr glue_CreateLogicalDescriptor(AEDesc *terms, DescType op, Boolean disposeInputs,
                                   AEDesc *result)
{
    return build_record("logi"_4, { { "logc"_4, "enum"_4, (uint32_t)op } },
                        { { "term"_4, terms } }, disposeInputs, result);
}

OSErr glue_CreateObjSpecifier(DescType desiredClass, AEDesc *container, DescType keyForm,
                              AEDesc *keyData, Boolean disposeInputs, AEDesc *result)
{
    return build_record("obj "_4,
                        { { "want"_4, "type"_4, (uint32_t)desiredClass },
                          { "form"_4, "enum"_4, (uint32_t)keyForm } },
                        { { "from"_4, container }, { "seld"_4, keyData } }, disposeInputs, result);
}

OSErr glue_CreateRangeDescriptor(AEDesc *start, AEDesc *stop, Boolean disposeInputs,
                                 AEDesc *result)
{
    return build_record("rang"_4, {}, { { "star"_4, start }, { "stop"_4, stop } },
                        disposeInputs, result);
}

/* ---- the Component Manager ($A82A, the selector in D0) ------------------- */

/* CallComponentUPP (Components.h: "a global variable exported from
   InterfaceLib ... passed to CallUniversalProc to manually call a component
   function"): a routine descriptor for a routine taking a ComponentParameters
   block, whose flags/paramSize/what words, parameters (paramSize bytes, the
   last first) and the instance after them are the stack a 68k component call
   pushes before _ComponentDispatch with D0 = 0. */
LONGINT glue_CallComponent(Ptr params)
{
    uint32_t size = 4 + (uint8_t)params[1] + 4;
    uint32_t save_a7 = EM_A7;
    EM_A7 -= 4;
    *ptr_from_longint<GUEST<uint32_t> *>(EM_A7) = 0;
    EM_A7 -= size;
    memcpy(ptr_from_longint<void *>(EM_A7), params, size);
    EM_D0 = 0;
    EM_D1 = 0xA82A;
    execute68K(tooltraptable[0xA82A & 0x3FF]);
    LONGINT result = *ptr_from_longint<GUEST<uint32_t> *>(save_a7 - 4);
    EM_A7 = save_a7;
    return result;
}

/* A component call from CFM-68K code: the instance and the routine's other
   parameters in 4-byte slots, pushed for _ComponentDispatch as a 68k
   caller's ComponentCallNow(what, paramSize) would have them: room for the
   result, the instance (first parameter, deepest), the rest at their own
   sizes, then paramSize and what; D0 = 0. */
namespace {
struct ComponentRoutine
{
    const char *name;
    uint16_t what;
    std::vector<int> sizes; /* after the instance: 2 or 4 bytes */
};
}


/* The call, as a 68k caller makes it: args[0] the instance, then the
   routine's other parameters (each in a 32-bit value). */
uint32_t make_component_call(const ComponentRoutine& r, const uint32_t *args)
{
    uint32_t paramSize = 0;
    for(int size : r.sizes)
        paramSize += size;

    uint32_t save_a7 = EM_A7;
    EM_A7 -= 4;
    *ptr_from_longint<GUEST<uint32_t> *>(EM_A7) = 0;
    EM_A7 -= 4;
    *ptr_from_longint<GUEST<uint32_t> *>(EM_A7) = args[0];
    for(size_t i = 0; i < r.sizes.size(); i++)
    {
        uint32_t value = args[1 + i];
        EM_A7 -= r.sizes[i];
        if(r.sizes[i] == 2)
            *ptr_from_longint<GUEST<uint16_t> *>(EM_A7) = value;
        else
            *ptr_from_longint<GUEST<uint32_t> *>(EM_A7) = value;
    }
    EM_A7 -= 4;
    *ptr_from_longint<GUEST<uint32_t> *>(EM_A7) = (paramSize << 16) | r.what;
    EM_D0 = 0;
    EM_D1 = 0xA82A;
    execute68K(tooltraptable[0xA82A & 0x3FF]);
    uint32_t result = *ptr_from_longint<GUEST<uint32_t> *>(save_a7 - 4);
    EM_A7 = save_a7;
    return result;
}

syn68k_addr_t call_component_routine(const ComponentRoutine& r)
{
    uint32_t nargs = 1 + r.sizes.size();
    std::vector<uint32_t> args(nargs);
    for(uint32_t i = 0; i < nargs; i++)
        args[i] = *ptr_from_longint<GUEST<uint32_t> *>(EM_A7 + 4 + 4 * i);
    uint32_t result = make_component_call(r, args.data());

    syn68k_addr_t ret = POPADDR();
    EM_A7 += 4 * nargs; /* a routine with fixed arguments pops them */
    EM_D0 = EM_A0 = result;
    return ret;
}

/* ... and from PowerPC code: the instance in r3, the others after it. */
uint32_t call_component_routine_ppc(const ComponentRoutine& r, PowerCore& cpu)
{
    uint32_t args[8];
    for(size_t i = 0; i < 1 + r.sizes.size() && i < 8; i++)
        args[i] = cpu.r[3 + i];
    /* 68k code run from here pushes below the caller's frame. */
    EM_A7 = cpu.r[1];
    cpu.r[3] = make_component_call(r, args);
    return cpu.lr;
}

void add_call_component_upp()
{
    builtinlibs::addCFM68KEntrypoint("ExecutorPrivate", "CallComponent",
        [](syn68k_addr_t) {
            return callfromCFM68K::Invoker<LONGINT (Ptr)>::invokeFromCFM68K(&glue_CallComponent, true);
        });
    ConnectionID cid = builtinlibs::getBuiltinLib((StringPtr) "\017ExecutorPrivate", "m68k"_4);
    GUEST<Ptr> tv;
    FindSymbol(cid, (StringPtr) "\015CallComponent", &tv, nullptr);

    TheZoneGuard guard(LM(SysZone));
    /* kPascalStackBased, a 4-byte result, one 4-byte parameter. */
    UniversalProcPtr upp = NewRoutineDescriptor((ProcPtr)(Ptr)tv, 0xF0, kM68kISA | 0x10);
    auto *cell = (GUEST<UniversalProcPtr> *)NewPtrClear(sizeof(GUEST<UniversalProcPtr>));
    *cell = upp;
    builtinlibs::addCFM68KData("InterfaceLib", "CallComponentUPP", cell);
    /* PowerPC callers reach it through Mixed Mode the same way. */
    builtinlibs::addPPCData("InterfaceLib", "CallComponentUPP", cell);
}

}

/* Called before the first builtin library connection is made (the file is
   otherwise unreferenced, so a static initializer would not be linked). */
void Executor::builtinlibs::addGlueEntrypoints()
{
    {
        /* the older names (Strings.h's OLDROUTINENAMES): MPW's CFM-68K
           runtime calls P2CStr for the application's name */
        add_call_component_upp();
    }
}

/* A component call a library exports (the definitions' COMPONENT_CALL):
   AppleScriptLib's OSA routines for CFM-68K and PowerPC code. CFM finds
   Apple's AppleScriptLib first where the System has it (it exports more
   than the definitions have); these answer where it doesn't. */
void Executor::traps::ComponentCallExport::init()
{
    auto *r = new ComponentRoutine{ name, what, {} };
    for(const char *p = sizes; *p; p++)
        r->sizes.push_back(*p - '0');
    assert(r->sizes.size() < 8);
    builtinlibs::addCFM68KEntrypoint(library, name,
        [r](syn68k_addr_t) { return call_component_routine(*r); });
    builtinlibs::addPPCEntrypoint(library, name,
        [r](PowerCore &cpu) { return call_component_routine_ppc(*r, cpu); });
}
