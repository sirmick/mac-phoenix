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
#include <PEFBinaryFormat.h>
#include <file/file.h>
#include <rsys/launch.h>
#include <hfs/hfs.h>
#include <util/string.h>
#include <base/builtinlibs.h>
#include <base/cpu.h>
#include <clib/clib.h>
#include <algorithm>
#include <string>
#include <vector>
#include <strings.h>

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
};

std::vector<loaded_fragment> loaded;
const load_origin *current_origin;

bool same_name(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && !strncasecmp(a.data(), b.data(), a.size());
}

bool same_file(const FSSpec& a, const FSSpec& b)
{
    return a.vRefNum == b.vRefNum && a.parID == b.parID
        && EqualString(a.name, b.name, false, true);
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
}

void Executor::ROMlib_cfm_application_start()
{
    THz zone = LM(ApplZone);
    loaded.erase(std::remove_if(loaded.begin(), loaded.end(),
        [&](const loaded_fragment& f) { return f.zone == zone; }), loaded.end());
}

static OSErr
try_to_get_memory(void **addrp, syn68k_addr_t default_syn_address,
                  uint32_t total_size, int alignment)
{
    // TODO: alignment
    *addrp = NewPtr(total_size);
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
            *cidp = f.cid;
            *mainaddrp = main_address(f.cid);
            return noErr;
        }
    return fragLibNotFound;
}

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
                /* MacPhoenix: the application's own file (and the System's)
                   was open before: leave it open. */
                if(rn != LM(CurApRefNum) && rn != LM(SysMap))
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
    for(dirindex = 1;
        retval != noErr && err != fnfErr && errcount != 3;
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
    if(retval == fragLibNotFound && loadflags == kFindLib)
        return fragLibNotFound;
    /* MacPhoenix: libraries whose routines Executor answers itself, as it
       does Open Transport, before Apple's own: the 68k ObjectSupportLib
       runs its 'proc' resources against the AppleEvent Manager's private
       selectors (docs/executor/CFM68K.md). */
    if(retval == fragLibNotFound && arch == "m68k"_4
       && EqualString(library, (StringPtr) "\020ObjectSupportLib", false, true))
        if(ConnectionID cid = builtinlibs::getBuiltinLib(library, arch))
        {
            *cidp = cid;
            *mainaddrp = nullptr;
            return noErr;
        }
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
            if(ConnectionID cid = builtinlibs::getBuiltinLib(library, arch))
            {
                *cidp = cid;
                *mainaddrp = nullptr;
                retval = noErr;
            }
        }
    }
    return retval;
}

OSErr Executor::C_CloseConnection(GUEST<ConnectionID> *cidp)
{
    warning_trace_info("cidp = %p, cid = 0x%p", cidp, toHost(*cidp));
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
                        *valp = kUnresolvedCFragSymbolAddress;
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
    sectionC = ptr_to_longint(connp->sects[0].start);
    sectionD = ptr_to_longint(connp->sects[1].start);

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
                            sectionC = ptr_to_longint(connp->sects[index].start);
                            warning_unimplemented("RelocSmSetSectC not tested much");
                            assert(0);
                            break;
                        case 2:
                            sectionD = ptr_to_longint(connp->sects[index].start);
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

    for(i = 0, retval = noErr; retval == noErr && i < n_reloc_headers; ++i)
    {
        uint32_t reloc_count;
        uint8_t(*reloc_instrs)[2];

        reloc_count = PEFRLH_RELOC_COUNT(&reloc_headers[i]);
        reloc_instrs = (decltype(reloc_instrs))(relocation_area
                                                + PEFRLH_FIRST_RELOC_OFFSET(&reloc_headers[i]));
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
        OSErr err = arch == "m68k"_4 ? (int16_t)ROMlib_call_cfm68k(tv, &arg)
                                     : (int16_t)callPPC(tv, { arg });
        if(err != noErr)
        {
            fprintf(stderr, "CFM: init routine of %s: %d\n", o ? o->name.c_str() : "?", err);
            retval = err;
        }
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

#if 1
    {
        uint32_t def;

        def = PEFSH_DEFAULT_ADDRESS(shp);
        if(def)
            fprintf(stderr, "***def = 0x%x***\n", def);
    }
#endif

#if 0
  default_address = PEFSH_DEFAULT_ADDRESS (shp);
#else
    default_address = 0;
// #warning defaultAddress ignored -- dont implement without testing
#endif
    total_size = PEFSH_TOTAL_SIZE(shp);
    packed_size = PEFSH_PACKED_SIZE(shp);
    unpacked_size = PEFSH_UNPACKED_SIZE(shp);
    section_offset = PEFSH_CONTAINER_OFFSET(shp);
    share_kind = PEFSH_SHARE_KIND(shp);
    alignment = PEFSH_ALIGNMENT(shp);

    switch(PEFSH_SECTION_KIND(shp))
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

    loaded.push_back({ origin, arch, conn, LM(ApplZone), 0, false });
    const load_origin *saved_origin = current_origin;
    load_origin origin_copy = origin;
    current_origin = &origin_copy; /* the list may grow while we bind */

    retval = do_pef_sections(conn, headp, &main_addr, arch);
    current_origin = saved_origin;

    fprintf(stderr, "[CFM] %s '%s' (%.4s): %d", origin.where == kInMem ? "memory" : "file",
            origin.name.c_str(), (const char *)&headp->architecture, retval);
    for(uint32_t i = 0; retval == noErr && i < conn->n_sects; i++)
        if(conn->sects[i].length)
            fprintf(stderr, " %u:%08x+%x", i, (uint32_t)guest_cast<uint32_t>(conn->sects[i].start),
                    (uint32_t)conn->sects[i].length);
    fprintf(stderr, "\n");
    if(retval != noErr)
    {
        loaded.erase(std::remove_if(loaded.begin(), loaded.end(),
            [&](const loaded_fragment& f) { return f.cid == conn; }), loaded.end());
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
    return prepare_container(addr, origin, connp, mainAddrp);
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
                *connp = f.cid;
                *mainAddrp = main_address(f.cid);
                return noErr;
            }
    }
    if(flags == kFindLib)
        return fragConnectionIDNotFound;

    GUEST<INTEGER> rn;

    retval = FSpOpenDF(fsp, fsRdPerm, &rn);
    if(retval == noErr)
    {
        GUEST<LONGINT> eof;
        GetEOF(rn, &eof);

        /* MacPhoenix: the container at offset, length bytes (kWholeFork: to
           the end); was the whole data fork, so a fat file's second
           container never loaded. */
        LONGINT len = length == kWholeFork ? eof - offset : length;
        Ptr p = len > 0 ? NewPtr(len) : nullptr;
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
        }
        if(retval != noErr && p)
            DisposePtr(p);
    }
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
