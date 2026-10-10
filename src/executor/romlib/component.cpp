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

#include <algorithm>
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
    cmpWantsRegisterMessage = 0x80000000,
    registerComponentGlobal = 1,
    registerComponentNoDuplicates = 2,
    registerComponentAfterExisting = 4,
    componentDoAutoVersion = 1,
    componentHasMultiplePlatforms = 8,
    badComponentInstance = 0x80008001,
    badComponentSelector = 0x80008002,
};
enum : int16_t
{
    kComponentOpenSelect = -1,
    kComponentCloseSelect = -2,
    kComponentRegisterSelect = -5,
};
const OSErr invalidComponentID = -3000;

uint32_t rd32(uint32_t a) { return READUL(a); }
uint16_t rd16(uint32_t a) { return READUW(a); }

struct Comp
{
    uint32_t id;
    uint32_t type, subtype, manufacturer, flags, mask;
    uint32_t entry = 0;     /* guest code; 0 until loaded */
    Handle code = nullptr;  /* loaded from the resource file */
    bool from_file = false;
    FSSpec file = {};
    uint32_t code_type = 0;
    int16_t code_id = 0;
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

/* Call a component's entry point: (ComponentParameters *, storage). */
int32_t call_entry(Comp *c, Inst *in, uint32_t params)
{
    if(!c->entry)
        return badComponentSelector;
    uint32_t saved_a5 = EM_A5;
    if(in && in->a5)
        EM_A5 = in->a5;
    PUSHUL(0); /* result */
    PUSHUL(params);
    PUSHUL(in ? in->storage : 0);
    execute68K(c->entry);
    int32_t result = POPUL();
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
Comp *add(std::unique_ptr<Comp> c, int16_t global, bool auto_version)
{
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
    if(global & registerComponentAfterExisting)
        comps.push_back(std::move(c));
    else
        comps.insert(comps.begin(), std::move(c));
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

/* Register one 'thng' (ComponentResource, maybe extended) of the current
   resource file, which is file. */
Comp *register_thng(uint32_t tr, int16_t global, const FSSpec &file)
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
            int32_t n = rd32(tr + 54);
            bool found = false;
            for(int32_t i = 0; i < n && 58 + (i + 1) * 12 <= size; i++)
            {
                uint32_t p = tr + 58 + i * 12;
                if(rd16(p + 10) == 1) /* 68K */
                {
                    c->flags = rd32(p);
                    c->code_type = rd32(p + 4);
                    c->code_id = rd16(p + 8);
                    found = true;
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
    /* MacPhoenix: last first, so that with each going to the head of the
       search order the file's resources keep their order: Color Picker
       2.0 takes the first 'cpkr' that FindNextComponent answers, and on
       7.5.5 that is its HSL picker (thng 2020), not the RGB one (3020). */
    std::reverse(things.begin(), things.end());
    for(Handle h : things)
    {
        HLock(h);
        if(register_thng(US_TO_SYN68K(*h), global, file))
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

int32_t Executor::ROMlib_register_components(INTEGER refnum, bool global)
{
    return register_file(refnum, global ? registerComponentGlobal : 0);
}

void Executor::ROMlib_components_process_exit(const ProcessSerialNumber *psn)
{
    for(size_t i = comps.size(); i-- > 0;)
    {
        Comp *c = comps[i].get();
        if(!c->global && c->owner.highLongOfPSN == psn->highLongOfPSN
           && c->owner.lowLongOfPSN == psn->lowLongOfPSN)
        {
            c->instances = 0;
            unregister(c);
        }
    }
}

RAW_68K_IMPLEMENTATION(ComponentDispatch)
{
    uint32_t ret = POPADDR();
    int16_t sel = (int16_t)EM_D0;
    auto res32 = [](uint32_t v) { WRITEUL(EM_A7, v); };
    auto res16 = [](uint16_t v) { WRITEUW(EM_A7, v); };

    switch(sel)
    {
        case 0: /* CallComponent */
        {
            uint32_t params = EM_A7;
            uint8_t size = READUB(params + 1);
            uint32_t ci = rd32(params + 4 + size);
            int32_t r;
            if(Inst *in = inst_of(ci))
                r = call_entry(in->comp, in, params);
            else
                r = badComponentInstance;
            EM_A7 += 4 + size + 4;
            res32(r);
            break;
        }
        case -1: /* CallComponentFunction[WithStorage] */
        {
            uint32_t func = POPUL();
            uint32_t params = POPUL();
            uint8_t size = READUB(params + 1);
            EM_A7 -= size;
            memmove(SYN68K_TO_US(EM_A7), SYN68K_TO_US(params + 4), size);
            PUSHADDR(ret);
            return func;
        }
        case 0x01: /* RegisterComponent */
        {
            uint32_t icon = POPUL(), info = POPUL(), name = POPUL();
            int16_t global = POPUW();
            uint32_t entry = POPUL(), cd = POPUL();
            auto c = std::make_unique<Comp>();
            c->type = rd32(cd);
            c->subtype = rd32(cd + 4);
            c->manufacturer = rd32(cd + 8);
            c->flags = rd32(cd + 12);
            c->mask = rd32(cd + 16);
            c->entry = entry;
            c->name = sys_copy(guest_cast<Handle>(name));
            c->info = sys_copy(guest_cast<Handle>(info));
            c->icon = sys_copy(guest_cast<Handle>(icon));
            Comp *p = add(std::move(c), global, false);
            res32(p ? p->id : 0);
            break;
        }
        case 0x02: /* UnregisterComponent */
        {
            Comp *c = comp_of(POPUL());
            res16(c ? unregister(c) : invalidComponentID);
            break;
        }
        case 0x03: /* CountComponents */
        {
            uint32_t looking = POPUL();
            int32_t n = 0;
            for(auto &c : comps)
                if(visible(*c) && matches(*c, looking))
                    n++;
            res32(n);
            break;
        }
        case 0x04: /* FindNextComponent */
        {
            uint32_t looking = POPUL(), after = POPUL();
            size_t i = 0;
            if(after)
            {
                while(i < comps.size() && comps[i]->id != after)
                    i++;
                i++;
            }
            uint32_t found = 0;
            for(; i < comps.size(); i++)
                if(visible(*comps[i]) && matches(*comps[i], looking))
                {
                    found = comps[i]->id;
                    break;
                }
            res32(found);
            break;
        }
        case 0x05: /* GetComponentInfo */
        {
            uint32_t icon = POPUL(), info = POPUL(), name = POPUL(), cd = POPUL();
            Comp *c = comp_of(POPUL());
            if(!c)
            {
                res16(invalidComponentID);
                break;
            }
            if(cd)
            {
                WRITEUL(cd, c->type);
                WRITEUL(cd + 4, c->subtype);
                WRITEUL(cd + 8, c->manufacturer);
                WRITEUL(cd + 12, c->flags);
                WRITEUL(cd + 16, c->mask);
            }
            copy_into(c->name, name);
            copy_into(c->info, info);
            copy_into(c->icon, icon);
            res16(noErr);
            break;
        }
        case 0x19: /* the Component Manager's version (private; Gestalt
                      'cpnt' on 7.5.5 calls this; name and value guessed) */
            res32(0x00030000);
            break;
        case 0x06: /* GetComponentListModSeed */
            res32(list_seed);
            break;
        case 0x2C: /* GetComponentTypeModSeed */
            res32(type_seeds[POPUL()]);
            break;
        case 0x07: /* OpenComponent */
        {
            Comp *c = comp_of(POPUL());
            OSErr err;
            res32(c ? open_component(c, &err) : 0);
            break;
        }
        case 0x2D: /* OpenAComponent */
        {
            uint32_t out = POPUL();
            Comp *c = comp_of(POPUL());
            OSErr err = invalidComponentID;
            uint32_t ci = c ? open_component(c, &err) : 0;
            WRITEUL(out, ci);
            res16(err);
            break;
        }
        case 0x08: /* CloseComponent */
            res16(close_component(POPUL()));
            break;
        case 0x0A: /* GetComponentInstanceError */
        {
            Inst *in = inst_of(POPUL());
            res16(in ? in->error : (OSErr)(badComponentInstance & 0xFFFF));
            break;
        }
        case 0x0B: /* SetComponentInstanceError */
        {
            int16_t err = POPUW();
            if(Inst *in = inst_of(POPUL()))
                in->error = err;
            break;
        }
        case 0x0C: /* GetComponentInstanceStorage */
        {
            Inst *in = inst_of(POPUL());
            res32(in ? in->storage : 0);
            break;
        }
        case 0x0D: /* SetComponentInstanceStorage */
        {
            uint32_t h = POPUL();
            if(Inst *in = inst_of(POPUL()))
                in->storage = h;
            break;
        }
        case 0x0E: /* GetComponentInstanceA5 */
        {
            Inst *in = inst_of(POPUL());
            res32(in ? in->a5 : 0);
            break;
        }
        case 0x0F: /* SetComponentInstanceA5 */
        {
            uint32_t a5 = POPUL();
            if(Inst *in = inst_of(POPUL()))
                in->a5 = a5;
            break;
        }
        case 0x10: /* GetComponentRefcon */
        {
            Comp *c = comp_of(POPUL());
            res32(c ? c->refcon : 0);
            break;
        }
        case 0x11: /* SetComponentRefcon */
        {
            int32_t refcon = POPUL();
            if(Comp *c = comp_of(POPUL()))
                c->refcon = refcon;
            break;
        }
        case 0x12: /* RegisterComponentResource */
        {
            int16_t global = POPUW();
            Handle cr = guest_cast<Handle>(POPUL());
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
            res32(p ? p->id : 0);
            break;
        }
        case 0x13: /* CountComponentInstances */
        {
            Comp *c = comp_of(POPUL());
            res32(c ? c->instances : 0);
            break;
        }
        case 0x14: /* RegisterComponentResourceFile */
        {
            int16_t global = POPUW();
            INTEGER refnum = POPUW();
            res32(register_file(refnum, global));
            break;
        }
        case 0x15: /* OpenComponentResFile */
        case 0x2F: /* OpenAComponentResFile */
        {
            uint32_t out = sel == 0x2F ? POPUL() : 0;
            Comp *c = comp_of(POPUL());
            INTEGER rn = -1;
            if(c && c->from_file)
            {
                FSSpec spec = c->file;
                rn = FSpOpenResFile(&spec, fsRdPerm);
            }
            if(sel == 0x2F)
            {
                if(out)
                    WRITEUW(out, rn);
                res16(rn == -1 ? (OSErr)resFNotFound : noErr);
            }
            else
                res16(rn);
            break;
        }
        case 0x18: /* CloseComponentResFile */
        {
            INTEGER rn = POPUW();
            CloseResFile(rn);
            res16(ResError());
            break;
        }
        case 0x1C: /* CaptureComponent */
        {
            Comp *by = comp_of(POPUL());
            Comp *c = comp_of(POPUL());
            if(c && by)
                c->captured_by = by;
            res32(c ? c->id : 0);
            break;
        }
        case 0x1D: /* UncaptureComponent */
        {
            Comp *c = comp_of(POPUL());
            if(c)
                c->captured_by = nullptr;
            res16(c ? noErr : invalidComponentID);
            break;
        }
        case 0x1E: /* SetDefaultComponent: first in the search order */
        {
            POPUW();
            Comp *c = comp_of(POPUL());
            if(c)
            {
                auto it = std::find_if(comps.begin(), comps.end(),
                                       [c](auto &x) { return x.get() == c; });
                std::rotate(comps.begin(), it, it + 1);
            }
            res16(c ? noErr : invalidComponentID);
            break;
        }
        case 0x20: /* ResolveComponentAlias: no aliases here */
        {
            uint32_t v = POPUL();
            res32(v);
            break;
        }
        case 0x21: /* OpenDefaultComponent */
        case 0x2E: /* OpenADefaultComponent */
        {
            uint32_t out = sel == 0x2E ? POPUL() : 0;
            uint32_t subtype = POPUL(), type = POPUL();
            uint32_t ci = 0;
            OSErr err = invalidComponentID;
            for(auto &c : comps)
                if(visible(*c) && c->type == type && (!subtype || c->subtype == subtype))
                {
                    ci = open_component(c.get(), &err);
                    break;
                }
            if(sel == 0x2E)
            {
                if(out)
                    WRITEUL(out, ci);
                res16(err);
            }
            else
                res32(ci);
            break;
        }
        case 0x24: /* DelegateComponentCall */
        {
            uint32_t ci = POPUL(), params = POPUL();
            Inst *in = inst_of(ci);
            res32(in ? call_entry(in->comp, in, params) : badComponentInstance);
            break;
        }
        case 0x35: /* GetComponentResource */
        {
            uint32_t out = POPUL();
            int16_t id = POPUW();
            uint32_t type = POPUL();
            Comp *c = comp_of(POPUL());
            OSErr err = resNotFound;
            if(out)
                WRITEUL(out, 0);
            if(c && c->from_file)
            {
                FSSpec spec = c->file;
                INTEGER saved = CurResFile();
                INTEGER rn = FSpOpenResFile(&spec, fsRdPerm);
                if(rn != -1)
                {
                    UseResFile(rn);
                    Handle h = Get1Resource(type, id);
                    if(h)
                    {
                        DetachResource(h);
                        WRITEUL(out, US_TO_SYN68K(h));
                        err = noErr;
                    }
                    UseResFile(saved);
                    CloseResFile(rn);
                }
            }
            res16(err);
            break;
        }
        default:
            fprintf(stderr, "[Executor] ComponentDispatch: selector %d not implemented\n", sel);
            gui_fatal("ComponentDispatch selector 0x%x", (unsigned)(uint16_t)sel);
    }
    return ret;
}
