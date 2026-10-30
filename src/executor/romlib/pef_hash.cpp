/* Copyright 2000 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <rsys/cfm.h>

#include <MemoryMgr.h>

#include <PEFBinaryFormat.h>
#include <CodeFragments.h>

#include <math.h>
#include <algorithm>
#include <cstring>
#include <iostream>

using namespace Executor;


#if 0
typedef struct pef_hash
{
  uint32_t n_symbols; /* exportedSymbolCount */
  uint32_t n_hash_entries; /* 1 << exportHashTablePower */
  PEFExportedSymbolHashSlot *hash_entries; /* exportHashOffset */
  uint32_t *export_key_table; /* hash_entries + n_hash_entries */
  PEFExportedSymbol *symbol_table; /* hash_entries + 2 * n_hash_entries */
  const char *symbol_names; /* loaderStringsOffset */
}
pef_hash_t;
#endif

/*
 *
 * IM 8-43 provides C code that uses a loop to determine the exponent
 * for a given number of symbols.  This loop is equivalent:
 *
 * static UInt8
 * PEFComputeHashTableExponent (int32_t exportCount)
 * {
 *   int exponent;
 * 
 *   for (exponent = 0;
 *        exponent < kExponentLimit;
 *        ++exponent)
 *     if ((exportCount / (1 << exponent)) < kAverageChainLimit)
 *       break;
 * 
 *   return exponent;
 * }
 *
 * We get the same values by doing a little math.
 * 
 */

#if !defined(__USE_ISOC9X)

static uint32_t
floor_log2(double d)
{
    uint32_t retval;

    if(d <= 1)
        retval = 0;
    else
    {
        uint32_t u;

        u = d;
        retval = 31;
        while(!(u & 0x80000000))
        {
            u <<= 1;
            --retval;
        }
    }
    return retval;
}
#endif

static uint8_t
PEFComputeHashTableExponent(int32_t count)
{
    int retval;

    if(count < kAverageChainLimit)
        retval = 0;
    else if(count >= kAverageChainLimit * (1 << kExponentLimit))
        retval = kExponentLimit;
    else
#if defined(__USE_ISOC9X)
        retval = floor(log2((double)count / kAverageChainLimit)) + 1;
#else
        retval = floor_log2((double)count / kAverageChainLimit) + 1;
#endif

    return retval;
}

/* 8-41 has a hash-word function that will produce the same values as
   the following code */

inline int32_t PseudoRotate(int32_t x)
{
    return (x << 1) - (x >> 16);
}

static uint32_t
PEFComputeHashWord(const unsigned char *orig_p, uint32_t namemax)
{
    uint32_t retval;
    const unsigned char *p;
    unsigned char c;
    int32_t val;

    val = 0;
    for(p = orig_p; (c = *p++) && namemax-- > 0;)
        val = PseudoRotate(val) ^ c;

    retval = (((p - orig_p - 1) << kPEFHashLengthShift) | ((val ^ (val >> 16)) & kPEFHashValueMask));

    return retval;
}

inline uint32_t PEFHashTableIndex(uint32_t fullHashWord, uint32_t hashTablePower)
{
    return (fullHashWord ^ (fullHashWord >> hashTablePower)) & ((1 << hashTablePower) - 1);
}

/*
 * This is used for pseudo-libraries (MathLib, InterfaceLib)
 */

typedef struct
{
    int hash_index;
    uint32_t hash_word;
    GUEST<uint32_t> class_and_name_x;
    GUEST<void *> value;
} sort_entry_t;

static int
hash_index_compare(const void *p1, const void *p2)
{
    const sort_entry_t *sp1 = (const sort_entry_t *)p1;
    const sort_entry_t *sp2 = (const sort_entry_t *)p2;
    int retval;

    retval = sp1->hash_index - sp2->hash_index;
    return retval;
}

static void
update_export_hash_table(GUEST<uint32_t> *hashp, int hash_index, int first_index,
                         int run_count)
{
    uint32_t new_value;

    new_value = ((run_count << CHAIN_COUNT_SHIFT) | (first_index & FIRST_INDEX_MASK));
    hashp[hash_index] = new_value;
}

PEFLoaderInfoHeader *
Executor::ROMlib_build_pef_hash(const map_entry_t table[], int count)
{
    PEFLoaderInfoHeader *retval;
    uint32_t hash_power;
    int n_hash_entries;
    uint32_t hash_offset, export_offset, symbol_table_offset, string_table_offset;
    int hash_length, export_length, symbol_table_length, string_table_length;
    Size n_bytes_needed;
    GUEST<uint32_t> *hashp;
    GUEST<uint32_t> *exportp;
    PEFExportedSymbol *symbol_tablep;
    char *string_tablep;
    int i;

    hash_power = PEFComputeHashTableExponent(count);
    n_hash_entries = 1 << hash_power;

    hash_offset = sizeof(PEFLoaderInfoHeader);
    hash_length = sizeof(*hashp) * n_hash_entries;

    export_offset = hash_offset + hash_length;
    export_length = sizeof(*exportp) * count;

    symbol_table_offset = export_offset + export_length;
    symbol_table_length = sizeof(*symbol_tablep) * count;

    string_table_offset = symbol_table_offset + symbol_table_length;
    string_table_length = 0;
    for(i = 0; i < count; ++i)
        string_table_length += strlen(table[i].symbol_name);

    n_bytes_needed = (string_table_offset + string_table_length);

    retval = (decltype(retval))NewPtrSysClear(n_bytes_needed);
    if(retval)
    {
        sort_entry_t *sorted;
        int previous_hash_index;
        int run_count;
        int previous_index_of_first_element;
        uint32_t name_offset;

        PEFLIH_MAIN_SECTION(retval) = -1;
        PEFLIH_MAIN_OFFSET(retval) = -1;
        PEFLIH_INIT_SECTION(retval) = -1;
        PEFLIH_INIT_OFFSET(retval) = -1;
        PEFLIH_TERM_SECTION(retval) = -1;
        PEFLIH_TERM_OFFSET(retval) = -1;
        /* totalImportedSymbolCount, relocSectionCount, relocInstrOffset
	 are all already 0 */
        PEFLIH_STRINGS_OFFSET(retval) = string_table_offset;
        PEFLIH_HASH_OFFSET(retval) = hash_offset;
        PEFLIH_HASH_TABLE_POWER(retval) = hash_power;
        PEFLIH_SYMBOL_COUNT(retval) = count;

        hashp = (decltype(hashp))((char *)retval + hash_offset);
        exportp = (decltype(exportp))((char *)retval + export_offset);
        symbol_tablep = ((decltype(symbol_tablep))((char *)retval + symbol_table_offset));
        string_tablep = ((decltype(string_tablep))((char *)retval + string_table_offset));
        sorted = (sort_entry_t *)alloca(sizeof *sorted * count);
        name_offset = 0;
        for(i = 0; i < count; ++i)
        {
            int length;

            sorted[i].hash_word = PEFComputeHashWord((unsigned char *)table[i].symbol_name,
                                                     strlen(table[i].symbol_name));
            sorted[i].hash_index = PEFHashTableIndex(sorted[i].hash_word,
                                                     hash_power);
            sorted[i].class_and_name_x = (kPEFTVectorSymbol << 24) | name_offset;
            sorted[i].value = guest_cast<void *>(table[i].value);
            length = strlen(table[i].symbol_name);
            memcpy(string_tablep + name_offset, table[i].symbol_name, length);
            name_offset += length;
        }
        qsort(sorted, count, sizeof *sorted, hash_index_compare);
        previous_hash_index = -1;
#if !defined(LETGCCWAIL)
        previous_index_of_first_element = 0;
        run_count = 0;
#endif
        for(i = 0; i < count; ++i)
        {
            if(sorted[i].hash_index == previous_hash_index)
                ++run_count;
            else
            {
                if(previous_hash_index >= 0)
                    update_export_hash_table(hashp, previous_hash_index,
                                             previous_index_of_first_element,
                                             run_count);
                run_count = 1;
                previous_hash_index = sorted[i].hash_index;
                previous_index_of_first_element = i;
            }
            exportp[i] = sorted[i].hash_word;
            PEFEXS_CLASS_AND_NAME(&symbol_tablep[i]) = sorted[i].class_and_name_x;
            PEFEXS_SYMBOL_VALUE(&symbol_tablep[i]) = guest_cast<uint32_t>(sorted[i].value);
            PEFEXS_SECTION_INDEX(&symbol_tablep[i]) = -2;
        }
        update_export_hash_table(hashp, previous_hash_index,
                                 previous_index_of_first_element,
                                 run_count);
    }
    return retval;
}

#if 0
PEFExportedSymbol *
lookup_by_index (const pef_hash_t *hashp, int index,
		 const char **namep, int *namelen)
{
  PEFExportedSymbol *retval;

  if (index >= hashp->n_symbols)
    retval = nullptr;
  else
    {
      int name_offset;

      retval = &hashp->symbol_table[index];
      name_offset = retval->classAndName & NAME_MASK;
      *namep = hashp->symbol_names + name_offset;
      *namelen = hashp->export_key_table[index] >> 16;
    }
  
  return retval;
}
#endif

#define SYMBOL_NAME(pefexp, str) (str + PEFEXS_NAME(pefexp))

static PEFExportedSymbol *
lookup_by_name(const ConnectionID connp,
               const char *name, int name_len)
{
    uint32_t hash_word;
    uint32_t hash_index;
    uint32_t chain_count_and_first_index;
    int chain_count;
    int index;
    int past_index;
    GUEST<uint32_t> hash_word_swapped;
    PEFExportedSymbol *retval;
    PEFLoaderInfoHeader *lihp;
    GUEST<uint32_t> *hash_entries;
    GUEST<uint32_t> *export_key_table;
    PEFExportedSymbol *symbol_table;
    uint32_t offset;
    const char *string_tablep;

#if 1
    // FIXME: #warning get rid of this eventually

    if(connp == (ConnectionID)0x12348765)
        return nullptr;

#endif

    lihp = connp->lihp;

    hash_word = PEFComputeHashWord((unsigned char *)name, name_len);
    hash_index = PEFHashTableIndex(hash_word, PEFLIH_HASH_TABLE_POWER(lihp));

    offset = PEFLIH_HASH_OFFSET(lihp);
    hash_entries = (decltype(hash_entries))((char *)lihp + offset);

    offset += sizeof *hash_entries * (1 << PEFLIH_HASH_TABLE_POWER(lihp));
    export_key_table = (decltype(export_key_table))((char *)lihp + offset);

    offset += sizeof *export_key_table * PEFLIH_SYMBOL_COUNT(lihp);
    symbol_table = (decltype(symbol_table))((char *)lihp + offset);

    offset = PEFLIH_STRINGS_OFFSET(lihp);
    string_tablep = (decltype(string_tablep))((char *)lihp + offset);

    chain_count_and_first_index = hash_entries[hash_index];
    chain_count = ((chain_count_and_first_index >> CHAIN_COUNT_SHIFT)
                   & CHAIN_COUNT_MASK);
    index = ((chain_count_and_first_index >> FIRST_INDEX_SHIFT)
             & FIRST_INDEX_MASK);
    hash_word_swapped = hash_word;
    for(past_index = index + chain_count;
        index < past_index && (export_key_table[index] != hash_word_swapped || strncmp(name, SYMBOL_NAME(&symbol_table[index], string_tablep), name_len) != 0);
        ++index)
        ;
    if(index >= past_index)
        retval = nullptr;
    else
        retval = &symbol_table[index];

    if(!retval)
    {
        std::cerr << "Symbol not found: " << std::string(name, name+name_len) << std::endl;
    }

    return retval;
}

/* MacPhoenix: a symbol's address, as FindSymbol computes it */
static Ptr symbol_address(ConnectionID connID, const PEFExportedSymbol *pefs)
{
    int section_index = PEFEXS_SECTION_INDEX(pefs);
    uint32_t val = PEFEXS_SYMBOL_VALUE(pefs);
    switch(section_index)
    {
        case -2: /* absolute address */
        case -3: /* re-exported */
            return guest_cast<Ptr>(val);
        default:
        {
            if(section_index < 0 || (uint32_t)section_index >= connID->n_sects)
                return nullptr;
            GUEST<void *> sect_start = connID->sects[section_index].start;
            return val + guest_cast<Ptr>(sect_start);
        }
    }
}

/* MacPhoenix: whether a connection's export tables look whole (in RAM,
   counts that fit): the PC sampler looks at connections at any moment,
   some of a process whose heap is going away. */
bool Executor::ROMlib_cfm_exports_sane(ConnectionID id)
{
    if(!id || !id->lihp)
        return false;
    PEFLoaderInfoHeader *lihp = id->lihp;
    uint32_t at = ptr_to_longint(lihp), top = guest_cast<int32_t>(LM(MemTop));
    uint32_t count = PEFLIH_SYMBOL_COUNT(lihp), power = PEFLIH_HASH_TABLE_POWER(lihp);
    uint32_t hash = PEFLIH_HASH_OFFSET(lihp), strings = PEFLIH_STRINGS_OFFSET(lihp);
    if(at >= top || count > 65536 || power > 16 || hash > 0x1000000 || strings > 0x1000000)
        return false;
    uint32_t end = hash + 4 * (1u << power) + 4 * count + sizeof(PEFExportedSymbol) * count;
    return at + end <= top && at + strings <= top;
}

/* MacPhoenix: the exports in their table's order (Inside Macintosh:
   PowerPC System Software 3-, CountSymbols and GetIndSymbol, index from 0):
   the loader section's export key table gives each name's length, the
   symbol table its class, string and value. */
OSErr Executor::C_CountSymbols(ConnectionID id, GUEST<LONGINT> *countp)
{
    if(!id || !id->lihp)
        return fragConnectionIDNotFound;
    *countp = PEFLIH_SYMBOL_COUNT((PEFLoaderInfoHeader *)id->lihp);
    return noErr;
}

OSErr Executor::C_GetIndSymbol(ConnectionID id, LONGINT index, Str255 name,
                               GUEST<Ptr> *addrp, SymClass *classp)
{
    if(!id || !id->lihp)
        return fragConnectionIDNotFound;
    PEFLoaderInfoHeader *lihp = id->lihp;
    if(index < 0 || index >= (LONGINT)PEFLIH_SYMBOL_COUNT(lihp))
        return fragSymbolNotFound;
    uint32_t offset = PEFLIH_HASH_OFFSET(lihp)
        + sizeof(GUEST<uint32_t>) * (1 << PEFLIH_HASH_TABLE_POWER(lihp));
    auto *export_key_table = (GUEST<uint32_t> *)((char *)lihp + offset);
    offset += sizeof(GUEST<uint32_t>) * PEFLIH_SYMBOL_COUNT(lihp);
    auto *symbol_table = (PEFExportedSymbol *)((char *)lihp + offset);
    const char *strings = (const char *)lihp + PEFLIH_STRINGS_OFFSET(lihp);
    PEFExportedSymbol *pefs = &symbol_table[index];
    if(name)
    {
        int len = std::min<uint32_t>(export_key_table[index] >> 16, 255);
        name[0] = len;
        memcpy(name + 1, SYMBOL_NAME(pefs, strings), len);
    }
    if(classp)
        *classp = *(SymClass *)&PEFEXS_CLASS_AND_NAME(pefs);
    if(addrp)
        *addrp = symbol_address(id, pefs);
    return noErr;
}

OSErr Executor::C_FindSymbol(ConnectionID connID, ConstStr255Param symName,
                             GUEST<Ptr> *symAddr, SymClass *symClass)
{
    OSErr retval;
    PEFExportedSymbol *pefs;

    pefs = lookup_by_name(connID, (char *)symName + 1, symName[0]);
    if(!pefs)
        retval = fragSymbolNotFound;
    else
    {
        if(symClass)
            *symClass = *(SymClass *)&PEFEXS_CLASS_AND_NAME(pefs);
        if(PEFEXS_SECTION_INDEX(pefs) == -3) /* re-exported */
            warning_unimplemented("name = '%.*s', val = 0x%x", symName[0],
                                  symName + 1, (unsigned)PEFEXS_SYMBOL_VALUE(pefs));
        *symAddr = symbol_address(connID, pefs);

        retval = noErr;
    }
    return retval;
}
