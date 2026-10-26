/* MacPhoenix: InterfaceLib routines that are glue on a Mac rather than traps
 * of their own (docs/executor/CFM68K.md). Classic 68k applications get them
 * from MPW's Interface.o or inline trap words; code fragments import them
 * from InterfaceLib by name, and Executor's generated InterfaceLib exports
 * only routines that have a wrapper. Each is written from Inside Macintosh's
 * description of the routine; the trap-based ones run their trap through
 * the trap table, so an extension's patch sees the call. */

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

namespace
{

/* A routine for both CFM-68K and PowerPC fragments. */
template<typename F>
struct Export;

template<typename Ret, typename... Args>
struct Export<Ret (Args...)>
{
    static void add(const char *name, Ret (*fn)(Args...))
    {
        builtinlibs::addCFM68KEntrypoint("InterfaceLib", name,
            [fn, pops = builtinlibs::cfm68kCalleePops(name)](syn68k_addr_t) {
                return callfromCFM68K::Invoker<Ret (Args...)>::invokeFromCFM68K(fn, pops);
            });
        builtinlibs::addPPCEntrypoint("InterfaceLib", name,
            [fn](PowerCore& cpu) {
                return callfromPPC::Invoker<Ret (Args...)>::invokeFromPPC(cpu, fn);
            });
    }
};

template<typename F>
void exportGlue(const char *name, F *fn)
{
    Export<F>::add(name, fn);
}

/* A Pascal trap called as 68k code calls it: room for the result, then the
   arguments, the first deepest; D0 the selector of a dispatcher that takes
   it there. Through the trap table, so patches run. */
struct PascalArg
{
    int size; /* 2 or 4 */
    uint32_t value;
};

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

Boolean glue_EqualString(ConstStringPtr a, ConstStringPtr b, Boolean caseSens, Boolean diacSens)
{
    return EqualString(a, b, caseSens, diacSens);
}

INTEGER glue_RelString(ConstStringPtr a, ConstStringPtr b, Boolean caseSens, Boolean diacSens)
{
    return RelString(a, b, caseSens, diacSens);
}

INTEGER glue_IUEqualString(ConstStringPtr a, ConstStringPtr b)
{
    return IUEqualString(a, b);
}

INTEGER glue_IUCompString(ConstStringPtr a, ConstStringPtr b)
{
    return IUCompString(a, b);
}

/* The itl2 handle chooses the sorting rules; Executor has one set. */
INTEGER glue_IUEqualPString(ConstStringPtr a, ConstStringPtr b, Handle)
{
    return IUEqualString(a, b);
}

INTEGER glue_IUCompPString(ConstStringPtr a, ConstStringPtr b, Handle)
{
    return IUCompString(a, b);
}

LONGINT glue_SetCurrentA5()
{
    return SetCurrentA5();
}

LONGINT glue_SetA5(LONGINT a5)
{
    return SetA5(a5);
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

LONGINT glue_GetDblTime()
{
    return GetDblTime();
}

LONGINT glue_GetCaretTime()
{
    return GetCaretTime();
}

INTEGER glue_GetSysDirection()
{
    return LM(TESysJust); /* GetSysDirection: TESysJust */
}

/* ---- the Dialog Manager --------------------------------------------------- */

void glue_DialogCut(DialogPtr d) { DialogCut(d); }
void glue_DialogCopy(DialogPtr d) { DialogCopy(d); }
void glue_DialogPaste(DialogPtr d) { DialogPaste(d); }
void glue_DialogDelete(DialogPtr d) { DialogDelete(d); }

INTEGER glue_CountDITL(DialogPtr d)
{
    return CountDITL(d);
}

void glue_AppendDITL(DialogPtr d, Handle h, INTEGER method)
{
    AppendDITL(d, h, (DITLMethod)method);
}

void glue_ShortenDITL(DialogPtr d, INTEGER n)
{
    ShortenDITL(d, n);
}

void glue_GetIndPattern(Byte *pat, INTEGER listID, INTEGER index)
{
    GetIndPattern(pat, listID, index);
}

/* ---- the File Manager ----------------------------------------------------- */

OSErr glue_GetVInfo(INTEGER drive, StringPtr name, GUEST<INTEGER> *vRefNum, GUEST<LONGINT> *freeBytes)
{
    return GetVInfo(drive, name, vRefNum, freeBytes);
}

/* Guest memory for a parameter block or descriptor handed to the Toolbox:
   a patched trap gets its address, and host stacks aren't guest memory. */
template<typename T>
struct GuestTemp
{
    T *p = (T *)NewPtrClear(sizeof(T));
    ~GuestTemp() { DisposePtr((Ptr)p); }
    T *operator->() { return p; }
};

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
struct KeyedDesc
{
    AEKeyword key;
    AEDesc *desc;
};

struct KeyedType
{
    AEKeyword key;
    DescType type;
    uint32_t value;
};

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
   block, whose flags/paramSize/what words and parameters are the stack a 68k
   component call pushes before _ComponentDispatch with D0 = 0. */
LONGINT glue_CallComponent(Ptr params)
{
    uint32_t size = 4 + (uint8_t)params[1];
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
struct ComponentRoutine
{
    const char *name;
    uint16_t what;
    std::initializer_list<int> sizes; /* after the instance: 2 or 4 bytes */
};

syn68k_addr_t call_component_routine(const ComponentRoutine& r)
{
    uint32_t args = EM_A7 + 4;
    uint32_t nargs = 1 + r.sizes.size();
    uint32_t paramSize = 0;
    for(int size : r.sizes)
        paramSize += size;

    uint32_t save_a7 = EM_A7;
    EM_A7 -= 4;
    *ptr_from_longint<GUEST<uint32_t> *>(EM_A7) = 0;
    EM_A7 -= 4;
    *ptr_from_longint<GUEST<uint32_t> *>(EM_A7) = *ptr_from_longint<GUEST<uint32_t> *>(args);
    uint32_t slot = args + 4;
    for(int size : r.sizes)
    {
        uint32_t value = *ptr_from_longint<GUEST<uint32_t> *>(slot);
        slot += 4;
        EM_A7 -= size;
        if(size == 2)
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

    syn68k_addr_t ret = POPADDR();
    EM_A7 += 4 * nargs; /* a routine with fixed arguments pops them */
    EM_D0 = EM_A0 = result;
    return ret;
}

/* AppleScriptLib: the Open Scripting Architecture's routines (OSA.h,
   OSAGeneric.h: each a component call, its selector and parameter sizes
   as the 68k interfaces' inline code gives them). */
const ComponentRoutine osa_routines[] = {
    { "OSALoad", 0x0001, { 4, 4, 4 } },
    { "OSAStore", 0x0002, { 4, 4, 4, 4 } },
    { "OSAExecute", 0x0003, { 4, 4, 4, 4 } },
    { "OSADisplay", 0x0004, { 4, 4, 4, 4 } },
    { "OSAScriptError", 0x0005, { 4, 4, 4 } },
    { "OSADispose", 0x0006, { 4 } },
    { "OSASetScriptInfo", 0x0007, { 4, 4, 4 } },
    { "OSAGetScriptInfo", 0x0008, { 4, 4, 4 } },
    { "OSASetActiveProc", 0x0009, { 4, 4 } },
    { "OSAGetActiveProc", 0x000A, { 4, 4 } },
    { "OSAScriptingComponentName", 0x0102, { 4 } },
    { "OSACompile", 0x0103, { 4, 4, 4 } },
    { "OSACopyID", 0x0104, { 4, 4 } },
    { "OSAGetSource", 0x0201, { 4, 4, 4 } },
    { "OSACoerceFromDesc", 0x0301, { 4, 4, 4 } },
    { "OSACoerceToDesc", 0x0302, { 4, 4, 4, 4 } },
    { "OSASetSendProc", 0x0401, { 4, 4 } },
    { "OSAGetSendProc", 0x0402, { 4, 4 } },
    { "OSASetCreateProc", 0x0403, { 4, 4 } },
    { "OSAGetCreateProc", 0x0404, { 4, 4 } },
    { "OSASetDefaultTarget", 0x0405, { 4 } },
    { "OSAStartRecording", 0x0501, { 4 } },
    { "OSAStopRecording", 0x0502, { 4 } },
    { "OSALoadExecute", 0x0601, { 4, 4, 4, 4 } },
    { "OSACompileExecute", 0x0602, { 4, 4, 4, 4 } },
    { "OSADoScript", 0x0603, { 4, 4, 4, 4, 4 } },
    { "OSASetCurrentDialect", 0x0701, { 2 } },
    { "OSAGetCurrentDialect", 0x0702, { 4 } },
    { "OSAAvailableDialects", 0x0703, { 4 } },
    { "OSAGetDialectInfo", 0x0704, { 2, 4, 4 } },
    { "OSAAvailableDialectCodeList", 0x0705, { 4 } },
    { "OSASetResumeDispatchProc", 0x0801, { 4, 4 } },
    { "OSAGetResumeDispatchProc", 0x0802, { 4, 4 } },
    { "OSAExecuteEvent", 0x0803, { 4, 4, 4, 4 } },
    { "OSADoEvent", 0x0804, { 4, 4, 4, 4 } },
    { "OSAMakeContext", 0x0805, { 4, 4, 4 } },
    { "OSAGetDefaultScriptingComponent", 0x1001, { 4 } },
    { "OSASetDefaultScriptingComponent", 0x1002, { 4 } },
    { "OSAGetScriptingComponent", 0x1003, { 4, 4 } },
    { "OSAGetScriptingComponentFromStored", 0x1004, { 4, 4 } },
    { "OSAGenericToRealID", 0x1005, { 4, 4 } },
    { "OSARealToGenericID", 0x1006, { 4, 4 } },
};

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
}

ComponentInstance glue_OpenDefaultComponent(OSType type, OSType subType)
{
    return ptr_from_longint<ComponentInstance>(
        pascal_trap(0xA82A, 0x21, 4, { { 4, (uint32_t)type }, { 4, (uint32_t)subType } }));
}

ComponentInstance glue_OpenComponent(Component c)
{
    return ptr_from_longint<ComponentInstance>(
        pascal_trap(0xA82A, 0x07, 4, { { 4, US_TO_SYN68K(c) } }));
}

OSErr glue_CloseComponent(ComponentInstance ci)
{
    return pascal_trap(0xA82A, 0x08, 2, { { 4, US_TO_SYN68K(ci) } });
}

}

/* Called before the first builtin library connection is made (the file is
   otherwise unreferenced, so a static initializer would not be linked). */
void Executor::builtinlibs::addGlueEntrypoints()
{
    {
        exportGlue("c2pstr", &glue_c2pstr);
        exportGlue("p2cstr", &glue_p2cstr);
        exportGlue("c2pstrcpy", &glue_c2pstrcpy);
        exportGlue("p2cstrcpy", &glue_p2cstrcpy);
        exportGlue("CopyPascalStringToC", &glue_CopyPascalStringToC);
        exportGlue("CopyCStringToPascal", &glue_CopyCStringToPascal);
        exportGlue("EqualString", &glue_EqualString);
        exportGlue("RelString", &glue_RelString);
        exportGlue("IUEqualString", &glue_IUEqualString);
        exportGlue("IUCompString", &glue_IUCompString);
        exportGlue("IUEqualPString", &glue_IUEqualPString);
        exportGlue("IUCompPString", &glue_IUCompPString);
        exportGlue("SetCurrentA5", &glue_SetCurrentA5);
        exportGlue("SetA5", &glue_SetA5);
        exportGlue("ReadLocation", &glue_ReadLocation);
        exportGlue("WriteLocation", &glue_WriteLocation);
        exportGlue("FlushInstructionCache", &glue_FlushInstructionCache);
        exportGlue("FlushDataCache", &glue_FlushDataCache);
        exportGlue("GetMyZone", &glue_GetMyZone);
        exportGlue("InlineGetHandleSize", &glue_InlineGetHandleSize);
        exportGlue("GetSysFont", &glue_GetSysFont);
        exportGlue("GetAppFont", &glue_GetAppFont);
        exportGlue("LMGetSysFontSize", &glue_LMGetSysFontSize);
        exportGlue("LMSetSysFontSize", &glue_LMSetSysFontSize);
        exportGlue("GetDblTime", &glue_GetDblTime);
        exportGlue("GetCaretTime", &glue_GetCaretTime);
        exportGlue("GetSysDirection", &glue_GetSysDirection);
        exportGlue("DialogCut", &glue_DialogCut);
        exportGlue("DialogCopy", &glue_DialogCopy);
        exportGlue("DialogPaste", &glue_DialogPaste);
        exportGlue("DialogDelete", &glue_DialogDelete);
        exportGlue("CountDITL", &glue_CountDITL);
        exportGlue("AppendDITL", &glue_AppendDITL);
        exportGlue("ShortenDITL", &glue_ShortenDITL);
        exportGlue("GetIndPattern", &glue_GetIndPattern);
        exportGlue("GetVInfo", &glue_GetVInfo);
        exportGlue("DirCreate", &glue_DirCreate);
        exportGlue("PBHOpenRFDenySync", &glue_PBHOpenRFDenySync);
        exportGlue("PBHOpenRFDenyAsync", &glue_PBHOpenRFDenyAsync);
        exportGlue("YieldToAnyThread", &glue_YieldToAnyThread);
        exportGlue("OpenDefaultComponent", &glue_OpenDefaultComponent);
        exportGlue("OpenComponent", &glue_OpenComponent);
        exportGlue("CloseComponent", &glue_CloseComponent);
        exportGlue("AEObjectInit", &glue_AEObjectInit);
        add_call_component_upp();
        for(const ComponentRoutine& r : osa_routines)
            builtinlibs::addCFM68KEntrypoint("AppleScriptLib", r.name,
                [&r](syn68k_addr_t) { return call_component_routine(r); });
        exportGlue("CreateOffsetDescriptor", &glue_CreateOffsetDescriptor);
        exportGlue("CreateCompDescriptor", &glue_CreateCompDescriptor);
        exportGlue("CreateLogicalDescriptor", &glue_CreateLogicalDescriptor);
        exportGlue("CreateObjSpecifier", &glue_CreateObjSpecifier);
        exportGlue("CreateRangeDescriptor", &glue_CreateRangeDescriptor);
    }
}
