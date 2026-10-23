/* ot_slm.cpp — the part of the Shared Library Manager that Open
 * Transport's clients reach, in C++, so OT needs none of Apple's code.
 *
 * A 68k OT application links Apple's static glue, which talks to ASLM
 * through a handful of entries (named here from what the glue calls;
 * the classes and errors are the ASLM SDK's, LibraryManager.h):
 *
 *   ExpandMem+$10C   SLMGlobal, ASLM's globals.
 *   SLMGlobal+$10    the root manager: an object whose vtable's
 *                    +$9C makes a client's TLibraryManager, C:
 *                      OSErr (root, TLibraryManager **out, void *frame,
 *                             size_t pool, zone, memtype, Boolean nonApp)
 *                    +$A0 frees it, C:
 *                      Boolean (root, TLibraryManager **, Boolean nonApp)
 *                    (true: unload ASLM too). The glue takes "+$10 is
 *                    not nil" as "ASLM is loaded".
 *   SLMGlobal+$64    a procedure that loads ASLM (D0 1) or unloads it
 *                    (D0 2); nil here.
 *   SLMGlobal+$88    the binder: a function set's stub record in A0, the
 *                    client's TLibraryManager in D0, the caller's frame
 *                    as it was; binds the stub and jumps to the function.
 *   TLibraryManager  LibraryManager.h: TDynamic's slots (the first SC's
 *                    dummy), then the manager's; LoadClass is slot 16
 *                    ($40), GetLibrary slot 28 ($70). Fields after the
 *                    vtable: fPool, fLibraryFile, fDefaultPool,
 *                    fGlobalWorld.
 *
 * InitOpenTransport's glue asks Gestalt 'otan', makes its manager through
 * the root manager unless it has one, LoadClass("OTClnt$LclUtils"), then
 * binds stubs as they are first called. At ExitToShell (the glue patches
 * it) it frees the manager.
 *
 * Without Apple's ASLM (Executor's own System, or Apple's with the ASLM
 * Loader not allowed) Executor makes SLMGlobal and the root manager here
 * at the first Gestalt 'otan'. With Apple's ASLM running, its binder and
 * its root manager's two slots are wrapped: an OT client (one that asked
 * Gestalt 'otan' just before making its manager, as the glue does) gets a
 * manager of ours and never reaches Apple's code; anything else goes on to
 * Apple's, with every register as it came. A manager of Apple's still
 * answers LoadClass for OT's function sets here, for an ASLM library that
 * uses OT.
 */

#include "ot_internal.h"
#include "ot_client.h"

#include <MemoryMgr.h>

#include <map>

using namespace Executor;
using namespace Executor::ot;

namespace
{

/* ASLM's "not found" (LibraryManager.h, kNotFound). */
constexpr int16_t kASLMNotFound = -3120;

constexpr int kSlots = 64; /* our vtables' length */

uint32_t slm;           /* SLMGlobal, ours or Apple's */
bool own_slm;           /* we made it */
uint32_t apple_binder;  /* what SLMGlobal+$88 held */
uint32_t apple_make, apple_free; /* the root manager's +$9C, +$A0 */
std::map<uint32_t, uint32_t> apple_load_class; /* by vtable */

syn68k_addr_t cb_binder, cb_make, cb_free, cb_load_class;
uint32_t our_root_vtable, our_manager_vtable;

struct Manager
{
    uint32_t out; /* where the client keeps it (its __gLibraryManager) */
};
std::map<uint32_t, Manager> managers; /* ours, by address */

/* Gestalt 'otan' was asked with this A5 and no manager made since. */
uint32_t ot_pending_a5;

uint32_t current_a5_world() { return READUL(0x904); /* CurrentA5 */ }

/* Return from a C function: D0, the caller pops. */
syn68k_addr_t return_c(uint32_t d0)
{
    uint32_t sp = EM_A7;
    EM_D0 = d0;
    EM_A7 = sp + 4;
    return READUL(sp);
}

/* ---- the binder -------------------------------------------------------- */

syn68k_addr_t binder(syn68k_addr_t, void *)
{
    uint32_t stub = EM_A0;
    if(syn68k_addr_t addr = bind_ot_stub(stub))
        return addr;
    std::string id = guest_cstring(READUL(READUL(stub + 4) + 16), 63);
    if(apple_binder && !managers.count(EM_D0))
        return apple_binder;
    char msg[160];
    snprintf(msg, sizeof msg, "Shared Library Manager: function set \"%s\" needs Apple's ASLM",
             id.c_str());
    fatal(msg);
}

/* ---- managers ----------------------------------------------------------- */

/* Unnamed slots of our objects: log, answer 0 (MPW C: the caller pops,
   so any argument list is safe). */
syn68k_addr_t unknown_slot(syn68k_addr_t, void *arg)
{
    uintptr_t v = (uintptr_t)arg;
    fprintf(stderr, "[ot] %s slot $%02x called: not implemented, answered 0\n",
            v & 0x1000 ? "root manager" : "TLibraryManager", (unsigned)(v & 0xFFF) * 4);
    return return_c(0);
}

/* OSErr LoadClass(TLibraryManager *, const TClassID &, Boolean loadAll) */
syn68k_addr_t load_class(syn68k_addr_t, void *)
{
    uint32_t sp = EM_A7;
    uint32_t self = READUL(sp + 4);
    std::string id = guest_cstring(READUL(sp + 8), 63);
    if(is_ot_set(id))
    {
        OT_LOG("LoadClass \"%s\": Executor's\n", id.c_str());
        return return_c(0);
    }
    if(managers.count(self))
    {
        OT_LOG("LoadClass \"%s\": no such library\n", id.c_str());
        return return_c((uint16_t)kASLMNotFound);
    }
    auto it = apple_load_class.find(READUL(self));
    return it != apple_load_class.end() ? it->second : apple_load_class.begin()->second;
}

/* TLibrary *GetLibrary(TLibraryManager *): nil, an application's. */
syn68k_addr_t get_library(syn68k_addr_t, void *)
{
    return return_c(0);
}

uint32_t make_vtable(bool root)
{
    uint32_t vt = guest_addr(NewPtrSysClear(kSlots * 4));
    for(int i = 0; i < kSlots; i++)
        WRITEUL(vt + 4 * i,
                callback_install(unknown_slot, (void *)(uintptr_t)(i | (root ? 0x1000 : 0))));
    return vt;
}

/* Our root manager's +$9C, or the wrapper on Apple's. */
syn68k_addr_t make_manager(syn68k_addr_t, void *)
{
    uint32_t sp = EM_A7;
    uint32_t out = READUL(sp + 8);
    bool ot_client = ot_pending_a5 && ot_pending_a5 == current_a5_world();
    ot_pending_a5 = 0;
    if(own_slm || ot_client)
    {
        /* TLibraryManager: the vtable and four fields, all nil. */
        uint32_t m = guest_addr(NewPtrSysClear(20));
        if(!m)
            return return_c((uint16_t)memFullErr);
        WRITEUL(m, our_manager_vtable);
        managers[m] = { out };
        if(out)
            WRITEUL(out, m);
        OT_LOG("TLibraryManager $%08x for the client at $%08x\n", m, out);
        return return_c(0);
    }

    /* Apple's makes it; its LoadClass is wrapped for OT's sets. */
    uint32_t ret = READUL(sp);
    uint32_t args[7];
    for(int i = 0; i < 7; i++)
        args[i] = READUL(sp + 4 + 4 * i);
    EM_A7 = sp - 28;
    for(int i = 0; i < 7; i++)
        WRITEUL(EM_A7 + 4 * i, args[i]);
    execute68K(apple_make);
    uint32_t result = EM_D0;
    EM_A7 = sp;
    if(uint32_t m = out ? READUL(out) : 0)
    {
        uint32_t vt = READUL(m);
        if(READUL(vt + 0x40) != cb_load_class)
        {
            apple_load_class[vt] = READUL(vt + 0x40);
            WRITEUL(vt + 0x40, cb_load_class);
        }
    }
    EM_D0 = result;
    EM_A7 = sp + 4;
    return ret;
}

void free_manager_at(uint32_t m)
{
    auto it = managers.find(m);
    if(it == managers.end())
        return;
    OT_LOG("TLibraryManager $%08x freed\n", m);
    if(it->second.out && READUL(it->second.out) == m)
        WRITEUL(it->second.out, 0);
    managers.erase(it);
    DisposePtr((Ptr)SYN68K_TO_US(m));
}

/* Our root manager's +$A0, or the wrapper on Apple's. */
syn68k_addr_t free_manager(syn68k_addr_t, void *)
{
    uint32_t sp = EM_A7;
    uint32_t pp = READUL(sp + 8);
    uint32_t m = pp ? READUL(pp) : 0;
    if(managers.count(m))
    {
        free_manager_at(m);
        return return_c(0); /* keep ASLM loaded */
    }
    if(apple_free)
        return apple_free;
    return return_c(0);
}

void install_callbacks()
{
    if(cb_binder)
        return;
    register_ot_functions();
    cb_binder = callback_install(binder, nullptr);
    cb_make = callback_install(make_manager, nullptr);
    cb_free = callback_install(free_manager, nullptr);
    cb_load_class = callback_install(load_class, nullptr);
    our_manager_vtable = make_vtable(false);
    WRITEUL(our_manager_vtable + 0x40, cb_load_class);
    WRITEUL(our_manager_vtable + 0x70, callback_install(get_library, nullptr));
}

/* SLMGlobal and the root manager of our own. */
uint32_t make_own_slm()
{
    uint32_t em = READUL(0x2B6);
    if(!em || GetPtrSize((Ptr)SYN68K_TO_US(em)) < 0x110)
        return 0;
    uint32_t g = guest_addr(NewPtrSysClear(0x100));
    our_root_vtable = make_vtable(true);
    uint32_t root = guest_addr(NewPtrSysClear(8));
    if(!g || !our_root_vtable || !root)
        return 0;
    WRITEUL(our_root_vtable + 0x9C, cb_make);
    WRITEUL(our_root_vtable + 0xA0, cb_free);
    WRITEUL(root, our_root_vtable);
    WRITEUL(g + 0x10, root);
    WRITEUL(g + 0x88, cb_binder);
    WRITEUL(em + 0x10C, g);
    own_slm = true;
    OT_LOG("Shared Library Manager: Executor's (SLMGlobal $%08x)\n", g);
    return g;
}

} // anonymous namespace

/* Put our entries in place (again, if Apple's ASLM rewrote one); false if
   there is no ExpandMem to hang SLMGlobal on. */
bool Executor::ot::slm_ready()
{
    install_callbacks();
    uint32_t em = READUL(0x2B6);
    uint32_t g = em ? READUL(em + 0x10C) : 0;
    if(!g)
        g = make_own_slm();
    if(!g)
        return false;
    slm = g;
    if(own_slm)
        return true;

    if(READUL(g + 0x88) != cb_binder)
    {
        apple_binder = READUL(g + 0x88);
        WRITEUL(g + 0x88, cb_binder);
        OT_LOG("Apple's ASLM: binder wrapped (SLMGlobal $%08x, binder $%08x)\n", g,
               apple_binder);
    }
    if(uint32_t root = READUL(g + 0x10))
    {
        uint32_t vt = READUL(root);
        if(READUL(vt + 0x9C) != cb_make)
        {
            apple_make = READUL(vt + 0x9C);
            WRITEUL(vt + 0x9C, cb_make);
        }
        if(READUL(vt + 0xA0) != cb_free)
        {
            apple_free = READUL(vt + 0xA0);
            WRITEUL(vt + 0xA0, cb_free);
        }
    }
    return true;
}

void Executor::ot::slm_expect_ot_client()
{
    ot_pending_a5 = current_a5_world();
}

/* A process quits: the managers its clients kept in its globals. */
void Executor::ot::slm_process_exit(uint32_t lo, uint32_t hi)
{
    std::vector<uint32_t> gone;
    for(auto &[m, info] : managers)
        if(info.out >= lo && info.out < hi)
            gone.push_back(m);
    for(uint32_t m : gone)
        free_manager_at(m);
    ot_pending_a5 = 0;
}
