#pragma once

#include <CodeFragments.h>
#include <PEFBinaryFormat.h>
#include <MixedMode.h>

namespace Executor
{

/* NOTE: The following data structures are just for proof-of-concept
   use.  They are not going to be compatible with what is really on a
   Macintosh, but they may be good enough to get PS5.5 limping. */

struct section_info_t
{
    GUEST_STRUCT;
    GUEST<void*> start;
    GUEST<uint32_t> length;
    GUEST<uint32_t> ref_count;
    GUEST<uint8_t> perms;
    GUEST<uint8_t> pad[3];/* TODO: verifying that it works this way on a Mac. */
};

typedef struct CFragConnection
{
    GUEST_STRUCT;
    GUEST<FragmentLocator> frag;
    GUEST<PEFLoaderInfoHeader *> lihp;
    GUEST<uint32_t> ref_count;
    GUEST<uint32_t> n_sects;
    GUEST<section_info_t> sects[0];
} CFragConnection;

enum
{
    fragConnectionIDNotFound = -2801,
    fragSymbolNotFound = -2802,
    fragLibNotFound = -2804,
    cfragFragmentFormatErr = -2806,
    fragNoMem = -2809
};

struct lib_t
{
    GUEST_STRUCT;
    GUEST<ConnectionID> cid;
    GUEST<int32_t> n_symbols;
    GUEST<int32_t> first_symbol;
    GUEST<uint8_t> weak; /* MacPhoenix: kPEFWeakImportLibMask */
    GUEST<uint8_t> pad[3];
};

#define LIB_CID(l) ((l)->cid)


#define LIB_N_SYMBOLS(l) ((l)->n_symbols)


#define LIB_FIRST_SYMBOL(l) ((l)->first_symbol)


struct CFragClosure
{
    GUEST_STRUCT;
    GUEST<uint32_t> n_libs;
    GUEST<lib_t> libs[0];
};

#define N_LIBS(c) ((c)->n_libs)


struct map_entry_t
{
    const char *symbol_name;
    void *value;
};


extern char *ROMlib_p2cstr(StringPtr str);


extern cfir_t *ROMlib_find_cfrg(Handle cfrg, OSType arch, uint8_t type,
                                ConstStr255Param name);


extern ConnectionID ROMlib_new_connection(uint32_t n_sects);

/* MacPhoenix: CFM-68K (docs/executor/CFM68K.md). Calls a transition vector
   on the 68k CPU (A1 the vector + 4, so the routine loads its A5; A5 and the
   stack restored afterwards), with one stack argument if `arg` is given;
   answers D0. */
extern uint32_t ROMlib_call_cfm68k(uint32_t tvector, const uint32_t *arg = nullptr);

/* The term routines of the fragments the current application prepared, last
   prepared first (ExitToShell), and forgetting them when its partition goes
   (lo..hi) or a new application starts in the same zone. */
extern void ROMlib_cfm_terminate();
extern void ROMlib_cfm_process_exit(uint32_t part_lo, uint32_t part_hi);
extern void ROMlib_cfm_application_start();

/* CallUniversalProc from CFM-68K code: the routine's arguments start at
   args, 4 bytes each (mixed_mode.cpp). */
extern uint32_t ROMlib_cfm68k_call_universal_proc(UniversalProcPtr theProcPtr,
                                                  ProcInfoType procinfo, uint32_t args);

extern PEFLoaderInfoHeader *ROMlib_build_pef_hash(const map_entry_t table[],
                                                    int count);

static_assert(sizeof(section_info_t) == 16);
static_assert(sizeof(CFragConnection) == 28);
static_assert(sizeof(lib_t) == 16);
static_assert(sizeof(CFragClosure) == 4);

}