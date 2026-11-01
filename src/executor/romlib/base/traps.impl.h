#pragma once

#include <base/traps.h>
#include <base/functions.impl.h>
#include <base/logging.h>
#include <base/debugger.h>
#include <error/error.h>

#include <cassert>
#include <type_traits>
#include <iostream>
#include <functional>

namespace Executor
{
namespace builtinlibs
{
    void addPPCEntrypoint(const char *library, const char *function, std::function<uint32_t (PowerCore&)> code);
    void addCFM68KEntrypoint(const char *library, const char *function,
                             std::function<syn68k_addr_t (syn68k_addr_t)> code);
    bool cfm68kCalleePops(const char *function);
    void addASLMEntrypoint(const char *set, unsigned index, const char *function,
                           std::function<uint32_t (PowerCore&)> code);
}

namespace traps
{

namespace selectors
{
/* MacPhoenix: inRegister and put: how a library call made through a
   patched dispatcher's table entry passes the selector; stackBytes: a
   whole selector word or long pushed after the arguments, as the inline
   glue does (move.w #sel,-(sp); _Pack3), 0 for a register one or one
   with other bits in its word (those calls go to Executor's routine). */
template <uint32_t mask>
struct D0
{
    static const uint32_t selectorMask = mask;
    static uint32_t get() { return EM_D0 & mask; }
    static void commit() {}
    static constexpr bool inRegister = true;
    static constexpr int stackBytes = 0;
    static void put(uint32_t sel) { EM_D0 = (EM_D0 & ~mask) | (sel & mask); }
};
template <uint32_t mask>
struct D1
{
    static const uint32_t selectorMask = mask;
    static uint32_t get() { return EM_D1 & mask; }
    static void commit() {}
    static constexpr bool inRegister = true;
    static constexpr int stackBytes = 0;
    static void put(uint32_t sel) { EM_D1 = (EM_D1 & ~mask) | (sel & mask); }
};


template <uint32_t mask>
struct StackWMasked
{
    static const uint32_t selectorMask = mask;
    static constexpr bool inRegister = false;
    static constexpr int stackBytes = mask == 0xFFFF ? 2 : 0;
    static void put(uint32_t) {}
    static uint32_t get() { return READUW(EM_A7 + 4) & mask; }
    static void commit()
    {
        auto ret = POPADDR();
        EM_A7 += 2;
        PUSHADDR(ret);
    }
};

template <uint32_t mask>
struct StackLMasked
{
    static const uint32_t selectorMask = mask;
    static constexpr bool inRegister = false;
    static constexpr int stackBytes = mask == 0xFFFFFFFF ? 4 : 0;
    static void put(uint32_t) {}
    static uint32_t get() { return READUL(EM_A7 + 4) & mask; }
    static void commit()
    {
        auto ret = POPADDR();
        EM_A7 += 4;
        PUSHADDR(ret);
    }
};
template <uint32_t mask>
struct StackWLookahead
{
    static const uint32_t selectorMask = mask;
    static constexpr bool inRegister = false;
    static constexpr int stackBytes = 0;
    static void put(uint32_t) {}
    static uint32_t get() { return READUW(EM_A7 + 4) & mask; }
    static void commit() {}
};

} /* end namespace selectors */

template<typename F>
syn68k_addr_t callback_install (const F& func)
{
    return ::callback_install(
        [](syn68k_addr_t a, void * b) -> syn68k_addr_t
        {
            currentCPUMode = CPUMode::m68k;
            currentM68KPC = *ptr_from_longint<GUEST<uint32>*>(EM_A7);
            const F& f = *(const F*)b;
            return f(a);
        },
        (void*)new F(func)
    );
}

template<typename Ret, typename... Args, Ret (*fptr)(Args...)>
void LibraryFunction<Ret (Args...), fptr>::init()
{
    Entrypoint::init();
#ifdef EXECUTOR_ENABLE_LOGGING
    if(logging::enabled())
        builtinlibs::addPPCEntrypoint(libname, name,
            [this](PowerCore& cpu) {
                if(auto ret = this->checkBreakPPC(cpu); ~ret)
                    return ret;
                return callfromPPC::Invoker<Ret (Args...)>::invokeFromPPC(cpu, logging::makeLoggedFunction(name, fptr));
            }
        );
    else
#endif
        builtinlibs::addPPCEntrypoint(libname, name,
            [this](PowerCore& cpu) {
                if(auto ret = this->checkBreakPPC(cpu); ~ret)
                    return ret;
                return callfromPPC::Invoker<Ret (Args...)>::invokeFromPPC(cpu, fptr);
            }
        );
    builtinlibs::addCFM68KEntrypoint(libname, name,
        [this, pops = builtinlibs::cfm68kCalleePops(name)](syn68k_addr_t addr) {
            if(auto ret = this->checkBreak68K(addr); ~ret)
                return ret;
#ifdef EXECUTOR_ENABLE_LOGGING
            if(logging::enabled())
                return callfromCFM68K::Invoker<Ret (Args...)>::invokeFromCFM68K(
                    logging::makeLoggedFunction1<Ret (Args...)>(name, fptr), pops);
#endif
            return callfromCFM68K::Invoker<Ret (Args...)>::invokeFromCFM68K(fptr, pops);
        }
    );
}

template<typename Ret, typename... Args, Ret (*fptr)(Args...)>
void ASLMFunction<Ret (Args...), fptr>::init()
{
    builtinlibs::addASLMEntrypoint(set, index, name, [this](PowerCore& cpu) {
#ifdef EXECUTOR_ENABLE_LOGGING
        if(logging::enabled())
            return callfromPPC::Invoker<Ret (Args...)>::invokeFromPPC(cpu, logging::makeLoggedFunction(this->name, fptr));
#endif
        return callfromPPC::Invoker<Ret (Args...)>::invokeFromPPC(cpu, fptr);
    });
}

template<typename Ret, typename... Args>
Ret TrapGlue<Ret (Args...)>::call(Args... args) const
{
    if(dispatcher->isPatched() && !dispatcher->putSelector(selector)
       && dispatcher->stackSelectorBytes() && dispatcher->stackSelectorLibraryCalls)
        return callWithStackSelector<Ret, callconv::Pascal, Args...>(*dispatcher, selector, args...);
    if(!dispatcher->isPatched() || !dispatcher->putSelector(selector))
    {
        log_unimplemented(std::string("library glue ") + name + " (" + library
                          + "): nothing answers its trap; answered 0");
        if constexpr(std::is_void_v<Ret>)
            return;
        else
            return Ret();
    }
    return (UPP<Ret (Args...), callconv::Pascal>(SYN68K_TO_US(dispatcher->tableEntry())))(args...);
}

template<typename Ret, typename... Args>
void TrapGlue<Ret (Args...)>::init()
{
    builtinlibs::addPPCEntrypoint(library, name, [this](PowerCore& cpu) {
        auto f = [this](Args... args) -> Ret { return this->call(args...); };
#ifdef EXECUTOR_ENABLE_LOGGING
        if(logging::enabled())
            return callfromPPC::Invoker<Ret (Args...)>::invokeFromPPC(cpu,
                logging::makeLoggedFunction1<Ret (Args...)>(name, f));
#endif
        return callfromPPC::Invoker<Ret (Args...)>::invokeFromPPC(cpu, f);
    });
    builtinlibs::addCFM68KEntrypoint(library, name,
        [this, pops = builtinlibs::cfm68kCalleePops(name)](syn68k_addr_t) {
            auto f = [this](Args... args) -> Ret { return this->call(args...); };
            return callfromCFM68K::Invoker<Ret (Args...)>::invokeFromCFM68K(f, pops);
        });
}

/* component.cpp: the routine answering `what` for Executor's components of
   this type, given the call's ComponentParameters. */
void registerComponentFunction(const char *component, int16_t what, const char *name,
                               std::function<int32_t (syn68k_addr_t params)> code);

/* ComponentParameters: flags, paramSize, what, then the arguments as a
   Pascal caller pushed them, the last first and the instance after the
   rest: read from the end of Args backwards. */
template<typename... Args, std::size_t... Indices>
void readComponentArgs(std::tuple<Args...>& args, syn68k_addr_t ptr, std::index_sequence<Indices...>)
{
    (callfrom68K::readCArg(std::get<sizeof...(Args) - 1 - Indices>(args), ptr), ...);
}

template<typename Ret, typename... Args, Ret (*fptr)(Args...)>
void ComponentFunction<Ret (Args...), fptr>::init()
{
    registerComponentFunction(component, what, name,
        [this](syn68k_addr_t params) -> int32_t {
            std::tuple<Args...> args;
            readComponentArgs(args, params + 4, std::index_sequence_for<Args...>());
#ifdef EXECUTOR_ENABLE_LOGGING
            if(logging::enabled())
                return std::apply(logging::makeLoggedFunction1<Ret (Args...)>(name, fptr), args);
#endif
            return std::apply(fptr, args);
        });
}

template<typename Ret, typename... Args, Ret (*fptr)(Args...), typename CallConv>
void WrappedFunction<Ret (Args...), fptr, CallConv>::init()
{
    Entrypoint::init();
#ifdef EXECUTOR_ENABLE_LOGGING
    if(logging::enabled())
        guestFP = (UPP<Ret (Args...),CallConv>)SYN68K_TO_US(callback_install(
                [this](syn68k_addr_t addr)
                {
                    if(auto ret = this->checkBreak68K(addr); ~ret)
                        return ret;

                    return callfrom68K::Invoker<Ret (Args...), CallConv>
                        ::invokeFrom68K(addr, logging::makeLoggedFunction<CallConv>(name, fptr));
                }
            ));    
    else
#endif    
        guestFP = (UPP<Ret (Args...),CallConv>)SYN68K_TO_US(callback_install(
                [this](syn68k_addr_t addr)
                {
                    if(auto ret = this->checkBreak68K(addr); ~ret)
                        return ret;

                    return callfrom68K::Invoker<Ret (Args...), CallConv>
                        ::invokeFrom68K(addr, fptr);
                }
            ));    

    if(libname)
    {
        /* MacPhoenix: PowerPC code reaches the routine as a library call:
           through the trap's table entry once something has patched it
           (Apple's PACK 8 for the Apple Event Manager's calls), as
           InterfaceLib's glue does on a Mac, and as CFM-68K code does. */
#ifdef EXECUTOR_ENABLE_LOGGING
        if(logging::enabled())
            builtinlibs::addPPCEntrypoint(libname, name,
                [this](PowerCore& cpu) { 
                    if(auto ret = this->checkBreakPPC(cpu); ~ret)
                        return ret;
                    auto call = [this](Args... args) -> Ret { return this->callFromLibrary(args...); };
                    return callfromPPC::Invoker<Ret (Args...)>::invokeFromPPC(cpu,
                        logging::makeLoggedFunction1<Ret (Args...)>(name, call));
                }
            );
        else
#endif
            builtinlibs::addPPCEntrypoint(libname, name,
                [this](PowerCore& cpu) { 
                    if(auto ret = this->checkBreakPPC(cpu); ~ret)
                        return ret;
                    auto call = [this](Args... args) -> Ret { return this->callFromLibrary(args...); };
                    return callfromPPC::Invoker<Ret (Args...)>::invokeFromPPC(cpu, call);
                }
            );
        builtinlibs::addCFM68KEntrypoint(libname, name,
            [this, pops = builtinlibs::cfm68kCalleePops(name)](syn68k_addr_t addr) {
                if(auto ret = this->checkBreak68K(addr); ~ret)
                    return ret;
                auto call = [this](Args... args) -> Ret { return this->callFromLibrary(args...); };
#ifdef EXECUTOR_ENABLE_LOGGING
                if(logging::enabled())
                    return callfromCFM68K::Invoker<Ret (Args...)>::invokeFromCFM68K(
                        logging::makeLoggedFunction1<Ret (Args...)>(name, call), pops);
#endif
                return callfromCFM68K::Invoker<Ret (Args...)>::invokeFromCFM68K(call, pops);
            }
        );
    }
}

template<typename Ret, typename... Args, Ret (*fptr)(Args...), int trapno, typename CallConv>
Ret TrapFunction<Ret (Args...), fptr, trapno, CallConv>::invokeViaTrapTable(Args... args) const
{
    putOSTrapWord(trapno);
    return (UPP<Ret (Args...),CallConv>(SYN68K_TO_US(tableEntry())))(args...);
}

template<typename Ret, typename... Args, Ret (*fptr)(Args...), int trapno, typename CallConv>
void TrapFunction<Ret (Args...), fptr, trapno, CallConv>::init()
{
    WrappedFunction<Ret (Args...), fptr, CallConv>::init();
    originalFunction = US_TO_SYN68K(((void*)this->guestFP));
    assert(trapno);
    assert(!tableEntry());
    tableEntry() = originalFunction;
}

template<typename Ret, typename... Args, Ret (*fptr)(Args...), int trapno, uint32_t selector, typename CallConv>
SubTrapFunction<Ret (Args...), fptr, trapno, selector, CallConv>::SubTrapFunction(
    const char* name, GenericDispatcherTrap& dispatcher, const char *exportToLib)
    : WrappedFunction<Ret(Args...),fptr,CallConv>(name, exportToLib), dispatcher(dispatcher)
{
}

template<typename Ret, typename... Args, Ret (*fptr)(Args...), int trapno, uint32_t selector, typename CallConv>
void SubTrapFunction<Ret (Args...), fptr, trapno, selector, CallConv>::init()
{
    WrappedFunction<Ret(Args...),fptr,CallConv>::init();
#ifdef EXECUTOR_ENABLE_LOGGING
    if(logging::enabled())
        dispatcher.addSelector(selector, this,
            [this](syn68k_addr_t addr)
            {
                return callfrom68K::Invoker<Ret (Args...), CallConv>
                    ::invokeFrom68K(addr, logging::makeLoggedFunction<CallConv>(this->name, fptr));
            }
        );
    else
#endif
        dispatcher.addSelector(selector, this,
            [](syn68k_addr_t addr)
            {
                return callfrom68K::Invoker<Ret (Args...), CallConv>
                    ::invokeFrom68K(addr, fptr); 
            }
        );
}

template<class SelectorConvention>
syn68k_addr_t DispatcherTrap<SelectorConvention>::invokeFrom68K(syn68k_addr_t addr, void* extra)
{
    DispatcherTrap<SelectorConvention>* self = (DispatcherTrap<SelectorConvention>*)extra;

    if(auto ret = self->checkBreak68K(addr); ~ret)
        return ret;

    uint32 sel = SelectorConvention::get();
    auto it = self->selectors.find(sel);
    if(it != self->selectors.end())
    {
        if(auto ret = it->second.entrypoint->checkBreak68K(addr); ~ret)
            return ret;

        SelectorConvention::commit();
        return it->second.invoke(addr);
    }
    else
    {
        if(syn68k_addr_t ret = unknown_selector_68k(self->name, sel))
            return ret;
        std::cerr << "Unknown selector 0x" << std::hex << sel << " for trap " << self->name << std::endl;
        if(base::Debugger::instance)
        {
            if(auto ret = base::Debugger::instance->trapBreak68K(addr, "Unimplemented selector"); ~ret)
                return ret;
            return POPADDR();
        }
        else
        {
            /* MacPhoenix: a fatal error (snapshot + log) instead of abort(). */
            char msg[96];
            snprintf(msg, sizeof msg, "Unknown selector 0x%x for trap %s",
                     (unsigned)sel, self->name);
            if(ROMlib_fatal_hook)
            {
                auto hook = ROMlib_fatal_hook;
                ROMlib_fatal_hook = nullptr;
                hook(msg);
            }
            std::abort();
        }
    }
}

template<class SelectorConvention>
void DispatcherTrap<SelectorConvention>::addSelector(uint32_t sel, Entrypoint *ep, std::function<syn68k_addr_t(syn68k_addr_t)> handler)
{
    selectors[sel & SelectorConvention::selectorMask] = { ep, handler };
}

template<class SelectorConvention>
void DispatcherTrap<SelectorConvention>::init()
{
    GenericDispatcherTrap::init();
    if(trapno)
    {
        ProcPtr guestFP = (ProcPtr)SYN68K_TO_US(::callback_install(&invokeFrom68K, this));
        originalFunction = US_TO_SYN68K(((void*)guestFP));
        if(trapno & TOOLBIT)
        {
            tooltraptable[trapno & 0x3FF] = originalFunction;
        }
        else
        {
            ostraptable[trapno & 0xFF] = originalFunction;
        }
    }
}

template<class SelectorConvention>
bool DispatcherTrap<SelectorConvention>::putSelector(uint32_t sel) const
{
    if constexpr(SelectorConvention::inRegister)
    {
        SelectorConvention::put(sel);
        return true;
    }
    return false;
}

template<class SelectorConvention>
int DispatcherTrap<SelectorConvention>::stackSelectorBytes() const
{
    return SelectorConvention::stackBytes;
}

template<typename Trap, typename Ret, typename... Args, bool... flags>
void TrapVariant<Trap, Ret (Args...), flags...>::init()
{
    Entrypoint::init();
    if(libname)
    {
#ifdef EXECUTOR_ENABLE_LOGGING
        if(logging::enabled())
        {
            builtinlibs::addPPCEntrypoint(libname, name,
                [this](PowerCore& cpu)
                {
                    if(auto ret = this->checkBreakPPC(cpu); ~ret)
                        return ret;

                    return callfromPPC::Invoker<Ret (Args...)>::invokeFromPPC(cpu,
                        logging::makeLoggedFunction1<Ret (Args...)>(name, 
                            [this](Args... args) -> Ret { return (*this)(args...); }
                        )
                    );
                });
        }
        else
#endif        
        {
            builtinlibs::addPPCEntrypoint(libname, name,
                [this](PowerCore& cpu)
                {
                    if(auto ret = this->checkBreakPPC(cpu); ~ret)
                        return ret;

                    return callfromPPC::Invoker<Ret (Args...)>::invokeFromPPC(cpu,
                        [this](Args... args) -> Ret { return (*this)(args...); }
                    );
                });
        }
        builtinlibs::addCFM68KEntrypoint(libname, name,
            [this, pops = builtinlibs::cfm68kCalleePops(name)](syn68k_addr_t addr) {
                if(auto ret = this->checkBreak68K(addr); ~ret)
                    return ret;
                auto call = [this](Args... args) -> Ret { return (*this)(args...); };
#ifdef EXECUTOR_ENABLE_LOGGING
                if(logging::enabled())
                    return callfromCFM68K::Invoker<Ret (Args...)>::invokeFromCFM68K(
                        logging::makeLoggedFunction1<Ret (Args...)>(name, call), pops);
#endif
                return callfromCFM68K::Invoker<Ret (Args...)>::invokeFromCFM68K(call, pops);
            }
        );
    }
}

template<typename Trap, typename Ret, typename... Args, bool... flags>
TrapVariant<Trap, Ret (Args...), flags...>::TrapVariant(const Trap& trap, const char* name, const char* exportToLib)
    : Entrypoint(name, exportToLib), trap(trap)
{
}


}
}
