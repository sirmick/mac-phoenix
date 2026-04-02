/*
 *  sigsegv.h - KPX compatibility stub for SIGSEGV handling
 *
 *  mac-phoenix has its own SIGSEGV handler in sigsegv.cpp.
 *  This stub provides the types the KPX code references.
 */

#ifndef KPX_SIGSEGV_H
#define KPX_SIGSEGV_H

#include "sysdeps.h"

typedef void * sigsegv_address_t;
typedef void * sigsegv_info_t;

enum sigsegv_return_t {
    SIGSEGV_RETURN_SUCCESS,
    SIGSEGV_RETURN_FAILURE,
    SIGSEGV_RETURN_SKIP_INSTRUCTION,
};

static inline sigsegv_address_t sigsegv_get_fault_address(sigsegv_info_t *sip) { return NULL; }
static inline sigsegv_address_t sigsegv_get_fault_instruction_address(sigsegv_info_t *sip) { return NULL; }

#endif /* KPX_SIGSEGV_H */
