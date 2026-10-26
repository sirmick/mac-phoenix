
#pragma once

#include <base/functions.h>
#include <base/mactype.h>

#include <cstring>
#include <tuple>

#include <base/cpu.h>
#include <PowerCore.h>

namespace Executor
{

namespace callconv
{

template<int n> struct D
{
    template<typename T>
    operator T() { return cpu_state.regs[n].ul.n; }
    template<typename T>
    operator T*() { return ptr_from_longint<T*>(cpu_state.regs[n].ul.n); }
    template<typename T>
    operator UPP<T>() { return UPP<T>(ptr_from_longint<ProcPtr>(cpu_state.regs[n].ul.n)); }

    static void set(uint32_t x) { cpu_state.regs[n].ul.n = x; }
    static void set(const void *x) { cpu_state.regs[n].ul.n = ptr_to_longint(x); }
    template<class T, class CC>
    static void set(UPP<T,CC> x) { cpu_state.regs[n].ul.n = ptr_to_longint((void*)x); }

    template<class T>
    static void afterCall(T) {}
};

template<int n> struct A : D<n+8> {};

struct D0HighWord
{
    operator uint16_t() { return EM_D0 >> 16; }

    static void set(uint16_t x)
    {
        EM_D0 = (EM_D0 & 0xFFFF) | (x << 16);
    }

    template<class T>
    static void afterCall(T) {}    
};
struct D0LowWord
{
    operator uint16_t() { return EM_D0 & 0xFFFF; }

    static void set(uint16_t x)
    {
        EM_D0 = (EM_D0 & 0xFFFF0000) | x;
    }

    template<class T>
    static void afterCall(T) {}    
};

template<typename T, typename Loc> struct Out
{
    GUEST<T> temp;

    operator GUEST<T>*() { return &temp; }

    ~Out() { Loc::set(toRawHostOrder<T>(temp)); }

    static void set(GUEST<T>* p) {}
    static void afterCall(GUEST<T>* p) { setRawHostOrder<T>(*p,Loc()); }
};

template<typename T, typename InLoc, typename OutLoc> struct InOut
{
    GUEST<T> temp;

    operator GUEST<T>*() { return &temp; }

    InOut() { setRawHostOrder<T>(temp,InLoc()); }
    ~InOut() { OutLoc::set(toRawHostOrder<T>(temp)); }

    static void set(GUEST<T>* p) { InLoc::set(toRawHostOrder<T>(*p)); }
    static void afterCall(GUEST<T>* p) { setRawHostOrder<T>(*p,OutLoc()); }
};

template<int mask> struct TrapBit
{
    operator bool() { return !!(EM_D1 & mask); }

    static void set(bool b)
    {
        if(b)
            EM_D1 |= mask;
        else
            EM_D1 &= ~mask;
    }
    template<class T>
    static void afterCall(T) {}
};

struct CCFromD0
{
    syn68k_addr_t afterwards(syn68k_addr_t ret)
    {
        cpu_state.ccc = 0;
        cpu_state.ccn = (cpu_state.regs[0].sw.n < 0);
        cpu_state.ccv = 0;
        cpu_state.ccx = cpu_state.ccx; /* unchanged */
        cpu_state.ccnz = (cpu_state.regs[0].sw.n != 0);
        return ret;
    }
};

struct ClearD0
{
    syn68k_addr_t afterwards(syn68k_addr_t ret)
    {
        EM_D0 = 0;
        return ret;
    }  
};

struct SaveA1D1D2
{
    syn68k_addr_t a1, d1, d2;
    SaveA1D1D2()
    {
        a1 = EM_A1;
        d1 = EM_D1;
        d2 = EM_D2;
    }
    syn68k_addr_t afterwards(syn68k_addr_t ret)
    {
        EM_A1 = a1;
        EM_D1 = d1;
        EM_D2 = d2;
        return ret;
    }  
};

struct MoveA1ToA0
{
    syn68k_addr_t a1;
    MoveA1ToA0()
    {
        a1 = EM_A1;
    }
    syn68k_addr_t afterwards(syn68k_addr_t ret)
    {
        EM_A0 = a1;
        return ret;
    }
};

namespace stack
{
    template<class T>
    void push(T x)
    {
        EM_A7 -= (sizeof(GUEST<T>) + 1) & ~1;
        *(ptr_from_longint<GUEST<T>*>(EM_A7)) = x;
    }
    template<class T>
    T pop()
    {
        T retval = *ptr_from_longint<GUEST<T>*>(EM_A7);
        EM_A7 += (sizeof(GUEST<T>) + 1) & ~1;
        return retval;
    }
}
}

namespace callfrom68K
{
    template<typename F, typename CallConv>
    struct Invoker;

    template<typename... Xs> struct List;

    template<typename F, typename Args, typename ToDoArgs>
    struct PascalInvoker;

    template<typename Ret, typename... Args>
    struct PascalInvoker<Ret (Args...), List<Args...>, List<>>
    {
        template<typename F>
        static void invokeFrom68K(const F& fptr, Args... args)
        {
            Ret retval = fptr(args...);
            *ptr_from_longint<GUEST<Ret>*>(EM_A7) = retval;
        }
    };

    template<typename... Args>
    struct PascalInvoker<void (Args...), List<Args...>, List<>>
    {
        template<typename F>
        static void invokeFrom68K(const F& fptr, Args... args)
        {
            fptr(args...);
        }
    };

    template<typename F, typename... Args, typename Arg, typename... ToDoArgs>
    struct PascalInvoker<F, List<Args...>, List<Arg, ToDoArgs...>>
    {
        template<typename F1>
        static void invokeFrom68K(const F1& fptr, Args... args)
        {
            Arg arg = callconv::stack::pop<Arg>();
            PascalInvoker<F, List<Arg, Args...>, List<ToDoArgs...>>::invokeFrom68K(fptr, arg, args...);
        }
    };

    template<typename SrcList, typename DstList = List<>>
    struct Reverse;

    template<typename... Xs, typename X, typename... Ys>
    struct Reverse<List<X, Xs...>, List<Ys...>>
    {
        using type = typename Reverse<List<Xs...>, List<X,Ys...>>::type;
    };

    template<typename... Ys>
    struct Reverse<List<>, List<Ys...>>
    {
        using type = List<Ys...>;
    };
    
    template<typename Ret, typename... Args>
    struct Invoker<Ret (Args...), callconv::Pascal>
    {
        template<typename F>
        static syn68k_addr_t invokeFrom68K(syn68k_addr_t addr, const F& fptr)
        {
            syn68k_addr_t retaddr = callconv::stack::pop<syn68k_addr_t>();
            PascalInvoker<Ret (Args...), List<>, typename Reverse<List<Args...>>::type>::invokeFrom68K(fptr);
            return retaddr;
        }
    };

    template<typename Arg>
    void readCArg(Arg& arg, syn68k_addr_t& ptr)
    {
        arg = *ptr_from_longint<GUEST<Arg>*>(ptr);
        ptr += (sizeof(GUEST<Arg>) + 1) & ~1;
    }

    template<typename F, typename... Args, std::size_t... Indices>
    auto readCArgsAndCall(std::tuple<Args...>& args, const F& fptr, syn68k_addr_t ptr, std::index_sequence<Indices...>)
    {
        (readCArg(std::get<Indices>(args), ptr),...);
        return fptr(std::get<Indices>(args)...);
    }

    template<typename Ret, typename... Args>
    struct Invoker<Ret (Args...), callconv::CCall>
    {
        template<typename F>
        static syn68k_addr_t invokeFrom68K(syn68k_addr_t addr, const F& fptr)
        {
            std::tuple<Args...> args;
            EM_D0 = readCArgsAndCall(args, fptr, EM_A7 + 4, std::index_sequence_for<Args...>());
            return callconv::stack::pop<syn68k_addr_t>();
        }
    };

    template<typename... Args>
    struct Invoker<void (Args...), callconv::CCall>
    {
        template<typename F>
        static syn68k_addr_t invokeFrom68K(syn68k_addr_t addr, const F& fptr)
        {
            std::tuple<Args...> args;
            readCArgsAndCall(args, fptr, EM_A7 + 4, std::index_sequence_for<Args...>());
            return callconv::stack::pop<syn68k_addr_t>();
        }
    };

    template<>
    struct Invoker<syn68k_addr_t (syn68k_addr_t addr, void *), callconv::Raw>
    {
        template<typename F>
        static syn68k_addr_t invokeFrom68K(syn68k_addr_t addr, const F& fptr)
        {
            return fptr(addr, nullptr);
        }
    };

    template<typename F, typename DoneArgs, typename ToDoArgs, typename ToDoCC>
    struct RegInvoker;

    template<typename Ret, typename... Args, typename... DoneArgs>
    struct RegInvoker<Ret (Args...), List<DoneArgs...>, List<>, List<>>
    {
        template<typename F>
        static Ret invokeFrom68K(syn68k_addr_t addr, const F& fptr, DoneArgs... args)
        {
            return fptr(args...);
        }
    };

    template<typename Ret, typename... Args, typename... DoneArgs, typename Arg, typename... TodoArgs, typename CC, typename... TodoCC>
    struct RegInvoker<Ret (Args...), List<DoneArgs...>, List<Arg, TodoArgs...>, List<CC, TodoCC...>>
    {
        template<typename F>
        static Ret invokeFrom68K(syn68k_addr_t addr, const F& fptr, DoneArgs... args)
        {
            CC newarg;
            return RegInvoker<Ret (Args...),List<DoneArgs...,Arg>,List<TodoArgs...>,List<TodoCC...>>
                ::invokeFrom68K(addr, fptr, args..., newarg);
        }
    };

    template<typename Ret, typename... Args, typename RetConv, typename... ArgConvs>
    struct Invoker<Ret (Args...), callconv::Register<RetConv (ArgConvs...)>>
    {
        template<typename F>
        static syn68k_addr_t invokeFrom68K(syn68k_addr_t addr, const F& fptr)
        {
            syn68k_addr_t retaddr = POPADDR();
            Ret retval = RegInvoker<Ret (Args...), List<>, List<Args...>, List<ArgConvs...>>::invokeFrom68K(addr, fptr);
            RetConv::set(retval);  // ### double conversion?
            return retaddr;
        }
    };
    template<typename... Args, typename RetConv, typename... ArgConvs>
    struct Invoker<void (Args...), callconv::Register<RetConv (ArgConvs...)>>
    {
        template<typename F>
        static syn68k_addr_t invokeFrom68K(syn68k_addr_t addr, const F& fptr)
        {
            syn68k_addr_t retaddr = POPADDR();
            RegInvoker<void (Args...), List<>, List<Args...>, List<ArgConvs...>>::invokeFrom68K(addr, fptr);
            return retaddr;
        }
    };

    template<typename Ret, typename... Args, typename RetArgConv, typename Extra1, typename... Extras>
    struct Invoker<Ret (Args...), callconv::Register<RetArgConv, Extra1, Extras...>>
    {
        template<typename F>
        static syn68k_addr_t invokeFrom68K(syn68k_addr_t addr, const F& fptr)
        {
            Extra1 state;
            syn68k_addr_t retval = Invoker<Ret(Args...), callconv::Register<RetArgConv, Extras...>>::invokeFrom68K(addr,fptr);
            return state.afterwards(retval);
        }
    };
}

namespace callto68K
{
    template<typename F, typename CallConv>
    struct Invoker;

    template<typename F, typename... Args>
    struct InvokerRec;

    template<>
    struct InvokerRec<void ()>
    {
        static void invoke68K(void *ptr)
        {
            execute68K(US_TO_SYN68K(ptr));
        }
    };

    template<typename Arg, typename... Args, typename ArgConv, typename... ArgConvs>
    struct InvokerRec<void (Arg, Args...), ArgConv, ArgConvs...>
    {
        static void invoke68K(void *ptr, Arg arg, Args... args)
        {
            ArgConv::set(arg);
            InvokerRec<void (Args...), ArgConvs...>::invoke68K(ptr, args...);
            ArgConv::afterCall(arg);
        }
    };
   
    template<typename Ret, typename... Args, typename RetConv, typename... ArgConvs, typename... Extras>
    struct Invoker<Ret (Args...), callconv::Register<RetConv(ArgConvs...), Extras...>>
    {
        static Ret invoke68K(void *ptr, Args... args)
        {
            InvokerRec<void (Args...), ArgConvs...>::invoke68K(ptr, args...);
            return RetConv();
        }
    };

    template<typename... Args, typename... ArgConvs, typename... Extras>
    struct Invoker<void (Args...), callconv::Register<void(ArgConvs...), Extras...>>
    {
        static void invoke68K(void *ptr, Args... args)
        {
            InvokerRec<void (Args...), ArgConvs...>::invoke68K(ptr, args...);
        }
    };

    template<>
    struct Invoker<void (), callconv::Pascal>
    {
        static void invoke68K(void *ptr)
        {
            M68kReg saveregs[14];
            memcpy(saveregs, &EM_D1, sizeof(saveregs)); /* d1-d7/a0-a6 */
            execute68K(US_TO_SYN68K(ptr));
            memcpy(&EM_D1, saveregs, sizeof(saveregs)); /* d1-d7/a0-a6 */
        }
    };
    template<typename Arg, typename... Args>
    struct Invoker<void (Arg, Args...), callconv::Pascal>
    {
        static void invoke68K(void *ptr, Arg arg, Args... args)
        {
            callconv::stack::push(arg);
            Invoker<void (Args...), callconv::Pascal>::invoke68K(ptr, args...);
        }
    };
    template<typename Ret, typename... Args>
    struct Invoker<Ret (Args...), callconv::Pascal>
    {
        static Ret invoke68K(void *ptr, Args... args)
        {
            callconv::stack::push(Ret());
            Invoker<void (Args...), callconv::Pascal>::invoke68K(ptr, args...);
            return callconv::stack::pop<Ret>();
        }
    };

    template<>
    struct Invoker<syn68k_addr_t (syn68k_addr_t, void*), callconv::Raw>
    {
        static syn68k_addr_t invoke68K(void *ptr, syn68k_addr_t, void *)
        {
            execute68K(US_TO_SYN68K(ptr));
            return POPADDR();   // ###
        }
    };
}

namespace callfromPPC
{
    namespace detail
    {
        // True for types whose in-memory representation already is the guest
        // (big-endian) representation, i.e. GUEST_STRUCT types and GUEST<>
        // wrappers.  Those can be copied to and from the parameter area
        // verbatim.
        template<typename T, typename = void>
        struct IsGuestStruct : std::false_type {};

        template<typename T>
        struct IsGuestStruct<T, std::void_t<typename T::is_guest_struct>>
            : std::true_type {};

        template<typename T>
        inline constexpr bool isGuestStruct = IsGuestStruct<T>::value;

        template<typename T>
        inline constexpr bool isFloatingParameter =
            std::is_same_v<T, float> || std::is_same_v<T, double>;

        // Number of 4-byte words a parameter occupies in the PowerPC
        // parameter area.  Composite parameters are padded to a multiple of
        // 4 bytes (Mac OS Runtime Architectures, "Routine Calling
        // Conventions").
        template<typename T>
        constexpr int wordsFor()
        {
            return (int)((sizeof(GUEST<T>) + 3) / 4);
        }

        // A parameter-area word holds guest (big-endian) bytes.  Composite
        // parameters start at the most significant byte of their first word
        // and are followed by padding, so a partially used last word has its
        // bytes at the top.
        inline uint32_t wordFromBytes(const uint8_t *bytes, size_t len)
        {
            uint32_t word = 0;
            for(size_t i = 0; i < len; i++)
                word |= uint32_t(bytes[i]) << (24 - 8 * i);
            return word;
        }

        inline void bytesFromWord(uint8_t *bytes, size_t len, uint32_t word)
        {
            for(size_t i = 0; i < len; i++)
                bytes[i] = uint8_t(word >> (24 - 8 * i));
        }
    }

    struct ParameterPasser
    {
        PowerCore& cpu;
        uint8_t *paramPtr;
        int gprIndex = 3;
        int fprIndex = 1;

        template<typename T>
        T get();
        template<typename T>
        void set(const T&);
        
        template<typename T>
        ParameterPasser operator<<(const T& arg);

        template<typename T>
        ParameterPasser operator>>(T& arg);

        template<typename T>
        constexpr ParameterPasser operator+(const T& arg);
    };

    template<typename T>
    ParameterPasser ParameterPasser::operator<<(const T& arg)
    {
        set(arg);
        return *this + arg;
    }

    template<typename T>
    ParameterPasser ParameterPasser::operator>>(T& arg)
    {
        arg = get<T>();
        return *this + arg;
    }

    template<typename T>
    constexpr ParameterPasser ParameterPasser::operator+(const T&)
    {
        // The parameter area contains every argument, whether or not it is
        // passed in a register, so both cursors advance by its full size.
        constexpr int words = detail::wordsFor<T>();
        return ParameterPasser { cpu, paramPtr + 4 * words,
                                  gprIndex + words,
                                  fprIndex + (detail::isFloatingParameter<T> ? 1 : 0) };
    }
 
    template<typename T>
    T ParameterPasser::get()
    {
        if constexpr(detail::isFloatingParameter<T>)
        {
            if(fprIndex <= 13)
                return T(cpu.f[fprIndex]);
            else
            {
                using Traits = guestvalues::GuestTypeTraits<T>;
                using GuestType = typename Traits::GuestType;
                GuestType raw;
                std::memcpy(&raw, paramPtr, sizeof raw);
                return Traits::guest_to_host(raw);
            }
        }
        else if constexpr(detail::isGuestStruct<T>)
        {
            static_assert(std::is_trivially_copyable_v<T>,
                          "guest structures must be trivially copyable");
            T result;
            auto *bytes = reinterpret_cast<uint8_t *>(&result);
            for(int i = 0; i < detail::wordsFor<T>(); i++)
            {
                size_t offset = 4 * i;
                size_t len = sizeof(T) - offset;
                if(len > 4)
                    len = 4;
                if(gprIndex + i <= 10)
                    detail::bytesFromWord(bytes + offset, len, cpu.r[gprIndex + i]);
                else
                    std::memcpy(bytes + offset, paramPtr + offset, len);
            }
            return result;
        }
        else
        {
            using Traits = guestvalues::GuestTypeTraits<T>;
            using GuestType = typename Traits::GuestType;
            static_assert(sizeof(GuestType) <= 4,
                          "parameter does not fit in a PowerPC parameter word");
            if(gprIndex <= 10)
                return Traits::reg_to_host(cpu.r[gprIndex]);
            else
            {
                GuestType raw;
                std::memcpy(&raw, paramPtr + 4 - sizeof(GuestType), sizeof raw);
                return Traits::guest_to_host(raw);
            }
        }
    }
    template<typename T>
    void ParameterPasser::set(const T& arg)
    {
        if constexpr(detail::isFloatingParameter<T>)
        {
            if(fprIndex <= 13)
                cpu.f[fprIndex] = arg;
            else
            {
                using Traits = guestvalues::GuestTypeTraits<T>;
                auto raw = Traits::host_to_guest(arg);
                std::memcpy(paramPtr, &raw, sizeof raw);
            }
        }
        else if constexpr(detail::isGuestStruct<T>)
        {
            static_assert(std::is_trivially_copyable_v<T>,
                          "guest structures must be trivially copyable");
            const auto *bytes = reinterpret_cast<const uint8_t *>(&arg);
            for(int i = 0; i < detail::wordsFor<T>(); i++)
            {
                size_t offset = 4 * i;
                size_t len = sizeof(T) - offset;
                if(len > 4)
                    len = 4;
                if(gprIndex + i <= 10)
                    cpu.r[gprIndex + i] = detail::wordFromBytes(bytes + offset, len);
                else
                    std::memcpy(paramPtr + offset, bytes + offset, len);
            }
        }
        else
        {
            using Traits = guestvalues::GuestTypeTraits<T>;
            using GuestType = typename Traits::GuestType;
            static_assert(sizeof(GuestType) <= 4,
                          "parameter does not fit in a PowerPC parameter word");
            if(gprIndex <= 10)
                cpu.r[gprIndex] = Traits::host_to_reg(arg);
            else
            {
                auto raw = Traits::host_to_guest(arg);
                std::memcpy(paramPtr + 4 - sizeof(GuestType), &raw, sizeof raw);
            }
        }
    }
    template<class Ret>
    struct InvokerRet;

    template<typename Ret, typename... Args>
    struct InvokerRet<Ret (Args...)>
    {
        template<typename F>
        static void invokeRet(PowerCore& cpu, const F& fptr, Args... args)
        {
            Ret ret = fptr(args...);
            ParameterPasser{cpu,nullptr}.set(ret);
        }
    };

    template<typename... Args>
    struct InvokerRet<void (Args...)>
    {
        template<typename F>
        static void invokeRet(PowerCore& cpu, const F& fptr, Args... args)
        {
            fptr(args...);
        }
    };

    template<typename F>
    struct Invoker;

    template<typename Ret, typename... Args>
    struct Invoker<Ret (Args...)>
    {
        template<typename F, size_t... Is>
        static void invokeFromPPCHelper(PowerCore& cpu, const F& fptr, std::index_sequence<Is...>)
        {
            std::tuple<Args...> args;
            ParameterPasser argGetter{cpu, guestvalues::GuestTypeTraits<uint8_t*>::reg_to_host(cpu.r[1]+24)};

            (void)((argGetter >> ... >> std::get<Is>(args)));
            InvokerRet<Ret (Args...)>::invokeRet(cpu, fptr, std::get<Is>(args)...);
        }
        
        template<typename F>
        static uint32_t invokeFromPPC(PowerCore& cpu, const F& fptr)
        {
            uint32_t saveLR = cpu.lr;
            EM_A7 = cpu.r[1];
            invokeFromPPCHelper(cpu, fptr, std::index_sequence_for<Args...>());
            cpu.r[1] = EM_A7;
            return saveLR; //cpu.lr;
        }
    };


}

/* MacPhoenix: calls from CFM-68K code into a library routine (docs/executor/
   CFM68K.md): arguments pushed right to left, each in a 4-byte slot
   (narrower values widened, at the slot's low end), structures by value
   padded to 4 bytes, the result in D0. A routine with fixed arguments pops
   them, Pascal or C; a variadic one leaves them to the caller. */
namespace callfromCFM68K
{
    template<typename T>
    constexpr uint32_t slotSize()
    {
        return (uint32_t)((sizeof(GUEST<T>) + 3) & ~3);
    }

    template<typename T>
    T readArg(uint32_t slot)
    {
        using GT = GUEST<T>;
        if constexpr(callfromPPC::detail::isGuestStruct<T>)
        {
            T result;
            std::memcpy(&result, ptr_from_longint<uint8_t *>(slot), sizeof result);
            return result;
        }
        else if constexpr(sizeof(GT) < 4)
            return *ptr_from_longint<GT *>(slot + 4 - sizeof(GT));
        else
            return *ptr_from_longint<GT *>(slot);
    }

    template<typename Ret>
    void setResult(const Ret& ret)
    {
        if constexpr(callfromPPC::detail::isGuestStruct<Ret>)
        {
            static_assert(sizeof(Ret) <= 4, "structure results are not passed in D0");
            EM_D0 = callfromPPC::detail::wordFromBytes(reinterpret_cast<const uint8_t *>(&ret), sizeof ret)
                    >> (8 * (4 - sizeof ret));
        }
        else if constexpr(callfromPPC::detail::isFloatingParameter<Ret>)
        {
            /* Not seen yet from CFM-68K code; D0:D1 holds the IEEE double. */
            GUEST<double> g = ret;
            const uint8_t *b = reinterpret_cast<const uint8_t *>(&g);
            EM_D0 = callfromPPC::detail::wordFromBytes(b, 4);
            EM_D1 = callfromPPC::detail::wordFromBytes(b + 4, 4);
        }
        else
        {
            using Traits = guestvalues::GuestTypeTraits<Ret>;
            static_assert(sizeof(typename Traits::GuestType) <= 4, "result does not fit in D0");
            EM_D0 = EM_A0 = Traits::host_to_reg(ret);
        }
    }

    template<typename F>
    struct Invoker;

    template<typename Ret, typename... Args>
    struct Invoker<Ret (Args...)>
    {
        static constexpr uint32_t argBytes() { return (0 + ... + slotSize<Args>()); }

        template<typename F, size_t... Is>
        static void helper(const F& fptr, std::index_sequence<Is...>)
        {
            constexpr uint32_t sizes[] = { slotSize<Args>()..., 0 };
            uint32_t offsets[sizeof...(Args) + 1] = {};
            for(size_t i = 1; i <= sizeof...(Args); i++)
                offsets[i] = offsets[i - 1] + sizes[i - 1];
            uint32_t base = EM_A7 + 4;
            std::tuple<Args...> args { readArg<Args>(base + offsets[Is])... };
            if constexpr(std::is_void_v<Ret>)
                fptr(std::get<Is>(args)...);
            else
                setResult<Ret>(fptr(std::get<Is>(args)...));
        }

        template<typename F>
        static syn68k_addr_t invokeFromCFM68K(const F& fptr, bool calleePops)
        {
            helper(fptr, std::index_sequence_for<Args...>());
            syn68k_addr_t ret = callconv::stack::pop<syn68k_addr_t>();
            if(calleePops)
                EM_A7 += argBytes();
            return ret;
        }
    };
}

template<typename Ret, typename... Args, typename CallConv>
Ret UPP<Ret(Args...), CallConv>::operator()(Args... args) const
{
    return callto68K::Invoker<Ret(Args...), CallConv>::invoke68K(ptr, args...);
}

}
