/* Copyright 1986, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <osevent/osevent.h>

#include <AppleEvents.h>
#include <MemoryMgr.h>
#include <SegmentLdr.h>

#include <error/system_error.h>
#include <mman/mman.h>
#include "apple_events.h"
#include <base/functions.impl.h>

using namespace Executor;

/* MacPhoenix: Executor's own handler tables. ExpandMem's Apple event
   fields (+$154 and +$17C) belong to the guest's Apple Event Manager:
   AppleScript installs Apple's PACK 8, which keeps its own (differently
   laid out) tables there and loops forever on Executor's. */
static AE_info_t *ae_private;

AE_info_t *Executor::ROMlib_ae_private()
{
    return ae_private;
}

static AE_zone_tables_h new_zone_tables(THz zone);

inline AE_zone_tables_h get_zone_tables(bool system_p)
{
    auto info = ae_private;
    if(system_p)
        return info->system_zone_tables;
    /* MacPhoenix: before the first application (9.0.4's AppleScript
       component sends itself an event from its register routine at
       startup): tables of the startup's own in the system heap, which the
       first launch replaces (AE_reinit). */
    if(!info->appl_zone_tables)
        info->appl_zone_tables = new_zone_tables(LM(SysZone));
    return info->appl_zone_tables;
}
#define hdlr_table(system_p, class) (*get_zone_tables(system_p))->class##_hdlr_table

void Executor::AE_init(void)
{
    AE_info_t *info;
    OSErr err;

    /* ExpandMem, as a real 7.5.5 boot has it: version $0144, 648 bytes. */
    enum { expandmem_size = 0x288 };
    static_assert(sizeof(AE_info_t) <= expandmem_size);
    AE_info_t *em = (AE_info_t *)NewPtrSysClear(expandmem_size);
    *(GUEST<uint16_t> *)em = 0x0144;
    *(GUEST<uint32_t> *)((char *)em + 2) = expandmem_size;
    em->sys_heap_reserve = 0x10000;
    /* MacPhoenix: ExpandMem's key cache, as the Script Manager sets it. */
    em->emKeyCache = ROMlib_kchr_ptr();
    LM(ExpandMem) = em;

    info = (AE_info_t *)NewPtrSysClear(sizeof *info);

    TheZoneGuard guard(LM(SysZone));

    AE_zone_tables_h zone_tables;

    zone_tables
        = (AE_zone_tables_h)NewHandleClear(sizeof(AE_zone_tables_t));

    err = _AE_hdlr_table_alloc(16, 0x80008, 0,
                               /* #### */
                               false,
                               &(*zone_tables)->event_hdlr_table);
    err = _AE_hdlr_table_alloc(16, 0x80008, 0,
                               /* #### */
                               false,
                               &(*zone_tables)->coercion_hdlr_table);
    err = _AE_hdlr_table_alloc(16, 0x80008, 0,
                               /* #### */
                               false,
                               &(*zone_tables)->special_hdlr_table);
    info->system_zone_tables = zone_tables;
    ae_private = info;
}

static AE_zone_tables_h new_zone_tables(THz zone)
{
    TheZoneGuard guard(zone);

    AE_zone_tables_h zone_tables
        = (AE_zone_tables_h)NewHandleClear(sizeof(AE_zone_tables_t));

    _AE_hdlr_table_alloc(16, 0x80008, 0,
                         /* #### */
                         false,
                         &(*zone_tables)->event_hdlr_table);
    _AE_hdlr_table_alloc(16, 0x80008, 0,
                         /* #### */
                         false,
                         &(*zone_tables)->coercion_hdlr_table);
    _AE_hdlr_table_alloc(16, 0x80008, 0,
                         /* #### */
                         false,
                         &(*zone_tables)->special_hdlr_table);
    return zone_tables;
}

void Executor::AE_reinit(void)
{
    ae_private->appl_zone_tables = new_zone_tables(LM(ApplZone));
}

/* MacPhoenix: an entry's value size (_AE_hdlr_table_alloc's) */
static int32_t value_size(AE_hdlr_table_h table)
{
    int32_t sizes = (*table)->pad_2[0];
    return sizes ? (uint32_t)sizes >> 16 : sizeof(AE_hdlr_t);
}

static OSErr
hdlr_table_elt(AE_hdlr_table_h table,
               AE_hdlr_selector_t *selector, AE_hdlr_t *hdlr,
               bool create_p,
               AE_hdlr_table_elt_t **retval)
{
    AE_hdlr_table_elt_t *elts, *elt;
    int32_t n_elts, elt_index;

    elts = AE_TABLE_ELTS(table);
    n_elts = AE_TABLE_N_ELTS(table);

    for(elt_index = 0; elt_index < n_elts; elt_index++)
    {
        elt = &elts[elt_index];

        if((hdlr == nullptr
            || hdlr->fn == elt->hdlr.fn)
           && elt->selector.sel0 == selector->sel0
           && elt->selector.sel1 == selector->sel1)
        {
            *retval = elt;
            AE_RETURN_ERROR(noErr);
        }
    }

    if(create_p)
    {
        n_elts++;

        /* AE_hdlr_table_alloc's capacity argument reserves room up front
           (HyperCard preallocates for 175/105 handlers); only grow the
           handle once the reservation is used up. */
        int32_t n_bytes = sizeof(AE_hdlr_table_t) + n_elts * sizeof(AE_hdlr_table_elt_t);
        if(n_bytes > AE_TABLE_N_ALLOCATED_BYTES(table))
        {
            SetHandleSize((Handle)table, n_bytes);

            if(LM(MemErr) != noErr)
                AE_RETURN_ERROR(LM(MemErr));

            AE_TABLE_N_ALLOCATED_BYTES(table) = n_bytes;
        }

        AE_TABLE_N_ELTS(table) = n_elts;

        elts = AE_TABLE_ELTS(table);
        elt = &elts[n_elts - 1];

        elt->pad_1 = -1;
        elt->selector = *selector;
        elt->pad_2 = -1;

        *retval = elt;
        AE_RETURN_ERROR(noErr);
    }

    AE_RETURN_ERROR(errAEHandlerNotFound);
}

OSErr Executor::C__AE_hdlr_table_alloc(int32_t n_elts, int32_t unknown_2,
                                       int32_t unknown_3, int8_t unknown_p,
                                       GUEST<AE_hdlr_table_h> *table_return)
{
    AE_hdlr_table_h table;

    /* MacPhoenix: unknown_2 is the entries' sizes, value << 16 | key: the
       handler tables' are 8 and 8 (class and ID; handler and refcon); 8.6's
       AppleScript INIT asks 12-byte values (and unknown_p 1). An entry
       holds a key of up to 8 bytes and a value of up to 12. */
    int32_t key_size = unknown_2 & 0xFFFF, value_size = (uint32_t)unknown_2 >> 16;
    if(key_size == 0 || key_size > 8 || value_size == 0 || value_size > 12 || unknown_3 != 0)
    {
        fprintf(stderr, "[Executor] _AE_hdlr_table_alloc: entries of %d-byte keys and %d-byte values, "
                        "unknown_3 %d: answered paramErr\n", key_size, value_size, unknown_3);
        AE_RETURN_ERROR(paramErr);
    }

    /* #### unknown_p is currently ignored */

    /* n_elts is the initial capacity to reserve, not an element count
       (the table starts out empty either way): the ROM always passes 16,
       HyperCard 2.4.1 preallocates room for 175 and 105 handlers so its
       many AE_hdlr_install calls don't grow the handle one element at a
       time. hdlr_table_elt() below only resizes once this reservation
       is used up. */
    int32_t n_bytes = sizeof(AE_hdlr_table_t) + n_elts * sizeof(AE_hdlr_table_elt_t);
    table = (AE_hdlr_table_h)NewHandleClear(n_bytes);
    if(LM(MemErr) != noErr)
        AE_RETURN_ERROR(LM(MemErr));

    AE_TABLE_N_ALLOCATED_BYTES(table) = n_bytes;
    AE_TABLE_N_ELTS(table) = 0;
    (*table)->pad_2[0] = unknown_2;

    *table_return = table;
    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C__AE_hdlr_delete(AE_hdlr_table_h table, int32_t unknown_1,
                                  AE_hdlr_selector_t *selector)
{
    AE_hdlr_table_elt_t *elt, *elts;
    int n_elts, elt_offset;
    OSErr err;

    gui_assert(unknown_1 == 0);

    err = hdlr_table_elt(table, selector, nullptr, false, &elt);
    if(err != noErr)
        AE_RETURN_ERROR(err);

    elts = AE_TABLE_ELTS(table);
    n_elts = AE_TABLE_N_ELTS(table);
    elt_offset = elt - elts;

    memmove(&elts[elt_offset], &elts[elt_offset + 1],
            (n_elts - elt_offset - 1) * sizeof *elts);

    AE_TABLE_N_ELTS(table) = n_elts - 1;

    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C__AE_hdlr_lookup(AE_hdlr_table_h table, int32_t unknown_1,
                                  AE_hdlr_selector_t *selector,
                                  AE_hdlr_t *hdlr_return)
{
    AE_hdlr_table_elt_t *elt;
    OSErr err;

    gui_assert(unknown_1 == 0);

    err = hdlr_table_elt(table, selector, nullptr, false, &elt);
    if(err != noErr)
        AE_RETURN_ERROR(err);

    memcpy(hdlr_return, &elt->hdlr, value_size(table));
    AE_RETURN_ERROR(noErr);
}

/* MacPhoenix: Pack8 $0830, beside install ($0831), delete ($0832) and
   lookup ($0833): a lookup, its key and value buffers laid out as
   _AE_hdlr_lookup's. HyperCard 2.4.1 calls it on its fresh tables, an
   event handler's key ('aevt' 'oapp') and a property's ('pnam' '????'),
   and goes on when they aren't there; unknown before, it stopped the
   session ("Unknown selector 0x830 for trap Pack8") and HyperCard never
   came up. */
OSErr Executor::C__AE_hdlr_0830(AE_hdlr_table_h table, int32_t unknown_1,
                                AE_hdlr_selector_t *selector, AE_hdlr_t *hdlr_return)
{
    AE_hdlr_table_elt_t *elt;
    (void)unknown_1;
    if(!table || !selector)
        AE_RETURN_ERROR(paramErr);
    OSErr err = hdlr_table_elt(table, selector, nullptr, false, &elt);
    if(err != noErr)
        AE_RETURN_ERROR(err);
    if(hdlr_return)
        memcpy(hdlr_return, &elt->hdlr, value_size(table));
    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C__AE_hdlr_install(AE_hdlr_table_h table, int32_t unknown_1,
                                   AE_hdlr_selector_t *selector,
                                   AE_hdlr_t *hdlr)
{
    AE_hdlr_table_elt_t *elt;
    OSErr err;

    gui_assert(unknown_1 == 0);

    /* MacPhoenix: a table of other values (AppleScript's): the key's entry,
       whatever its value */
    bool handlers = value_size(table) == sizeof(AE_hdlr_t);
    err = hdlr_table_elt(table, selector, handlers ? hdlr : nullptr, true, &elt);
    if(err != noErr)
        AE_RETURN_ERROR(err);

    memcpy(&elt->hdlr, hdlr, value_size(table));
    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AEInstallEventHandler(AEEventClass event_class,
                                        AEEventID event_id,
                                        AEEventHandlerUPP hdlr_fn,
                                        int32_t refcon,
                                        Boolean system_handler_p)
{
    AE_hdlr_table_h table;
    AE_hdlr_table_elt_t *elt;
    AE_hdlr_selector_t selector;
    AE_hdlr_t hdlr;
    OSErr err;

    if(!hdlr_fn)
        AE_RETURN_ERROR(paramErr);

    table = hdlr_table(system_handler_p, event);

    selector.sel0 = event_class;
    selector.sel1 = event_id;

    hdlr.fn = (void *)hdlr_fn;
    hdlr.refcon = refcon;

    err = hdlr_table_elt(table, &selector, &hdlr, true, &elt);
    if(err != noErr)
        AE_RETURN_ERROR(err);

    /* warning: `elt' is a pointer into an unlocked handle */
    elt->hdlr = hdlr;

    AE_RETURN_ERROR(noErr);
}

/* event handler functions */

static void
dummy(void)
{
}

OSErr Executor::C_AEGetEventHandler(AEEventClass event_class,
                                    AEEventID event_id,
                                    GUEST<AEEventHandlerUPP> *hdlr,
                                    GUEST<int32_t> *refcon,
                                    Boolean system_handler_p)
{
    AE_hdlr_table_h table;
    AE_hdlr_table_elt_t *elt;
    AE_hdlr_selector_t selector;
    OSErr err;

    /* MacPhoenix: upstream put up an alert for the 'go b'/'ears' probe
       (Disk Copy 6.3.3 makes it on launch, and goes on fine when the
       answer is errAEHandlerNotFound, as a real System gives). */
    table = hdlr_table(system_handler_p, event);

    selector.sel0 = event_class;
    selector.sel1 = event_id;

    err = hdlr_table_elt(table, &selector, nullptr, false, &elt);
    if(err != noErr)
        AE_RETURN_ERROR(err);

    /* warning: `elt' is a pointer into an unlocked handle */
    *hdlr = guest_cast<AEEventHandlerUPP>(elt->hdlr.fn);
    *refcon = elt->hdlr.refcon;

    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AERemoveEventHandler(AEEventClass event_class,
                                       AEEventID event_id,
                                       AEEventHandlerUPP hdlr,
                                       Boolean system_handler_p)
{
    AE_hdlr_table_h table;
    AE_hdlr_selector_t selector;

    table = hdlr_table(system_handler_p, event);

    selector.sel0 = event_class;
    selector.sel1 = event_id;

    /* #### fail if `hdlr' is not the currently installed handler? */
    AE_RETURN_ERROR(_AE_hdlr_delete(table, 0, &selector));
}

/* coercion handler functions */

OSErr Executor::C_AEInstallCoercionHandler(
    DescType from_type, DescType to_type, AECoerceDescUPP hdlr_fn,
    int32_t refcon, Boolean from_type_is_desc_p, Boolean system_handler_p)
{
    AE_hdlr_table_h table;
    AE_hdlr_table_elt_t *elt;
    AE_hdlr_selector_t selector;
    AE_hdlr_t hdlr;
    OSErr err;

    if(hdlr_fn == nullptr)
        AE_RETURN_ERROR(paramErr);

    table = hdlr_table(system_handler_p, coercion);

    selector.sel0 = from_type;
    selector.sel1 = to_type;

    hdlr.fn = (void *)hdlr_fn;
    hdlr.refcon = refcon;

    /* MacPhoenix: the same pair again replaces the entry (another
       handler was added after it and never found) */
    err = hdlr_table_elt(table, &selector, nullptr, false, &elt);
    if(err != noErr)
        err = hdlr_table_elt(table, &selector, &hdlr, true, &elt);
    if(err != noErr)
        AE_RETURN_ERROR(err);

    /* warning: `elt' is a pointer into an unlocked handle */
    elt->hdlr = hdlr;

    /* MacPhoenix: whether the handler takes a descriptor, in the entry's
       spare long (AECoerceDesc read an unset flag and called handlers
       either way) */
    elt->pad_2 = from_type_is_desc_p ? 1 : 0;

    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AEGetCoercionHandler(DescType from_type, DescType to_type,
                                       GUEST<AECoerceDescUPP> *hdlr_out,
                                       GUEST<int32_t> *refcon_out,
                                       GUEST<Boolean> *from_type_is_desc_p_out,
                                       Boolean system_handler_p)
{
    AE_hdlr_table_h table;
    AE_hdlr_table_elt_t *elt;
    AE_hdlr_selector_t selector;
    OSErr err;

    table = hdlr_table(system_handler_p, coercion);

    selector.sel0 = from_type;
    selector.sel1 = to_type;

    err = hdlr_table_elt(table, &selector, nullptr, false, &elt);
    if(err != noErr)
        AE_RETURN_ERROR(err);

    /* warning: `elt' is a pointer into an unlocked handle */
    *hdlr_out = guest_cast<AECoerceDescUPP>(elt->hdlr.fn);
    *refcon_out = elt->hdlr.refcon;

    if(from_type_is_desc_p_out)
        *from_type_is_desc_p_out = elt->pad_2 == 1;

    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AERemoveCoercionHandler(
    DescType from_type, DescType to_type, AECoerceDescUPP hdlr,
    Boolean system_handler_p)
{
    AE_hdlr_table_h table;
    AE_hdlr_selector_t selector;

    table = hdlr_table(system_handler_p, coercion);

    selector.sel0 = from_type;
    selector.sel1 = to_type;

    /* #### fail if `hdlr' is not the currently installed handler? */
    AE_RETURN_ERROR(_AE_hdlr_delete(table, 0, &selector));
}

/* special handler functions */

#define k_special_sel1 ("\000\000\000\000"_4)

OSErr Executor::C_AEInstallSpecialHandler(
    AEKeyword function_class, AEEventHandlerUPP hdlr_fn,
    Boolean system_handler_p)
{
    AE_hdlr_table_h table;
    AE_hdlr_table_elt_t *elt;
    AE_hdlr_selector_t selector;
    AE_hdlr_t hdlr;
    OSErr err;

    if(hdlr_fn == nullptr)
        AE_RETURN_ERROR(paramErr);

    table = hdlr_table(system_handler_p, special);

    selector.sel0 = function_class;
    selector.sel1 = k_special_sel1;

    hdlr.fn = (void *)hdlr_fn;
    hdlr.refcon = -1;

    err = hdlr_table_elt(table, &selector, &hdlr, true, &elt);
    if(err != noErr)
        AE_RETURN_ERROR(err);

    /* warning: `elt' is a pointer into an unlocked handle */
    elt->hdlr = hdlr;

    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AEGetSpecialHandler(AEKeyword function_class,
                                      GUEST<AEEventHandlerUPP> *hdlr_out,
                                      Boolean system_handler_p)
{
    AE_hdlr_table_h table;
    AE_hdlr_table_elt_t *elt;
    AE_hdlr_selector_t selector;
    OSErr err;

    table = hdlr_table(system_handler_p, special);

    selector.sel0 = function_class;
    selector.sel1 = k_special_sel1;

    err = hdlr_table_elt(table, &selector, nullptr, false, &elt);
    if(err != noErr)
        AE_RETURN_ERROR(err);

    /* warning: `elt' is a pointer into an unlocked handle */
    *hdlr_out = guest_cast<AEEventHandlerUPP>(elt->hdlr.fn);

    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AERemoveSpecialHandler(
    AEKeyword function_class, AEEventHandlerUPP hdlr,
    Boolean system_handler_p)
{
    AE_hdlr_table_h table;
    AE_hdlr_selector_t selector;

    table = hdlr_table(system_handler_p, special);

    selector.sel0 = function_class;
    selector.sel1 = k_special_sel1;

    /* #### fail if `hdlr' is not the currently installed handler? */
    AE_RETURN_ERROR(_AE_hdlr_delete(table, 0, &selector));
}

/* MacPhoenix: the Object Support Library's accessor table and callbacks,
   as 8.6's ObjectSupportLib keeps them (difftest aevt, G3). An accessor
   is found by desired class and container type, wildcards allowed;
   AECallObjectAccessor takes the container type from the container token
   (its containerClass argument goes to the accessor as it is) and tries
   the exact entry, then the class with any container, any class in the
   container, then any in any. AEGetObjectAccessor and removing take the
   exact entry. BBEdit Lite 6.1's 'crel' handler looks up the accessors
   it installed. */

static AE_hdlr_table_h object_accessor_table(bool system_p, bool create_p)
{
    AE_zone_tables_h zone_tables = get_zone_tables(system_p);
    AE_hdlr_table_h table = (*zone_tables)->object_accessor_table;
    if(!table && create_p)
    {
        TheZoneGuard guard(system_p ? LM(SysZone) : LM(ApplZone));
        GUEST<AE_hdlr_table_h> table_s;
        if(_AE_hdlr_table_alloc(16, 0x80008, 0, false, &table_s) == noErr)
        {
            table = table_s;
            (*get_zone_tables(system_p))->object_accessor_table = table;
        }
    }
    return table;
}

OSErr Executor::C_AEInstallObjectAccessor(DescType desiredClass, DescType containerType,
                                          ProcPtr theAccessor, LONGINT refcon,
                                          Boolean isSysHandler)
{
    if(!theAccessor)
        AE_RETURN_ERROR(paramErr);
    AE_hdlr_table_h table = object_accessor_table(isSysHandler, true);
    if(!table)
        AE_RETURN_ERROR(memFullErr);
    AE_hdlr_selector_t selector;
    selector.sel0 = desiredClass;
    selector.sel1 = containerType;
    AE_hdlr_t hdlr;
    hdlr.fn = (void *)theAccessor;
    hdlr.refcon = refcon;
    AE_hdlr_table_elt_t *elt;
    /* the same pair again replaces the entry */
    OSErr err = hdlr_table_elt(table, &selector, nullptr, false, &elt);
    if(err != noErr)
        err = hdlr_table_elt(table, &selector, &hdlr, true, &elt);
    if(err != noErr)
        AE_RETURN_ERROR(err);
    elt->hdlr = hdlr;
    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AEGetObjectAccessor(DescType desiredClass, DescType containerType,
                                      GUEST<ProcPtr> *theAccessor,
                                      GUEST<LONGINT> *accessorRefcon, Boolean isSysHandler)
{
    AE_hdlr_table_h table = object_accessor_table(isSysHandler, false);
    AE_hdlr_selector_t selector;
    selector.sel0 = desiredClass;
    selector.sel1 = containerType;
    AE_hdlr_table_elt_t *elt;
    if(!table || hdlr_table_elt(table, &selector, nullptr, false, &elt) != noErr)
        AE_RETURN_ERROR(errAEAccessorNotFound);
    *theAccessor = guest_cast<ProcPtr>(elt->hdlr.fn);
    *accessorRefcon = elt->hdlr.refcon;
    AE_RETURN_ERROR(noErr);
}

OSErr Executor::C_AERemoveObjectAccessor(DescType desiredClass, DescType containerType,
                                         ProcPtr theAccessor, Boolean isSysHandler)
{
    AE_hdlr_table_h table = object_accessor_table(isSysHandler, false);
    AE_hdlr_selector_t selector;
    selector.sel0 = desiredClass;
    selector.sel1 = containerType;
    AE_hdlr_t hdlr;
    hdlr.fn = (void *)theAccessor;
    hdlr.refcon = 0;
    AE_hdlr_table_elt_t *elt;
    if(!table || hdlr_table_elt(table, &selector, &hdlr, false, &elt) != noErr)
        AE_RETURN_ERROR(errAEAccessorNotFound);
    AE_RETURN_ERROR(_AE_hdlr_delete(table, 0, &selector));
}

OSErr Executor::C_AECallObjectAccessor(DescType desiredClass, AEDesc *containerToken,
                                       DescType containerClass, DescType keyForm,
                                       AEDesc *keyData, AEDesc *theToken)
{
    DescType container_type = containerToken ? (DescType)DESC_TYPE(containerToken) : typeNull;
    const DescType classes[] = { desiredClass, desiredClass, typeWildCard, typeWildCard };
    const DescType containers[] = { container_type, typeWildCard, container_type, typeWildCard };
    for(bool system_p : { false, true })
    {
        AE_hdlr_table_h table = object_accessor_table(system_p, false);
        if(!table)
            continue;
        for(int i = 0; i < 4; i++)
        {
            AE_hdlr_selector_t selector;
            selector.sel0 = classes[i];
            selector.sel1 = containers[i];
            AE_hdlr_table_elt_t *elt;
            if(hdlr_table_elt(table, &selector, nullptr, false, &elt) != noErr)
                continue;
            AEObjectAccessorUPP accessor = guest_cast<AEObjectAccessorUPP>(elt->hdlr.fn);
            int32_t refcon = elt->hdlr.refcon;
            AE_RETURN_ERROR(accessor(desiredClass, containerToken, containerClass, keyForm,
                                     keyData, theToken, refcon));
        }
    }
    AE_RETURN_ERROR(errAEAccessorNotFound);
}

/* the callbacks: compare, count, dispose token, get mark token, mark,
   adjust marks, get error descriptor (nil: none) */
enum { kDisposeTokenCallback = 2, kObjectCallbacks = 7 };

OSErr Executor::C_AESetObjectCallbacks(ProcPtr myCompareProc, ProcPtr myCountProc,
                                       ProcPtr myDisposeTokenProc, ProcPtr myGetMarkTokenProc,
                                       ProcPtr myMarkProc, ProcPtr myAdjustMarksProc,
                                       ProcPtr myGetErrDescProc)
{
    AE_zone_tables_h zone_tables = get_zone_tables(false);
    Handle h = (*zone_tables)->object_callbacks;
    if(!h)
    {
        TheZoneGuard guard(LM(ApplZone));
        h = NewHandleClear(kObjectCallbacks * sizeof(GUEST<ProcPtr>));
        if(!h)
            AE_RETURN_ERROR(memFullErr);
        (*get_zone_tables(false))->object_callbacks = h;
    }
    const ProcPtr procs[kObjectCallbacks] = { myCompareProc, myCountProc, myDisposeTokenProc,
                                              myGetMarkTokenProc, myMarkProc, myAdjustMarksProc,
                                              myGetErrDescProc };
    GUEST<ProcPtr> *slots = (GUEST<ProcPtr> *)*h;
    for(int i = 0; i < kObjectCallbacks; i++)
        slots[i] = procs[i];
    AE_RETURN_ERROR(noErr);
}

/* The application's dispose callback if it set one, else AEDisposeDesc. */
OSErr Executor::C_AEDisposeToken(AEDesc *theToken)
{
    Handle h = (*get_zone_tables(false))->object_callbacks;
    ProcPtr dispose = h ? (ProcPtr)((GUEST<ProcPtr> *)*h)[kDisposeTokenCallback] : nullptr;
    if(dispose)
    {
        AEDisposeTokenUPP proc = guest_cast<AEDisposeTokenUPP>((void *)dispose);
        AE_RETURN_ERROR(proc(theToken));
    }
    AE_RETURN_ERROR(AEDisposeDesc(theToken));
}

/* AEResolve: an object specifier ('obj ': a record of want, from, form,
   seld) resolved container first, each step one accessor call, as 8.6's
   ObjectSupportLib (difftest aevt resolve, G3): the application's
   container (from: null) is a null token of class null; a container's
   token goes back through AEDisposeToken once used; a failing step's
   error is the answer and the token a null descriptor; no accessor is
   errAEAccessorNotFound. Anything but an 'obj ' (a null descriptor, a
   plain record) is errAENotAnObjSpec, the token left alone. The key data
   goes to the accessor as it is: ranges and tests (formRange,
   formTest) are the accessors' to take apart, no marking callbacks yet.
   BBEdit Lite 6.1's 'save' handler resolves its document so; MacPerl's
   'dosc' its window. */
namespace
{
OSErr resolve_specifier(AEDesc *spec, AEDesc *token)
{
    AEDesc rec, from, seld, container;
    GUEST<DescType> want, form, type;
    GUEST<Size> size;
    OSErr err = AECoerceDesc(spec, typeAERecord, &rec);
    if(err != noErr)
        return errAENotAnObjSpec;
    from.descriptorType = seld.descriptorType = container.descriptorType = typeNull;
    from.dataHandle = seld.dataHandle = container.dataHandle = nullptr;
    if(AEGetParamPtr(&rec, "want"_4, typeType, &type, &want, sizeof want, &size) != noErr
       || AEGetParamPtr(&rec, "form"_4, typeWildCard, &type, &form, sizeof form, &size) != noErr
       || AEGetParamDesc(&rec, "from"_4, typeWildCard, &from) != noErr
       || AEGetParamDesc(&rec, "seld"_4, typeWildCard, &seld) != noErr)
    {
        AEDisposeDesc(&from);
        AEDisposeDesc(&rec);
        return errAENotAnObjSpec;
    }
    AEDisposeDesc(&rec);

    DescType container_class;
    bool container_resolved = false;
    if(from.descriptorType == "obj "_4)
    {
        AEDesc from_rec;
        container_class = typeNull;
        if(AECoerceDesc(&from, typeAERecord, &from_rec) == noErr)
        {
            GUEST<DescType> cls;
            if(AEGetParamPtr(&from_rec, "want"_4, typeType, &type, &cls, sizeof cls, &size) == noErr)
                container_class = cls;
            AEDisposeDesc(&from_rec);
        }
        err = resolve_specifier(&from, &container);
        container_resolved = err == noErr;
    }
    else
    {
        /* the null container, or a token given as the container */
        container_class = from.descriptorType;
        err = AEDuplicateDesc(&from, &container);
    }
    if(err == noErr)
        err = AECallObjectAccessor(want, &container, container_class, form, &seld, token);
    if(container_resolved)
        AEDisposeToken(&container);
    else
        AEDisposeDesc(&container);
    AEDisposeDesc(&from);
    AEDisposeDesc(&seld);
    if(err != noErr)
    {
        token->descriptorType = typeNull;
        token->dataHandle = nullptr;
    }
    return err;
}
}

OSErr Executor::C_AEResolve(AEDesc *objectSpecifier, INTEGER callbackFlags, AEDesc *theToken)
{
    (void)callbackFlags;
    if(!objectSpecifier || !theToken || objectSpecifier->descriptorType != "obj "_4)
        AE_RETURN_ERROR(errAENotAnObjSpec);
    AE_RETURN_ERROR(resolve_specifier(objectSpecifier, theToken));
}
