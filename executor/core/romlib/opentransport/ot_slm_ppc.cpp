/* ot_slm_ppc.cpp — the Shared Library Manager's PowerPC side, as far as
 * Open Transport's PowerPC clients reach it (the TCP/IP control panel), in
 * C++: Apple's "Shared Library Manager PPC" stays off.
 *
 * A PowerPC client links ASLM's static glue (the ASLM SDK's
 * LibraryManagerPPC.o), which finds ASLM's globals through the fragment
 * "SLMGlobal" (GetSharedLibrary, then the data export __gSLMGlobal, a
 * pointer to them) and uses a handful of entries. Which ones was seen in
 * the TCP/IP panel's linked copy of the glue; ASLM's own code was not
 * read. The classes, fields, function sets and errors are the ASLM SDK's
 * (LibraryManager.h, LibraryManagerUtilities.h, LibraryManagerClasses.h;
 * the "!$fset" indices are those of the glue's stub records):
 *
 *   SLMGlobal+$10    the root manager. Its vtable (pointers to transition
 *                    vectors, as every PowerPC vtable here) has
 *                      +$74 make a client's TLibraryManager, C:
 *                        OSErr (root, TLibraryManager **out, void *frame,
 *                               size_t pool, zone, memtype, Boolean nonApp)
 *                      +$78 free it: Boolean (root, TLibraryManager **,
 *                           Boolean nonApp) (true: unload ASLM too)
 *                      +$60, +$64 InstallDeathWatcher and
 *                           RemoveDeathWatcher: Boolean (root, TNotifier *)
 *                           (the panel's TProcNotifier).
 *                    The glue takes "+$10 is not nil" as "ASLM is loaded".
 *   SLMGlobal+$28    a trace object (nil: no tracing).
 *   SLMGlobal+$64    a routine descriptor that loads (1) or unloads (2)
 *                    ASLM; nil, as it is always loaded.
 *   SLMGlobal+$7C    the constructor binder, +$88 the function binder:
 *                    pointers to code. A stub record in r12 (its bound
 *                    vector, the client vector record, the set's ID at the
 *                    record's +16, the index at +12), the client's manager
 *                    in r11, r3-r10 also in the caller's parameter area,
 *                    LR the caller's. The stubs are never bound, so every
 *                    call comes here.
 *   TLibraryManager  the vtable, fPool, fLibraryFile, fDefaultPool,
 *                    fGlobalWorld. The glue's operator new allocates from
 *                    fDefaultPool (or the current client's fPool) through
 *                    the pool's +$24 (pool, size), and operator delete
 *                    finds a block's pool in the long before it (the low
 *                    four bits flags) and calls its +$2C (pool, block).
 *
 * The function set "!$fset" (ASLM's own) has the 58 functions below;
 * Executor answers the ones the panel calls, which are definitions
 * (multiversal/defs/LibraryManager.yaml: `aslm:` without `library:`,
 * registered with the binder by set and index): PushException,
 * PopException, Fail (raw; a TException's fBuffer is SetupException's save
 * of r0-r31 and LR), MatchException, GetCurrentClient,
 * ASLMGetCurrentGlobalWorld and ASLMSetCurrentGlobalWorld. Of the classes,
 * which the definitions have no form for, TProcNotifier's constructor
 * ("!$pnot", 3). Anything else is logged and answers 0.
 */

#include "ot_internal.h"

#include <base/builtinlibs.h>
#include <base/traps.h>
#include <MemoryMgr.h>
#include <SegmentLdr.h>
#include <PowerCore.h>
#include <LibraryManager.h>

#include <map>
#include <vector>

using namespace Executor;
using namespace Executor::ot;

namespace
{

/* ASLM's errors (LibraryManager.h). */
constexpr int16_t kASLMOutOfMemory = -3126;

constexpr int kSlots = 48; /* our vtables' length */

/* The function set "!$fset", by index, as the SDK's glue names them. */
const char *const fset_names[] = {
    nullptr, "FormatTrace", "SLMsprintf", "TDynamic::Trace", "GetObjectsClassID",
    "GetObjectsParentClassID", "GetObjectsSize", "GetObjectsLocalLibraryFile",
    "GetObjectsLocalPool", "SetObjectsLocalPool", "EnterSystemMode", "LeaveSystemMode",
    "NewPool", "NewPoolWithZone", "DeletePool", "GetObjectsLocalLibrary",
    "TClassID::operator==", "TClassID::GetLength", "TClassID::ExtractVersion",
    "PushException", "PopException", "Fail", "MatchException",
    "RegisterLibraryFileFolder", "UnregisterLibraryFileFolder", "RegisterLibraryFile",
    "UnregisterLibraryFile", "UnregisterLibraryFileByFileSpec", "GetFSSpec",
    "GetCurrentClient", "SetCurrentClient", "DummySetClientToWorld",
    "GetClientLibraryManager", "IsFileSpecTypeSupported", "CompareFileSpecs",
    "InitMacFileSpec", "InitFileIDFileSpec", "FindLibrary", "FindLibraryWithClassID",
    "SLMmemcpy", "SLMmemmove", "SLMmemset", "ZeroMem", "SetBit", "ClearBit", "TestBit",
    "DestroyPointer", "CreateLibraryIterator", "CreateLibraryFileIterator",
    "ASLMGetCurrentGlobalWorld", "ASLMSetCurrentGlobalWorld", "SLMvsprintf",
    "SetSystemAsClient", "GetSystemClient", "GetLibraryClient", "GetObjectsVersion",
    "GetObjectsMinVersion", "UnloadUnusedLibraries", "DummyGetClientFromWorld",
};

/* What the guest sees, in storage it can address. */
alignas(16) uint8_t slm_global[0x100];
alignas(16) uint8_t root_object[16];
alignas(8) uint8_t slm_global_cell[4]; /* __gSLMGlobal */
alignas(16) uint32_t root_vtable[kSlots], manager_vtable[kSlots], pool_vtable[kSlots],
    notifier_vtable[kSlots];

/* A pool: 16-aligned (operator delete masks the low four bits off the
   long before a block), the vtable, then the zone it allocates in. */
constexpr int kPools = 64;
alignas(16) uint8_t pool_objects[kPools][16];
bool pool_used[kPools];

struct Manager
{
    uint32_t out;   /* where the client keeps it (its __gLibraryManager) */
    uint32_t world; /* the client's A5 world */
    uint32_t pool;
};
std::map<uint32_t, Manager> managers; /* by address */

/* Each process's exceptions (PushException), the innermost first. */
std::map<uint32_t, uint32_t> exception_top; /* by A5 world */

uint32_t current_a5_world() { return READUL(0x904); /* CurrentA5 */ }

uint32_t addr(const void *p) { return guest_addr(p); }

/* A call's argument: r3-r10. */
uint32_t arg(PowerCore &cpu, int i) { return cpu.r[3 + i]; }

uint32_t answer(PowerCore &cpu, uint32_t r3)
{
    cpu.r[3] = r3;
    return cpu.lr;
}

/* ---- pools -------------------------------------------------------------- */

uint32_t new_pool(uint32_t zone)
{
    for(int i = 0; i < kPools; i++)
        if(!pool_used[i])
        {
            pool_used[i] = true;
            uint32_t p = addr(pool_objects[i]);
            WRITEUL(p, addr(pool_vtable));
            WRITEUL(p + 4, zone);
            return p;
        }
    return 0;
}

void free_pool(uint32_t p)
{
    for(int i = 0; i < kPools; i++)
        if(addr(pool_objects[i]) == p)
            pool_used[i] = false;
}

bool is_pool(uint32_t p)
{
    for(int i = 0; i < kPools; i++)
        if(pool_used[i] && addr(pool_objects[i]) == p)
            return true;
    return false;
}

/* A block: 8 bytes before it, the pool in the second long. */
uint32_t pool_allocate(uint32_t pool, uint32_t size)
{
    THz saved = GetZone();
    if(uint32_t zone = READUL(pool + 4))
        SetZone((THz)SYN68K_TO_US(zone));
    uint32_t b = guest_addr(NewPtrClear(size + 8));
    SetZone(saved);
    if(!b)
        return 0;
    WRITEUL(b + 4, pool);
    return b + 8;
}

void pool_free(uint32_t block)
{
    if(block && is_pool(READUL(block - 4) & ~15u))
        DisposePtr((Ptr)SYN68K_TO_US(block - 8));
}

/* ---- managers ----------------------------------------------------------- */

void free_manager_at(uint32_t m)
{
    auto it = managers.find(m);
    if(it == managers.end())
        return;
    OT_LOG("PowerPC TLibraryManager $%08x freed\n", m);
    if(it->second.out && READUL(it->second.out) == m)
        WRITEUL(it->second.out, 0);
    free_pool(it->second.pool);
    managers.erase(it);
    DisposePtr((Ptr)SYN68K_TO_US(m));
}

uint32_t current_client()
{
    uint32_t a5 = current_a5_world();
    for(auto &[m, info] : managers)
        if(info.world == a5)
            return m;
    return 0;
}

/* Root manager +$74. */
uint32_t make_manager(PowerCore &cpu)
{
    uint32_t out = arg(cpu, 1);
    uint32_t m = guest_addr(NewPtrSysClear(20));
    uint32_t pool = new_pool(guest_addr(GetZone()));
    if(!m || !pool)
    {
        if(m)
            DisposePtr((Ptr)SYN68K_TO_US(m));
        free_pool(pool);
        return answer(cpu, (uint32_t)(int32_t)kASLMOutOfMemory);
    }
    uint32_t world = current_a5_world();
    WRITEUL(m, addr(manager_vtable));
    WRITEUL(m + 4, pool);  /* fPool */
    WRITEUL(m + 12, pool); /* fDefaultPool */
    WRITEUL(m + 16, world);
    managers[m] = { out, world, pool };
    if(out)
        WRITEUL(out, m);
    OT_LOG("PowerPC TLibraryManager $%08x for the client at $%08x\n", m, out);
    return answer(cpu, 0);
}

/* Root manager +$78: false, ASLM stays loaded. */
uint32_t free_manager(PowerCore &cpu)
{
    uint32_t pp = arg(cpu, 1);
    free_manager_at(pp ? READUL(pp) : 0);
    return answer(cpu, 0);
}

/* ---- exceptions --------------------------------------------------------- */

TException *exception_at(uint32_t e) { return e ? ptr_from_longint<TException *>(e) : nullptr; }

/* Fail(long err, const char *msg) (raw: it leaves through another frame):
   to the innermost handler, as SetupException's second return (nonzero),
   with the error in it. */
uint32_t fail(PowerCore &cpu)
{
    uint32_t err = arg(cpu, 0), msg = arg(cpu, 1);
    uint32_t &top = exception_top[current_a5_world()];
    TException *e = exception_at(top);
    OT_LOG("PowerPC Fail(%d) to $%08x\n", (int)err, top);
    if(!e)
    {
        fprintf(stderr, "[ot] Shared Library Manager: Fail(%d) with no handler: "
                        "the application ends\n", (int)err);
        ExitToShell();
        return cpu.lr;
    }
    top = guest_addr(e->fPrev.get());
    e->fError = (int16_t)err;
    e->fMessage = (char *)(msg ? SYN68K_TO_US(msg) : nullptr);
    cpu.r[1] = e->fBuffer[1];
    cpu.r[2] = e->fBuffer[2];
    for(int i = 13; i < 32; i++)
        cpu.r[i] = e->fBuffer[i];
    cpu.r[3] = 1;
    return e->fBuffer[32];
}

/* ---- binding ------------------------------------------------------------ */

/* The arguments as the caller passed them: the stub glue keeps r3-r10 in
   the caller's parameter area and reuses r9-r12. */
void reload_arguments(PowerCore &cpu)
{
    for(int i = 0; i < 8; i++)
        cpu.r[3 + i] = READUL(cpu.r[1] + 24 + 4 * i);
}

std::string stub_set(uint32_t stub)
{
    return guest_cstring(READUL(stub + 4) + 16, 63);
}

/* SLMGlobal+$88: the routine the definitions register for the set and
   index (LibraryManager.yaml). */
uint32_t function_binder(PowerCore &cpu)
{
    static bool checked;
    if(!checked)
    {
        checked = true;
        builtinlibs::checkASLMRawEntrypoints();
    }
    uint32_t stub = cpu.r[12];
    std::string id = stub_set(stub);
    unsigned index = READUW(stub + 12);
    reload_arguments(cpu);
    if(auto *f = builtinlibs::findASLMEntrypoint(id, index))
        return (*f)(cpu);
    std::string name = id == "!$fset" && index < std::size(fset_names) && fset_names[index]
        ? std::string(" (") + fset_names[index] + ")" : "";
    traps::log_unimplemented("Shared Library Manager PowerPC function set \"" + id + "\" "
                             + std::to_string(index) + name + ": answered 0");
    return answer(cpu, 0);
}

/* TProcNotifier(NotifyProcPtr, void *refPtr): the vtable, fClient, fProc,
   fPtr (the notifier itself when refPtr is nil). */
uint32_t construct_proc_notifier(PowerCore &cpu)
{
    uint32_t self = arg(cpu, 0);
    if(!self)
    {
        uint32_t m = current_client();
        self = m ? pool_allocate(READUL(m + 12), 16) : 0;
        if(!self)
            return answer(cpu, 0);
    }
    WRITEUL(self, addr(notifier_vtable));
    WRITEUL(self + 4, current_client());
    WRITEUL(self + 8, arg(cpu, 1));
    WRITEUL(self + 12, arg(cpu, 2) ? arg(cpu, 2) : self);
    OT_LOG("PowerPC TProcNotifier $%08x\n", self);
    return answer(cpu, self);
}

/* SLMGlobal+$7C. */
uint32_t constructor_binder(PowerCore &cpu)
{
    uint32_t stub = cpu.r[12];
    std::string id = stub_set(stub);
    unsigned index = READUW(stub + 12);
    reload_arguments(cpu);
    if(id == "!$pnot" && index == 3)
        return construct_proc_notifier(cpu);
    traps::log_unimplemented("Shared Library Manager PowerPC class \"" + id + "\" "
                             + std::to_string(index) + ": answered nil");
    return answer(cpu, 0);
}

/* ---- vtables ------------------------------------------------------------ */

void fill_vtable(uint32_t *vt, const char *what)
{
    for(int i = 0; i < kSlots; i++)
        WRITEUL(addr(&vt[i]), builtinlibs::newPPCRoutine(what, [what, i](PowerCore &cpu) {
            fprintf(stderr, "[ot] PowerPC %s slot $%02x called: not implemented, answered 0\n",
                    what, i * 4);
            return answer(cpu, 0);
        }));
}

void set_slot(uint32_t *vt, int offset, const char *name, std::function<uint32_t(PowerCore &)> f)
{
    WRITEUL(addr(&vt[offset / 4]), builtinlibs::newPPCRoutine(name, std::move(f)));
}

} // anonymous namespace

/* ---- "!$fset", typed (LibraryManager.yaml) ------------------------------ */

void Executor::PushException(TException *e)
{
    uint32_t &top = exception_top[current_a5_world()];
    e->fPrev = exception_at(top);
    top = guest_addr(e);
}

/* Takes e and any inside it off the stack; nothing if it isn't there
   (Fail took it already: ENDTRY pops it again). */
TException *Executor::PopException(TException *e)
{
    uint32_t &top = exception_top[current_a5_world()];
    for(TException *x = exception_at(top); x; x = x->fPrev)
        if(x == e)
        {
            top = guest_addr(e->fPrev.get());
            break;
        }
    return e;
}

Boolean Executor::MatchException(TException *e, LONGINT err)
{
    return e->fError == err;
}

TLibraryManager *Executor::GetCurrentClient()
{
    uint32_t m = current_client();
    return m ? ptr_from_longint<TLibraryManager *>(m) : nullptr;
}

/* A PowerPC client's global world is its process's A5 world (as on the
   G3: SLMTest), which setting it doesn't move. */
GlobalWorld Executor::ASLMGetCurrentGlobalWorld()
{
    return LM(CurrentA5);
}

GlobalWorld Executor::ASLMSetCurrentGlobalWorld(GlobalWorld)
{
    return LM(CurrentA5);
}

/* The library "SLMGlobal" for PowerPC code, and what __gSLMGlobal points
   to. */
void Executor::ot::slm_ppc_init()
{
    fill_vtable(root_vtable, "root manager");
    fill_vtable(manager_vtable, "TLibraryManager");
    fill_vtable(pool_vtable, "TMemoryPool");
    fill_vtable(notifier_vtable, "TProcNotifier");

    /* Nothing dies here that a watcher would hear of: kept, never
       notified. */
    set_slot(root_vtable, 0x60, "InstallDeathWatcher", [](PowerCore &cpu) {
        OT_LOG("PowerPC death watcher $%08x installed\n", arg(cpu, 1));
        return answer(cpu, 1);
    });
    set_slot(root_vtable, 0x64, "RemoveDeathWatcher", [](PowerCore &cpu) {
        OT_LOG("PowerPC death watcher $%08x removed\n", arg(cpu, 1));
        return answer(cpu, 1);
    });
    set_slot(root_vtable, 0x74, "MakeClientManager", make_manager);
    set_slot(root_vtable, 0x78, "FreeClientManager", free_manager);

    /* void *Allocate(pool, size), void Free(pool, block). */
    set_slot(pool_vtable, 0x24, "TMemoryPool::Allocate", [](PowerCore &cpu) {
        return answer(cpu, pool_allocate(arg(cpu, 0), arg(cpu, 1)));
    });
    set_slot(pool_vtable, 0x2C, "TMemoryPool::Free", [](PowerCore &cpu) {
        pool_free(arg(cpu, 1));
        return answer(cpu, 0);
    });

    /* ~TProcNotifier(this, flags): flags & 1, delete it too. */
    set_slot(notifier_vtable, 0x04, "TProcNotifier::~TProcNotifier", [](PowerCore &cpu) {
        if(arg(cpu, 1) & 1)
            pool_free(arg(cpu, 0));
        return answer(cpu, 0);
    });

    uint32_t g = addr(slm_global);
    WRITEUL(addr(root_object), addr(root_vtable));
    WRITEUL(g + 0x10, addr(root_object));
    WRITEUL(g + 0x7C, builtinlibs::newPPCRoutine("ASLM constructor binder", constructor_binder));
    WRITEUL(g + 0x88, builtinlibs::newPPCRoutine("ASLM function binder", function_binder));
    WRITEUL(addr(slm_global_cell), g);
    builtinlibs::addASLMRawEntrypoint("!$fset", 21, "Fail", fail);
    builtinlibs::addPPCData("SLMGlobal", "__gSLMGlobal", slm_global_cell);
}

/* A process quits: the managers its clients kept in its globals, its
   exceptions. */
void Executor::ot::slm_ppc_process_exit(uint32_t lo, uint32_t hi)
{
    std::vector<uint32_t> gone;
    for(auto &[m, info] : managers)
        if((info.out >= lo && info.out < hi) || (info.world >= lo && info.world < hi))
            gone.push_back(m);
    for(uint32_t m : gone)
        free_manager_at(m);
    for(auto it = exception_top.begin(); it != exception_top.end();)
        it = it->first >= lo && it->first < hi ? exception_top.erase(it) : std::next(it);
}
