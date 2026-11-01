/* Copyright 2000 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/*
 * In addition to fleshing this out, it probably makes sense to use mmap
 * to pull in certain sections (including temporarily the loader section).
 */

#include <base/common.h>

#include <assert.h>

#include <FileMgr.h>
#include <OSUtil.h>
#include <MemoryMgr.h>
#include <SegmentLdr.h>
#include <AliasMgr.h>
#include <ResourceMgr.h>

#include <rsys/cfm.h>
#include <base/traps.h>
#include <rsys/opentransport.h>
#include <PEFBinaryFormat.h>
#include <file/file.h>
#include <rsys/launch.h>
#include <rsys/process.h>
#include <mman/mman.h>
#include <hfs/hfs.h>
#include <util/string.h>
#include <base/builtinlibs.h>
#include <res/resource.h>
#include <base/cpu.h>
#include <clib/clib.h>
#include <algorithm>
#include <string>
#include <vector>
#include <strings.h>
#include <rsys/ppc_break.h>

using namespace Executor;

typedef int share_kind_t;

/* MacPhoenix: the fragments prepared so far (docs/executor/CFM68K.md). A
   library is prepared once per application and shared by its importers;
   the list is per application zone, and its term routines run at
   ExitToShell. */
namespace
{
struct load_origin
{
    uint32_t where; /* kInMem, kOnDiskFlat */
    FSSpec file;
    uint32_t offset;
    uint32_t length;
    Ptr mem;
    std::string name;
};

struct loaded_fragment
{
    load_origin origin;
    OSType arch;
    ConnectionID cid;
    THz zone;
    uint32_t term;
    bool terminated;
    /* MacPhoenix: what CloseConnection gives back. Connections to it (the
       loader's, a share, an importer's closure); the container it was
       prepared from (sections used in place point into it), given back
       with it when read in by GetDiskFragment; its closure: the libraries
       it imports, released with it. */
    uint32_t refs = 1;
    Ptr image = nullptr;
    uint32_t image_length = 0;
    bool owns_image = false;
    CFragClosureID closure = nullptr;
};

std::vector<loaded_fragment> loaded;
const load_origin *current_origin;

/* MacPhoenix: Executor's own libraries GetSharedLibrary has connected an
   application to (its imports, its own calls), which kFindLib finds. */
struct builtin_connection
{
    std::string name;
    OSType arch;
    ConnectionID cid;
    THz zone;
};
std::vector<builtin_connection> builtin_connections;

/* MacPhoenix: init routines run once the whole closure is prepared, as
   Apple's CFM does: libraries that import each other (8.x's ToolboxUtils,
   AppearanceLib, PreferencesLib, ...) are all relocated before any of them
   runs. In the order their preparation finished: imports first. */
struct pending_init
{
    uint32_t tv;
    uint32_t arg;
    OSType arch;
    std::string name;
    ConnectionID cid;
};
std::vector<pending_init> pending_inits;
int prepare_depth;
/* the library whose absence failed the last preparation (errName) */
std::string missing_library;

/* The fragments in a Power Mac ROM's library table ('nlib' ROM resources;
   the G3's, tools/macdecode/libraries.py rom_libraries): always there on a
   Mac. */
bool rom_library(const char *name)
{
    static const char *const names[] = {
        "ATAManager", "ATIUtils", "DSLGlobalsLib", "DriverLoaderLib",
        "DriverServicesLib", "FMSwapFont", "FontDispatch", "InterfaceLib",
        "InterruptTreeHeathrow", "Math64Lib", "MathLib", "MemoryMgr",
        "MotherBoardHAL", "NQD", "NameRegistryLib", "PCILib", "PowerMgrLib",
        "PrivateInterfaceLib", "QuickdrawLib", "QuickdrawText", "Resources",
        "SoundLib", "StdCLib", "StdText", "VideoServicesGlobals",
        "VideoServicesLib",
    };
    for(const char *n : names)
        if(!strcasecmp(n, name))
            return true;
    return false;
}

void set_err_name(Str255 errname)
{
    errname[0] = std::min<size_t>(missing_library.size(), 255);
    memcpy(errname + 1, missing_library.data(), errname[0]);
}

/* MacPhoenix: the instances of sections whose share kind is global
   (kPEFGlobalShare): one for the whole system, as Apple's CFM keeps them,
   whichever process prepares the container. 8.6's FontObjectsGlobals and
   GlyphsCache keep the font object and glyph cache lists there; given a
   fresh copy per application, the second application's ATS walked the
   first's skip list to an end node it no longer recognised, and spun
   (docs/executor/MacOS8-9.md). Relocated by the first preparation only.
   Containers in files only: an in-memory one has no lasting identity. */
struct global_section
{
    FSSpec file;
    uint32_t offset;
    int index;
    void *start;
    uint32_t length;
    bool relocated;
};
std::vector<global_section> global_sections;

bool same_name(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && !strncasecmp(a.data(), b.data(), a.size());
}

bool same_file(const FSSpec& a, const FSSpec& b)
{
    return a.vRefNum == b.vRefNum && a.parID == b.parID
        && EqualString(a.name, b.name, false, true);
}

/* The system's instance of section index of the container being prepared,
   if it has one yet. */
global_section *find_global_section(int index)
{
    const load_origin *o = current_origin;
    if(!o || o->where != kOnDiskFlat)
        return nullptr;
    for(global_section& g : global_sections)
        if(g.index == index && g.offset == o->offset && same_file(g.file, o->file))
            return &g;
    return nullptr;
}

loaded_fragment *find_loaded(ConnectionID cid)
{
    for(auto& f : loaded)
        if(f.cid == cid)
            return &f;
    return nullptr;
}

std::string pstr(ConstStringPtr p)
{
    return p ? std::string((const char *)p + 1, p[0]) : std::string();
}
}

uint32_t Executor::ROMlib_call_cfm68k(uint32_t tvector, const uint32_t *arg)
{
    uint32_t save_a7 = EM_A7, save_a5 = EM_A5;
    if(arg)
    {
        EM_A7 -= 4;
        *ptr_from_longint<GUEST<uint32_t> *>(EM_A7) = *arg;
    }
    uint32_t code = *ptr_from_longint<GUEST<uint32_t> *>(tvector);
    EM_A0 = code;
    EM_A1 = tvector + 4;
    execute68K(code);
    uint32_t d0 = EM_D0;
    EM_A7 = save_a7;
    EM_A5 = save_a5;
    return d0;
}

uint32_t Executor::ROMlib_call_cfm68k(uint32_t tvector, std::initializer_list<uint32_t> args)
{
    uint32_t save_a7 = EM_A7, save_a5 = EM_A5;
    for(auto it = args.end(); it != args.begin();)
    {
        EM_A7 -= 4;
        *ptr_from_longint<GUEST<uint32_t> *>(EM_A7) = *--it;
    }
    uint32_t code = *ptr_from_longint<GUEST<uint32_t> *>(tvector);
    EM_A0 = code;
    EM_A1 = tvector + 4;
    execute68K(code);
    uint32_t d0 = EM_D0;
    EM_A7 = save_a7;
    EM_A5 = save_a5;
    return d0;
}

void Executor::ROMlib_cfm_terminate()
{
    THz zone = LM(ApplZone);
    for(size_t i = loaded.size(); i-- > 0;)
    {
        if(loaded[i].zone != zone || loaded[i].terminated)
            continue;
        loaded[i].terminated = true;
        if(loaded[i].term && loaded[i].arch == "m68k"_4)
            ROMlib_call_cfm68k(loaded[i].term);
        else if(loaded[i].term)
            callPPC(loaded[i].term, {});
    }
    /* Executor's StdCLib: the application's streams are flushed and closed. */
    clib::stdclibProcessExit();
}

void Executor::ROMlib_cfm_process_exit(uint32_t part_lo, uint32_t part_hi)
{
    loaded.erase(std::remove_if(loaded.begin(), loaded.end(),
        [&](const loaded_fragment& f) {
            uint32_t z = US_TO_SYN68K((void *)f.zone);
            return z >= part_lo && z < part_hi;
        }), loaded.end());
    builtin_connections.erase(std::remove_if(builtin_connections.begin(), builtin_connections.end(),
        [&](const builtin_connection& b) {
            uint32_t z = US_TO_SYN68K((void *)b.zone);
            return z >= part_lo && z < part_hi;
        }), builtin_connections.end());
}

bool Executor::ROMlib_cfm_ppc_code(uint32_t addr)
{
    for(const loaded_fragment& f : loaded)
    {
        if(f.arch != "pwpc"_4 || !f.cid)
            continue;
        for(uint32_t i = 0; i < f.cid->n_sects; i++)
        {
            const section_info_t& sect = f.cid->sects[i];
            uint32_t start = ptr_to_longint(sect.start.get());
            if((sect.perms & (1 << 2)) /* executable_section */ && addr >= start && addr - start < sect.length)
                return true;
        }
    }
    return false;
}

/* MacPhoenix: where a PowerPC address is, for the PC sampler: "Fragment
   sect N+$off (Export+$off)", the export the nearest routine at or before
   it; empty when no loaded fragment's section holds it. */
std::string Executor::ROMlib_cfm_describe(uint32_t addr)
{
    /* newest first: a fragment of a process being torn down may still be
       listed, over memory a live one has now */
    for(auto it = loaded.rbegin(); it != loaded.rend(); ++it)
    {
        const loaded_fragment& f = *it;
        if(f.arch != "pwpc"_4 || !f.cid)
            continue;
        for(uint32_t i = 0; i < f.cid->n_sects; i++)
        {
            const section_info_t& sect = f.cid->sects[i];
            uint32_t start = ptr_to_longint(sect.start.get());
            if(addr < start || addr - start >= sect.length)
                continue;
            char buf[400];
            int n = snprintf(buf, sizeof buf, "%s sect %u+$%x", f.origin.name.c_str(), i, addr - start);
            GUEST<LONGINT> count;
            std::string best;
            uint32_t best_at = 0;
            if(ROMlib_cfm_exports_sane(f.cid) && C_CountSymbols(f.cid, &count) == noErr)
                for(LONGINT k = 0; k < count; k++)
                {
                    Str255 name;
                    GUEST<Ptr> where;
                    SymClass cls;
                    if(C_GetIndSymbol(f.cid, k, name, &where, &cls) != noErr)
                        continue;
                    uint32_t at = ptr_to_longint(where.get());
                    if((cls & 0x0F) == kPEFTVectorSymbol)
                        at = *(GUEST<uint32_t> *)SYN68K_TO_US(at); /* the code */
                    else if((cls & 0x0F) != kPEFCodeSymbol)
                        continue;
                    if(at <= addr && at >= best_at && at >= start)
                    {
                        best_at = at;
                        best.assign((char *)name + 1, name[0]);
                    }
                }
            if(!best.empty())
                snprintf(buf + n, sizeof buf - n, " (%s+$%x)", best.c_str(), addr - best_at);
            return buf;
        }
    }
    return "";
}

/* MacPhoenix: --executor-ppc-break. A loaded PowerPC fragment's section
   (the first executable one for sect < 0) + off, by the fragment's name or
   its file's; 0 when none is loaded, the section has no such offset. */
uint32_t Executor::ROMlib_cfm_resolve(const std::string& name, int sect, uint32_t off)
{
    for(auto it = loaded.rbegin(); it != loaded.rend(); ++it)
    {
        const loaded_fragment& f = *it;
        if(f.arch != "pwpc"_4 || !f.cid || f.terminated)
            continue;
        if(f.origin.name != name && pstr(f.origin.file.name) != name)
            continue;
        for(uint32_t i = 0; i < f.cid->n_sects; i++)
        {
            const section_info_t& s = f.cid->sects[i];
            bool want = sect < 0 ? (s.perms & (1 << 2)) != 0 : (int)i == sect;
            if(want && s.length && off < s.length)
                return ptr_to_longint(s.start.get()) + off;
            if(want)
                return 0;
        }
    }
    return 0;
}

/* "Fragment N+$off" without the export search (a trace's every line). */
std::string Executor::ROMlib_cfm_where(uint32_t addr)
{
    for(auto it = loaded.rbegin(); it != loaded.rend(); ++it)
    {
        const loaded_fragment& f = *it;
        if(f.arch != "pwpc"_4 || !f.cid)
            continue;
        for(uint32_t i = 0; i < f.cid->n_sects; i++)
        {
            const section_info_t& sect = f.cid->sects[i];
            uint32_t start = ptr_to_longint(sect.start.get());
            if(addr < start || addr - start >= sect.length)
                continue;
            char buf[300];
            snprintf(buf, sizeof buf, "%s %u+$%x", f.origin.name.c_str(), i, addr - start);
            return buf;
        }
    }
    return "";
}

void Executor::ROMlib_cfm_application_start()
{
    THz zone = LM(ApplZone);
    loaded.erase(std::remove_if(loaded.begin(), loaded.end(),
        [&](const loaded_fragment& f) { return f.zone == zone; }), loaded.end());
    builtin_connections.erase(std::remove_if(builtin_connections.begin(), builtin_connections.end(),
        [&](const builtin_connection& b) { return b.zone == zone; }), builtin_connections.end());
}

/* MacPhoenix: memory for a fragment: the application heap, else
   temporary memory (the Process Manager's zone), as Apple's CFM does when
   the heap is too small: Mac OS 8.6's and 9's Finder loads its 1.8 MB
   library into a partition of 930 KB. A block in temporary memory isn't
   given back when the process quits (Executor's temporary memory isn't
   tracked per process). A fragment from the System file (AppearanceLib &
   co.) is the system's, not the application's: temporary memory first, so
   that it doesn't fill the heap of the application that happens to load
   it. */
static bool loading_system_fragment;
/* A global section outlives the process that prepares it: never in its
   heap. */
static bool loading_global_section;

static Ptr
cfm_new_ptr(Size n)
{
    if(loading_global_section)
    {
        Ptr p = nullptr;
        if(ROMlib_pm_zone)
        {
            TheZoneGuard guard(ROMlib_pm_zone);
            p = NewPtr(n);
        }
        return p ? p : NewPtrSys(n);
    }
    Ptr p = loading_system_fragment ? nullptr : NewPtr(n);
    if(!p && ROMlib_pm_zone)
    {
        TheZoneGuard guard(ROMlib_pm_zone);
        p = NewPtr(n);
    }
    if(!p && loading_system_fragment)
        p = NewPtr(n);
    return p;
}

static OSErr
try_to_get_memory(void **addrp, syn68k_addr_t default_syn_address,
                  uint32_t total_size, int alignment)
{
    // TODO: alignment
    *addrp = cfm_new_ptr(total_size);
    return 0;
}

enum
{
    readable_section = (1 << 0),
    writable_section = (1 << 1),
    executable_section = (1 << 2),
};

static OSErr
load_unpacked_section(const void *mmapped_addr,
                      syn68k_addr_t default_address, uint32_t total_size,
                      uint32_t packed_size, uint32_t unpacked_size,
                      uint32_t section_offset, share_kind_t share_kind,
                      int alignment, section_info_t *infop)
{
    OSErr retval;
    void *addr;

    if(packed_size != unpacked_size)
        warning_unexpected("%d %d", packed_size, unpacked_size);
    if(!(infop->perms & writable_section) && total_size == unpacked_size)
    {
        retval = noErr;
        addr = (char *)mmapped_addr + section_offset;
    }
    else
    {
        retval = try_to_get_memory(&addr, default_address, total_size,
                                   alignment);
        if(retval == noErr)
        {
            memcpy(addr, (char *)mmapped_addr + section_offset, packed_size);
            memset((char *)addr + unpacked_size, 0, total_size - unpacked_size);
        }
    }
    if(retval == noErr)
    {
        infop->start = addr;
        infop->length = total_size;
    }

    return retval;
}

static OSErr
repeat_block(uint32_t repeat_count, uint32_t block_size, uint8_t **destpp,
             const uint8_t **srcpp, uint32_t *dest_lengthp, uint32_t *src_lengthp)
{
    OSErr retval;
    uint32_t total_bytes_to_write;

    total_bytes_to_write = repeat_count * block_size;
    if(total_bytes_to_write > *dest_lengthp || block_size > *src_lengthp)
        retval = paramErr;
    else
    {
        while(repeat_count-- > 0)
        {
            memcpy(*destpp, *srcpp, block_size);
            *destpp += block_size;
        }
        *srcpp += block_size;
        *dest_lengthp -= total_bytes_to_write;
        *src_lengthp -= block_size;
        retval = noErr;
    }
    return retval;
}

static OSErr
extract_count(const uint8_t **srcpp, uint32_t *lengthp, uint32_t *countp)
{
    OSErr retval;
    uint32_t count;
    uint8_t next_piece;

    count = *countp;
    do
    {
        retval = (*lengthp > 0) ? noErr : paramErr;
        if(retval == noErr)
        {
            next_piece = **srcpp;
            ++*srcpp;
            --*lengthp;
            count = (count << 7) | (next_piece & 0x7F);
        }
    } while(retval == noErr && (next_piece & 0x80));
    if(retval == noErr)
        *countp = count;

    return retval;
}

static OSErr
interleave(uint32_t repeat_count, uint32_t custom_size, uint8_t **destpp,
           const uint8_t **srcpp, uint32_t *dest_lengthp, uint32_t *src_lengthp,
           uint32_t common_size, const uint8_t *common_mem)
{
    OSErr retval;
    uint32_t total_size;

    total_size = repeat_count * (common_size + custom_size) + common_size;
    if(total_size > *dest_lengthp || custom_size * repeat_count > *src_lengthp)
        retval = paramErr;
    else
    {
        ++repeat_count;
        while(repeat_count-- > 0)
        {
            if(common_mem)
                memcpy(*destpp, common_mem, common_size);
            else
                memset(*destpp, 0, common_size);
            *destpp += common_size;
            *dest_lengthp -= common_size;
            if(repeat_count > 0)
            {
                memcpy(*destpp, *srcpp, custom_size);
                *destpp += custom_size;
                *dest_lengthp -= custom_size;
                *srcpp += custom_size;
                *src_lengthp -= custom_size;
            }
        }
        retval = noErr;
    }
    return retval;
}

static OSErr
pattern_initialize(uint8_t *addr, const uint8_t *patmem, uint32_t packed_size,
                   uint32_t unpacked_size)
{
    OSErr retval;

    retval = noErr;
    while(retval == noErr && packed_size > 0)
    {
        uint8_t opcode;
        uint32_t count;

        opcode = *patmem >> 5;
        count = (*patmem & 0x1f);
        ++patmem;
        --packed_size;
        if(!count)
            retval = extract_count(&patmem, &packed_size, &count);
        if(retval == noErr)
        {
            switch(opcode)
            {
                case 0:
                    if(count > unpacked_size)
                        retval = paramErr;
                    else
                    {
                        memset(addr, 0, count);
                        addr += count;
                        unpacked_size -= count;
                    }
                    break;
                case 1:
                    retval = repeat_block(1, count, &addr, &patmem, &unpacked_size,
                                          &packed_size);
                    break;
                case 2:
                {
                    uint32_t repeat_count;

                    repeat_count = 0;
                    retval = extract_count(&patmem, &packed_size, &repeat_count);
                    if(retval == noErr)
                        retval = repeat_block(repeat_count + 1, count, &addr,
                                              &patmem, &unpacked_size,
                                              &packed_size);
                }
                break;
                case 3:
                case 4:
                {
                    uint32_t common_size;
                    uint32_t custom_size;

                    common_size = count;
                    custom_size = 0;
                    retval = extract_count(&patmem, &packed_size, &custom_size);
                    if(retval == noErr)
                    {
                        uint32_t repeat_count;

                        repeat_count = 0;
                        retval = extract_count(&patmem, &packed_size,
                                               &repeat_count);
                        if(retval == noErr)
                        {
                            const uint8_t *common_mem;

                            if(opcode == 4)
                                common_mem = 0;
                            else
                            {
                                common_mem = patmem;
                                if(common_size > packed_size)
                                    retval = paramErr;
                                else
                                {
                                    patmem += common_size;
                                    packed_size -= common_size;
                                }
                            }
                            if(retval == noErr)
                                interleave(repeat_count, custom_size, &addr,
                                           &patmem, &unpacked_size, &packed_size,
                                           common_size, common_mem);
                        }
                    }
                }
                break;
                default:
                    warning_unexpected("%d", opcode);
                    assert(0);
                    break;
            }
        }
    }
    if(retval == noErr && (packed_size || unpacked_size))
    {
        retval = paramErr;
        warning_unexpected("%d %d", packed_size, unpacked_size);
        assert(0);
    }
    return retval;
}

static OSErr
load_pattern_section(const void *mmapped_addr,
                     syn68k_addr_t default_address, uint32_t total_size,
                     uint32_t packed_size, uint32_t unpacked_size,
                     uint32_t section_offset, share_kind_t share_kind,
                     int alignment, section_info_t *infop)
{
    OSErr retval;
    void *addr;

    retval = try_to_get_memory(&addr, default_address, total_size, alignment);
    if(retval == noErr)
    {
        uint8_t *patmem;

        patmem = (decltype(patmem))((char *)mmapped_addr + section_offset);
        retval = pattern_initialize((uint8_t*)addr, patmem, packed_size, unpacked_size);
        if(retval == noErr)
        {
            memset((char*)addr + unpacked_size, 0,
                   total_size - unpacked_size);
            infop->start = addr;
            infop->length = total_size;
        }
    }

    return retval;
}

static void
repeatedly_relocate(uint32_t count, uint8_t **relocAddressp, uint32_t val)
{
    GUEST<uint32_t> *p;
    p = (GUEST<uint32_t> *)*relocAddressp;
    while(count-- > 0)
    {
        *p = *p + val;
        ++p;
    }
    *relocAddressp = (uint8_t *)p;
}

static Ptr main_address(ConnectionID cid)
{
    PEFLoaderInfoHeader *lihp = cid->lihp;
    if(!lihp || lihp->mainSection < 0)
        return nullptr;
    return (Ptr)cid->sects[lihp->mainSection].start + lihp->mainOffset;
}

static OSErr
check_existing_connections(ConstStr63Param library, OSType arch, LoadFlags loadflags,
                           GUEST<ConnectionID> *cidp, GUEST<Ptr> *mainaddrp, Str255 errName)
{
    std::string name = pstr(library);
    THz zone = LM(ApplZone);
    for(auto& f : loaded)
        if(f.arch == arch && f.zone == zone && same_name(f.origin.name, name))
        {
            ++f.refs;
            *cidp = f.cid;
            *mainaddrp = main_address(f.cid);
            return noErr;
        }
    return fragLibNotFound;
}

/* MacPhoenix: the file registry (Mac OS Runtime Architectures, 1-17):
   files with fragments that CodeFragmentDispatch's private selector -20
   names, searched after the Extensions folder (Mac OS Easy Open's control
   panel holds the PowerPC "Translation" library). */
static std::vector<FSSpec> registered_files;

static OSErr
check_file(INTEGER vref, LONGINT dirid, Str255 file, bool shlb_test_p,
           ConstStr63Param library, OSType arch, LoadFlags loadflags,
           GUEST<ConnectionID> *cidp, GUEST<Ptr> *mainaddrp, Str255 errName)
{
    OSErr retval;
    FSSpec fs;

    retval = FSMakeFSSpec(vref, dirid, file, &fs);
    if(retval != noErr)
        retval = fragLibNotFound;
    else
    {
        if(shlb_test_p)
        {
            OSErr err;
            FInfo finfo;

            err = FSpGetFInfo(&fs, &finfo);
            if(err != noErr || finfo.fdType != "shlb"_4)
                retval = fragLibNotFound;
        }
        if(retval == noErr)
        {
            INTEGER rn;
            INTEGER save_map = LM(CurMap);

            /* MacPhoenix: a file already open (a library being prepared
               from it imports another) answers its refnum: stays open. */
            std::vector<INTEGER> open_before;
            resmaphand map;
            WALKMAPTOP(map)
                open_before.push_back((*map)->resfn);
            EWALKMAP()
            rn = FSpOpenResFile(&fs, fsRdPerm);
            if(rn == -1)
                retval = fragLibNotFound;
            else
            {
                Handle cfrg0;

                cfrg0 = Get1Resource("cfrg"_4, 0);
                if(!cfrg0)
                    retval = fragLibNotFound;
                else
                {
                    cfir_t *cfirp;

                    cfirp = ROMlib_find_cfrg(cfrg0, arch, kImportLibraryCFrag,
                                             library);
                    if(!cfirp)
                        retval = fragLibNotFound;
                    else
                    {
                        /* MacPhoenix: named as the library asked for, which
                           is how the next importer finds it prepared. */
                        int32_t offset = CFIR_OFFSET_TO_FRAGMENT(cfirp);
                        int32_t length = CFIR_FRAGMENT_LENGTH(cfirp);
                        Str63 name;
                        memcpy(name, CFIR_NAME(cfirp), std::min<int>(CFIR_NAME(cfirp)[0], 63) + 1);
                        name[0] = std::min<int>(name[0], 63);
                        /* The file stays open while the fragment's init
                           routine runs: it may read its own resources. */
                        retval = GetDiskFragment(&fs, offset, length, name,
                                                 loadflags, cidp, mainaddrp,
                                                 errName);
                    }
                }
                /* MacPhoenix: a file that was open before (the
                   application's own, the System, a library's being
                   prepared): leave it open. */
                if(std::find(open_before.begin(), open_before.end(), rn) == open_before.end())
                    CloseResFile(rn);
                LM(CurMap) = save_map;
            }
        }
    }

    return retval;
}

static OSErr
check_vanddir(INTEGER vref, LONGINT dirid, int descend_count, ConstStr63Param library,
              OSType arch, LoadFlags loadflags, GUEST<ConnectionID> *cidp,
              GUEST<Ptr> *mainaddrp, Str255 errName)
{
    CInfoPBRec pb;
    Str255 s;
    OSErr retval;
    INTEGER dirindex;
    OSErr err;
    int errcount;

    retval = fragLibNotFound;
    pb.hFileInfo.ioNamePtr = &s[0];
    pb.hFileInfo.ioVRefNum = vref;
    err = noErr;
    errcount = 0;
    /* kFindLib: fragConnectionIDNotFound is a file that has the library,
       not prepared; it ends the search as a find does. */
    for(dirindex = 1;
        retval != noErr && retval != fragConnectionIDNotFound && err != fnfErr && errcount != 3;
        dirindex++)
    {
        pb.hFileInfo.ioFDirIndex = dirindex;
        pb.hFileInfo.ioDirID = dirid;
        err = PBGetCatInfo(&pb, false);
        if(err)
        {
            if(err != fnfErr)
            {
                warning_unexpected("PBGetCatInfo err = %d\n", err);
                ++errcount;
            }
        }
        else
        {
            errcount = 0;
            if(pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)
            {
                if(descend_count > 0)
                {
                    retval = check_vanddir(vref, pb.hFileInfo.ioDirID,
                                           descend_count - 1, library,
                                           arch, loadflags, cidp, mainaddrp,
                                           errName);
                }
            }
            else if(pb.hFileInfo.ioFlFndrInfo.fdType == "shlb"_4)
                retval = check_file(vref, dirid, s, false, library, arch,
                                    loadflags, cidp, mainaddrp, errName);
        }
    }
    return retval;
}

/* MacPhoenix: whether a file in the folder (and its folders, descend_count
   deep) lists the library in its 'cfrg', of whatever type: only looks. A
   library there that Executor doesn't load (QuickTime™ PowerPlug's
   QuickTimeLib, QuickTime being off) is one the Mac would have. */
static bool
library_in_folder(INTEGER vref, LONGINT dirid, int descend_count, ConstStr63Param library,
                  OSType arch)
{
    CInfoPBRec pb;
    Str255 s;
    pb.hFileInfo.ioNamePtr = &s[0];
    pb.hFileInfo.ioVRefNum = vref;
    for(INTEGER dirindex = 1;; dirindex++)
    {
        pb.hFileInfo.ioFDirIndex = dirindex;
        pb.hFileInfo.ioDirID = dirid;
        if(PBGetCatInfo(&pb, false) != noErr)
            return false;
        if(pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)
        {
            if(descend_count > 0
               && library_in_folder(vref, pb.hFileInfo.ioDirID, descend_count - 1, library, arch))
                return true;
            continue;
        }
        FSSpec fs;
        if(FSMakeFSSpec(vref, dirid, s, &fs) != noErr)
            continue;
        INTEGER save_map = LM(CurMap);
        std::vector<INTEGER> open_before;
        resmaphand map;
        WALKMAPTOP(map)
            open_before.push_back((*map)->resfn);
        EWALKMAP()
        INTEGER rn = FSpOpenResFile(&fs, fsRdPerm);
        if(rn == -1)
            continue;
        Handle cfrg0 = Get1Resource("cfrg"_4, 0);
        bool found = cfrg0 && ROMlib_find_cfrg(cfrg0, arch, kImportLibraryCFrag, library);
        if(std::find(open_before.begin(), open_before.end(), rn) == open_before.end())
            CloseResFile(rn);
        LM(CurMap) = save_map;
        if(found)
            return true;
    }
}

/* ... in Extensions, or in "Extensions (Disabled)", where Executor's
   extension policy keeps every file that runs code at startup it doesn't
   list (romlib/extensions.cpp): there on the Mac. */
static bool
library_in_extensions(const char *name, OSType arch)
{
    Str63 library;
    library[0] = std::min<size_t>(strlen(name), 63);
    memcpy(library + 1, name, library[0]);
    GUEST<INTEGER> vref;
    GUEST<LONGINT> dirid;
    if(FindFolder(0, kExtensionFolderType, false, &vref, &dirid) == noErr
       && library_in_folder(vref, dirid, 1, library, arch))
        return true;
    if(FindFolder(0, kSystemFolderType, false, &vref, &dirid) != noErr)
        return false;
    Str255 disabled;
    memcpy(disabled, "\025Extensions (Disabled)", 22);
    CInfoPBRec pb = {};
    pb.hFileInfo.ioNamePtr = disabled;
    pb.hFileInfo.ioVRefNum = vref;
    pb.hFileInfo.ioDirID = dirid;
    return PBGetCatInfo(&pb, false) == noErr && (pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)
        && library_in_folder(vref, pb.hFileInfo.ioDirID, 1, library, arch);
}

/* MacPhoenix: the System file's own fragments. From 8.x the System's
   Toolbox libraries (AppearanceLib, WindowsLib, IconServicesLib, ...) are
   PEF containers in its data fork, listed in its 'cfrg' resources (several,
   by machine; any one will do). */
static bool
system_file_spec(FSSpec *fs)
{
    FCBPBRec fcb = {};
    Str255 name;
    fcb.ioNamePtr = name;
    fcb.ioRefNum = LM(SysMap);
    if(PBGetFCBInfo(&fcb, false) != noErr)
        return false;
    fs->vRefNum = fcb.ioFCBVRefNum;
    fs->parID = fcb.ioFCBParID;
    memcpy(fs->name, name, std::min<int>(name[0], 63) + 1);
    return true;
}

static bool
is_system_file(const FSSpec& fs)
{
    FSSpec sys;
    return system_file_spec(&sys) && same_file(fs, sys);
}

static OSErr
check_system_file(ConstStr63Param library, OSType arch, LoadFlags loadflags,
                  GUEST<ConnectionID> *cidp, GUEST<Ptr> *mainaddrp, Str255 errName)
{
    /* The managers Executor is: Apple's replace the ROM's on 8.x and
       expect its private state. */
    static const char *const executors[] = {
        "WindowsLib", "MenusLib", "ControlsLib", "EventsLib", "MacDialogsLib",
        "ListsLib", "NQD", "MemoryMgr", "Resources", "QuickDrawText",
    };
    for(const char *e : executors)
        if(library[0] == strlen(e) && !strncasecmp((const char *)library + 1, e, library[0]))
            return fragLibNotFound;
    FSSpec fs;
    if(!system_file_spec(&fs))
        return fragLibNotFound;

    INTEGER save_map = LM(CurMap);
    LM(CurMap) = LM(SysMap);
    cfir_t *cfirp = nullptr;
    for(INTEGER i = 1, n = Count1Resources("cfrg"_4); !cfirp && i <= n; i++)
        if(Handle h = Get1IndResource("cfrg"_4, i))
            cfirp = ROMlib_find_cfrg(h, arch, kImportLibraryCFrag, library);
    LM(CurMap) = save_map;
    if(!cfirp || CFIR_LOCATION(cfirp) != kOnDiskFlat)
        return fragLibNotFound;
    Str63 fragname;
    memcpy(fragname, CFIR_NAME(cfirp), std::min<int>(CFIR_NAME(cfirp)[0], 63) + 1);
    fragname[0] = std::min<int>(fragname[0], 63);
    warning_trace_info("CFM: \"%.*s\" from the System file", library[0], library + 1);
    return GetDiskFragment(&fs, CFIR_OFFSET_TO_FRAGMENT(cfirp),
                           CFIR_FRAGMENT_LENGTH(cfirp), fragname, loadflags,
                           cidp, mainaddrp, errName);
}

/* MacPhoenix: one of Executor's own libraries, connected to the current
   application. */
static OSErr
connect_builtin(ConstStr63Param library, OSType arch, GUEST<ConnectionID> *cidp,
                GUEST<Ptr> *mainaddrp)
{
    ConnectionID cid = builtinlibs::getBuiltinLib(library, arch);
    if(!cid)
        return fragLibNotFound;
    std::string name = pstr(library);
    if(std::none_of(builtin_connections.begin(), builtin_connections.end(),
                    [&](const builtin_connection& b) {
                        return b.arch == arch && b.zone == LM(ApplZone) && same_name(b.name, name);
                    }))
        builtin_connections.push_back({ name, arch, cid, LM(ApplZone) });
    *cidp = cid;
    *mainaddrp = nullptr;
    return noErr;
}

/* MacPhoenix: GetSharedLibrary's kFindLib, past the files prepared for this
   application: a library connected to it is found; one that is there but
   not connected answers cfragLibConnErr; only one that isn't there at all
   fragLibNotFound. So LibProbe finds on the G3 with 8.6: InterfaceLib 0,
   AppearanceLib, WindowsLib, MenusLib & co. -2817, DialogsLib -2804; BBEdit
   Lite 6.1 asks that way whether the Appearance Manager is there. */
static OSErr
find_library(ConstStr63Param library, OSType arch, GUEST<ConnectionID> *cidp,
             GUEST<Ptr> *mainaddrp, Str255 errName)
{
    std::string name = pstr(library);
    for(const builtin_connection& b : builtin_connections)
        if(b.arch == arch && b.zone == LM(ApplZone) && same_name(b.name, name))
        {
            *cidp = b.cid;
            *mainaddrp = nullptr;
            return noErr;
        }
    /* the files, with kFindLib: noErr prepared, fragConnectionIDNotFound
       there but not prepared */
    OSErr err = check_file(ROMlib_exevrefnum, 0, ROMlib_exefname, false, library, arch,
                           kFindLib, cidp, mainaddrp, errName);
    if(err != noErr && err != fragConnectionIDNotFound)
        err = check_vanddir(ROMlib_exevrefnum, 0, 0, library, arch, kFindLib, cidp,
                            mainaddrp, errName);
    if(err != noErr && err != fragConnectionIDNotFound)
    {
        GUEST<INTEGER> vref;
        GUEST<LONGINT> dirid;
        if(FindFolder(0, kExtensionFolderType, false, &vref, &dirid) == noErr)
            err = check_vanddir(vref, dirid, 1, library, arch, kFindLib, cidp, mainaddrp,
                                errName);
    }
    if(err == noErr)
        return noErr;
    /* 8.6 answers cfragLibConnErr for any library not connected, there or
       not (LibProbe, the sys probe's cfm keys: DialogsLib, NoSuchLibrary);
       before 8.5 not checked, as it was */
    if((uint16_t)LM(SysVersion) >= 0x0850)
        return fragLibConnErr;
    if(err == fragConnectionIDNotFound || builtinlibs::haveBuiltinLib(library, arch))
        return fragLibConnErr;
    err = check_system_file(library, arch, kFindLib, cidp, mainaddrp, errName);
    if(err == noErr)
        return noErr;
    return err == fragConnectionIDNotFound ? fragLibConnErr : fragLibNotFound;
}

/* On 8.6 InitFonts, InitWindows and InitMenus prepare the native library
   their manager is in, in the application's context: kFindLib finds
   FontManager, WindowsLib and MenusLib once they have run (the sys probe),
   not before (LibProbe). */
void Executor::ROMlib_cfm_toolbox_library(const char *library)
{
    if((uint16_t)LM(SysVersion) < 0x0850)
        return;
    Str63 name;
    name[0] = strlen(library);
    memcpy(name + 1, library, name[0]);
    GUEST<ConnectionID> cid;
    GUEST<Ptr> mainaddr;
    connect_builtin(name, "pwpc"_4, &cid, &mainaddr);
}

/*
 * This sort of implements the "Searching for Import Libraries" algorithm
 * described on 1-17 and 1-18 of the Mac Runtime Architectures manual.  The
 * biggest difference is that we don't search for the best fit between
 * everything available in the Extensions folder, the ROM registry and the
 * file registry.  Instead we currently stop when we find any fit.
 *
 * Additionally, we don't really have a ROM registry per-se.  Instead
 * we have the home-brewed InterfaceLib and MathLib files.  We don't
 * have anything corresponding to the file registry.
 *
 */

OSErr Executor::C_GetSharedLibrary(ConstStr63Param library, OSType arch,
                                   LoadFlags loadflags, GUEST<ConnectionID> *cidp,
                                   GUEST<Ptr> *mainaddrp, Str255 errName)
{
    OSErr retval;

    warning_trace_info("GetSharedLibrary (\"%.*s\")\n", library[0],
                       library + 1);

    /* MacPhoenix: one copy per application (kLoadNewCopy aside); then the
       application's own file, its folder, the Extensions folder, and last
       Executor's own libraries. (The root and application folders were
       uninitialized TODOs.) */
    retval = fragLibNotFound;
    if((loadflags & 0xF) != kLoadNewCopy)
        retval = check_existing_connections(library, arch, loadflags, cidp,
                                            mainaddrp, errName);
    /* Executor's own Open Transport libraries are always there, so they
       are found with kFindLib too. */
    if(retval == fragLibNotFound && loadflags == kFindLib && arch == "pwpc"_4
       && ROMlib_open_transport && ROMlib_ot_library(std::string((const char *)library + 1, library[0])))
        if(ConnectionID cid = builtinlibs::getBuiltinLib(library, arch))
        {
            *cidp = cid;
            *mainaddrp = nullptr;
            return noErr;
        }
    if(retval == fragLibNotFound && loadflags == kFindLib)
        return find_library(library, arch, cidp, mainaddrp, errName);
    /* MacPhoenix: libraries whose routines Executor answers itself, as it
       does Open Transport, before Apple's own: the 68k ObjectSupportLib
       runs its 'proc' resources against the AppleEvent Manager's private
       selectors (docs/executor/CFM68K.md). */
    if(retval == fragLibNotFound && arch == "m68k"_4
       && EqualString(library, (StringPtr) "\020ObjectSupportLib", false, true)
       && connect_builtin(library, arch, cidp, mainaddrp) == noErr)
        return noErr;
    /* ... and, Open Transport on, OT's PowerPC libraries (OpenTransport.yaml:
       OTClientLib & co.): Apple's files need Apple's OT kernel. */
    if(retval == fragLibNotFound && arch == "pwpc"_4 && ROMlib_open_transport
       && ROMlib_ot_library(std::string((const char *)library + 1, library[0]))
       && connect_builtin(library, arch, cidp, mainaddrp) == noErr)
        return noErr;
    if(retval == fragLibNotFound)
    {
        retval = check_file(ROMlib_exevrefnum, 0, ROMlib_exefname, false,
                            library, arch, loadflags, cidp, mainaddrp,
                            errName);
        if(retval != noErr)
            retval = check_vanddir(ROMlib_exevrefnum, 0, 0, library, arch,
                                   loadflags, cidp, mainaddrp, errName);
        if(retval != noErr)
        {
            GUEST<INTEGER> extensions_vref;
            GUEST<LONGINT> extensions_dirid;

            if(FindFolder(0, kExtensionFolderType, false, &extensions_vref,
                          &extensions_dirid)
               == noErr)
                retval = check_vanddir(extensions_vref, extensions_dirid, 1,
                                       library, arch, loadflags, cidp,
                                       mainaddrp, errName);
        }

        for(size_t i = 0; retval != noErr && i < registered_files.size(); ++i)
        {
            Str255 name;
            memcpy(name, registered_files[i].name, registered_files[i].name[0] + 1);
            retval = check_file(registered_files[i].vRefNum, registered_files[i].parID, name,
                                false, library, arch, loadflags, cidp, mainaddrp, errName);
        }

        if(retval != noErr)
        {
           /* if(EqualString(library, (unsigned char*)"\7MathLib", false, true))
                //retval = ROMlib_GetMathLib(library, arch, loadflags, cidp,
                //                           mainaddrp, errName);
                printf("Mathlib\n");
            else if(EqualString(library, (unsigned char*)"\14InterfaceLib", false, true))
                //retval = ROMlib_GetInterfaceLib(library, arch, loadflags, cidp,
                //                                mainaddrp, errName);
                printf("InterfaceLib\n");*/
            retval = connect_builtin(library, arch, cidp, mainaddrp);
        }
        /* MacPhoenix: then what the System file has that Executor hasn't. */
        if(retval != noErr)
            retval = check_system_file(library, arch, loadflags, cidp,
                                       mainaddrp, errName);
    }
    return retval;
}

/* MacPhoenix: the last connection to a fragment closed: its termination
   routine runs, its memory goes back, and the libraries it imports lose a
   connection (Inside Macintosh: PowerPC System Software, CloseConnection).
   CodeWarrior IDE prepares each of its plug-ins, asks it what it is and
   closes it: kept, the thirty-odd filled its heap, and NewThread found no
   room for the stack it quits without. Executor's own libraries stay. */
static void close_connection(ConnectionID cid)
{
    loaded_fragment *f = find_loaded(cid);
    if(!f || --f->refs > 0)
        return;
    if(f->term && !f->terminated)
    {
        f->terminated = true;
        uint32_t term = f->term;
        if(f->arch == "m68k"_4)
            ROMlib_call_cfm68k(term);
        else
            callPPC(term, {});
        f = find_loaded(cid);   /* the routine may have loaded or closed others */
        if(!f)
            return;
    }
    loaded_fragment gone = *f;
    loaded.erase(loaded.begin() + (f - loaded.data()));

    uint32_t lo = guest_cast<uint32_t>(gone.image), hi = lo + gone.image_length;
    for(uint32_t i = 0; i < cid->n_sects; i++)
    {
        uint32_t at = guest_cast<uint32_t>((Ptr)(void *)cid->sects[i].start);
        if(at && (at < lo || at >= hi))
            DisposePtr((Ptr)(void *)cid->sects[i].start);
    }
    if(gone.owns_image)
        DisposePtr(gone.image);
    DisposePtr((Ptr)cid);
    if(CFragClosureID c = gone.closure)
    {
        for(uint32_t i = 0; i < N_LIBS(c); i++)
            if(ConnectionID lib = LIB_CID(&c->libs[i]))
                close_connection(lib);
        DisposePtr((Ptr)c);
    }
}

OSErr Executor::C_CloseConnection(GUEST<ConnectionID> *cidp)
{
    ConnectionID cid = *cidp;
    if(!cid)
        return fragConnectionIDNotFound;
    close_connection(cid);
    return noErr;
}

static OSErr
symbol_lookup(uint32_t *indexp, GUEST<Ptr> *valp, uint8_t imports[][4],
              const char *symbol_names, CFragClosureID closure_id, OSType arch)
{
    OSErr retval;
    int index;
    uint8_t flags;
    uint8_t class1;
    const char *symbol_name;

    index = *indexp;
    flags = imports[index][0] >> 4;
    class1 = imports[index][0] & 0xf;
    symbol_name = (symbol_names + (imports[index][1] << 16) + (imports[index][2] << 8) + (imports[index][3] << 0));

    {
        int i;
        int n_libs;

        n_libs = N_LIBS(closure_id);
        for(i = 0; i < n_libs; ++i)
        {
            const lib_t *l;
            uint32_t first_symbol;

            l = &closure_id->libs[i];
            first_symbol = LIB_FIRST_SYMBOL(l);
            if(index >= first_symbol && index < first_symbol + LIB_N_SYMBOLS(l))
            {
                Str255 sym255;
                OSErr err;

                sym255[0] = std::min<int>(strlen(symbol_name), 255);
                memcpy(sym255 + 1, symbol_name, sym255[0]);
                if(LIB_CID(l))
                    err = FindSymbol(LIB_CID(l), sym255, valp, 0);
                else
                    err = -1;//###
                if(err != noErr)
                {
                    /* MacPhoenix: a weak library's imports are weak too. */
                    if((flags & 8) || l->weak)   // weak symbol
                    {
                        /* Said too: a program that calls a weak import
                           without checking it jumps to 0 (Date & Time on
                           8.6, MenusLib's clock calls). */
                        if(LIB_CID(l))
                            fprintf(stderr, "CFM: weak import %s unresolved (nil)\n", symbol_name);
                        *valp = kUnresolvedCFragSymbolAddress;
                    }
                    else
                    {
                        /* MacPhoenix: say so at load time; calling it is
                           answered 0 and logged. A data import gets zeroed
                           storage of its own, not a routine to read. */
                        fprintf(stderr, "CFM: unresolved import %s\n", symbol_name);
                        if(class1 == kPEFDataSymbol)
                            *valp = NewPtrSysClear(256);
                        else
                            *valp = builtinlibs::makeUndefinedSymbolStub(symbol_name, arch);
                    }
                }
                /*-->*/ break;
            }
        }
    }
    ++*indexp;
    retval = noErr;
    return retval;
}

static OSErr
relocate(const PEFLoaderRelocationHeader reloc_headers[],
         int section, uint32_t reloc_count, uint8_t reloc_instrs[][2],
         uint8_t imports[][4], const char *symbol_names,
         CFragClosureID closure_id, ConnectionID connp, OSType arch)
{
    OSErr retval;
    uint8_t *relocAddress;
    uint32_t importIndex;
    uint32_t sectionC;
    uint32_t sectionD;
    int32_t repeat_remaining;

    repeat_remaining = -1; /* i.e. not currently processing RelocSmRepeat or
			    RelocLgRepeat */

    relocAddress = (uint8_t *)connp->sects[section].start;
    importIndex = 0;
    /* What a relocation adds: where a section's instance is, less the
       default address the container's values assume (MPW Shell 3.5's
       PowerShell: data at $6750). */
    auto section_base = [connp](int k) -> uint32_t {
        return ptr_to_longint(connp->sects[k].start) - connp->sects[k].default_address;
    };
    sectionC = section_base(0);
    sectionD = section_base(1);

    retval = noErr;
    while(reloc_count-- > 0)
    {
        uint8_t msb;

        msb = reloc_instrs[0][0];
        if((msb >> 6) == 0)
        {
            uint8_t lsb;
            int skipCount;
            int relocCount;

            lsb = reloc_instrs[0][1];
            skipCount = (msb << 2) | (lsb >> 6);
            relocCount = (lsb & 0x3f);
            relocAddress += skipCount * 4;
            repeatedly_relocate(relocCount, &relocAddress, sectionD);
        }
        else
        {
            switch((msb >> 5))
            {
                case 2:
                {
                    int sub_op;
                    int run_length;
                    sub_op = (msb >> 1) & 0xf;
                    run_length = (((msb & 1) << 8)
                                  | reloc_instrs[0][1]);
                    ++run_length;
                    switch(sub_op)
                    {
                        case 0: /* RelocBySectC */
                            repeatedly_relocate(run_length, &relocAddress, sectionC);
                            break;
                        case 1: /* RelocBySectD */
                            repeatedly_relocate(run_length, &relocAddress, sectionD);
                            break;
                        case 2:
                            while(run_length-- > 0)
                            {
                                repeatedly_relocate(1, &relocAddress, sectionC);
                                repeatedly_relocate(1, &relocAddress, sectionD);
                                relocAddress += 4;
                            }
                            break;
                        case 3: /* RelocTVector8 */
                            while(run_length-- > 0)
                            {
                                repeatedly_relocate(1, &relocAddress, sectionC);
                                repeatedly_relocate(1, &relocAddress, sectionD);
                            }
                            break;
                        case 4:
                            while(run_length-- > 0)
                            {
                                repeatedly_relocate(1, &relocAddress, sectionD);
                                relocAddress += 4;
                            }
                            break;
                        case 5: /* RelocImportRun */
                            while(retval == noErr && run_length-- > 0)
                            {
                                GUEST<Ptr> symbol_val;

                                retval = symbol_lookup(&importIndex, &symbol_val,
                                                       imports, symbol_names,
                                                       closure_id, arch);
                                if(retval == noErr)
                                    repeatedly_relocate(1, &relocAddress,
                                                        guest_cast<uint32_t>(symbol_val));
                            }
                            break;
                        default:
                            warning_unexpected("%d", sub_op);
                            assert(0);
                            retval = paramErr;
                    }
                }
                break;
                case 3:
                {
                    int sub_op;
                    int index;
                    GUEST<Ptr> symbol_val;

                    sub_op = (msb >> 1) & 0xf;
                    index = (((msb & 1) << 8)
                             | reloc_instrs[0][1]);
                    switch(sub_op)
                    {
                        case 0:
                            importIndex = index;
                            retval = symbol_lookup(&importIndex, &symbol_val,
                                                   imports, symbol_names,
                                                   closure_id, arch);
                            if(retval == noErr)
                                repeatedly_relocate(1, &relocAddress,
                                                    guest_cast<uint32_t>(symbol_val));
                            break;
                        case 1:
                            sectionC = section_base(index);
                            warning_unimplemented("RelocSmSetSectC not tested much");
                            assert(0);
                            break;
                        case 2:
                            sectionD = section_base(index);
                            warning_unimplemented("RelocSmSetSectD not tested much");
                            break;
                        case 3:
                            fprintf(stderr, "RelocSmBySection\n");
                            assert(0);
                            break;
                        default:
                            warning_unexpected("sub_op");
                            fprintf(stderr, "Relocte By Index sub_op = %d\n", sub_op);
                            assert(0);
                            break;
                    }
                }
                break;
                default:
                    switch((msb >> 4))
                    {
                        case 8:
                        {
                            uint32_t offset;

                            offset = ((msb & 0xf) << 8) | (reloc_instrs[0][1]);
                            ++offset;
                            relocAddress += offset;
                        }
                        break;
                        case 9:
                        {
                            warning_unimplemented("RelocSmRepeat not tested much");
                            if(repeat_remaining != -1)
                                --repeat_remaining;
                            else
                            {
                                uint8_t lsb;

                                lsb = reloc_instrs[0][1];
                                repeat_remaining = lsb + 1;
                            }
                            if(repeat_remaining > 0)
                            {
                                int blockCount;

                                blockCount = (msb & 0xF) + 2;
                                reloc_count += blockCount;
                                reloc_instrs -= blockCount;
                            }
                            else
                                repeat_remaining = -1;
                        }
                        break;
                        default:
                            switch(msb >> 2)
                            {
                                case 0x28:
                                {
                                    uint32_t offset;

                                    offset = ((msb & 3) << 24 | (reloc_instrs[0][1]) << 16 | (reloc_instrs[1][0]) << 8 | (reloc_instrs[1][1]));

                                    relocAddress
                                        = (uint8_t *)
                                              connp->sects[section].start
                                        + offset;
                                }
                                    --reloc_count;
                                    ++reloc_instrs;
                                    break;
                                case 0x29:
                                {
                                    GUEST<Ptr> symbol_val;

                                    importIndex = ((msb & 3) << 24 | (reloc_instrs[0][1]) << 16 | (reloc_instrs[1][0]) << 8 | (reloc_instrs[1][1]));
                                    retval = symbol_lookup(&importIndex, &symbol_val,
                                                           imports, symbol_names,
                                                           closure_id, arch);
                                    if(retval == noErr)
                                        repeatedly_relocate(1, &relocAddress,
                                                            guest_cast<uint32_t>(symbol_val));
                                }
                                    --reloc_count;
                                    ++reloc_instrs;
                                    break;
                                case 0x2c:
                                    fprintf(stderr, "RelocLgRepeat\n");
                                    assert(0);
                                    --reloc_count;
                                    ++reloc_instrs;
                                    break;
                                case 0x2d:
                                    fprintf(stderr, "RelocLgSetOrBySection\n");
                                    assert(0);
                                    --reloc_count;
                                    ++reloc_instrs;
                                    break;
                                default:
                                    warning_unexpected("0x%x", msb);
                                    retval = paramErr;
                                    break;
                            }
                    }
            }
        }
        ++reloc_instrs;
    }
    return retval;
}

static CFragClosureID
begin_closure(uint32_t n_libs, PEFImportedLibrary *libs,
              const char *symbol_names, OSType arch)
{
    CFragClosureID retval;
    int i;
    OSErr err;

    retval = (decltype(retval))NewPtr(sizeof *retval + n_libs * sizeof(lib_t));
    N_LIBS(retval) = n_libs;

    // FIXME: #warning eventually need to worry about errors

    for(err = noErr, i = 0; /* err == noErr && */ i < n_libs; ++i)
    {
        Str63 libName;
        GUEST<Ptr> mainAddr;
        Str255 errName;
        int offset;
        const char *cname;

        offset = PEFIL_NAME_OFFSET(&libs[i]);
        cname = symbol_names + offset;
        libName[0] = std::min<int>(strlen(cname), 63);
        memcpy(libName + 1, cname, libName[0]);
        err = GetSharedLibrary(libName, arch, kReferenceCFrag,
                               &LIB_CID(&retval->libs[i]),
                               &mainAddr, errName);
        if(err != noErr)
        {
            warning_unexpected("%.*s", libName[0], libName + 1);
            LIB_CID(&retval->libs[i]) = nullptr;//### v(void *)0x12348765;
        }
        LIB_N_SYMBOLS(&retval->libs[i]) = PEFIL_SYMBOL_COUNT(&libs[i]);
        LIB_FIRST_SYMBOL(&retval->libs[i]) = PEFIL_FIRST_SYMBOL(&libs[i]);
        retval->libs[i].weak = !!(libs[i].options & kPEFWeakImportLibMask);
    }
    return retval;
}

static OSErr
load_loader_section(const void *addr,
                    syn68k_addr_t default_address, uint32_t total_size,
                    uint32_t packed_size, uint32_t unpacked_size,
                    uint32_t section_offset, share_kind_t share_kind,
                    int alignment, syn68k_addr_t *mainAddrp, OSType arch,
                    ConnectionID connp)
{
    OSErr retval;
    char *loader_section_bytes;
    PEFLoaderInfoHeader *lihp;
    uint32_t n_libs;
    uint32_t n_imports;
    uint32_t n_reloc_headers;
    PEFImportedLibrary *libs;
    uint8_t(*imports)[4];
    PEFLoaderRelocationHeader *reloc_headers;
    uint8_t *relocation_area;
    char *symbol_names;
    int i;
    CFragClosureID closure_id;

    loader_section_bytes = (char *)addr + section_offset;
    lihp = (PEFLoaderInfoHeader *)loader_section_bytes;
    connp->lihp = lihp;
    n_libs = PEFLIH_IMPORTED_LIBRARY_COUNT(lihp);
    libs = (PEFImportedLibrary *)&lihp[1];
    n_imports = PEFLIH_IMPORTED_SYMBOL_COUNT(lihp);
    imports = (uint8_t(*)[4]) & libs[n_libs];
    n_reloc_headers = PEFLIH_RELOC_SECTION_COUNT(lihp);
    reloc_headers = (PEFLoaderRelocationHeader *)imports[n_imports];

    relocation_area = (uint8_t *)(loader_section_bytes + PEFLIH_RELOC_INSTR_OFFSET(lihp));

    symbol_names = (decltype(symbol_names))(loader_section_bytes + PEFLIH_STRINGS_OFFSET(lihp));

    closure_id = begin_closure(n_libs, libs, symbol_names, arch);
    if(loaded_fragment *f = find_loaded(connp))
        f->closure = closure_id;

    /* MacPhoenix: a library that isn't there fails the preparation, as on
       a Mac (cfragNoLibraryErr, errName the library), unless it is imported
       weakly, is one a Power Mac's ROM has (Executor stands in for the
       ROM) or is in the Extensions folder but not loaded (QuickTime off):
       what the Mac would have is answered by stubs, as before.
       InputSprocket USB on a G3 without USBManagerLib is skipped by
       InputSprocketLib, not run with USB calls that answer noErr and set
       nothing. */
    for(i = 0; i < n_libs; ++i)
        if(!LIB_CID(&closure_id->libs[i]) && !closure_id->libs[i].weak
           && !rom_library(symbol_names + PEFIL_NAME_OFFSET(&libs[i]))
           && !library_in_extensions(symbol_names + PEFIL_NAME_OFFSET(&libs[i]), arch))
        {
            missing_library = symbol_names + PEFIL_NAME_OFFSET(&libs[i]);
            std::string who = "?";
            if(current_origin)
                who = !current_origin->name.empty() || current_origin->where != kOnDiskFlat
                    ? current_origin->name : pstr(current_origin->file.name);
            fprintf(stderr, "CFM: library %s not found: %s not prepared\n",
                    missing_library.c_str(), who.c_str());
            return fragLibNotFound;
        }

    for(i = 0, retval = noErr; retval == noErr && i < n_reloc_headers; ++i)
    {
        uint32_t reloc_count;
        uint8_t(*reloc_instrs)[2];

        /* a global section was relocated by its first preparation */
        global_section *g = find_global_section(PEFRLH_SECTION_INDEX(&reloc_headers[i]));
        if(g && g->relocated)
            continue;
        reloc_count = PEFRLH_RELOC_COUNT(&reloc_headers[i]);
        reloc_instrs = (decltype(reloc_instrs))(relocation_area
                                                + PEFRLH_FIRST_RELOC_OFFSET(&reloc_headers[i]));
        if(g)
            g->relocated = true;
        retval = relocate(reloc_headers,
                          PEFRLH_SECTION_INDEX(&reloc_headers[i]),
                          reloc_count,
                          reloc_instrs, imports, symbol_names,
                          closure_id, connp, arch);
    }
    if(retval == noErr && lihp->initSection != -1)
    {
        /* MacPhoenix: the init routine's transition vector, called with a
           CFragSystem7InitBlock that says where the fragment came from (its
           file stays valid while the application runs); CFM-68K and
           PowerPC alike (upstream ran PowerPC ones only on a PowerPC host). */
        uint32_t tv = guest_cast<uint32_t>(connp->sects[PEFLIH_INIT_SECTION(lihp)].start)
                      + PEFLIH_INIT_OFFSET(lihp);
        InitBlock *ib = (InitBlock *)NewPtrClear(sizeof(InitBlock));
        FSSpec *fsp = (FSSpec *)NewPtrClear(sizeof(FSSpec));
        StringPtr name = (StringPtr)NewPtrClear(64);
        const load_origin *o = current_origin;
        if(o)
        {
            name[0] = std::min<size_t>(o->name.size(), 63);
            memcpy(name + 1, o->name.data(), name[0]);
            ib->fragLocator.where = o->where;
            if(o->where == kOnDiskFlat)
            {
                *fsp = o->file;
                ib->fragLocator.u.onDisk.fileSpec = fsp;
                ib->fragLocator.u.onDisk.offset = o->offset;
                ib->fragLocator.u.onDisk.length = o->length;
            }
            else
            {
                ib->fragLocator.u.inMem.address = o->mem;
                ib->fragLocator.u.inMem.length = o->length;
            }
        }
        ib->contextID = 1;
        ib->closureID = guest_cast<uint32_t>((Ptr)closure_id);
        ib->connectionID = guest_cast<uint32_t>((Ptr)connp);
        ib->libName = name;
        uint32_t arg = guest_cast<uint32_t>((Ptr)ib);
        pending_inits.push_back({ tv, arg, arch, o ? o->name : std::string("?"), connp });
    }
    if(retval == noErr && lihp->termSection != -1)
        if(loaded_fragment *f = find_loaded(connp))
            f->term = guest_cast<uint32_t>(connp->sects[PEFLIH_TERM_SECTION(lihp)].start)
                      + PEFLIH_TERM_OFFSET(lihp);
    if(retval == noErr)
        *mainAddrp = lihp->mainSection == -1 ? 0
            : (guest_cast<uint32_t>(connp->sects[PEFLIH_MAIN_SECTION(lihp)].start) + PEFLIH_MAIN_OFFSET(lihp));
    return retval;
}

static OSErr
do_pef_section(ConnectionID connp, const void *addr,
               const PEFSectionHeader *sections, int i,
               bool instantiate_p,
               syn68k_addr_t *mainAddrp, OSType arch)
{
    OSErr retval;
    const PEFSectionHeader *shp;
    syn68k_addr_t default_address;
    uint32_t total_size;
    uint32_t packed_size;
    uint32_t unpacked_size;
    uint32_t section_offset;
    int share_kind;
    int alignment;

    shp = &sections[i];

    /* MacPhoenix: the instance goes anywhere; relocations make up for a
       default address other than 0 (relocate, section_base). */
    default_address = 0;
    connp->sects[i].default_address = PEFSH_DEFAULT_ADDRESS(shp);
    total_size = PEFSH_TOTAL_SIZE(shp);
    packed_size = PEFSH_PACKED_SIZE(shp);
    unpacked_size = PEFSH_UNPACKED_SIZE(shp);
    section_offset = PEFSH_CONTAINER_OFFSET(shp);
    share_kind = PEFSH_SHARE_KIND(shp);
    alignment = PEFSH_ALIGNMENT(shp);

    int kind = PEFSH_SECTION_KIND(shp);
    bool global = share_kind == kPEFGlobalShare && current_origin
        && current_origin->where == kOnDiskFlat
        && (kind == kPEFUnpackedDataSection || kind == kPEFPatternDataSection
            || kind == kPEFExecutableDataSection);
    if(global)
    {
        if(global_section *g = find_global_section(i))
        {
            connp->sects[i].start = g->start;
            connp->sects[i].length = g->length;
            connp->sects[i].perms = readable_section | writable_section
                | (kind == kPEFExecutableDataSection ? executable_section : 0);
            return noErr;
        }
        loading_global_section = true;
    }

    switch(kind)
    {
        case kPEFCodeSection:
            connp->sects[i].perms = readable_section | executable_section;
            goto unpacked_common;

        case kPEFUnpackedDataSection:
            connp->sects[i].perms = readable_section | writable_section;
            goto unpacked_common;

        case kPEFConstantSection:
            connp->sects[i].perms = readable_section;
            goto unpacked_common;

        case kPEFExecutableDataSection:
            connp->sects[i].perms = (readable_section | writable_section | executable_section);
            goto unpacked_common;

        unpacked_common:
            retval = load_unpacked_section(addr, default_address, total_size,
                                           packed_size, unpacked_size,
                                           section_offset, share_kind, alignment,
                                           &connp->sects[i]);
            break;
        case kPEFPatternDataSection:
            connp->sects[i].perms = readable_section | writable_section;
            retval = load_pattern_section(addr, default_address, total_size,
                                          packed_size, unpacked_size,
                                          section_offset, share_kind, alignment,
                                          &connp->sects[i]);
            break;
        case kPEFLoaderSection:
            retval = load_loader_section(addr, default_address, total_size,
                                         packed_size, unpacked_size,
                                         section_offset, share_kind, alignment,
                                         mainAddrp, arch, connp);
            break;
        default:
            warning_unexpected("%d", PEFSH_SECTION_KIND(shp));
            retval = noErr;
            break;
    }

    if(global)
    {
        loading_global_section = false;
        if(retval == noErr)
            global_sections.push_back({ current_origin->file, current_origin->offset, i,
                                        connp->sects[i].start.get(), connp->sects[i].length,
                                        false });
    }
    return retval;
}

static OSErr
do_pef_sections(ConnectionID connp, const PEFContainerHeader *headp,
                syn68k_addr_t *mainAddrp, OSType arch)
{
    OSErr retval;
    PEFSectionHeader *sections;
    int n_sects;
    int i;

    n_sects = connp->n_sects;
    sections = (decltype(sections))((char *)headp + sizeof *headp);

    memset(connp->sects, 0, sizeof connp->sects[0] * n_sects);
    for(i = 0, retval = noErr; retval == noErr && i < n_sects; ++i)
        retval = do_pef_section(connp, headp, sections, i,
                                i < PEF_CONTAINER_INSTSECTION_COUNT(headp),
                                mainAddrp, arch);
// #warning need to back out cleanly if a section fails to load

    if(retval == noErr)
    {
        int i;

        for(i = 0; i < n_sects; ++i)
        {
            if(connp->sects[i].length)
            {
                if(connp->sects[i].perms & executable_section)
                {
                    MakeDataExecutable(
                        connp->sects[i].start,
                        connp->sects[i].length);
                }

#if 0
                int prot;
                prot = 0;
                if((connp->sects[i].perms & readable_section) || (connp->sects[i].perms & executable_section))
                    prot |= PROT_READ;
                if(connp->sects[i].perms & writable_section)
                    prot |= PROT_WRITE;
                if(!prot)
                    prot |= PROT_NONE;
                if(mprotect(SYN68K_TO_US(connp->sects[i].start),
                            roundup(connp->sects[i].length, getpagesize()),
                            prot)
                   != 0)
                    warning_unexpected("%d", errno);
#endif                    
            }
        }
    }

    return retval;
}

ConnectionID
Executor::ROMlib_new_connection(uint32_t n_sects)
{
    ConnectionID retval;
    Size n_bytes;

    n_bytes = sizeof *retval + n_sects * sizeof(section_info_t);
    retval = (ConnectionID)NewPtrSysClear(n_bytes);
    if(retval)
        retval->n_sects = n_sects;
    return retval;
}

/* MacPhoenix: a preparation that fails leaves nothing behind, as on a
   Mac: the fragments it prepared (the container itself and every library
   its closure loaded new) are released and their init routines never run,
   so a later load prepares them again rather than finding them half done.
   InitBlocks are the init routines' arguments (load_loader_section). */
static void
release_init_block(uint32_t arg)
{
    InitBlock *ib = (InitBlock *)SYN68K_TO_US(arg);
    if(ib->fragLocator.where == kOnDiskFlat)
        DisposePtr((Ptr)ib->fragLocator.u.onDisk.fileSpec);
    DisposePtr((Ptr)ib->libName);
    DisposePtr((Ptr)ib);
}

/* A preparation that failed gives back what it loaded through
   CloseConnection's path: the fragment's connection closed, its closure's
   libraries lose theirs (the ones loaded for it go, Executor's own and
   ones already prepared stay). No termination routine runs: the ones
   initialised were terminated already. */
static void
release_fragments(const std::vector<ConnectionID>& cids, const char *why)
{
    std::vector<std::string> names;
    for(ConnectionID cid : cids)
    {
        loaded_fragment *f = find_loaded(cid);
        names.push_back(f ? f->origin.name : "");
        if(f)
            f->terminated = true;
    }
    close_connection(cids.front());
    for(size_t i = 1; i < cids.size(); i++)
        if(!find_loaded(cids[i]))
            fprintf(stderr, "CFM: %s released (%s not prepared)\n",
                    names[i].c_str(), why);
}

/* MacPhoenix: prepares the container at addr, named and placed as origin
   says, and lists it before its imports are bound (a library that imports
   its importer finds it). */
static OSErr
prepare_container(void *addr, const load_origin& origin, GUEST<ConnectionID> *connp,
                  GUEST<Ptr> *mainAddrp)
{
    OSErr retval;

    syn68k_addr_t main_addr;
    PEFContainerHeader *headp;
    ConnectionID conn;

    main_addr = 0;
    *connp = 0;

    headp = (PEFContainerHeader*)addr;

    if(PEF_CONTAINER_TAG1(headp) != "Joy!"_4
       || PEF_CONTAINER_TAG2(headp) != "peff"_4)
        return cfragFragmentFormatErr;

    OSType arch = PEF_CONTAINER_ARCHITECTURE(headp);
    if(arch != "pwpc"_4 && arch != "m68k"_4)
        return cfragFragmentFormatErr;

    if(PEF_CONTAINER_FORMAT_VERSION(headp) != 1)
        warning_unexpected("0x%x",
                           toHost(PEF_CONTAINER_FORMAT_VERSION(headp)));

    // #warning ignoring (old_dev, old_imp, current) version

    conn = ROMlib_new_connection(PEF_CONTAINER_SECTION_COUNT(headp));
    if(!conn)
        return fragNoMem;

    size_t loaded_mark = loaded.size();
    size_t init_mark = pending_inits.size();
    loaded.push_back({ origin, arch, conn, LM(ApplZone), 0, false });
    loaded.back().image = (Ptr)addr;
    loaded.back().image_length = origin.length;
    const load_origin *saved_origin = current_origin;
    load_origin origin_copy = origin;
    current_origin = &origin_copy; /* the list may grow while we bind */

    ++prepare_depth;
    retval = do_pef_sections(conn, headp, &main_addr, arch);
    --prepare_depth;
    current_origin = saved_origin;
    /* breakpoints in it, before its init routine runs */
    if(retval == noErr)
        ppc_break_fragments_changed();

    /* this preparation's fragments: this one first, then its closure's */
    std::vector<ConnectionID> closure;
    for(size_t i = loaded_mark; i < loaded.size(); i++)
        closure.push_back(loaded[i].cid);
    if(retval != noErr)
    {
        for(size_t i = init_mark; i < pending_inits.size(); i++)
            release_init_block(pending_inits[i].arg);
        pending_inits.resize(init_mark);
    }
    else if(prepare_depth == 0)
    {
        /* An init routine may load libraries of its own: they are a closure
           of their own. One that fails fails the preparation: the fragments
           already initialised are terminated, last first. */
        std::vector<pending_init> inits = std::move(pending_inits);
        pending_inits.clear();
        size_t done;
        for(done = 0; done < inits.size(); done++)
        {
            const pending_init& init = inits[done];
            uint32_t arg = init.arg;
            OSErr err = init.arch == "m68k"_4 ? (int16_t)ROMlib_call_cfm68k(init.tv, &arg)
                                              : (int16_t)callPPC(init.tv, { arg });
            if(err != noErr)
            {
                fprintf(stderr, "CFM: init routine of %s: %d\n", init.name.c_str(), err);
                retval = err;
                break;
            }
        }
        if(retval != noErr)
        {
            for(size_t i = done; i-- > 0;)
                if(loaded_fragment *f = find_loaded(inits[i].cid))
                    if(f->term && !f->terminated)
                    {
                        f->terminated = true;
                        if(f->arch == "m68k"_4)
                            ROMlib_call_cfm68k(f->term);
                        else
                            callPPC(f->term, {});
                    }
            for(const pending_init& init : inits)
                release_init_block(init.arg);
        }
    }

    fprintf(stderr, "[CFM] %s '%s' (%.4s): %d", origin.where == kInMem ? "memory" : "file",
            origin.name.c_str(), (const char *)&headp->architecture, retval);
    for(uint32_t i = 0; retval == noErr && i < conn->n_sects; i++)
        if(conn->sects[i].length)
            fprintf(stderr, " %u:%08x+%x", i, (uint32_t)guest_cast<uint32_t>(conn->sects[i].start),
                    (uint32_t)conn->sects[i].length);
    fprintf(stderr, "\n");
    if(retval != noErr)
    {
        std::string why = !origin.name.empty() || origin.where != kOnDiskFlat
            ? origin.name : pstr(origin.file.name);
        release_fragments(closure, why.c_str());
        return retval;
    }
    *connp = conn;
    *mainAddrp = guest_cast<Ptr>(main_addr);
    return noErr;
}

OSErr Executor::C_GetMemFragment(void *addr, uint32_t length, ConstStr63Param fragname,
                                 LoadFlags flags, GUEST<ConnectionID> *connp,
                                 GUEST<Ptr> *mainAddrp, Str255 errname)
{
    load_origin origin {};
    origin.where = kInMem;
    origin.mem = (Ptr)addr;
    origin.length = length;
    origin.name = pstr(fragname);
    OSErr retval = prepare_container(addr, origin, connp, mainAddrp);
    if(retval == fragLibNotFound && errname)
        set_err_name(errname);
    return retval;
}



OSErr Executor::C_GetDiskFragment(FSSpecPtr fsp, LONGINT offset,
                                  LONGINT length, ConstStr63Param fragname,
                                  LoadFlags flags, GUEST<ConnectionID> *connp,
                                  GUEST<Ptr> *mainAddrp, Str255 errname)
{
    OSErr retval;

    /* MacPhoenix: a fragment already prepared from this file and offset is
       shared (kLoadNewCopy aside); kFindLib only looks. */
    if((flags & 0xF) != kLoadNewCopy)
    {
        THz zone = LM(ApplZone);
        for(auto& f : loaded)
            if(f.zone == zone && f.origin.where == kOnDiskFlat
               && f.origin.offset == (uint32_t)offset && same_file(f.origin.file, *fsp))
            {
                ++f.refs;
                *connp = f.cid;
                *mainAddrp = main_address(f.cid);
                return noErr;
            }
    }
    if(flags == kFindLib)
        return fragConnectionIDNotFound;

    GUEST<INTEGER> rn;

    bool saved_system = loading_system_fragment;
    loading_system_fragment = is_system_file(*fsp);
    retval = FSpOpenDF(fsp, fsRdPerm, &rn);
    if(retval == noErr)
    {
        GUEST<LONGINT> eof;
        GetEOF(rn, &eof);

        /* MacPhoenix: the container at offset, length bytes (kWholeFork: to
           the end); was the whole data fork, so a fat file's second
           container never loaded. */
        LONGINT len = length == kWholeFork ? eof - offset : length;
        Ptr p = len > 0 ? cfm_new_ptr(len) : nullptr;
        if(!p)
            retval = len > 0 ? fragNoMem : cfragFragmentFormatErr;
        else
        {
            GUEST<LONGINT> count = len;
            retval = SetFPos(rn, fsFromStart, offset);
            if(retval == noErr)
                retval = FSRead(rn, &count, p);
        }
        FSClose(rn);

        if(retval == noErr)
        {
            load_origin origin {};
            origin.where = kOnDiskFlat;
            origin.file = *fsp;
            origin.offset = offset;
            origin.length = len;
            origin.name = pstr(fragname);
            retval = prepare_container(p, origin, connp, mainAddrp);
            if(retval == noErr)
                if(loaded_fragment *f = find_loaded(*connp))
                    f->owns_image = true;
        }
        if(retval != noErr && p)
            DisposePtr(p);
        if(retval == fragLibNotFound && errname)
            set_err_name(errname);
    }
    loading_system_fragment = saved_system;
    return retval;
}


static bool
cfrg_match(const cfir_t *cfirp, GUEST<OSType> arch_x, uint8_t type_x, ConstStr255Param name)
{
    bool retval;

    retval = (CFIR_ISA(cfirp) == arch_x && CFIR_TYPE(cfirp) == type_x && (!name[0] || EqualString(name, (StringPtr)CFIR_NAME(cfirp),
                                                                                                      false, true)));
    return retval;
}

cfir_t *
Executor::ROMlib_find_cfrg(Handle cfrg, OSType arch, uint8_t type, ConstStr255Param name)
{
    cfrg_resource_t *cfrgp;
    int n_descripts;
    cfir_t *cfirp;
    GUEST<OSType> desired_arch_x;
    uint8_t type_x;
    cfir_t *retval;

    cfrgp = (cfrg_resource_t *)*cfrg;
    cfirp = (cfir_t *)((char *)cfrgp + sizeof *cfrgp);
    desired_arch_x = arch;
    type_x = type;
    for(n_descripts = CFRG_N_DESCRIPTS(cfrgp);
        n_descripts > 0 && !cfrg_match(cfirp, desired_arch_x, type_x, name);
        --n_descripts, cfirp = (cfir_t *)((char *)cfirp + CFIR_LENGTH(cfirp)))
        ;
    retval = n_descripts > 0 ? cfirp : 0;

    return retval;
}

/* MacPhoenix: CodeFragmentDispatch's private selector -20
   (CodeFragments.yaml): a file with fragments, from an extension's INIT
   (8.x's Color Picker, Mac OS Easy Open), into the file registry. */
OSErr Executor::C_CFragRegisterFilePrivate(FSSpecPtr file, Boolean flag)
{
    warning_trace_info("CFM: file registry: \"%.*s\" (flag %d)", file->name[0], (char *)file->name + 1, flag);
    for(const FSSpec &f : registered_files)
        if(f.vRefNum == file->vRefNum && f.parID == file->parID
           && EqualString(f.name, file->name, false, true))
            return noErr;
    registered_files.push_back(*file);
    return noErr;
}

/* MacPhoenix: CodeFragmentDispatch -2 (CodeFragments.yaml): a fragment by a
   CFM 2 locator, from 68k code (8.6's AppleScript INIT). The data fork
   kind ({kind.l = 1, FSSpecPtr, offset.l, length.l}) as GetDiskFragment;
   the others, and other architectures, not here. */
OSErr Executor::C_CFragPrepareLocatorPrivate(int32_t options, Ptr locator, ConstStr63Param fragname,
                                             LoadFlags flags, OSType arch, GUEST<ConnectionID> *connp,
                                             GUEST<Ptr> *mainAddrp, Ptr flagp, Str255 errname)
{
    if(!locator || !connp || !mainAddrp)
        return paramErr;
    if(flagp)
        *flagp = 0;
    if(errname)
        errname[0] = 0;
    int32_t kind = *(GUEST<int32_t> *)locator;
    if(arch != "pwpc"_4 || kind != 1 /* kDataForkCFragLocator */)
    {
        char what[120];
        snprintf(what, sizeof what, "CFragPrepareLocatorPrivate: locator kind %d, architecture '%.4s'",
                 kind, (const char *)&arch);
        traps::log_unimplemented(what);
        return paramErr;
    }
    FSSpecPtr fsp = *(GUEST<FSSpecPtr> *)(locator + 4);
    int32_t offset = *(GUEST<int32_t> *)(locator + 8);
    int32_t length = *(GUEST<int32_t> *)(locator + 12);
    if(!fsp)
        return paramErr;
    return C_GetDiskFragment(fsp, offset, length, fragname, flags, connp, mainAddrp, errname);
}
