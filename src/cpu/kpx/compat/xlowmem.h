/*
 *  xlowmem.h - Extra Low Memory globals for PPC emulation (0x2800..)
 *
 *  Adapted from SheepShaver for mac-phoenix PPC backend.
 *  Original: SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *  Licensed under GPL v2+
 */

#ifndef XLOWMEM_H
#define XLOWMEM_H

// Modes for XLM_RUN_MODE
#define MODE_68K 0        // 68k emulator active
#define MODE_NATIVE 1     // Switched to native mode
#define MODE_EMUL_OP 2    // 68k emulator active, within EMUL_OP routine

#define XLM_SIGNATURE 0x2800
#define XLM_KERNEL_DATA 0x2804
#define XLM_TOC 0x2808
#define XLM_SHEEP_OBJ 0x280c
#define XLM_RUN_MODE 0x2810
#define XLM_68K_R25 0x2814
#define XLM_IRQ_NEST 0x2818
#define XLM_PVR 0x281c
#define XLM_BUS_CLOCK 0x2820
#define XLM_EMUL_RETURN_PROC 0x2824
#define XLM_EXEC_RETURN_PROC 0x2828
#define XLM_EMUL_OP_PROC 0x282c
#define XLM_EMUL_RETURN_STACK 0x2830
#define XLM_RES_LIB_TOC 0x2834
#define XLM_GET_RESOURCE 0x2838
#define XLM_GET_1_RESOURCE 0x283c
#define XLM_GET_IND_RESOURCE 0x2840
#define XLM_GET_1_IND_RESOURCE 0x2844
#define XLM_R_GET_RESOURCE 0x2848
#define XLM_EXEC_RETURN_OPCODE 0x284c
#define XLM_ZERO_PAGE 0x2850
#define XLM_R13 0x2854
#define XLM_GET_NAMED_RESOURCE 0x2858
#define XLM_GET_1_NAMED_RESOURCE 0x285c

#define XLM_ETHER_AO_GET_HWADDR 0x28b0
#define XLM_ETHER_AO_ADD_MULTI 0x28b4
#define XLM_ETHER_AO_DEL_MULTI 0x28b8
#define XLM_ETHER_AO_SEND_PACKET 0x28bc
#define XLM_ETHER_INIT 0x28c0
#define XLM_ETHER_TERM 0x28c4
#define XLM_ETHER_OPEN 0x28c8
#define XLM_ETHER_CLOSE 0x28cc
#define XLM_ETHER_WPUT 0x28d0
#define XLM_ETHER_RSRV 0x28d4
#define XLM_VIDEO_DOIO 0x28d8

#endif /* XLOWMEM_H */
