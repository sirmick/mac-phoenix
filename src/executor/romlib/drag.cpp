/* MacPhoenix: Apple's Drag Manager, the System file's own, in place of
 * Executor's stubs (DragMgr.yaml declares the public routines; none had a
 * body, and Finder's first drag died on a private selector).
 *
 * On a 7.5.5 boot the Drag Manager is five 'proc' resources of the System
 * file (-16512 to -16516: the handler lists, the drags and their items,
 * hiliting, the zoom animations, and the Finder's private calls), 28 KB
 * of 68k code the System's patch 'gpch' 668 dispatches: its DragDispatch
 * entry maps the selector to an index (public $01-$28 as they are,
 * $Cxnn private ones through a ten-entry table, negative ones to $28 and
 * up), takes the piece and routine word for the index from a table,
 * loads the piece if purged and enters it through CallDetachedPackage
 * ($668) with A0 the handle, A1 a lock count and D0 the routine word
 * (parameter words in the high byte, routine in the low). The same patch
 * makes the globals the pieces share at ExpandMem+$118 (350 bytes: +$4
 * the drag being tracked, +$8 flags, +$14 the Gestalt 'drag' attributes,
 * +$1C the handler list, +$84 and +$88 the two trampolines below),
 * registers Gestalt 'drag', and adds TEGetHiliteRgn (textedit/teIMV.cpp)
 * and a cleanup on $AAFB when a process exits (ROMlib_drag_process_exit).
 *
 * The pieces call the Toolbox and a few private Process Manager and
 * Layer Manager selectors (process.cpp: GetLayerAtPoint, HoldProcess-
 * Switching, CallProcessRoutine, GetDesktopProcess, GetWindowProcess,
 * GetLayerWindowList, FindWindowInLayer), the Translation Manager
 * (TranslationMgr.yaml answers "none") and each other through the trap.
 * Everything here is what the patch did around them, in C++: the
 * dispatcher, the Finder's hook trampolines and the globals.
 *
 * Finder registers a hook routine (SetFinderDragHooks, $C301) that the
 * patch installs as the process's tracking and receive handlers through
 * the trampolines; a Finder drag is FinderTrackDrag ($C802), which adopts
 * Finder's own item list and tracks it. The trampolines call the hook
 * with (message, globals+4): tracking messages 1-5 as 4, 7, 9, 8 and 5,
 * then on a drop $B, and $F (in the sender's process, through
 * CallProcessRoutine when that is another one) and $C. Non-Finder drops
 * on Finder go through the pieces' private -4 first (promised files and
 * clippings), as Apple's receive trampoline does. Apple's code was read
 * from the reference boot's RAM ($89D60-$8A1D2, the patch as loaded) and
 * the pieces themselves; tools/macdecode.
 */
#include <base/common.h>
#include <base/trapglue.h>
#include <base/functions.impl.h>
#include <rsys/drag.h>
#include <rsys/version.h>
#include <mman/mman.h>
#include <error/error.h>

#include <AppleEvents.h>
#include <DragMgr.h>
#include <Gestalt.h>
#include <MemoryMgr.h>
#include <Package.h>
#include <ProcessMgr.h>
#include <QuickDraw.h>
#include <ResourceMgr.h>
#include <TranslationMgr.h>
#include <WindowMgr.h>

#include <cstdio>
#include <cstring>

using namespace Executor;

namespace
{
/* The pieces, by the dispatcher's chunk number: 'proc' -16512 - chunk. */
Handle pieces[5];
Ptr globals; /* ExpandMem+$118 */
GUEST<int16_t> *locks; /* a lock count per piece, for CallDetachedPackage */
Ptr thunks; /* 68k: the private routines C++ calls through */
syn68k_addr_t entry_stub, tracking_stub, receive_stub;

enum
{
    globals_size = 0x15E,
    g_version = 0x00, /* $01206001 on 7.5.5 */
    g_current = 0x04, /* the drag being tracked */
    g_flags = 0x08,
    g_attrs = 0x14, /* Gestalt 'drag' */
    g_shield = 0x18, /* JShieldCursor while replaced */
    g_handlers = 0x1C, /* handle: the handler list */
    g_tracking = 0x84, /* the trampolines, as UPPs */
    g_receive = 0x88,
    g_image = 0x150, /* ours: a stack image for CallProcessRoutine (8 bytes) */
};

/* gestaltDragMgrPresent, gestaltDragMgrFloatingWind,
   gestaltDragMgrHasImageSupport and bit 31, as 7.5.5 answers with Color
   QuickDraw. */
const uint32_t drag_attrs = 0x8000000B;

/* A drag record's fields the trampolines touch. */
enum
{
    d_attrs = 0x04,
    d_hook = 0x08, /* Finder's hook, when the drop target is Finder */
    d_sender = 0x10, /* the sender's PSN */
    d_hook_window = 0x34,
    d_items = 0x40, /* handle: the item list */
    d_flags = 0x8C,
};

/* The dispatcher's routine table: for an index, the piece (high nibble),
   the parameter words and the routine (System 'gpch' 668 +$108, as
   loaded at $89DA0 on the reference boot). Public selectors are their
   own index; $28 and up are the private ones. */
const uint16_t routines[0x32] = {
    0x0000, 0x0600, 0x0601, 0x0402, 0x0403, 0x1220, 0x1221, 0x1C22, /* $00: handlers, NewDrag.. */
    0x1823, 0x1C24, 0x1625, 0x1626, 0x1627, 0x1628, 0x1429, 0x152A, /* $08: ..TrackDrag, items */
    0x162B, 0x172C, 0x182D, 0x182E, 0x1C2F, 0x1630, 0x1631, 0x1432, /* $10: flavors */
    0x1433, 0x1434, 0x1635, 0x1436, 0x1437, 0x1838, 0x2540, 0x2241, /* $18: ..hiliting */
    0x2442, 0x2243, 0x2444, 0x3260, 0x3661, 0x3662, 0x2445, 0x1A3B, /* $20: ..zooming, SetDragImage */
    0x4380, 0x4881, 0x4682, 0x4283, 0x4284, 0x0204, 0x1839, 0x183A, /* $28: private -1..-8 */
    0x0805, 0x0606, /* $30: -9, -10 */
};
/* A $Cxnn selector's low byte to an index (the patch's ten-entry table). */
const int private_index[10] = { 0, 0x28, 0x29, 0x2A, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x2C };

/* The trap: the stack holds the caller's return address and, above it,
   the routine's parameters, which CallDetachedPackage re-stacks. */
syn68k_addr_t dispatch(syn68k_addr_t, void *)
{
    int16_t sel = (int16_t)(EM_D0 & 0xFFFF);
    int idx;
    if(sel >= 0)
        idx = sel;
    else if(sel & 0x1000)
        idx = -sel + 0x27; /* -1 and down: $28 and up */
    else
    {
        /* $Cxnn: the parameter words in x, the private routine in nn. */
        unsigned low = sel & 0xFF;
        if(low >= 4 && low <= 8)
        {
            /* The first parameter is a pointer to the real one. */
            uint32_t slot = EM_A7 + ((sel >> 8) & 0xF) * 2;
            WRITEUL(slot, READUL(READUL(slot)));
        }
        idx = low < std::size(private_index) ? private_index[low] : -1;
    }

    Handle h = nullptr;
    if(idx > 0 && idx < (int)std::size(routines) && routines[idx])
    {
        h = pieces[routines[idx] >> 12];
        if(!*h)
            LoadResource(h);
        if(!*h)
            h = nullptr;
    }
    if(!h)
    {
        fprintf(stderr, "[Executor] Drag Manager: selector $%04X has no routine\n",
                (unsigned)(uint16_t)sel);
        EM_D0 = (uint16_t)paramErr;
        return POPADDR();
    }
    if(ERROR_ENABLED_P(ERROR_DRAG_LOG))
        fprintf(stderr, "[drag] selector $%04X: piece %d routine $%03X, from $%X\n",
                (unsigned)(uint16_t)sel, routines[idx] >> 12, routines[idx] & 0xFFF, READUL(EM_A7));
    EM_D0 = routines[idx] & 0xFFF;
    EM_A0 = US_TO_SYN68K(h);
    EM_A1 = US_TO_SYN68K(&locks[routines[idx] >> 12]);
    return US_TO_SYN68K(LM(CallDetachedPackage));
}

inline uint32_t drag_long(Ptr drag, int off)
{
    return *(GUEST<uint32_t> *)(drag + off);
}

inline void set_drag_long(Ptr drag, int off, uint32_t v)
{
    *(GUEST<uint32_t> *)(drag + off) = v;
}

/* The patch's helper before each hook call: the hook's window into the
   drag, and two of its flags into the attributes. */
void set_drag_window(Ptr drag, uint32_t window)
{
    set_drag_long(drag, d_hook_window, window);
    uint32_t f = drag_long(drag, d_flags);
    uint32_t a = drag_long(drag, d_attrs);
    a = (f & 1) ? (a | 2) : (a & ~2u);
    a = (f & 2) ? (a | 0xC) : (a & ~0xCu);
    set_drag_long(drag, d_attrs, a);
}

/* Finder's hook: pascal short hook(long message, Ptr globalsPlus4). */
void call_hook(uint32_t hook, int32_t message)
{
    if(!hook)
        return;
    bool log = ERROR_ENABLED_P(ERROR_DRAG_LOG);
    if(log)
        fprintf(stderr, "[drag] Finder's hook $%X, message %d\n", hook, (int)message);
    UPP<int16_t(int32_t, Ptr)> f((void *)SYN68K_TO_US(hook));
    int16_t r = f(message, globals + 4);
    if(log)
        fprintf(stderr, "[drag] Finder's hook -> %d\n", r);
}

/* The pieces' private routines C++ calls: Pascal routines of the
   selector's parameters for a UPP call. They enter the dispatcher as the
   trap would, with the call's return address right above the parameters
   (CallDetachedPackage re-stacks them from there), by jumping to its
   callback rather than taking the trap, which would push a return
   address of its own in between (helppkg.cpp does the same). */
enum
{
    thunk_size = 10,
    minus4_off = 0,
    minus6_off = thunk_size,
    thunks_size = 2 * thunk_size,
};

void put_thunk(size_t off, uint16_t selector)
{
    /* move.w #selector,d0; jmp entry_stub.l */
    const uint8_t code[thunk_size] = { 0x30, 0x3C, (uint8_t)(selector >> 8), (uint8_t)selector,
                                       0x4E, 0xF9, (uint8_t)(entry_stub >> 24), (uint8_t)(entry_stub >> 16),
                                       (uint8_t)(entry_stub >> 8), (uint8_t)entry_stub };
    memcpy(thunks + off, code, sizeof code);
}

/* The handlers' refcon: a handle to the two-long record {hook, refCon}
   SetFinderDragHooks made. */
GUEST<uint32_t> *hook_record(uint32_t refcon)
{
    Handle h = (Handle)SYN68K_TO_US(refcon);
    return h && *h ? (GUEST<uint32_t> *)*h : nullptr;
}

/* Tracking handler for Finder's hook: pascal OSErr(short message,
   WindowPtr window, void *refcon, DragRef drag). */
syn68k_addr_t tracking_handler(syn68k_addr_t, void *)
{
    syn68k_addr_t ret = POPADDR();
    Ptr drag = (Ptr)SYN68K_TO_US(POPUL());
    GUEST<uint32_t> *rec = hook_record(POPUL());
    POPUL(); /* the window */
    int16_t message = POPUW();
    static const int32_t finder_message[6] = { 0, 4, 7, 9, 8, 5 };
    OSErr result = paramErr;
    if(drag && rec && message >= 1 && message <= 5)
    {
        set_drag_window(drag, rec[1]);
        call_hook(rec[0], finder_message[message]);
        result = noErr;
    }
    WRITEUW(EM_A7, (uint16_t)result);
    return ret;
}

/* Does an item list hold a flavor with flag $4000 (the item list as
   Apple's pieces keep it: a count word at +$E, items from +$10, each a
   size long then its fields, flavors from +$1C counted at +$1A, each a
   size long, type, flags, data)? */
bool has_marked_flavor(Ptr drag)
{
    Handle items = (Handle)SYN68K_TO_US(drag_long(drag, d_items));
    if(!items || !*items)
        return false;
    Ptr base = *items;
    int nitems = *(GUEST<int16_t> *)(base + 0xE);
    Ptr item = base + 0x10;
    for(int i = 0; i < nitems; i++)
    {
        int nflavors = *(GUEST<int16_t> *)(item + 0x1A);
        Ptr flavor = item + 0x1C;
        for(int j = 0; j < nflavors; j++)
        {
            if(*(GUEST<uint32_t> *)(flavor + 8) & 0x4000)
                return true;
            flavor += *(GUEST<int32_t> *)flavor;
        }
        item += *(GUEST<int32_t> *)item;
    }
    return false;
}

/* Receive handler for Finder's hook: pascal OSErr(WindowPtr window,
   void *refcon, DragRef drag). */
syn68k_addr_t receive_handler(syn68k_addr_t, void *)
{
    syn68k_addr_t ret = POPADDR();
    Ptr drag = (Ptr)SYN68K_TO_US(POPUL());
    GUEST<uint32_t> *rec = hook_record(POPUL());
    POPUL(); /* the window */
    OSErr result = noErr;
    bool done = false;
    if(!drag || !rec)
        result = paramErr, done = true;
    else
    {
        uint32_t hook = rec[0];
        set_drag_window(drag, rec[1]);
        call_hook(hook, 0xB);
        if(!(drag_long(drag, d_attrs) & 0x2000))
        {
            /* Not Finder's own items: promised files and clippings. */
            UPP<OSErr(Ptr)> minus4((void *)(thunks + minus4_off));
            OSErr err = minus4(drag);
            if(ERROR_ENABLED_P(ERROR_DRAG_LOG))
                fprintf(stderr, "[drag] private -4 (promised files, clippings) -> %d\n", err);
            if(err)
                result = err, done = true;
        }
        else if(!has_marked_flavor(drag))
            result = dragNotAcceptedErr, done = true;
        else if(drag_long(drag, d_attrs) & 4)
            call_hook(hook, 0xF);
        else if(uint32_t finder_hook = drag_long(drag, d_hook))
        {
            /* In the sender's process: the hook's parameters as a stack
               image, in guest memory (the globals' spare words). */
            GUEST<uint32_t> *image = (GUEST<uint32_t> *)(globals + g_image);
            image[0] = US_TO_SYN68K(globals + 4);
            image[1] = 0xF;
            OSErr err = CallProcessRoutine((ProcessSerialNumber *)(drag + d_sender),
                                           (ProcPtr)SYN68K_TO_US(finder_hook),
                                           (int32_t)US_TO_SYN68K(image), 8);
            if(err && err != paramErr)
                result = err, done = true;
        }
        if(!done && !(drag_long(drag, d_attrs) & 0x200))
            call_hook(hook, 0xC);
    }
    WRITEUW(EM_A7, (uint16_t)result);
    return ret;
}

bool load()
{
    INTEGER saved = CurResFile();
    UseResFile(LM(SysMap));
    bool ok = true;
    for(int chunk = 0; chunk < 5; chunk++)
    {
        Handle h = Get1Resource("proc"_4, -16512 - chunk);
        pieces[chunk] = h;
        if(!h)
            ok = false;
    }
    UseResFile(saved);
    if(!ok)
    {
        memset(pieces, 0, sizeof pieces);
        return false;
    }
    TheZoneGuard guard(LM(SysZone));
    locks = (GUEST<int16_t> *)NewPtrSysClear(5 * sizeof *locks);
    thunks = NewPtrSys(thunks_size);
    if(!locks || !thunks)
        return false;
    entry_stub = callback_install(dispatch, nullptr);
    tracking_stub = callback_install(tracking_handler, nullptr);
    receive_stub = callback_install(receive_handler, nullptr);
    put_thunk(minus4_off, 0xFFFC);
    put_thunk(minus6_off, 0xFFFA);
    int32_t size = 0;
    for(Handle h : pieces)
        size += GetResourceSizeOnDisk(h);
    fprintf(stderr, "[Executor] Drag Manager: the System's 'proc' -16512..-16516 (%d bytes)\n",
            (int)size);
    return true;
}

bool make_globals()
{
    TheZoneGuard guard(LM(SysZone));
    Ptr g = NewPtrSysClear(globals_size);
    if(!g)
        return false;
    auto put = [g](int off, uint32_t v) { *(GUEST<uint32_t> *)(g + off) = v; };
    put(g_version, 0x01206001);
    put(g_attrs, drag_attrs);
    put(g_handlers, US_TO_SYN68K(NewHandleSys(0)));
    put(g_tracking, tracking_stub);
    put(g_receive, receive_stub);
    globals = g;
    if(NewGestaltValue("drag"_4, drag_attrs) == gestaltDupSelectorErr)
        ReplaceGestaltValue("drag"_4, drag_attrs);
    /* gestaltTEAttr: gestaltTEHasGetHiliteRgn, which the patch registers
       too (applications ask it before they drag text). */
    if(NewGestaltValue("teat"_4, 1) == gestaltDupSelectorErr)
        ReplaceGestaltValue("teat"_4, 1);
    return true;
}
}

void Executor::ROMlib_install_drag_manager()
{
    if(!ROMlib_apple_system_file || !LM(SysMapHndl))
        return;
    AE_info_t *em = LM(AE_info);
    if(!em)
        return;
    if(!pieces[0] && !load())
        return;
    if(!globals && !make_globals())
        return;
    em->emDragGlobals = globals;
    tooltraptable[0xABED & 0x3FF] = entry_stub;
}

bool Executor::ROMlib_drag_manager_p()
{
    return globals != nullptr;
}

void Executor::ROMlib_drag_process_exit()
{
    if(!globals)
        return;
    if(uint32_t shield = *(GUEST<uint32_t> *)(globals + g_shield))
    {
        LM(JShieldCursor) = (ProcPtr)SYN68K_TO_US(shield);
        *(GUEST<uint32_t> *)(globals + g_shield) = 0;
    }
    /* Private -6: the current process's handlers out of the list. */
    UPP<OSErr(int32_t)> minus6((void *)(thunks + minus6_off));
    minus6(0);
}

/* Executor's stub, for a System without Apple's pieces: Finder's hooks
   are noted and nothing else happens. */
static Ptr finder_drag_hooks;

OSErr Executor::C_SetFinderDragHooks(Ptr hooks, Boolean install)
{
    finder_drag_hooks = install ? hooks : nullptr;
    return noErr;
}

/* The Translation Manager is not loaded (Easy Open's): no translation. */
OSErr Executor::C_GetFlavorTranslation(int16_t, Handle, OSType, Ptr, ProcPtr)
{
    return -3025; /* noTranslationPathErr */
}
