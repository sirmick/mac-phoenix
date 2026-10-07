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
#include <algorithm>

using namespace Executor;

typedef int share_kind_t;

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

static OSErr
check_existing_connections(ConstStr63Param library, OSType arch, LoadFlags loadflags,
                           GUEST<ConnectionID> *cidp, GUEST<Ptr> *mainaddrp, Str255 errName)
{
    /* TODO */
    return fragLibNotFound;
}

static void
get_root_and_app(INTEGER *root_vrefp, LONGINT *root_diridp,
                 INTEGER *app_vrefp, LONGINT *app_diridp)
{
    /* TODO */
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
                        retval = GetDiskFragment(&fs,
                                                 CFIR_OFFSET_TO_FRAGMENT(cfirp),
                                                 CFIR_FRAGMENT_LENGTH(cfirp), (Byte*)"",
                                                 loadflags, cidp, mainaddrp,
                                                 errName);
                }
                CloseResFile(rn);
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

    retval = check_existing_connections(library, arch, loadflags, cidp,
                                        mainaddrp, errName);
    if(retval == fragLibNotFound)
    {
        INTEGER root_vref, app_vref;
        LONGINT root_dirid, app_dirid;

        get_root_and_app(&root_vref, &root_dirid, &app_vref, &app_dirid);
        if(root_vref != app_vref || root_dirid != app_dirid)
            retval = check_vanddir(root_vref, root_dirid, 0, library, arch,
                                   loadflags, cidp, mainaddrp, errName);
        if(retval != noErr)
            retval = check_file(ROMlib_exevrefnum, 0, ROMlib_exefname, false,
                                library, arch, loadflags, cidp, mainaddrp,
                                errName);
        if(retval != noErr)
            retval = check_vanddir(app_vref, app_dirid, 0, library, arch,
                                   loadflags, cidp, mainaddrp, errName);
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
            if(ConnectionID cid = builtinlibs::getBuiltinLib(library))
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
              const char *symbol_names, CFragClosureID closure_id)
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
                    if(flags & 8)   // weak symbol
                        *valp = kUnresolvedCFragSymbolAddress;
                    else
                        *valp = builtinlibs::makeUndefinedSymbolStub(symbol_name);
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
         CFragClosureID closure_id, ConnectionID connp)
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
                                                       closure_id);
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
                                                   closure_id);
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
                                                           closure_id);
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
                          closure_id, connp);
    }
    if(retval == noErr && lihp->initSection != 0xffffffff)
    {
        GUEST<uint32_t> *init_addr;
        InitBlock init_block;

        warning_unimplemented("register preservation, bad init_block");
        // #warning this code has a lot of problems (register preservation, bad init_block)

        init_addr = (GUEST<uint32_t> *)SYN68K_TO_US(guest_cast<uint32_t>(connp->sects[PEFLIH_INIT_SECTION(lihp)].start) + PEFLIH_INIT_OFFSET(lihp));

        memset(&init_block, 0xFA, sizeof init_block);
        //init_routine = (uint32_t(*)(uint32_t))SYN68K_TO_US(init_addr[0]);
        //init_toc = (uint32_t)SYN68K_TO_US(init_addr[1]);
#if defined(powerpc) || defined(__ppc__)
        retval = ppc_call(init_toc, init_routine, (uint32_t)&init_block);
#else
        warning_unexpected("");
        retval = paramErr;
#endif
        if(retval)
        {
            warning_unexpected("%d", retval);
            retval = noErr;
        }
    }
    if(retval == noErr)
        *mainAddrp = (guest_cast<uint32_t>(connp->sects[PEFLIH_MAIN_SECTION(lihp)].start) + PEFLIH_MAIN_OFFSET(lihp));
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

OSErr Executor::C_GetMemFragment(void *addr, uint32_t length, ConstStr63Param fragname,
                                 LoadFlags flags, GUEST<ConnectionID> *connp,
                                 GUEST<Ptr> *mainAddrp, Str255 errname)
{
    OSErr retval;

    syn68k_addr_t main_addr;
    PEFContainerHeader *headp;
    ConnectionID conn;

    warning_unimplemented("ignoring flags = 0x%x\n", flags);

    main_addr = 0;
    *connp = 0;

    headp = (PEFContainerHeader*)addr;

    if(PEF_CONTAINER_TAG1(headp) != "Joy!"_4)
        warning_unexpected("0x%x", toHost(PEF_CONTAINER_TAG1(headp)));

    if(PEF_CONTAINER_TAG2(headp) != "peff"_4)
        warning_unexpected("0x%x", toHost(PEF_CONTAINER_TAG2(headp)));

    if(PEF_CONTAINER_ARCHITECTURE(headp)
       != "pwpc"_4)
        warning_unexpected("0x%x",
                           toHost(PEF_CONTAINER_ARCHITECTURE(headp)));

    if(PEF_CONTAINER_FORMAT_VERSION(headp) != 1)
        warning_unexpected("0x%x",
                           toHost(PEF_CONTAINER_FORMAT_VERSION(headp)));

    // #warning ignoring (old_dev, old_imp, current) version

    conn = ROMlib_new_connection(PEF_CONTAINER_SECTION_COUNT(headp));
    if(!conn)
        retval = fragNoMem;
    else
        retval = do_pef_sections(conn, headp, &main_addr,
                                 PEF_CONTAINER_ARCHITECTURE(headp));
    *connp = conn;

    if(retval == noErr)
        *mainAddrp = guest_cast<Ptr>(main_addr);

    return retval;
}



OSErr Executor::C_GetDiskFragment(FSSpecPtr fsp, LONGINT offset,
                                  LONGINT length, ConstStr63Param fragname,
                                  LoadFlags flags, GUEST<ConnectionID> *connp,
                                  GUEST<Ptr> *mainAddrp, Str255 errname)
{
    OSErr retval;

    warning_unimplemented("ignoring flags = 0x%x\n", flags);

    GUEST<INTEGER> rn;

    retval = FSpOpenDF(fsp, fsRdPerm, &rn);
    if(retval == noErr)
    {
        GUEST<LONGINT> len;
            
        retval = GetEOF(rn, &len);

        Ptr p = NewPtr(len);
        FSRead(rn, &len, p);

        retval = GetMemFragment(p, len, fragname, flags, connp,
                                mainAddrp, errname);
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
