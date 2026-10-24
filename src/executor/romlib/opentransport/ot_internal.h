#pragma once

/* ot_internal.h — what the parts of Executor's Open Transport share: how
 * a function bound to an OT function set reads its arguments and returns,
 * and how each part registers the functions it implements (ot.cpp). */

#include <base/common.h>
#include <base/cpu.h>
#include <error/error.h>

#include <cstdint>
#include <string>

namespace Executor
{
namespace ot
{

bool log_enabled();
#define OT_LOG(...)                                \
    do                                             \
    {                                              \
        if(::Executor::ot::log_enabled())          \
            fprintf(stderr, "[ot] " __VA_ARGS__);  \
    } while(0)

/* The calling conventions of OT's 68k entries, as its header declares
 * them. Pascal: arguments pushed left to right, each a word or a long,
 * the result in a slot the caller reserved above them, the callee pops
 * the arguments. C (MPW): arguments pushed right to left, a long each,
 * the result in D0, the caller pops. */
enum class Conv : uint8_t
{
    Pascal,
    C,
};

/* One call's arguments, read where the caller left them. sig has one
 * letter per argument: 'L' a long (or pointer), 'W' a word, 'B' a
 * Boolean (a word, the value in its high byte). C arguments are all
 * longs; 'W' and 'B' then read the long's low bits. */
class Call
{
public:
    Call(uint32_t sp, Conv conv, const char *sig);

    uint32_t l(int i) const;
    uint16_t w(int i) const;
    bool b(int i) const;
    /* A guest address, as a host pointer (nullptr for 0). */
    template<class T = uint8_t>
    T *p(int i) const
    {
        uint32_t a = l(i);
        return a ? (T *)SYN68K_TO_US(a) : nullptr;
    }

    uint32_t sp() const { return sp_; }
    int nargs() const { return n_; }
    /* Bytes of arguments (what a pascal entry pops). */
    uint32_t arg_bytes() const { return bytes_; }

private:
    uint32_t sp_;
    Conv conv_;
    int n_ = 0;
    uint32_t bytes_ = 0;
    uint32_t off_[16] = {};
    char kind_[16] = {};
};

/* A function of an OT function set. ret: 'L' long, 'W' word, 'B'
 * Boolean, 'V' none. The implementation returns the result. */
using Impl = uint32_t (*)(const Call &);
struct Def
{
    const char *set;  /* the function set's ID */
    const char *name; /* as in ot_names.inc */
    Conv conv;
    const char *sig;
    char ret;
    Impl impl;
};

/* Each part registers its table when OT first binds (ot.cpp calls the
   register_* functions: romlib is an archive, so a part nothing refers to
   would not be linked at all). */
void register_defs(const Def *defs, size_t n);
template<size_t N>
void register_table(const Def (&defs)[N]) { register_defs(defs, N); }
void register_client();
void register_utils();
void register_providers();
void register_inet();
void register_tasks();
void register_ot_functions(); /* all of the above, once */

/* The function sets (ot.cpp): is this ID one of OT's, and bind a stub of
   one (0 for another set). */
bool is_ot_set(const std::string &id);
syn68k_addr_t bind_ot_stub(uint32_t stub);

/* A fatal error: logged, the front end told, the emulator stopped. */
[[noreturn]] void fatal(const std::string &msg);

/* The Shared Library Manager's entries OT's glue uses (ot_slm.cpp):
   ready (ours made, or Apple's wrapped); a client asked Gestalt 'otan' and
   is about to make its manager; a process's managers go with it. */
bool slm_ready();
void slm_expect_ot_client();
bool slm_apple_running();
void slm_process_exit(uint32_t lo, uint32_t hi);

/* Clients whose record is in [lo, hi): their providers closed (ot_client.cpp). */
void close_clients_in(uint32_t lo, uint32_t hi);

/* A configuration's path ("tcp", "udp", "dnr" for
   kDefaultInternetServicesPath), and its disposal once a provider took it
   (ot_utils.cpp). */
bool config_path(uint32_t cfg, std::string &path);
void dispose_config(uint32_t cfg);

/* An OTPortRef (ot_utils.cpp). */
uint32_t make_port_ref(unsigned bus, unsigned device, unsigned slot, unsigned other);

/* Deferred and system tasks (ot_tasks.cpp); OT's interrupt level: the
   pump, deferred tasks, and OTEnterInterrupt brackets. */
extern int interrupt_level;
void tasks_process_exit(uint32_t lo, uint32_t hi);

/* Guest memory helpers. */
std::string guest_cstring(uint32_t addr, size_t max = 255);
inline uint32_t guest_addr(const void *p) { return p ? US_TO_SYN68K(p) : 0; }

/* OT's errors (OpenTransport.h). */
enum : int32_t
{
    kOTNoError = 0,
    kOTOutOfMemoryErr = -3211,
    kOTNotFoundErr = -3201,
    kOTDuplicateFoundErr = -3216,
    kOTBadAddressErr = -3150,
    kOTBadOptionErr = -3151,
    kOTAccessErr = -3152,
    kOTBadReferenceErr = -3153,
    kOTNoAddressErr = -3154,
    kOTOutStateErr = -3155,
    kOTBadSequenceErr = -3156,
    kOTSysErrorErr = -3157,
    kOTLookErr = -3158,
    kOTBadDataErr = -3159,
    kOTBufferOverflowErr = -3160,
    kOTFlowErr = -3161,
    kOTNoDataErr = -3162,
    kOTNoDisconnectErr = -3163,
    kOTNoUDErrErr = -3164,
    kOTBadFlagErr = -3165,
    kOTNoReleaseErr = -3166,
    kOTNotSupportedErr = -3167,
    kOTStateChangeErr = -3168,
    kOTNoStructureTypeErr = -3169,
    kOTBadNameErr = -3170,
    kOTBadQLenErr = -3171,
    kOTAddressBusyErr = -3172,
    kOTIndOutErr = -3173,
    kOTProviderMismatchErr = -3174,
    kOTResQLenErr = -3175,
    kOTResAddressErr = -3176,
    kOTQFullErr = -3177,
    kOTProtocolErr = -3178,
    kOTBadSyncErr = -3179,
    kOTCanceledErr = -3180,
    kOTClientNotInittedErr = -3279,
    kOTPortHasDiedErr = -3280,
    kOTPortWasEjectedErr = -3281,
    kOTBadConfigurationErr = -3282,
    kOTConfigurationChangedErr = -3283,
    kOTUserRequestedErr = -3284,
    kOTPortLostConnection = -3285,
    kEINVALErr = -3221,
    kEWOULDBLOCKErr = -3234,
    kEALREADYErr = -3236,
    kECONNREFUSEDErr = -3260,
    kEHOSTDOWNErr = -3263,
    kEHOSTUNREACHErr = -3264,
    kETIMEDOUTErr = -3259,
    kOTCanceledErr2 = -3180,
};

} // namespace ot
} // namespace Executor
