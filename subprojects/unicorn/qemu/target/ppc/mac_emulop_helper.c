/*
 * Mac-phoenix EmulOp helper — dispatches major-opcode-6 PPC instructions
 * (POWERPC_EMUL_OP = 0x18xxxxxx) to a host-side callback stored on uc_struct.
 *
 * Added for the Unicorn PPC backend integration (docs/ppc/UnicornPpcPlan.md §5).
 * The translate.c decoder routes op1=0x06 to gen_mac_emulop, which emits a call
 * to helper_mac_emulop(env, opcode). env->nip has already been set to the
 * faulting instruction's PC by gen_update_nip, and we advance it to pc+4 here
 * as the default next-PC (the callback may overwrite it for branching EmulOps).
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"

#include "uc_priv.h"

void helper_mac_emulop(CPUPPCState *env, uint32_t opcode)
{
    struct uc_struct *uc = env->uc;
    uint32_t pc = (uint32_t)env->nip;

    /* Default: advance past this 4-byte instruction. The callback can
     * override env->nip (and any GPR/SPR state) via uc_reg_write before
     * returning. */
    env->nip = pc + 4;

    if (uc && uc->mac_emulop_cb) {
        uc->mac_emulop_cb(uc, pc, opcode);
    }
}
