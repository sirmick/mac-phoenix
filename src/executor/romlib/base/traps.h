#pragma once

#include <base/functions.h>
#include <syn68k_public.h>

#include <stdint.h>
#include <unordered_map>
#include <functional>
#include <string>

class PowerCore;

namespace Executor
{
namespace ot { class Call; }

namespace traps
{
#define TOOLBIT (0x0800)

namespace selectors
{
    template <uint32_t mask> struct D0;
    template <uint32_t mask> struct D1;
    using D0W = D0<0xFFFF>;
    using D0L = D0<0xFFFFFFFF>;
    template <uint32_t mask = 0xFFFF> struct StackWMasked;
    template <uint32_t mask = 0xFFFFFFFF> struct StackLMasked;
    using StackW = StackWMasked<>;
    using StackL = StackLMasked<>;
    template <uint32_t mask = 0xFFFF> struct StackWLookahead;
    using TrapBits = D1<0x600>;
}

namespace internal
{
    class DeferredInit
    {
    public:
        DeferredInit();
        virtual void init() = 0;
        static void initAll();
    private:
        DeferredInit *next;
        static DeferredInit *first, *last;
    };
}

/* MacPhoenix: a selector no Executor routine answers. Logged once per
   trap and selector, tagged "[unimplemented]" (log_unimplemented) so the
   gaps a session ran into can be found and considered for implementing.
   Where the selector says how many argument bytes the caller pushed, the
   call is answered as the Mac's own dispatcher answers a selector it has
   no routine for (unknown_selector_68k returns where to continue);
   elsewhere 0, and the caller stops the session. */
uint32_t unknown_selector_68k(const char *trap_name, uint32_t sel);
void log_unimplemented(const std::string &what);
extern std::string unimplemented_log_path; /* also appended to; empty: not */

class Entrypoint : public internal::DeferredInit
{
public:
    Entrypoint(const char* name, const char* exportToLib = nullptr)
        : name(name), libname(exportToLib) {}
    virtual void init() override;

    const char *name;
    const char *libname;
    bool breakpoint = false;

    uint32_t checkBreak68K(uint32_t addr)
    {
        if(breakpoint)
            return break68K(addr);
        else
            return ~(uint32_t)0;
    }

    uint32_t checkBreakPPC(PowerCore& cpu)
    {
        if(breakpoint)
            return breakPPC(cpu);
        else
            return ~(uint32_t)0;
    }
private:
    uint32_t break68K(uint32_t addr);
    uint32_t breakPPC(PowerCore& cpu);
};

class GenericDispatcherTrap : public Entrypoint
{
public:
    virtual void addSelector(uint32_t sel, Entrypoint* entrypoint, std::function<syn68k_addr_t(syn68k_addr_t)> handler) = 0;
    GenericDispatcherTrap(const char* name, uint16_t trapno) : Entrypoint(name), trapno(trapno) {}

    /* MacPhoenix: something other than Executor's dispatcher is in the
       trap's table entry (Apple's PACK 8, installed by AppleScript, in
       Pack8's): a library call goes there, as InterfaceLib's do on a Mac,
       with the selector where the dispatcher takes it (putSelector: false
       if that is the stack). */
    bool isPatched() const
    {
        return trapno && tooltraptable && originalFunction && tableEntry() != originalFunction;
    }
    syn68k_addr_t tableEntry() const
    {
        return (trapno & TOOLBIT) ? tooltraptable[trapno & 0x3FF] : ostraptable[trapno & 0xFF];
    }
    virtual bool putSelector(uint32_t sel) const = 0;
protected:
    syn68k_addr_t originalFunction = 0;
    struct SelectorEntry
    {
        Entrypoint* entrypoint;
        std::function<syn68k_addr_t(syn68k_addr_t)> invoke;
    };
    std::unordered_map<uint32_t, SelectorEntry> selectors;
    uint16_t trapno;
};

template<class SelectorConvention>
class DispatcherTrap : public GenericDispatcherTrap
{
    static syn68k_addr_t invokeFrom68K(syn68k_addr_t addr, void* extra);
public:
    virtual void init() override;
    virtual void addSelector(uint32_t sel, Entrypoint* entrypoint, std::function<syn68k_addr_t(syn68k_addr_t)> handler) override;
    virtual bool putSelector(uint32_t sel) const override;

    using GenericDispatcherTrap::GenericDispatcherTrap;
};


template<typename F, F* fptr, typename CallConv = callconv::Pascal>
class WrappedFunction {};

template<typename Ret, typename... Args, Ret (*fptr)(Args...), typename CallConv>
class WrappedFunction<Ret (Args...), fptr, CallConv> : public Entrypoint
{
public:
    Ret operator()(Args... args) const
    {
        return (*fptr)(args...);
    }

    UPP<Ret (Args...), CallConv> operator&() const
    {
        return guestFP;
    }

    virtual void init() override;

    using Entrypoint::Entrypoint;

    using UPPType = UPP<Ret (Args...), CallConv>;
protected:
    UPPType guestFP;

    /* MacPhoenix: how a library call (CFM-68K) reaches the routine; a trap
       goes through its table entry once an extension has patched it. */
    virtual Ret callFromLibrary(Args... args) const { return (*fptr)(args...); }
};

template<typename F, F* fptr, int trapno, typename CallConv = callconv::Pascal>
class TrapFunction {};

template<typename Ret, typename... Args, Ret (*fptr)(Args...), int trapno, typename CallConv>
class TrapFunction<Ret (Args...), fptr, trapno, CallConv> : public WrappedFunction<Ret (Args...), fptr, CallConv>
{
public:
    using UPPType = typename WrappedFunction<Ret (Args...), fptr, CallConv>::UPPType;

    Ret operator()(Args... args) const
    {
        if(isPatched())
            return invokeViaTrapTable(args...);
        else
            return fptr(args...);
    }
    
    virtual void init() override;

    TrapFunction(const char* name, const char* exportToLib = nullptr) : WrappedFunction<Ret(Args...),fptr,CallConv>(name, exportToLib) {}

protected:
    Ret callFromLibrary(Args... args) const override { return (*this)(args...); }
public:

    /* MacPhoenix: the tables live in guest memory and exist from
       traps::init on; InitMemory calls traps (NewPtr) before that. */
    bool isPatched() const
    {
        return tooltraptable && tableEntry() != originalFunction;
    }
    Ret invokeViaTrapTable(Args...) const;
private:
    syn68k_addr_t originalFunction;

    TrapTableEntry& tableEntry() const
    {
        if(trapno & TOOLBIT)
            return tooltraptable[trapno & 0x3FF];
        else
            return ostraptable[trapno & 0xFF];
    }
};

template<typename F, F* fptr, int trapno, uint32_t selector, typename CallConv = callconv::Pascal>
class SubTrapFunction {};

template<typename Ret, typename... Args, Ret (*fptr)(Args...), int trapno, uint32_t selector, typename CallConv>
class SubTrapFunction<Ret (Args...), fptr, trapno, selector, CallConv> : public WrappedFunction<Ret (Args...), fptr, CallConv>
{
public:
    Ret operator()(Args... args) const { return fptr(args...); }
    SubTrapFunction(const char* name, GenericDispatcherTrap& dispatcher, const char* exportToLib = nullptr);
    virtual void init() override;
protected:
    Ret callFromLibrary(Args... args) const override
    {
        if(dispatcher.isPatched() && dispatcher.putSelector(selector))
            return (UPP<Ret (Args...), CallConv>(SYN68K_TO_US(dispatcher.tableEntry())))(args...);
        return fptr(args...);
    }
private:
    GenericDispatcherTrap& dispatcher;
};

template<typename Trap, typename F, bool... flags>
class TrapVariant;

template<typename Trap, typename Ret, typename... Args, bool... flags>
class TrapVariant<Trap, Ret (Args...), flags...>  : public Entrypoint
{
    template<class T1>
    struct cast_any_t
    {
        T1 x;

        template<class T2>
        operator T2() { return (T2)x; }
    };

    template<class T1>
    cast_any_t<T1> cast_any(const T1& x) const { return {x}; }
public:
    Ret operator()(Args... args) const { return trap(cast_any(args)..., flags...); }

    virtual void init() override;
    TrapVariant(const Trap& trap, const char* name, const char* exportToLib = nullptr);
private:
    const Trap& trap;
};

/* MacPhoenix: a routine a shared library exports (docs/executor/
   LIBRARIES.md, `library:` in the definitions): PowerPC and CFM-68K code
   import it by name; no trap and no 68k UPP. */
template<typename F, F* fptr>
class LibraryFunction {};

template<typename Ret, typename... Args, Ret (*fptr)(Args...)>
class LibraryFunction<Ret (Args...), fptr> : public Entrypoint
{
public:
    using Entrypoint::Entrypoint;
    virtual void init() override;
};

/* ... and one that reads its own arguments from the PowerPC registers
   (variadic, or decoded by hand: `raw: true`). Its library registers it
   itself; the definitions only say it is there, and builtinlibs checks
   that the two agree. */
class LibraryRawExport : public internal::DeferredInit
{
public:
    LibraryRawExport(const char *name, const char *library)
        : name(name), library(library) {}
    virtual void init() override;
private:
    const char *name;
    const char *library;
};

/* ... and a routine another library exports as well (`library:` on a
   trap's or subtrap's entry: SoundLib's Sound Manager calls; the second
   and later of a `library:` list): its entry in `from` (InterfaceLib for a
   trap) under that library's name, for PowerPC and CFM-68K. */
class LibraryAlsoExport : public internal::DeferredInit
{
public:
    LibraryAlsoExport(const char *name, const char *library, const char *from)
        : name(name), library(library), from(from) {}
    virtual void init() override;
private:
    const char *name;
    const char *library;
    const char *from;
};

/* ... and a library routine that is a trap's selector the guest's own code
   answers (`library:` on a subtrap's entry without `executor:`: the Drag
   Manager's, Apple's 68k code in the System): PowerPC and CFM-68K code
   reach it through the dispatcher's table entry, the selector where the
   dispatcher takes it, as InterfaceLib's glue does. While Executor's own
   dispatcher is still there (nothing installed the manager), the call is
   logged and answers 0. */
template<typename F>
class TrapGlue {};

template<typename Ret, typename... Args>
class TrapGlue<Ret (Args...)> : public internal::DeferredInit
{
public:
    TrapGlue(const char *name, const char *library, GenericDispatcherTrap *dispatcher, uint32_t selector)
        : name(name), library(library), dispatcher(dispatcher), selector(selector) {}
    virtual void init() override;
    Ret call(Args... args) const;

    const char *name, *library;
    GenericDispatcherTrap *dispatcher;
    uint32_t selector;
};

/* ... and a routine of an ASLM function set that PowerPC code binds
   through ASLM itself (`aslm:` without `library:`, LibraryManager.yaml):
   registered with Executor's PowerPC binder (opentransport/ot_slm_ppc.cpp)
   by its set and index, typed like a library export. */
template<typename F, F* fptr>
class ASLMFunction {};

template<typename Ret, typename... Args, Ret (*fptr)(Args...)>
class ASLMFunction<Ret (Args...), fptr> : public internal::DeferredInit
{
public:
    ASLMFunction(const char *name, const char *set, unsigned index)
        : name(name), set(set), index(index) {}
    virtual void init() override;

    const char *name, *set;
    unsigned index;
};

/* ... and one that handles the PowerPC registers itself (`raw: true`):
   its code registers it; the definitions say it is there. */
class ASLMRawExport : public internal::DeferredInit
{
public:
    ASLMRawExport(const char *name, const char *set, unsigned index)
        : name(name), set(set), index(index) {}
    virtual void init() override;
private:
    const char *name, *set;
    unsigned index;
};

/* ... and one of Open Transport's (`aslm:` in the definitions): 68k code
   binds it through the Shared Library Manager by its function set and that
   set's name for it, PowerPC code imports it from `library`. The routine
   reads its arguments through ot::Call (opentransport/ot_internal.h); sig
   has a letter per argument ('L' long, 'W' word, 'B' Boolean), ret the
   result's ('V' none). ot.cpp registers it both ways. */
class OTFunction : public internal::DeferredInit
{
public:
    OTFunction(const char *name, const char *library, const char *set, const char *setName,
               bool pascal, const char *sig, char ret, uint32_t (*impl)(const ot::Call &))
        : name(name), library(library), set(set), setName(setName), pascal(pascal),
          sig(sig), ret(ret), impl(impl) {}
    virtual void init() override;

    const char *name, *library, *set, *setName;
    bool pascal;
    const char *sig;
    char ret;
    uint32_t (*impl)(const ot::Call &);
};

/* ... and a call into a component a library exports (AppleScriptLib's OSA
   routines: `selector-location: Component` dispatchers in the
   definitions): the CFM-68K and PowerPC entries push the call's
   parameters (sizes: '2' or '4' per parameter after the instance) and make
   it through _ComponentDispatch (cfm68k_glue.cpp). */
class ComponentCallExport : public internal::DeferredInit
{
public:
    ComponentCallExport(const char *name, const char *library, uint16_t what, const char *sizes)
        : name(name), library(library), what(what), sizes(sizes) {}
    virtual void init() override;

    const char *name, *library;
    uint16_t what;
    const char *sizes;
};

/* ... and a call one of Executor's own components answers (a
   `selector-location: Component` dispatcher's function with `executor:`):
   registered with the component type's dispatch (component.cpp) by
   selector, its arguments read from the call's ComponentParameters. */
template<typename F, F* fptr>
class ComponentFunction {};

template<typename Ret, typename... Args, Ret (*fptr)(Args...)>
class ComponentFunction<Ret (Args...), fptr> : public internal::DeferredInit
{
public:
    ComponentFunction(const char *name, const char *component, int16_t what)
        : name(name), component(component), what(what) {}
    virtual void init() override;

    const char *name, *component;
    int16_t what;
};

#define EXTERN_FUNCTION_WRAPPER(NAME, FPTR, INIT, ...) \
    extern Executor::traps::__VA_ARGS__ NAME
#define EXTERN_DISPATCHER_TRAP(NAME, TRAP, SELECTOR) \
    extern Executor::traps::DispatcherTrap<Executor::traps::selectors::SELECTOR> NAME
#define EXTERN_LIBRARY_FUNCTION(NAME, INIT, ...) \
    extern Executor::traps::__VA_ARGS__ NAME
#define EXTERN_COMPONENT_CALL(NAME, INIT) \
    extern Executor::traps::ComponentCallExport NAME
#define EXTERN_COMPONENT_FUNCTION(NAME, INIT, ...) \
    extern Executor::traps::__VA_ARGS__ NAME
#define EXTERN_OT_FUNCTION(NAME, INIT) \
    extern Executor::traps::OTFunction NAME
#define EXTERN_LIBRARY_RAW(NAME, INIT) \
    extern Executor::traps::LibraryRawExport NAME
#define EXTERN_TRAP_GLUE(NAME, INIT, ...) \
    extern Executor::traps::__VA_ARGS__ NAME
#define EXTERN_LIBRARY_ALSO(NAME, INIT) \
    extern Executor::traps::LibraryAlsoExport NAME
#define EXTERN_ASLM_FUNCTION(NAME, INIT, ...) \
    extern Executor::traps::__VA_ARGS__ NAME
#define EXTERN_ASLM_RAW(NAME, INIT) \
    extern Executor::traps::ASLMRawExport NAME
#define DEFINE_FUNCTION_WRAPPER(NAME, FPTR, INIT, ...) \
    Executor::traps::__VA_ARGS__ NAME INIT;   \
    template class Executor::traps::__VA_ARGS__;
#define DEFINE_DISPATCHER_TRAP(NAME, TRAP, SELECTOR) \
    Executor::traps::DispatcherTrap<Executor::traps::selectors::SELECTOR> NAME { #NAME, TRAP }
/* No explicit instantiation: two names (memcpy, _memcpy) can share one. */
#define DEFINE_LIBRARY_FUNCTION(NAME, INIT, ...) \
    Executor::traps::__VA_ARGS__ NAME INIT
#define DEFINE_COMPONENT_CALL(NAME, INIT) \
    Executor::traps::ComponentCallExport NAME INIT
#define DEFINE_COMPONENT_FUNCTION(NAME, INIT, ...) \
    Executor::traps::__VA_ARGS__ NAME INIT
#define DEFINE_OT_FUNCTION(NAME, INIT) \
    Executor::traps::OTFunction NAME INIT
#define DEFINE_LIBRARY_RAW(NAME, INIT) \
    Executor::traps::LibraryRawExport NAME INIT
#define DEFINE_TRAP_GLUE(NAME, INIT, ...) \
    Executor::traps::__VA_ARGS__ NAME INIT
#define DEFINE_LIBRARY_ALSO(NAME, INIT) \
    Executor::traps::LibraryAlsoExport NAME INIT
#define DEFINE_ASLM_FUNCTION(NAME, INIT, ...) \
    Executor::traps::__VA_ARGS__ NAME INIT
#define DEFINE_ASLM_RAW(NAME, INIT) \
    Executor::traps::ASLMRawExport NAME INIT

#ifndef TRAP_INSTANTIATION
#define TRAP_INSTANTIATION EXTERN
#endif

#define PREPROCESSOR_CONCAT1(A,B) A##B
#define PREPROCESSOR_CONCAT(A,B) PREPROCESSOR_CONCAT1(A,B)
#define CREATE_FUNCTION_WRAPPER PREPROCESSOR_CONCAT(TRAP_INSTANTIATION, _FUNCTION_WRAPPER)
#define DISPATCHER_TRAP PREPROCESSOR_CONCAT(TRAP_INSTANTIATION, _DISPATCHER_TRAP)
#define CREATE_LIBRARY_FUNCTION PREPROCESSOR_CONCAT(TRAP_INSTANTIATION, _LIBRARY_FUNCTION)
#define CREATE_LIBRARY_RAW PREPROCESSOR_CONCAT(TRAP_INSTANTIATION, _LIBRARY_RAW)
#define CREATE_OT_FUNCTION PREPROCESSOR_CONCAT(TRAP_INSTANTIATION, _OT_FUNCTION)
#define CREATE_LIBRARY_ALSO PREPROCESSOR_CONCAT(TRAP_INSTANTIATION, _LIBRARY_ALSO)
#define CREATE_TRAP_GLUE PREPROCESSOR_CONCAT(TRAP_INSTANTIATION, _TRAP_GLUE)
#define CREATE_ASLM_FUNCTION PREPROCESSOR_CONCAT(TRAP_INSTANTIATION, _ASLM_FUNCTION)
#define CREATE_ASLM_RAW PREPROCESSOR_CONCAT(TRAP_INSTANTIATION, _ASLM_RAW)
#define CREATE_COMPONENT_CALL PREPROCESSOR_CONCAT(TRAP_INSTANTIATION, _COMPONENT_CALL)
#define CREATE_COMPONENT_FUNCTION PREPROCESSOR_CONCAT(TRAP_INSTANTIATION, _COMPONENT_FUNCTION)

#define COMMA ,
#define PASCAL_TRAP(NAME, TRAP) \
    CREATE_FUNCTION_WRAPPER(NAME, &C_##NAME, (#NAME, "InterfaceLib"), TrapFunction<decltype(C_##NAME) COMMA &C_##NAME COMMA TRAP>)
#define REGISTER_TRAP(NAME, TRAP, ...) \
    CREATE_FUNCTION_WRAPPER(NAME, &C_##NAME, (#NAME, "InterfaceLib"), TrapFunction<decltype(C_##NAME) COMMA &C_##NAME COMMA TRAP COMMA callconv::Register<__VA_ARGS__>>)
#define REGISTER_TRAP2(NAME, TRAP, ...) \
    CREATE_FUNCTION_WRAPPER(stub_##NAME, &NAME, (#NAME, "InterfaceLib"), TrapFunction<decltype(NAME) COMMA &NAME COMMA TRAP COMMA callconv::Register<__VA_ARGS__>>)

#define PASCAL_SUBTRAP(NAME, TRAP, SELECTOR, TRAPNAME) \
    CREATE_FUNCTION_WRAPPER(NAME, &C_##NAME, (#NAME, TRAPNAME, "InterfaceLib"), SubTrapFunction<decltype(C_##NAME) COMMA &C_##NAME COMMA TRAP COMMA SELECTOR>)
#define REGISTER_SUBTRAP(NAME, TRAP, SELECTOR, TRAPNAME, ...) \
    CREATE_FUNCTION_WRAPPER(NAME, &C_##NAME, (#NAME, TRAPNAME, "InterfaceLib"), SubTrapFunction<decltype(C_##NAME) COMMA &C_##NAME COMMA TRAP COMMA SELECTOR COMMA callconv::Register<__VA_ARGS__>>)
#define REGISTER_SUBTRAP2(NAME, TRAP, SELECTOR, TRAPNAME, ...) \
    CREATE_FUNCTION_WRAPPER(stub_##NAME, &NAME, (#NAME, TRAPNAME, "InterfaceLib"), SubTrapFunction<decltype(NAME) COMMA &NAME COMMA TRAP COMMA SELECTOR COMMA callconv::Register<__VA_ARGS__>>)

#define LIBRARY_FUNCTION(LIB, NAME, IMPL) \
    CREATE_LIBRARY_FUNCTION(lib_##LIB##_##NAME, (#NAME, #LIB), LibraryFunction<decltype(IMPL) COMMA &IMPL>)
/* A library whose name isn't an identifier ("Memory Manager"): TOKEN names
   the registration, LIBNAME is the library's. */
#define LIBRARY_FUNCTION_NAMED(TOKEN, LIBNAME, NAME, IMPL) \
    CREATE_LIBRARY_FUNCTION(lib_##TOKEN##_##NAME, (#NAME, LIBNAME), LibraryFunction<decltype(IMPL) COMMA &IMPL>)
#define COMPONENT_CALL(LIB, NAME, WHAT, SIZES) \
    CREATE_COMPONENT_CALL(lib_##LIB##_##NAME, (#NAME, #LIB, WHAT, SIZES))
#define COMPONENT_FUNCTION(COMPONENT, NAME, WHAT, IMPL) \
    CREATE_COMPONENT_FUNCTION(comp_##NAME, (#NAME, #COMPONENT, (int16_t)WHAT), ComponentFunction<decltype(IMPL) COMMA &IMPL>)
#define OT_FUNCTION(LIB, NAME, SET, SETNAME, PASCAL, SIG, RET, IMPL) \
    CREATE_OT_FUNCTION(lib_##LIB##_##NAME, (#NAME, #LIB, SET, SETNAME, PASCAL, SIG, RET, &ot::IMPL))
#define LIBRARY_RAW(LIB, NAME) \
    CREATE_LIBRARY_RAW(lib_##LIB##_##NAME, (#NAME, #LIB))
#define TRAP_GLUE(LIB, NAME, DISPATCHER, SELECTOR) \
    CREATE_TRAP_GLUE(glue_##LIB##_##NAME, (#NAME, #LIB, &DISPATCHER, SELECTOR), TrapGlue<decltype(NAME)>)
#define LIBRARY_ALSO(LIB, NAME, FROM) \
    CREATE_LIBRARY_ALSO(also_##LIB##_##NAME, (#NAME, #LIB, #FROM))
#define ASLM_FUNCTION(SET, INDEX, NAME, IMPL) \
    CREATE_ASLM_FUNCTION(aslm_##NAME, (#NAME, SET, INDEX), ASLMFunction<decltype(IMPL) COMMA &IMPL>)
#define ASLM_RAW(SET, INDEX, NAME) \
    CREATE_ASLM_RAW(aslm_##NAME, (#NAME, SET, INDEX))

#define NOTRAP_FUNCTION(NAME) \
    CREATE_FUNCTION_WRAPPER(NAME, &C_##NAME, (#NAME, "InterfaceLib"), WrappedFunction<decltype(C_##NAME) COMMA &C_##NAME>)
#define NOTRAP_FUNCTION2(NAME) \
    CREATE_FUNCTION_WRAPPER(stub_##NAME, &NAME, (#NAME, "InterfaceLib"), WrappedFunction<decltype(NAME) COMMA &NAME>)

#define PASCAL_FUNCTION_PTR(NAME) \
    DEFINE_FUNCTION_WRAPPER(NAME, &C_##NAME, (#NAME), WrappedFunction<decltype(C_##NAME) COMMA &C_##NAME>)
#define REGISTER_FUNCTION_PTR(NAME, ...) \
    DEFINE_FUNCTION_WRAPPER(NAME, &C_##NAME, (#NAME), WrappedFunction<decltype(C_##NAME) COMMA &C_##NAME COMMA callconv::Register<__VA_ARGS__>>)
#define CCALL_FUNCTION_PTR(NAME, ...) \
    DEFINE_FUNCTION_WRAPPER(NAME, &C_##NAME, (#NAME), WrappedFunction<decltype(C_##NAME) COMMA &C_##NAME COMMA callconv::CCall>)
#define EXTERN_PASCAL_FUNCTION_PTR(NAME) \
    EXTERN_FUNCTION_WRAPPER(NAME, &C_##NAME, (#NAME), WrappedFunction<decltype(C_##NAME) COMMA &C_##NAME>)
#define EXTERN_REGISTER_FUNCTION_PTR(NAME, ...) \
    EXTERN_FUNCTION_WRAPPER(NAME, &C_##NAME, (#NAME), WrappedFunction<decltype(C_##NAME) COMMA &C_##NAME COMMA callconv::Register<__VA_ARGS__>>)
#define EXTERN_CCALL_FUNCTION_PTR(NAME) \
    EXTERN_FUNCTION_WRAPPER(NAME, &C_##NAME, (#NAME), WrappedFunction<decltype(C_##NAME) COMMA &C_##NAME COMMA callconv::CCall>)

#define RAW_68K_FUNCTION(NAME) \
    syn68k_addr_t RAW_##NAME(syn68k_addr_t, void *); \
    CREATE_FUNCTION_WRAPPER(stub_##NAME, &RAW_##NAME, (#NAME), WrappedFunction<decltype(RAW_##NAME) COMMA &RAW_##NAME COMMA callconv::Raw>)
#define RAW_68K_TRAP(NAME, TRAP) \
    syn68k_addr_t RAW_##NAME(syn68k_addr_t, void *); \
    CREATE_FUNCTION_WRAPPER(stub_##NAME, &RAW_##NAME, (#NAME), TrapFunction<decltype(RAW_##NAME) COMMA &RAW_##NAME COMMA TRAP COMMA callconv::Raw>)

#define RAW_68K_IMPLEMENTATION(NAME) \
        syn68k_addr_t Executor::RAW_##NAME(syn68k_addr_t trap_address [[maybe_unused]], void *)

#define TRAP_VARIANT(NAME, IMPL_NAME, ...) \
    CREATE_FUNCTION_WRAPPER(NAME, , (stub_##IMPL_NAME, #NAME, "InterfaceLib"), TrapVariant<decltype(stub_##IMPL_NAME), __VA_ARGS__>)
#define REGISTER_FLAG_TRAP(IMPL_NAME, NAME0, NAME1, TRAP, TYPE, ...) \
    REGISTER_TRAP2(IMPL_NAME, TRAP, __VA_ARGS__); \
    TRAP_VARIANT(NAME0, IMPL_NAME, TYPE, false); \
    TRAP_VARIANT(NAME1, IMPL_NAME, TYPE, true)
#define REGISTER_2FLAG_TRAP(IMPL_NAME, NAME00, NAME01, NAME10, NAME11, TRAP, TYPE, ...) \
    REGISTER_TRAP2(IMPL_NAME, TRAP, __VA_ARGS__); \
    TRAP_VARIANT(NAME00, IMPL_NAME, TYPE, false, false); \
    TRAP_VARIANT(NAME10, IMPL_NAME, TYPE, true, false); \
    TRAP_VARIANT(NAME01, IMPL_NAME, TYPE, false, true); \
    TRAP_VARIANT(NAME11, IMPL_NAME, TYPE, true, true)

#define ASYNCBIT (1 << 10)
#define HFSBIT (1 << 9)

#define FILE_TRAP(NAME, PBTYPE, TRAP) \
    REGISTER_FLAG_TRAP(NAME, NAME##Sync, NAME##Async, TRAP, OSErr(PBTYPE), D0 (A0, TrapBit<ASYNCBIT>))

#define FILE_SUBTRAP(NAME, PBTYPE, TRAP, SELECTOR, TRAPNAME) \
    REGISTER_SUBTRAP2(NAME, TRAP, SELECTOR, TRAPNAME, D0 (A0, TrapBit<ASYNCBIT>)); \
    TRAP_VARIANT(NAME##Sync, NAME, OSErr(PBTYPE), false); \
    TRAP_VARIANT(NAME##Async, NAME, OSErr(PBTYPE), true)


#define HFS_TRAP(NAME, HNAME, PBTYPE, TRAP) \
    inline OSErr NAME##_##HNAME(ParmBlkPtr pb, Boolean async, Boolean hfs) \
    { \
        return hfs ? HNAME((PBTYPE)pb, async) : NAME(pb, async); \
    } \
    CREATE_FUNCTION_WRAPPER(stub_##NAME, &NAME##_##HNAME, \
        (#NAME "/" #HNAME), \
        TrapFunction<decltype(NAME##_##HNAME), \
            &NAME##_##HNAME, TRAP, \
            callconv::Register<D0 (A0, TrapBit<ASYNCBIT>, TrapBit<HFSBIT>)>>); \
    TRAP_VARIANT(NAME##Sync, NAME, OSErr(ParmBlkPtr), false, false); \
    TRAP_VARIANT(NAME##Async, NAME, OSErr(ParmBlkPtr), true, false); \
    TRAP_VARIANT(HNAME##Sync, NAME, OSErr(HParmBlkPtr), false, true); \
    TRAP_VARIANT(HNAME##Async, NAME, OSErr(HParmBlkPtr), true, true)

#define HFS_SUBTRAP(NAME, HNAME, PBTYPE, TRAP, SELECTOR, TRAPNAME) \
    inline OSErr NAME##_##HNAME(ParmBlkPtr pb, Boolean async, Boolean hfs) \
    { \
        return hfs ? HNAME((PBTYPE)pb, async) : NAME(pb, async); \
    } \
    CREATE_FUNCTION_WRAPPER(stub_##NAME, &NAME##_##HNAME, \
        (#NAME "/" #HNAME, TRAPNAME), \
        SubTrapFunction<decltype(NAME##_##HNAME), \
            &NAME##_##HNAME, TRAP, SELECTOR, \
            callconv::Register<D0 (A0, TrapBit<ASYNCBIT>, TrapBit<HFSBIT>)>>); \
    TRAP_VARIANT(NAME##Sync, NAME, OSErr(ParmBlkPtr), false, false); \
    TRAP_VARIANT(NAME##Async, NAME, OSErr(ParmBlkPtr), true, false); \
    TRAP_VARIANT(HNAME##Sync, NAME, OSErr(HParmBlkPtr), false, true); \
    TRAP_VARIANT(HNAME##Async, NAME, OSErr(HParmBlkPtr), true, true)



#define LOWMEM_ACCESSOR(NAME) \
    inline decltype(NAME)::type LMGet##NAME() { return LM(NAME); } \
    inline void LMSet##NAME(decltype(NAME)::type val) { LM(NAME) = val; } \
    NOTRAP_FUNCTION2(LMGet##NAME); \
    NOTRAP_FUNCTION2(LMSet##NAME)

void init(bool enableLogging, const std::string& trapFilter = std::string());
extern std::unordered_map<std::string, traps::Entrypoint*> entrypoints;

}
}
