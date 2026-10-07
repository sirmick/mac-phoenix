#pragma once

#include <CodeFragments.h>
#include <PEFBinaryFormat.h>

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
    fragNoMem = -2809
};

struct lib_t
{
    GUEST_STRUCT;
    GUEST<ConnectionID> cid;
    GUEST<int32_t> n_symbols;
    GUEST<int32_t> first_symbol;
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

extern PEFLoaderInfoHeader *ROMlib_build_pef_hash(const map_entry_t table[],
                                                    int count);

static_assert(sizeof(section_info_t) == 16);
static_assert(sizeof(CFragConnection) == 28);
static_assert(sizeof(lib_t) == 12);
static_assert(sizeof(CFragClosure) == 4);

}