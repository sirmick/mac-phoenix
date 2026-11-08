/* MacPhoenix: the Component Manager (_ComponentDispatch, $A82A).
 *
 * Inside Macintosh: More Macintosh Toolbox ch. 6; the selectors, calling
 * conventions and record layouts are those of Universal Interfaces'
 * Components.h. A call is `moveq #selector,d0; _ComponentDispatch` with
 * Pascal arguments on the stack. Two selectors are special:
 *
 *   0   CallComponent: on the stack, from the top, the parameter block
 *       header {flags.b, paramSize.b, what.w}, paramSize bytes of the
 *       call's parameters, the instance and the result. The component's
 *       entry point gets (ComponentParameters *, storage handle).
 *  -1   CallComponentFunction[WithStorage](params, func): the parameters
 *       are copied onto the stack and func is jumped to; a storage handle
 *       the caller pushed is then its first argument, and the caller's
 *       result slot is its result slot.
 *  -2   (private; QuickTime 3's 68k codecs) a fast-dispatch component hands
 *       the call it is in to another instance: on the stack the instance
 *       and the call's header long, over the component's own frame; see
 *       delegate_fast below.
 *
 * Components and instances live here, host side, and are named by opaque
 * 32-bit values ($0001xxxx components, $0002xxxx instances). Name, info
 * and icon handles and loaded code are in the System heap. A component
 * registered from a resource ('thng') loads its code the first time it is
 * opened. */
#include <base/common.h>
#include <FileMgr.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <ProcessMgr.h>
#include <file/file.h>
#include <mman/mman.h>
#include <base/emustubs.h>
#include <rsys/component.h>
#include <Components.h>
#include <CodeFragments.h>
#include <Gestalt.h>
#include <base/cpu.h>
#include <base/traps.impl.h>
#include <res/resource.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <vector>

using namespace Executor;

namespace
{
enum : uint32_t
{
    kCompBase = 0x00010000,
    kInstBase = 0x00020000,
};

uint32_t rd32(uint32_t a) { return READUL(a); }
uint16_t rd16(uint32_t a) { return READUW(a); }

struct Comp
{
    uint32_t id;
    uint32_t type, subtype, manufacturer, flags, mask;
    uint32_t entry = 0;     /* guest code; 0 until loaded */
    bool native = false;    /* Executor's own: entered as native_entry, whatever its flags say */
    Handle code = nullptr;  /* loaded from the resource file */
    bool from_file = false;
    FSSpec file = {};
    uint32_t code_type = 0;
    int16_t code_id = 0;
    bool ppc = false;       /* the code is a PowerPC fragment */
    Handle name = nullptr, info = nullptr, icon = nullptr;
    int32_t refcon = 0;
    uint32_t version = 0;
    int instances = 0;
    bool global = true;
    ProcessSerialNumber owner = {};
    Comp *captured_by = nullptr;
};

struct Inst
{
    uint32_t id;
    Comp *comp;
    uint32_t storage = 0;
    int16_t error = 0;
    uint32_t a5 = 0;
};

std::vector<std::unique_ptr<Comp>> comps; /* search order */
/* MacPhoenix: components Executor implements itself, by type; a 'thng' of
   the same type is not registered (it stands for something Executor does
   in C++, as Apple's 'sdev' components stand for sound hardware), and the
   first one lends the native one its name, info and icon. */
std::map<uint32_t, Comp *> native_by_type;
std::map<uint32_t, Inst> insts;
uint32_t next_comp = 1, next_inst = 1;
int32_t list_seed = 1;
std::map<uint32_t, int32_t> type_seeds;

Comp *comp_of(uint32_t v)
{
    for(auto &c : comps)
        if(c->id == v)
            return c.get();
    /* An instance stands for its component where a component is asked. */
    auto it = insts.find(v);
    return it == insts.end() ? nullptr : it->second.comp;
}

Inst *inst_of(uint32_t v)
{
    auto it = insts.find(v);
    return it == insts.end() ? nullptr : &it->second;
}

void changed(uint32_t type)
{
    list_seed++;
    type_seeds[type]++;
}

/* Copy a handle's contents into a new System-heap handle. */
Handle sys_copy(Handle h)
{
    if(!h || !*h)
        return nullptr;
    TheZoneGuard guard(LM(SysZone));
    Size n = GetHandleSize(h);
    Handle c = NewHandle(n);
    if(c)
        memcpy(*c, *h, n);
    return c;
}

bool matches(const Comp &c, uint32_t looking)
{
    if(!looking)
        return true;
    uint32_t t = rd32(looking), s = rd32(looking + 4), m = rd32(looking + 8);
    uint32_t f = rd32(looking + 12), fm = rd32(looking + 16);
    if((t && t != c.type) || (s && s != c.subtype) || (m && m != c.manufacturer))
        return false;
    return (c.flags & fm) == (f & fm);
}

/* Visible to the current process: global ones and its own. */
bool visible(const Comp &c)
{
    if(c.captured_by)
        return false;
    if(c.global)
        return true;
    ProcessSerialNumber me;
    GetCurrentProcess(&me);
    return c.owner.highLongOfPSN == me.highLongOfPSN && c.owner.lowLongOfPSN == me.lowLongOfPSN;
}

/* Call a component's entry point: (ComponentParameters *, storage). A
   component with cmpFastDispatch in its flags (QuickTime's codecs, media
   and data handlers) is entered with D0 the block's header long {flags,
   paramSize, what} and the call's parameters on the stack as the caller
   laid them, its storage in the instance's place, the result above: the
   routine its dispatcher picks is then a Pascal function of (storage,
   parameters...) that pops them and leaves the result. A0 points at the
   storage in that frame: QuickTime 3's codecs read their delegate's
   instance from their globals through it (selector -2). */
int32_t call_entry(Comp *c, Inst *in, uint32_t params)
{
    if(!c->entry)
        return badComponentSelector;
    uint32_t saved_a5 = EM_A5;
    if(in && in->a5)
        EM_A5 = in->a5;
    int32_t result;
    if(c->ppc)
        result = callPPC(c->entry, { params, in ? in->storage : 0 });
    else if((c->flags & cmpFastDispatch) && !c->native)
    {
        uint8_t size = READUB(params + 1);
        uint32_t saved_sp = EM_A7;
        PUSHUL(0); /* result */
        PUSHUL(in ? in->storage : 0);
        EM_A7 -= size;
        memmove(SYN68K_TO_US(EM_A7), SYN68K_TO_US(params + 4), size);
        uint32_t saved_d0 = EM_D0, saved_a0 = EM_A0;
        EM_D0 = READUL(params);
        EM_A0 = saved_sp - 8; /* the storage, in the frame */
        execute68K(c->entry);
        EM_D0 = saved_d0;
        EM_A0 = saved_a0;
        /* what the routine left: the result, with the stack as before its
           arguments (a routine that popped too much or too little is set
           right) */
        result = READUL(saved_sp - 4);
        EM_A7 = saved_sp;
    }
    else
    {
        PUSHUL(0); /* result */
        PUSHUL(params);
        PUSHUL(in ? in->storage : 0);
        execute68K(c->entry);
        result = POPUL();
    }
    EM_A5 = saved_a5;
    return result;
}

/* A call the Component Manager makes itself (open, close, register):
   a parameter block on the stack. */
int32_t call_select(Comp *c, Inst *in, int16_t what, const uint32_t *args, int nargs)
{
    for(int i = 0; i < nargs; i++)
        PUSHUL(args[i]);
    PUSHUW(what);
    PUSHUW(nargs * 4); /* flags 0, paramSize */
    uint32_t params = EM_A7;
    int32_t r = call_entry(c, in, params);
    EM_A7 += 4 + nargs * 4;
    return r;
}

}

int32_t Executor::ROMlib_call_component(uint32_t instance, int16_t what, std::initializer_list<ComponentArg> args)
{
    Inst *in = inst_of(instance);
    if(!in)
        return badComponentInstance;
    uint32_t size = 0;
    for(const ComponentArg &a : args)
    {
        if(a.bytes == 2)
            PUSHUW(a.value);
        else
            PUSHUL(a.value);
        size += a.bytes;
    }
    PUSHUW(what);
    PUSHUW(size); /* flags 0, paramSize */
    uint32_t params = EM_A7;
    int32_t r = call_entry(in->comp, in, params);
    EM_A7 = params + 4 + size;
    return r;
}

namespace
{
bool load_code(Comp *c)
{
    if(c->entry)
        return true;
    if(!c->from_file)
        return false;
    FSSpec spec = c->file; /* guest-visible: on this stack */
    INTEGER saved = CurResFile();
    INTEGER rn;
    {
        /* the map in the System heap, so the code loads there too */
        TheZoneGuard guard(LM(SysZone));
        rn = FSpOpenResFile(&spec, fsRdPerm);
    }
    if(rn == -1)
        return false;
    UseResFile(rn);
    Handle h;
    {
        TheZoneGuard guard(LM(SysZone));
        SetResLoad(true);
        h = Get1Resource(c->code_type, c->code_id);
        if(h)
        {
            DetachResource(h);
            HLock(h);
        }
    }
    UseResFile(saved);
    CloseResFile(rn);
    if(!h || !*h)
        return false;
    c->code = h;
    if(c->ppc)
    {
        /* MacPhoenix: a PowerPC component: its fragment's main, called as
           ComponentRoutine (the parameters and the storage, a long back) */
        GUEST<ConnectionID> conn;
        GUEST<Ptr> main;
        Str255 errname;
        OSErr err;
        {
            TheZoneGuard guard(LM(SysZone));
            err = GetMemFragment(*h, GetHandleSize(h), (StringPtr) "", kLoadNewCopy, &conn, &main,
                                 errname);
        }
        if(err != noErr || !main)
        {
            fprintf(stderr, "[Executor] component '%.4s' '%.4s': its PowerPC code doesn't prepare (%d)\n",
                    (char *)&c->type, (char *)&c->subtype, err);
            return false;
        }
        /* main is usually a routine descriptor (68k callers enter it
           through Mixed Mode, as a 68k Component Manager does); else a
           transition vector, called directly */
        c->entry = ptr_to_longint((Ptr)main);
        if(READUW(c->entry) == 0xAAFE /* _MixedModeMagic */)
            c->ppc = false;
        return true;
    }
    c->entry = US_TO_SYN68K(*h);
    return true;
}

uint32_t open_component(Comp *c, OSErr *err)
{
    *err = noErr;
    if(!load_code(c))
    {
        *err = invalidComponentID;
        return 0;
    }
    Inst in;
    in.id = kInstBase + next_inst++;
    in.comp = c;
    insts[in.id] = in;
    c->instances++;
    uint32_t args[1] = { in.id };
    int32_t r = call_select(c, &insts[in.id], kComponentOpenSelect, args, 1);
    if(r != 0)
    {
        uint32_t close_args[1] = { in.id };
        call_select(c, &insts[in.id], kComponentCloseSelect, close_args, 1);
        insts.erase(in.id);
        c->instances--;
        *err = (OSErr)r;
        return 0;
    }
    return in.id;
}

OSErr close_component(uint32_t ci)
{
    Inst *in = inst_of(ci);
    if(!in)
        return badComponentInstance & 0xFFFF;
    Comp *c = in->comp;
    uint32_t args[1] = { ci };
    int32_t r = call_select(c, in, kComponentCloseSelect, args, 1);
    insts.erase(ci);
    c->instances--;
    return (OSErr)r;
}

/* Add a component; null if a duplicate it was asked to avoid, or one
   that declined its register message. */
/* MacPhoenix: register_file's without_init: components that want their
   register message are left out. */
bool no_register_message = false;

Comp *add(std::unique_ptr<Comp> c, int16_t global, bool auto_version)
{
    if(no_register_message && (c->flags & cmpWantsRegisterMessage))
    {
        fprintf(stderr, "[Executor] component '%.4s' '%.4s': wants its register message; "
                        "left out without its INIT\n",
                (char *)&c->type, (char *)&c->subtype);
        return nullptr;
    }
    for(auto &o : comps)
        if(o->type == c->type && o->subtype == c->subtype && o->manufacturer == c->manufacturer)
        {
            if(global & registerComponentNoDuplicates)
                return nullptr;
            /* auto-versioned: only a newer version registers (and goes
               first in the search order) */
            if(auto_version && o->version >= c->version)
                return nullptr;
        }
    c->global = (global & registerComponentGlobal) != 0;
    GetCurrentProcess(&c->owner);
    c->id = kCompBase + next_comp++;
    Comp *p = c.get();
    /* MacPhoenix: a Mac (7.6.1) finds the components of one type in the
       order of their subtype, then manufacturer, the newest of equals
       first (unless registerComponentAfterExisting): QuickTime's 16 'imdc'
       come '.SGI', '8BPS' .. 'yuv2' whatever their resource IDs; the
       Sound control panel lists its 'sndP' pages ('alrt' .. 'vols') as
       FindNextComponent answers them, and Color Picker 2.0 takes the
       first 'cpkr', its HSL picker ('hsl ' before 'rgb '). */
    auto key = [](const Comp &o) { return ((uint64_t)o.subtype << 32) | o.manufacturer; };
    bool after = global & registerComponentAfterExisting;
    auto pos = std::find_if(comps.begin(), comps.end(), [&](const auto &o) {
        return o->type == p->type && (after ? key(*o) > key(*p) : key(*o) >= key(*p));
    });
    comps.insert(pos, std::move(c));
    changed(p->type);
    if(p->flags & cmpWantsRegisterMessage)
    {
        OSErr err;
        uint32_t ci = open_component(p, &err);
        int32_t r = 0;
        if(ci)
        {
            uint32_t args[1] = {};
            r = call_select(p, inst_of(ci), kComponentRegisterSelect, args, 0);
            close_component(ci);
        }
        if(!ci || r != 0)
        {
            comps.erase(std::find_if(comps.begin(), comps.end(),
                                     [p](auto &x) { return x.get() == p; }));
            return nullptr;
        }
    }
    return p;
}

/* A string or icon resource named by a ResourceSpec in the open file. */
Handle spec_resource(uint32_t type, int16_t id)
{
    if(!type)
        return nullptr;
    Handle h = Get1Resource(type, id);
    Handle c = sys_copy(h);
    if(h)
        ReleaseResource(h);
    return c;
}

void dispose(Handle h);

/* Register one 'thng' (ComponentResource, maybe extended) of the current
   resource file, which is file. */
Comp *register_thng(uint32_t tr, int16_t global, const FSSpec &file, bool allow_ppc = false)
{
    auto c = std::make_unique<Comp>();
    c->type = rd32(tr);
    c->subtype = rd32(tr + 4);
    c->manufacturer = rd32(tr + 8);
    c->flags = rd32(tr + 12);
    c->mask = rd32(tr + 16);
    c->code_type = rd32(tr + 20);
    c->code_id = rd16(tr + 24);
    c->name = spec_resource(rd32(tr + 26), rd16(tr + 30));
    c->info = spec_resource(rd32(tr + 32), rd16(tr + 36));
    c->icon = spec_resource(rd32(tr + 38), rd16(tr + 42));
    c->from_file = true;
    c->file = file;
    auto native = native_by_type.find(c->type);
    if(native != native_by_type.end())
    {
        Comp *n = native->second;
        if(!n->icon && c->icon)
        {
            std::swap(n->name, c->name);
            std::swap(n->info, c->info);
            std::swap(n->icon, c->icon);
        }
        dispose(c->name);
        dispose(c->info);
        dispose(c->icon);
        return nullptr;
    }
    bool auto_version = false;
    Handle h = RecoverHandle(guest_cast<Ptr>(tr));
    Size size = h ? GetHandleSize(h) : 44;
    if(size >= 54)
    {
        /* ExtComponentResource: version, register flags, icon family,
           then the platforms {flags, code type, id, platform}. */
        c->version = rd32(tr + 44);
        uint32_t reg_flags = rd32(tr + 48);
        auto_version = reg_flags & componentDoAutoVersion;
        if((reg_flags & componentHasMultiplePlatforms) && size >= 58)
        {
            /* MacPhoenix: the 68K platform's code; else, on a PowerPC Mac
               (Gestalt 'sysa': an 8.x System), the PowerPC platform's, a
               PEF container (8.6's AppleScript has only that) */
            int32_t n = rd32(tr + 54);
            GUEST<LONGINT> arch = 0;
            Gestalt(gestaltSysArchitecture, &arch);
            int found = 0;
            for(int32_t i = 0; i < n && 58 + (i + 1) * 12 <= size; i++)
            {
                uint32_t p = tr + 58 + i * 12;
                int16_t platform = rd16(p + 10);
                bool m68k = platform == 1 /* 68K */;
                bool ppc = platform == 2 /* PowerPC */ && arch == gestaltPowerPC && allow_ppc;
                if(m68k || (ppc && found != 1))
                {
                    c->flags = rd32(p);
                    c->code_type = rd32(p + 4);
                    c->code_id = rd16(p + 8);
                    c->ppc = ppc;
                    found = m68k ? 1 : 2;
                    if(m68k)
                        break;
                }
            }
            if(!found)
                return nullptr;
        }
    }
    return add(std::move(c), global, auto_version);
}

bool file_of(INTEGER refnum, FSSpec *spec)
{
    FCBPBRec fcb = {};
    Str255 name;
    fcb.ioNamePtr = name;
    fcb.ioRefNum = refnum;
    if(PBGetFCBInfo(&fcb, false) != noErr)
        return false;
    spec->vRefNum = fcb.ioFCBVRefNum;
    spec->parID = fcb.ioFCBParID;
    memcpy(spec->name, name, std::min<int>(name[0], 63) + 1);
    return true;
}

int32_t register_file(INTEGER refnum, int16_t global)
{
    FSSpec file;
    if(!file_of(refnum, &file))
        return 0;
    INTEGER saved = CurResFile();
    UseResFile(refnum);
    int32_t count = 0;
    INTEGER n = Count1Resources("thng"_4);
    std::vector<Handle> things;
    for(INTEGER i = 1; i <= n; i++)
        if(Handle h = Get1IndResource("thng"_4, i))
            things.push_back(h);
    /* in the file's order, which is the search order (see add): Color
       Picker 2.0 takes the first 'cpkr' that FindNextComponent answers,
       and on 7.5.5 that is its HSL picker (thng 2020), not the RGB one
       (3020). */
    for(Handle h : things)
    {
        HLock(h);
        if(register_thng(US_TO_SYN68K(*h), global, file, refnum != LM(SysMap)))
            count++;
        HUnlock(h);
    }
    UseResFile(saved);
    return count;
}

void dispose(Handle h)
{
    if(h)
        DisposeHandle(h);
}

OSErr unregister(Comp *c)
{
    if(c->instances)
        return -3002; /* validInstancesExist */
    changed(c->type);
    dispose(c->name);
    dispose(c->info);
    dispose(c->icon);
    dispose(c->code);
    comps.erase(std::find_if(comps.begin(), comps.end(),
                             [c](auto &x) { return x.get() == c; }));
    return noErr;
}

void copy_into(Handle from, uint32_t to)
{
    if(!to)
        return;
    Handle h = guest_cast<Handle>(to);
    if(!from || !*from)
    {
        SetHandleSize(h, 0);
        return;
    }
    Size n = GetHandleSize(from);
    SetHandleSize(h, n);
    if(MemError() == noErr)
        memcpy(*h, *from, n);
}
}

namespace
{
/* The routines Executor's components answer, by the definitions'
   dispatcher name and selector (COMPONENT_FUNCTION). */
struct ComponentRoutine
{
    const char *name;
    std::function<int32_t (syn68k_addr_t)> code;
};
std::map<std::string, std::map<int16_t, ComponentRoutine>> &component_routines()
{
    static std::map<std::string, std::map<int16_t, ComponentRoutine>> routines;
    return routines;
}

struct NativeComponent
{
    const char *calls;
    int32_t version;
};

int32_t native_call(const NativeComponent &nc, uint32_t params)
{
    int16_t what = (int16_t)READUW(params + 2);
    const auto &routines = component_routines()[nc.calls];
    auto r = routines.find(what);
    if(r != routines.end())
        return r->second.code(params);
    switch(what)
    {
        case kComponentOpenSelect:
        case kComponentCloseSelect:
        case kComponentRegisterSelect:
            return noErr;
        case kComponentVersionSelect:
            return nc.version;
        case kComponentCanDoSelect:
        {
            int16_t which = (int16_t)READUW(params + 4);
            return which == kComponentOpenSelect || which == kComponentCloseSelect
                || which == kComponentCanDoSelect || which == kComponentVersionSelect
                || which == kComponentRegisterSelect || routines.count(which);
        }
    }
    char sel[8];
    snprintf(sel, sizeof sel, "%04X", (uint16_t)what);
    traps::log_unimplemented(std::string(nc.calls) + " selector $" + sel
                             + ": answered badComponentSelector");
    return badComponentSelector;
}

/* A native component's entry point, called as a component's code is
   (call_entry): pascal ComponentResult (ComponentParameters *, Handle). */
syn68k_addr_t native_entry(syn68k_addr_t, void *arg)
{
    auto *nc = (const NativeComponent *)arg;
    syn68k_addr_t ret = POPADDR();
    POPUL(); /* storage: the routines ask the instance */
    uint32_t params = POPUL();
    WRITEUL(EM_A7, (uint32_t)native_call(*nc, params));
    return ret;
}

Handle sys_pstring(const char *s)
{
    TheZoneGuard guard(LM(SysZone));
    size_t n = strlen(s);
    Handle h = NewHandle(n + 1);
    if(h)
    {
        **h = n;
        memcpy(*h + 1, s, n);
    }
    return h;
}
}

uint32_t Executor::ROMlib_register_native_component(uint32_t type, uint32_t subtype,
                                                    uint32_t manufacturer, const char *name,
                                                    const char *calls, int32_t version, uint32_t flags)
{
    auto c = std::make_unique<Comp>();
    c->type = type;
    c->subtype = subtype;
    c->manufacturer = manufacturer;
    c->flags = flags;
    c->native = true;
    c->entry = callback_install(native_entry, new NativeComponent{ calls, version });
    c->version = version;
    c->name = sys_pstring(name);
    Comp *p = add(std::move(c), registerComponentGlobal, false);
    if(!p)
        return 0;
    native_by_type[type] = p;
    return p->id;
}

int32_t Executor::ROMlib_register_components(INTEGER refnum, bool global, bool without_init)
{
    no_register_message = without_init;
    int32_t n = register_file(refnum, global ? registerComponentGlobal : 0);
    no_register_message = false;
    return n;
}

void Executor::ROMlib_components_process_exit(const ProcessSerialNumber *psn,
                                               uint32_t part_lo, uint32_t part_hi)
{
    for(size_t i = comps.size(); i-- > 0;)
    {
        Comp *c = comps[i].get();
        bool owned = !c->global && c->owner.highLongOfPSN == psn->highLongOfPSN
            && c->owner.lowLongOfPSN == psn->lowLongOfPSN;
        /* MacPhoenix: a global component whose code was in the partition
           is gone with it (Frontier's, registered globally from its own
           heap: NetEvents opened it after Frontier quit and jumped into
           freed memory). */
        bool code_gone = part_hi > part_lo && c->entry >= part_lo && c->entry < part_hi;
        if(owned || code_gone)
        {
            c->instances = 0;
            unregister(c);
        }
    }
}

/* ---- the Component Manager's calls (Components.yaml) ----------------------- */

namespace
{
/* Components and instances are opaque IDs; the generated declarations type
   them as pointers, which carry the ID as their guest address. */
uint32_t gid(const void *p) { return p ? US_TO_SYN68K(p) : 0; }
template<class T>
T as(uint32_t v) { return v ? ptr_from_longint<T>(v) : nullptr; }
}

Component Executor::C_RegisterComponent(ComponentDescription *cdp, ComponentRoutineUPP componentEntryPoint,
                                        INTEGER global, Handle componentName, Handle componentInfo,
                                        Handle componentIcon)
{
    uint32_t cd = gid(cdp);
    auto c = std::make_unique<Comp>();
    c->type = rd32(cd);
    c->subtype = rd32(cd + 4);
    c->manufacturer = rd32(cd + 8);
    c->flags = rd32(cd + 12);
    c->mask = rd32(cd + 16);
    c->entry = gid((const void *)componentEntryPoint);
    c->name = sys_copy(componentName);
    c->info = sys_copy(componentInfo);
    c->icon = sys_copy(componentIcon);
    Comp *p = add(std::move(c), global, false);
    return as<Component>(p ? p->id : 0);
}

OSErr Executor::C_UnregisterComponent(Component aComponent)
{
    Comp *c = comp_of(gid(aComponent));
    return c ? unregister(c) : invalidComponentID;
}

LONGINT Executor::C_CountComponents(ComponentDescription *looking)
{
    int32_t n = 0;
    for(auto &c : comps)
        if(visible(*c) && matches(*c, gid(looking)))
            n++;
    return n;
}

Component Executor::C_FindNextComponent(Component aComponent, ComponentDescription *looking)
{
    uint32_t after = gid(aComponent);
    size_t i = 0;
    if(after)
    {
        while(i < comps.size() && comps[i]->id != after)
            i++;
        i++;
    }
    for(; i < comps.size(); i++)
        if(visible(*comps[i]) && matches(*comps[i], gid(looking)))
            return as<Component>(comps[i]->id);
    return nullptr;
}

OSErr Executor::C_GetComponentInfo(Component aComponent, ComponentDescription *cdp, Handle componentName,
                                   Handle componentInfo, Handle componentIcon)
{
    Comp *c = comp_of(gid(aComponent));
    if(!c)
        return invalidComponentID;
    if(uint32_t cd = gid(cdp))
    {
        WRITEUL(cd, c->type);
        WRITEUL(cd + 4, c->subtype);
        WRITEUL(cd + 8, c->manufacturer);
        WRITEUL(cd + 12, c->flags);
        WRITEUL(cd + 16, c->mask);
    }
    copy_into(c->name, gid(componentName));
    copy_into(c->info, gid(componentInfo));
    copy_into(c->icon, gid(componentIcon));
    return noErr;
}

/* Private (Gestalt 'cpnt' on 7.5.5 calls it); name and value guessed. */
LONGINT Executor::C_GetComponentMgrVersion()
{
    return 0x00030000;
}

LONGINT Executor::C_GetComponentListModSeed()
{
    return list_seed;
}

LONGINT Executor::C_GetComponentTypeModSeed(OSType componentType)
{
    return type_seeds[componentType];
}

ComponentInstance Executor::C_OpenComponent(Component aComponent)
{
    Comp *c = comp_of(gid(aComponent));
    OSErr err;
    return as<ComponentInstance>(c ? open_component(c, &err) : 0);
}

OSErr Executor::C_OpenAComponent(Component aComponent, GUEST<ComponentInstance> *ci)
{
    Comp *c = comp_of(gid(aComponent));
    OSErr err = invalidComponentID;
    uint32_t in = c ? open_component(c, &err) : 0;
    WRITEUL(gid(ci), in);
    return err;
}

OSErr Executor::C_CloseComponent(ComponentInstance aComponentInstance)
{
    return close_component(gid(aComponentInstance));
}

OSErr Executor::C_GetComponentInstanceError(ComponentInstance aComponentInstance)
{
    Inst *in = inst_of(gid(aComponentInstance));
    return in ? in->error : (OSErr)(badComponentInstance & 0xFFFF);
}

void Executor::C_SetComponentInstanceError(ComponentInstance aComponentInstance, OSErr theError)
{
    if(Inst *in = inst_of(gid(aComponentInstance)))
        in->error = theError;
}

Handle Executor::C_GetComponentInstanceStorage(ComponentInstance aComponentInstance)
{
    Inst *in = inst_of(gid(aComponentInstance));
    return as<Handle>(in ? in->storage : 0);
}

void Executor::C_SetComponentInstanceStorage(ComponentInstance aComponentInstance, Handle theStorage)
{
    if(Inst *in = inst_of(gid(aComponentInstance)))
        in->storage = gid(theStorage);
}

LONGINT Executor::C_GetComponentInstanceA5(ComponentInstance aComponentInstance)
{
    Inst *in = inst_of(gid(aComponentInstance));
    return in ? in->a5 : 0;
}

void Executor::C_SetComponentInstanceA5(ComponentInstance aComponentInstance, LONGINT theA5)
{
    if(Inst *in = inst_of(gid(aComponentInstance)))
        in->a5 = theA5;
}

LONGINT Executor::C_GetComponentRefcon(Component aComponent)
{
    Comp *c = comp_of(gid(aComponent));
    return c ? c->refcon : 0;
}

void Executor::C_SetComponentRefcon(Component aComponent, LONGINT theRefcon)
{
    if(Comp *c = comp_of(gid(aComponent)))
        c->refcon = theRefcon;
}

Component Executor::C_RegisterComponentResource(ComponentResourceHandle crh, INTEGER global)
{
    Handle cr = (Handle)crh;
    Comp *p = nullptr;
    FSSpec file;
    INTEGER home = cr ? HomeResFile(cr) : -1;
    if(cr && *cr && file_of(home, &file))
    {
        INTEGER saved = CurResFile();
        UseResFile(home);
        HLock(cr);
        p = register_thng(US_TO_SYN68K(*cr), global, file);
        HUnlock(cr);
        UseResFile(saved);
    }
    return as<Component>(p ? p->id : 0);
}

LONGINT Executor::C_CountComponentInstances(Component aComponent)
{
    Comp *c = comp_of(gid(aComponent));
    return c ? c->instances : 0;
}

LONGINT Executor::C_RegisterComponentResourceFile(INTEGER resRefNum, INTEGER global)
{
    return register_file(resRefNum, global);
}

namespace
{
/* A component's resource file, opened for it: how many opens are open, and
   whether the file was open before the first (then it is never closed
   here). QuickTime 3's codecs share the QuickTime file: OpenResFile gives
   each the same refnum, and one closing it freed what another had loaded
   from it (rpza's band ran code where a resource map had been put). */
struct ResFileUse
{
    int opens = 0;
    bool was_open = false;
};
std::map<INTEGER, ResFileUse> res_file_uses;

INTEGER open_res_file(Component aComponent)
{
    Comp *c = comp_of(gid(aComponent));
    if(!c || !c->from_file)
        return -1;
    std::vector<INTEGER> open_before;
    resmaphand map;
    WALKMAPTOP(map)
        open_before.push_back((*map)->resfn);
    EWALKMAP()
    FSSpec spec = c->file;
    INTEGER rn = FSpOpenResFile(&spec, fsRdPerm);
    if(rn != -1)
    {
        ResFileUse &u = res_file_uses[rn];
        if(u.opens++ == 0)
            u.was_open = std::find(open_before.begin(), open_before.end(), rn) != open_before.end();
    }
    return rn;
}
}

INTEGER Executor::C_OpenComponentResFile(Component aComponent)
{
    return open_res_file(aComponent);
}

OSErr Executor::C_OpenAComponentResFile(Component aComponent, GUEST<INTEGER> *resRef)
{
    INTEGER rn = open_res_file(aComponent);
    if(resRef)
        *resRef = rn;
    return rn == -1 ? (OSErr)resFNotFound : noErr;
}

OSErr Executor::C_CloseComponentResFile(INTEGER refnum)
{
    auto it = res_file_uses.find(refnum);
    if(it != res_file_uses.end())
    {
        if(--it->second.opens > 0)
            return noErr;
        bool was_open = it->second.was_open;
        res_file_uses.erase(it);
        if(was_open)
            return noErr;
    }
    CloseResFile(refnum);
    return ResError();
}

Component Executor::C_CaptureComponent(Component capturedComponent, Component capturingComponent)
{
    Comp *by = comp_of(gid(capturingComponent));
    Comp *c = comp_of(gid(capturedComponent));
    if(c && by)
        c->captured_by = by;
    return as<Component>(c ? c->id : 0);
}

OSErr Executor::C_UncaptureComponent(Component aComponent)
{
    Comp *c = comp_of(gid(aComponent));
    if(c)
        c->captured_by = nullptr;
    return c ? noErr : invalidComponentID;
}

/* First in the search order. */
OSErr Executor::C_SetDefaultComponent(Component aComponent, INTEGER)
{
    Comp *c = comp_of(gid(aComponent));
    if(c)
    {
        auto it = std::find_if(comps.begin(), comps.end(), [c](auto &x) { return x.get() == c; });
        std::rotate(comps.begin(), it, it + 1);
    }
    return c ? noErr : invalidComponentID;
}

/* No aliases here. */
Component Executor::C_ResolveComponentAlias(Component aComponent)
{
    return aComponent;
}

namespace
{
uint32_t open_default(OSType type, OSType subtype, OSErr *err)
{
    *err = invalidComponentID;
    for(auto &c : comps)
        if(visible(*c) && c->type == type && (!subtype || c->subtype == subtype))
            return open_component(c.get(), err);
    return 0;
}
}

ComponentInstance Executor::C_OpenDefaultComponent(OSType componentType, OSType componentSubType)
{
    OSErr err;
    return as<ComponentInstance>(open_default(componentType, componentSubType, &err));
}

OSErr Executor::C_OpenADefaultComponent(OSType componentType, OSType componentSubType,
                                        GUEST<ComponentInstance> *ci)
{
    OSErr err;
    uint32_t in = open_default(componentType, componentSubType, &err);
    if(ci)
        WRITEUL(gid(ci), in);
    return err;
}

ComponentResult Executor::C_DelegateComponentCall(ComponentParameters *originalParams, ComponentInstance ci)
{
    Inst *in = inst_of(gid(ci));
    return in ? call_entry(in->comp, in, gid(originalParams)) : (ComponentResult)badComponentInstance;
}

OSErr Executor::C_GetComponentResource(Component aComponent, OSType resType, INTEGER resID,
                                       GUEST<Handle> *theResource)
{
    Comp *c = comp_of(gid(aComponent));
    OSErr err = resNotFound;
    if(theResource)
        *theResource = nullptr;
    if(c && c->from_file)
    {
        FSSpec spec = c->file;
        INTEGER saved = CurResFile();
        INTEGER rn = FSpOpenResFile(&spec, fsRdPerm);
        if(rn != -1)
        {
            UseResFile(rn);
            Handle h = Get1Resource(resType, resID);
            if(h)
            {
                DetachResource(h);
                if(theResource)
                    *theResource = h;
                err = noErr;
            }
            UseResFile(saved);
            CloseResFile(rn);
        }
    }
    return err;
}

/* CallComponent (selector 0) and CallComponentFunction[WithStorage] (-1)
   reshape the caller's stack, so they are raw handlers on the same
   dispatcher. CallComponent: from the top, the parameter block header
   {flags.b, paramSize.b, what.w}, paramSize bytes of parameters, the
   instance and the result. CallComponentFunction: the parameters are
   copied onto the stack and func is jumped to. */
namespace
{
syn68k_addr_t call_component(syn68k_addr_t)
{
    uint32_t ret = POPADDR();
    uint32_t params = EM_A7;
    uint8_t size = READUB(params + 1);
    uint32_t ci = rd32(params + 4 + size);
    int32_t r;
    if(Inst *in = inst_of(ci))
        r = call_entry(in->comp, in, params);
    else if(Comp *c = comp_of(ci))
    {
        /* MacPhoenix: a component's ID where an instance is due: its open
           instance, as the Sound control panel calls the output device the
           Sound Manager keeps open; a native component needs none. */
        Inst *open = nullptr;
        for(auto &[id, i] : insts)
            if(i.comp == c)
            {
                open = &i;
                break;
            }
        if(open)
            r = call_entry(c, open, params);
        else if(native_by_type.count(c->type) && native_by_type[c->type] == c)
            r = call_entry(c, nullptr, params);
        else
            r = badComponentInstance;
    }
    else
        r = badComponentInstance;
    EM_A7 += 4 + size + 4;
    WRITEUL(EM_A7, r);
    return ret;
}

syn68k_addr_t call_component_function(syn68k_addr_t)
{
    uint32_t ret = POPADDR();
    uint32_t func = POPUL();
    uint32_t params = POPUL();
    uint8_t size = READUB(params + 1);
    EM_A7 -= size;
    memmove(SYN68K_TO_US(EM_A7), SYN68K_TO_US(params + 4), size);
    PUSHADDR(ret);
    return func;
}

/* Selector -2: a QuickTime 3 codec's dispatcher, for a call it doesn't
   answer itself, pushes the header long {flags, paramSize, what} and its
   delegate's instance (its globals' second long) and calls
   _ComponentDispatch -2. Under them is its own fast-dispatch frame: the
   return address, the parameters, the storage and the result. The call
   goes to the delegate with those parameters, its result into that slot,
   and returns to the frame's caller with the frame popped, as if the
   codec had answered. Only if the instance isn't one does it return to
   the codec, its two arguments popped (the codec then answers
   badComponentSelector). */
syn68k_addr_t delegate_fast(syn68k_addr_t)
{
    uint32_t ret = POPADDR();
    uint32_t ci = POPUL();
    uint32_t header = POPUL();
    Inst *in = inst_of(ci);
    if(!in)
    {
        warning_unexpected("ComponentDispatch -2: %08x isn't an instance", ci);
        return ret;
    }
    uint32_t frame_ret = POPADDR();
    uint8_t size = (header >> 16) & 0xFF;
    uint32_t slot = EM_A7 + size + 4;
    PUSHUL(header); /* the parameters, in place, behind the header */
    uint32_t params = EM_A7;
    int32_t r = call_entry(in->comp, in, params);
    WRITEUL(slot, r);
    EM_A7 = slot;
    return frame_ret;
}

/* MacPhoenix: CallComponentFunction[WithStorage] for PowerPC callers
   (Components.yaml): the block's arguments onto the 68k stack as the
   68k call lays them (the first argument highest; the storage above
   them), room for the result, and the routine entered there: a routine
   descriptor takes them through Mixed Mode. */
static ComponentResult call_function(uint32_t storage, bool with_storage, ComponentParameters *params,
                                     UniversalProcPtr func)
{
    if(!params || !func)
        return badComponentSelector;
    uint8_t size = params->paramSize;
    uint32_t saved_sp = EM_A7;
    PUSHUL(0); /* result */
    if(with_storage)
        PUSHUL(storage);
    EM_A7 -= size;
    memmove(SYN68K_TO_US(EM_A7), (uint8_t *)params + 4, size);
    execute68K(ptr_to_longint((Ptr)func));
    ComponentResult r = POPUL();
    EM_A7 = saved_sp;
    return r;
}


traps::Entrypoint call_component_ep("CallComponent");
traps::Entrypoint call_component_function_ep("CallComponentFunction");
traps::Entrypoint delegate_fast_ep("ComponentDispatch -2");

struct RawSelectors : traps::internal::DeferredInit
{
    void init() override
    {
        ComponentDispatch.addSelector(0, &call_component_ep, call_component);
        ComponentDispatch.addSelector(0xFFFF, &call_component_function_ep, call_component_function);
        ComponentDispatch.addSelector(0xFFFE, &delegate_fast_ep, delegate_fast);
    }
} raw_selectors;
}

void Executor::traps::registerComponentFunction(const char *component, int16_t what, const char *name,
                                                std::function<int32_t (syn68k_addr_t)> code)
{
    component_routines()[component][what] = ComponentRoutine{ name, std::move(code) };
}

ComponentResult Executor::C_CallComponentFunction(ComponentParameters *params, UniversalProcPtr func)
{
    return call_function(0, false, params, func);
}

ComponentResult Executor::C_CallComponentFunctionWithStorage(Handle storage, ComponentParameters *params,
                                                             UniversalProcPtr func)
{
    return call_function(ptr_to_longint(storage), true, params, func);
}
