#!/usr/bin/env python3
"""
Mac ROM Disassembler and Analysis Tool

Disassembles and analyzes Macintosh ROM files (512KB/1MB 32-bit clean ROMs).
Identifies key structures needed for emulator ROM patching:
  - ROM header fields and reset vector
  - UniversalInfo table (model IDs, hardware config)
  - Trap dispatch tables
  - ROM resource map
  - Key patch points (VIA, SCC, SCSI, IWM, ASC init routines)
  - Driver resources (Sony, SERD)

Usage:
    python3 tools/disasm_rom.py <rom_file> [options]

Options:
    --disasm START END     Disassemble address range (hex)
    --disasm-around ADDR   Disassemble 64 bytes around address (hex)
    --find-pattern HEX     Find byte pattern in ROM
    --resources            List ROM resources
    --universal            Dump UniversalInfo tables
    --traps                Dump trap dispatch table
    --patch-points         Find all standard emulator patch points
    --header               Show ROM header info
    --all                  Run all analyses
    --hex START END        Hex dump address range
    --compare ROM2         Compare two ROMs (find shared/different offsets)
"""

import struct
import sys
import argparse
from pathlib import Path


# ============================================================
# M68K Instruction Decoder
# ============================================================
# Covers the most common instructions found in Mac ROMs.
# Not a full decoder — covers ~90% of instructions you'll see.

# Condition code names
CC_NAMES = {
    0: 't', 1: 'f', 2: 'hi', 3: 'ls', 4: 'cc', 5: 'cs',
    6: 'ne', 7: 'eq', 8: 'vc', 9: 'vs', 10: 'pl', 11: 'mi',
    12: 'ge', 13: 'lt', 14: 'gt', 15: 'le'
}

SIZE_NAMES = {0: '.b', 1: '.w', 2: '.l'}
SIZE_BYTES = {0: 1, 1: 2, 2: 4}

REG_NAMES_D = ['d0', 'd1', 'd2', 'd3', 'd4', 'd5', 'd6', 'd7']
REG_NAMES_A = ['a0', 'a1', 'a2', 'a3', 'a4', 'a5', 'a6', 'sp']


def read16(data, offset):
    if offset + 2 > len(data):
        return None
    return struct.unpack_from('>H', data, offset)[0]


def read32(data, offset):
    if offset + 4 > len(data):
        return None
    return struct.unpack_from('>I', data, offset)[0]


def sign_extend_8(val):
    return val - 256 if val >= 128 else val


def sign_extend_16(val):
    return val - 65536 if val >= 32768 else val


def sign_extend_32(val):
    return val - (1 << 32) if val >= (1 << 31) else val


def decode_ea(mode, reg, size, data, offset, ext_offset):
    """Decode an effective address. Returns (string, bytes_consumed)."""
    if mode < 7:
        if mode == 0:
            return REG_NAMES_D[reg], 0
        elif mode == 1:
            return REG_NAMES_A[reg], 0
        elif mode == 2:
            return f'({REG_NAMES_A[reg]})', 0
        elif mode == 3:
            return f'({REG_NAMES_A[reg]})+', 0
        elif mode == 4:
            return f'-({REG_NAMES_A[reg]})', 0
        elif mode == 5:
            disp = read16(data, ext_offset)
            if disp is None:
                return '???', 0
            return f'${sign_extend_16(disp):x}({REG_NAMES_A[reg]})', 2
        elif mode == 6:
            ext = read16(data, ext_offset)
            if ext is None:
                return '???', 0
            da = 'a' if ext & 0x8000 else 'd'
            xreg = (ext >> 12) & 7
            wl = '.l' if ext & 0x0800 else '.w'
            disp = sign_extend_8(ext & 0xff)
            return f'${disp:x}({REG_NAMES_A[reg]},{da}{xreg}{wl})', 2
    else:
        if reg == 0:
            val = read16(data, ext_offset)
            if val is None:
                return '???', 0
            return f'(${"" if val < 0x100 else ""}{val:04x}).w', 2
        elif reg == 1:
            val = read32(data, ext_offset)
            if val is None:
                return '???', 0
            return f'(${val:08x}).l', 4
        elif reg == 2:
            disp = read16(data, ext_offset)
            if disp is None:
                return '???', 0
            target = ext_offset + sign_extend_16(disp)
            return f'(${target:x},pc)', 2
        elif reg == 3:
            ext = read16(data, ext_offset)
            if ext is None:
                return '???', 0
            da = 'a' if ext & 0x8000 else 'd'
            xreg = (ext >> 12) & 7
            wl = '.l' if ext & 0x0800 else '.w'
            disp = sign_extend_8(ext & 0xff)
            return f'(${disp:x},pc,{da}{xreg}{wl})', 2
        elif reg == 4:
            if size == 0:
                val = read16(data, ext_offset)
                if val is None:
                    return '???', 0
                return f'#${val & 0xff:02x}', 2
            elif size == 1:
                val = read16(data, ext_offset)
                if val is None:
                    return '???', 0
                return f'#${val:04x}', 2
            elif size == 2:
                val = read32(data, ext_offset)
                if val is None:
                    return '???', 0
                return f'#${val:08x}', 4
    return '???', 0


def decode_movem_regs(mask, direction):
    """Decode MOVEM register list."""
    regs = []
    if direction:  # predecrement: reversed
        for i in range(8):
            if mask & (1 << (15 - i)):
                regs.append(f'd{i}')
        for i in range(8):
            if mask & (1 << (7 - i)):
                regs.append(f'a{i}')
    else:
        for i in range(8):
            if mask & (1 << i):
                regs.append(f'd{i}')
        for i in range(8):
            if mask & (1 << (i + 8)):
                regs.append(f'a{i}')
    # Compress ranges
    return '/'.join(regs) if regs else '???'


def disasm_one(data, offset, base_addr=0):
    """Disassemble one instruction. Returns (mnemonic, operands, bytes_consumed, comment)."""
    w = read16(data, offset)
    if w is None:
        return 'dc.w', '???', 2, ''

    op = (w >> 12) & 0xf
    comment = ''

    # ---- Line 0: Bit manipulation / immediate ----
    if op == 0:
        if w & 0x0100:
            # BTST/BCHG/BCLR/BSET with register
            dreg = (w >> 9) & 7
            bop = (w >> 6) & 3
            bnames = ['btst', 'bchg', 'bclr', 'bset']
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 0, data, offset, offset + 2)
            return bnames[bop], f'd{dreg},{ea}', 2 + ext, ''
        else:
            subop = (w >> 9) & 7
            size = (w >> 6) & 3
            if subop == 4 and size == 0:
                # BTST #imm
                imm = read16(data, offset + 2)
                mode = (w >> 3) & 7
                reg = w & 7
                ea, ext = decode_ea(mode, reg, 0, data, offset, offset + 4)
                return 'btst', f'#${imm & 0xff:x},{ea}', 4 + ext, ''
            elif subop == 4 and size == 1:
                # BCHG #imm
                imm = read16(data, offset + 2)
                mode = (w >> 3) & 7
                reg = w & 7
                ea, ext = decode_ea(mode, reg, 0, data, offset, offset + 4)
                return 'bchg', f'#${imm & 0xff:x},{ea}', 4 + ext, ''
            elif subop == 4 and size == 2:
                # BCLR #imm
                imm = read16(data, offset + 2)
                mode = (w >> 3) & 7
                reg = w & 7
                ea, ext = decode_ea(mode, reg, 0, data, offset, offset + 4)
                return 'bclr', f'#${imm & 0xff:x},{ea}', 4 + ext, ''
            elif subop == 4 and size == 3:
                # BSET #imm
                imm = read16(data, offset + 2)
                mode = (w >> 3) & 7
                reg = w & 7
                ea, ext = decode_ea(mode, reg, 0, data, offset, offset + 4)
                return 'bset', f'#${imm & 0xff:x},{ea}', 4 + ext, ''
            elif size < 3:
                # ORI, ANDI, SUBI, ADDI, EORI, CMPI
                imm_ops = {0: 'ori', 1: 'andi', 2: 'subi', 3: 'addi', 5: 'eori', 6: 'cmpi'}
                if subop in imm_ops:
                    name = imm_ops[subop]
                    sz = SIZE_NAMES.get(size, '')
                    if size <= 1:
                        imm = read16(data, offset + 2)
                        if imm is None:
                            return f'{name}{sz}', '???', 2, ''
                        imm_len = 2
                        if size == 0:
                            imm_str = f'#${imm & 0xff:02x}'
                        else:
                            imm_str = f'#${imm:04x}'
                    else:
                        imm = read32(data, offset + 2)
                        if imm is None:
                            return f'{name}{sz}', '???', 2, ''
                        imm_len = 4
                        imm_str = f'#${imm:08x}'
                    mode = (w >> 3) & 7
                    reg = w & 7
                    # Special case: SR/CCR
                    if mode == 7 and reg == 4:
                        if size == 0:
                            return f'{name}', f'{imm_str},ccr', 2 + imm_len, ''
                        elif size == 1:
                            return f'{name}', f'{imm_str},sr', 2 + imm_len, ''
                    ea, ext = decode_ea(mode, reg, size, data, offset, offset + 2 + imm_len)
                    return f'{name}{sz}', f'{imm_str},{ea}', 2 + imm_len + ext, ''

    # ---- Line 1/2/3: MOVE ----
    if op in (1, 2, 3):
        size_map = {1: 0, 3: 1, 2: 2}  # MOVE size encoding
        size = size_map[op]
        sz = SIZE_NAMES[size]
        dst_reg = (w >> 9) & 7
        dst_mode = (w >> 6) & 7
        src_mode = (w >> 3) & 7
        src_reg = w & 7
        src, src_ext = decode_ea(src_mode, src_reg, size, data, offset, offset + 2)
        dst, dst_ext = decode_ea(dst_mode, dst_reg, size, data, offset, offset + 2 + src_ext)
        # MOVEA
        if dst_mode == 1:
            return f'movea{sz}', f'{src},{REG_NAMES_A[dst_reg]}', 2 + src_ext + dst_ext, ''
        return f'move{sz}', f'{src},{dst}', 2 + src_ext + dst_ext, ''

    # ---- Line 4: Misc ----
    if op == 4:
        if w == 0x4e70:
            return 'reset', '', 2, ''
        if w == 0x4e71:
            return 'nop', '', 2, ''
        if w == 0x4e72:
            val = read16(data, offset + 2)
            return 'stop', f'#${val:04x}', 4, ''
        if w == 0x4e73:
            return 'rte', '', 2, ''
        if w == 0x4e75:
            return 'rts', '', 2, ''
        if w == 0x4e76:
            return 'trapv', '', 2, ''
        if w == 0x4e77:
            return 'rtr', '', 2, ''
        if (w & 0xfff0) == 0x4e40:
            return 'trap', f'#${w & 0xf:x}', 2, ''
        if (w & 0xfff0) == 0x4e50:
            # LINK
            reg = w & 7
            disp = read16(data, offset + 2)
            return 'link', f'{REG_NAMES_A[reg]},#${sign_extend_16(disp) & 0xffff:04x}', 4, ''
        if (w & 0xfff0) == 0x4e58:
            return 'unlk', REG_NAMES_A[w & 7], 2, ''
        if (w & 0xfff0) == 0x4e60:
            return 'move.l', f'{REG_NAMES_A[w & 7]},usp', 2, ''
        if (w & 0xfff0) == 0x4e68:
            return 'move.l', f'usp,{REG_NAMES_A[w & 7]}', 2, ''

        # LEA
        if (w & 0xf1c0) == 0x41c0:
            areg = (w >> 9) & 7
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 2, data, offset, offset + 2)
            return 'lea', f'{ea},{REG_NAMES_A[areg]}', 2 + ext, ''

        # PEA
        if (w & 0xffc0) == 0x4840:
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 2, data, offset, offset + 2)
            return 'pea', ea, 2 + ext, ''

        # JSR
        if (w & 0xffc0) == 0x4e80:
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 2, data, offset, offset + 2)
            return 'jsr', ea, 2 + ext, ''

        # JMP
        if (w & 0xffc0) == 0x4ec0:
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 2, data, offset, offset + 2)
            return 'jmp', ea, 2 + ext, ''

        # MOVEM
        if (w & 0xfb80) == 0x4880:
            direction = (w >> 10) & 1  # 0=reg-to-mem, 1=mem-to-reg
            size = 1 if (w & 0x0040) == 0 else 2
            sz = '.w' if size == 1 else '.l'
            mode = (w >> 3) & 7
            reg = w & 7
            mask = read16(data, offset + 2)
            if mask is None:
                return 'movem' + sz, '???', 2, ''
            regs = decode_movem_regs(mask, mode == 4)
            ea, ext = decode_ea(mode, reg, size, data, offset, offset + 4)
            if direction == 0:
                return f'movem{sz}', f'{regs},{ea}', 4 + ext, ''
            else:
                return f'movem{sz}', f'{ea},{regs}', 4 + ext, ''

        # CLR
        if (w & 0xff00) == 0x4200:
            size = (w >> 6) & 3
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, size, data, offset, offset + 2)
            return f'clr{SIZE_NAMES.get(size, "")}', ea, 2 + ext, ''

        # NEG
        if (w & 0xff00) == 0x4400:
            size = (w >> 6) & 3
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, size, data, offset, offset + 2)
            return f'neg{SIZE_NAMES.get(size, "")}', ea, 2 + ext, ''

        # NOT
        if (w & 0xff00) == 0x4600:
            size = (w >> 6) & 3
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, size, data, offset, offset + 2)
            return f'not{SIZE_NAMES.get(size, "")}', ea, 2 + ext, ''

        # TST
        if (w & 0xff00) == 0x4a00:
            size = (w >> 6) & 3
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, size, data, offset, offset + 2)
            return f'tst{SIZE_NAMES.get(size, "")}', ea, 2 + ext, ''

        # EXT
        if (w & 0xfeb8) == 0x4880:
            reg = w & 7
            if w & 0x0040:
                return 'ext.l', f'd{reg}', 2, ''
            else:
                return 'ext.w', f'd{reg}', 2, ''

        # SWAP
        if (w & 0xfff8) == 0x4840:
            return 'swap', f'd{w & 7}', 2, ''

        # MOVEC
        if w == 0x4e7a or w == 0x4e7b:
            ext = read16(data, offset + 2)
            if ext is None:
                return 'movec', '???', 2, ''
            creg = ext & 0xfff
            dr = 'a' if ext & 0x8000 else 'd'
            rn = (ext >> 12) & 7
            cr_names = {0: 'sfc', 1: 'dfc', 2: 'cacr', 0x800: 'usp', 0x801: 'vbr',
                        0x002: 'cacr', 0x003: 'tc', 0x004: 'itt0', 0x005: 'itt1',
                        0x006: 'dtt0', 0x007: 'dtt1'}
            cr_name = cr_names.get(creg, f'cr${creg:03x}')
            if w == 0x4e7a:
                return 'movec', f'{cr_name},{dr}{rn}', 4, ''
            else:
                return 'movec', f'{dr}{rn},{cr_name}', 4, ''

    # ---- Line 5: ADDQ/SUBQ/Scc/DBcc ----
    if op == 5:
        size = (w >> 6) & 3
        if size == 3:
            # Scc / DBcc
            cond = (w >> 8) & 0xf
            mode = (w >> 3) & 7
            reg = w & 7
            if mode == 1:
                # DBcc
                disp = read16(data, offset + 2)
                if disp is not None:
                    target = base_addr + offset + 2 + sign_extend_16(disp)
                    return f'db{CC_NAMES[cond]}', f'd{reg},${target:x}', 4, ''
                return f'db{CC_NAMES[cond]}', f'd{reg},???', 4, ''
            else:
                ea, ext = decode_ea(mode, reg, 0, data, offset, offset + 2)
                return f's{CC_NAMES[cond]}', ea, 2 + ext, ''
        else:
            data_val = (w >> 9) & 7
            if data_val == 0:
                data_val = 8
            mode = (w >> 3) & 7
            reg = w & 7
            if w & 0x0100:
                name = 'subq'
            else:
                name = 'addq'
            ea, ext = decode_ea(mode, reg, size, data, offset, offset + 2)
            return f'{name}{SIZE_NAMES[size]}', f'#{data_val},{ea}', 2 + ext, ''

    # ---- Line 6: Bcc/BSR/BRA ----
    if op == 6:
        cond = (w >> 8) & 0xf
        disp8 = w & 0xff
        if disp8 == 0:
            disp = read16(data, offset + 2)
            if disp is None:
                return 'b??', '???', 2, ''
            target = base_addr + offset + 2 + sign_extend_16(disp)
            sz = 4
        elif disp8 == 0xff:
            disp = read32(data, offset + 2)
            if disp is None:
                return 'b??', '???', 2, ''
            target = base_addr + offset + 2 + sign_extend_32(disp)
            sz = 6
        else:
            target = base_addr + offset + 2 + sign_extend_8(disp8)
            sz = 2
        if cond == 0:
            return 'bra', f'${target:x}', sz, ''
        elif cond == 1:
            return 'bsr', f'${target:x}', sz, ''
        else:
            return f'b{CC_NAMES[cond]}', f'${target:x}', sz, ''

    # ---- Line 7: MOVEQ ----
    if op == 7:
        dreg = (w >> 9) & 7
        val = sign_extend_8(w & 0xff)
        return 'moveq', f'#${val & 0xff:02x},d{dreg}', 2, f'; ={val}'

    # ---- Line 8: OR / DIVU / DIVS / SBCD ----
    if op == 8:
        dreg = (w >> 9) & 7
        size = (w >> 6) & 3
        if size == 3:
            # DIVU/DIVS
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 1, data, offset, offset + 2)
            if w & 0x0100:
                return 'divs.w', f'{ea},d{dreg}', 2 + ext, ''
            else:
                return 'divu.w', f'{ea},d{dreg}', 2 + ext, ''
        else:
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, size, data, offset, offset + 2)
            if w & 0x0100:
                return f'or{SIZE_NAMES[size]}', f'd{dreg},{ea}', 2 + ext, ''
            else:
                return f'or{SIZE_NAMES[size]}', f'{ea},d{dreg}', 2 + ext, ''

    # ---- Line 9: SUB / SUBA ----
    if op == 9:
        dreg = (w >> 9) & 7
        size = (w >> 6) & 3
        if size == 3:
            # SUBA.W
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 1, data, offset, offset + 2)
            return 'suba.w', f'{ea},{REG_NAMES_A[dreg]}', 2 + ext, ''
        elif (w & 0x01c0) == 0x01c0:
            # SUBA.L
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 2, data, offset, offset + 2)
            return 'suba.l', f'{ea},{REG_NAMES_A[dreg]}', 2 + ext, ''
        else:
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, size, data, offset, offset + 2)
            if w & 0x0100:
                return f'sub{SIZE_NAMES[size]}', f'd{dreg},{ea}', 2 + ext, ''
            else:
                return f'sub{SIZE_NAMES[size]}', f'{ea},d{dreg}', 2 + ext, ''

    # ---- Line A: A-line traps ----
    if op == 0xa:
        trap_num = w & 0xfff
        # Check for EmulOps
        if (w & 0xff00) == 0xae00:
            comment = f'; EmulOp ${w & 0xff:02x}'
        elif (w & 0xff00) == 0xa800:
            comment = '; Toolbox trap'
        elif (w & 0xff00) == 0xa000:
            comment = '; OS trap'
        return 'dc.w', f'${w:04x}', 2, f'; A-line trap ${trap_num:03x} {comment}'

    # ---- Line B: CMP / EOR ----
    if op == 0xb:
        dreg = (w >> 9) & 7
        size = (w >> 6) & 3
        if size == 3:
            # CMPA.W
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 1, data, offset, offset + 2)
            return 'cmpa.w', f'{ea},{REG_NAMES_A[dreg]}', 2 + ext, ''
        elif (w & 0x01c0) == 0x01c0:
            # CMPA.L
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 2, data, offset, offset + 2)
            return 'cmpa.l', f'{ea},{REG_NAMES_A[dreg]}', 2 + ext, ''
        else:
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, size, data, offset, offset + 2)
            if w & 0x0100:
                return f'eor{SIZE_NAMES[size]}', f'd{dreg},{ea}', 2 + ext, ''
            else:
                return f'cmp{SIZE_NAMES[size]}', f'{ea},d{dreg}', 2 + ext, ''

    # ---- Line C: AND / MULU / MULS / EXG ----
    if op == 0xc:
        dreg = (w >> 9) & 7
        size = (w >> 6) & 3
        if size == 3:
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 1, data, offset, offset + 2)
            if w & 0x0100:
                return 'muls.w', f'{ea},d{dreg}', 2 + ext, ''
            else:
                return 'mulu.w', f'{ea},d{dreg}', 2 + ext, ''
        elif (w & 0x01f8) == 0x0140:
            # EXG Dx,Dy
            return 'exg', f'd{dreg},d{w & 7}', 2, ''
        elif (w & 0x01f8) == 0x0148:
            # EXG Ax,Ay
            return 'exg', f'a{dreg},a{w & 7}', 2, ''
        elif (w & 0x01f8) == 0x0188:
            # EXG Dx,Ay
            return 'exg', f'd{dreg},a{w & 7}', 2, ''
        else:
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, size, data, offset, offset + 2)
            if w & 0x0100:
                return f'and{SIZE_NAMES[size]}', f'd{dreg},{ea}', 2 + ext, ''
            else:
                return f'and{SIZE_NAMES[size]}', f'{ea},d{dreg}', 2 + ext, ''

    # ---- Line D: ADD / ADDA ----
    if op == 0xd:
        dreg = (w >> 9) & 7
        size = (w >> 6) & 3
        if size == 3:
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 1, data, offset, offset + 2)
            return 'adda.w', f'{ea},{REG_NAMES_A[dreg]}', 2 + ext, ''
        elif (w & 0x01c0) == 0x01c0:
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 2, data, offset, offset + 2)
            return 'adda.l', f'{ea},{REG_NAMES_A[dreg]}', 2 + ext, ''
        else:
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, size, data, offset, offset + 2)
            if w & 0x0100:
                return f'add{SIZE_NAMES[size]}', f'd{dreg},{ea}', 2 + ext, ''
            else:
                return f'add{SIZE_NAMES[size]}', f'{ea},d{dreg}', 2 + ext, ''

    # ---- Line E: Shifts/Rotates ----
    if op == 0xe:
        size = (w >> 6) & 3
        if size == 3:
            # Memory shift/rotate (word only)
            direction = 'l' if w & 0x0100 else 'r'
            kind = (w >> 9) & 3
            kinds = {0: 'as', 1: 'ls', 2: 'rox', 3: 'ro'}
            mode = (w >> 3) & 7
            reg = w & 7
            ea, ext = decode_ea(mode, reg, 1, data, offset, offset + 2)
            return f'{kinds[kind]}{direction}.w', ea, 2 + ext, ''
        else:
            direction = 'l' if w & 0x0100 else 'r'
            kind = (w >> 3) & 3
            kinds = {0: 'as', 1: 'ls', 2: 'rox', 3: 'ro'}
            dreg = w & 7
            if w & 0x0020:
                cnt = f'd{(w >> 9) & 7}'
            else:
                cnt = (w >> 9) & 7
                if cnt == 0:
                    cnt = 8
                cnt = f'#{cnt}'
            return f'{kinds[kind]}{direction}{SIZE_NAMES[size]}', f'{cnt},d{dreg}', 2, ''

    # ---- Line F: Coprocessor / 68040 ----
    if op == 0xf:
        # CPUSHA / CINVA etc
        if (w & 0xfff8) == 0xf4f8:
            scope = w & 7
            return 'cpusha', f'dc/ic', 2, '; flush caches'
        if (w & 0xff00) == 0xf200:
            return 'dc.w', f'${w:04x}', 2, '; FPU instruction'
        if (w & 0xffc0) == 0xf000:
            return 'dc.w', f'${w:04x}', 2, '; MMU instruction'
        return 'dc.w', f'${w:04x}', 2, '; F-line'

    # Fallback
    return 'dc.w', f'${w:04x}', 2, ''


def disassemble(data, start, end, base_addr=0):
    """Disassemble a range of ROM data."""
    lines = []
    offset = start
    while offset < end and offset < len(data):
        mnemonic, operands, size, comment = disasm_one(data, offset, base_addr)
        raw = ' '.join(f'{data[offset + i]:02x}' for i in range(min(size, end - offset)))
        addr = base_addr + offset
        line = f'  {addr:06x}:  {raw:<20s}  {mnemonic:<12s}{operands}'
        if comment:
            line += f'  {comment}'
        lines.append(line)
        offset += size
    return '\n'.join(lines)


# ============================================================
# ROM Structure Analysis
# ============================================================

ROM_VERSIONS = {
    0x0000: ('64K', 'Original Macintosh'),
    0x0075: ('128K', 'Mac Plus'),
    0x0276: ('Classic', 'Mac SE/Classic'),
    0x0178: ('Mac II', 'Non-32-bit-clean Mac II'),
    0x067c: ('32-bit', '32-bit clean Mac II+'),
}


def analyze_header(rom):
    """Analyze ROM header."""
    checksum = read32(rom, 0)
    version = read16(rom, 8)
    sub_version = read16(rom, 18)
    rsrc_map_offset = read32(rom, 0x1a)
    trap_table_offset = read32(rom, 0x22)

    ver_info = ROM_VERSIONS.get(version, ('Unknown', 'Unknown'))

    print(f'\n{"="*60}')
    print(f'ROM Header')
    print(f'{"="*60}')
    print(f'  Checksum       : ${checksum:08X}')
    print(f'  Size           : {len(rom)} bytes ({len(rom)//1024}KB)')
    print(f'  Version        : ${version:04X} ({ver_info[0]} - {ver_info[1]})')
    print(f'  Sub Version    : ${sub_version:04X}')
    print(f'  Resource Map   : ${rsrc_map_offset:08X}')
    print(f'  Trap Tables    : ${trap_table_offset:08X}')

    # Reset vector area (first few longwords)
    print(f'\n  Reset Vector Area:')
    for i in range(0, 0x30, 4):
        val = read32(rom, i)
        print(f'    +${i:04X}: ${val:08X}')

    return version


def find_universal_info(rom):
    """Find and dump UniversalInfo tables."""
    # Search for the magic pattern 0xDC000505
    magic = bytes([0xdc, 0x00, 0x05, 0x05])
    results = []

    print(f'\n{"="*60}')
    print(f'UniversalInfo Tables')
    print(f'{"="*60}')

    offset = 0x3000
    while offset < min(len(rom), 0x5000):
        pos = rom.find(magic, offset, 0x5000)
        if pos < 0:
            break

        # UniversalInfo is 16 bytes before the magic
        info_offset = pos - 16

        # Walk backwards to find the pointer table
        q = info_offset
        while q > 0:
            ptr_val = read32(rom, q)
            if ptr_val == info_offset - q:
                break
            q -= 4

        if q > 0:
            print(f'\n  Universal Table at ${q:06X}:')
            print(f'  {"Offset":<10s} {"ID":<5s} {"HWCfg":<8s} {"ROM85":<8s} {"Model"}')
            print(f'  {"-"*50}')

            tbl_offset = q
            while True:
                ptr = read32(rom, tbl_offset)
                if ptr == 0:
                    break
                entry_addr = ptr + tbl_offset

                if entry_addr + 24 > len(rom):
                    break

                product_kind = rom[entry_addr + 18]
                hw_cfg = read16(rom, entry_addr + 16)
                rom85 = read16(rom, entry_addr + 20)

                # Decode model name
                model_id = product_kind + 6  # offset used in the emulator
                model_names = {
                    1: 'Classic', 2: 'Mac XL', 3: 'Mac 512KE', 4: 'Mac Plus',
                    5: 'Mac SE', 6: 'Mac II', 7: 'Mac IIx', 8: 'Mac IIcx',
                    9: 'Mac SE/030', 10: 'Mac Portable', 11: 'Mac IIci',
                    13: 'Mac IIfx', 17: 'Mac Classic', 18: 'Mac IIsi',
                    19: 'Mac LC', 20: 'Quadra 900', 22: 'Quadra 700',
                    36: 'Quadra 650',
                }
                model_name = model_names.get(model_id, f'ID {model_id}')

                # Decode hardware config flags
                decoder_info_ptr = read32(rom, entry_addr)
                nubus_info_ptr = read32(rom, entry_addr + 12)

                print(f'  ${entry_addr:06X}   {product_kind:<5d} ${hw_cfg:04X}    ${rom85:04X}    {model_name}')
                results.append({
                    'offset': entry_addr,
                    'product_kind': product_kind,
                    'model_id': model_id,
                    'model_name': model_name,
                    'hw_cfg': hw_cfg,
                    'rom85': rom85,
                    'decoder_info_ptr': decoder_info_ptr,
                    'nubus_info_ptr': nubus_info_ptr,
                })

                tbl_offset += 4

        offset = pos + 4

    if not results:
        print('  No UniversalInfo tables found.')

    return results


def list_resources(rom):
    """List ROM resources."""
    rsrc_map_rel = read32(rom, 0x1a)
    if rsrc_map_rel == 0 or rsrc_map_rel >= len(rom):
        print('\n  No resource map found.')
        return []

    print(f'\n{"="*60}')
    print(f'ROM Resources (map at ${rsrc_map_rel:06X})')
    print(f'{"="*60}')
    print(f'  {"Offset":<10s} {"Type":<6s} {"ID":<6s} {"Size":<8s} {"Name"}')
    print(f'  {"-"*50}')

    resources = []
    rsrc_ptr = read32(rom, rsrc_map_rel)

    count = 0
    while rsrc_ptr and count < 500:
        if rsrc_ptr >= len(rom) - 24:
            break

        data_offset = read32(rom, rsrc_ptr + 12)
        rtype = rom[rsrc_ptr + 16:rsrc_ptr + 20]
        rid = struct.unpack_from('>h', rom, rsrc_ptr + 20)[0]

        # Resource name
        name_len = rom[rsrc_ptr + 23] if rsrc_ptr + 23 < len(rom) else 0
        name = ''
        if name_len > 0 and rsrc_ptr + 24 + name_len <= len(rom):
            name = rom[rsrc_ptr + 24:rsrc_ptr + 24 + name_len].decode('mac_roman', errors='replace')

        # Resource size (stored 8 bytes before data)
        rsize = 0
        if data_offset >= 8 and data_offset < len(rom):
            rsize = read32(rom, data_offset - 8)

        type_str = rtype.decode('mac_roman', errors='replace')
        print(f'  ${data_offset:06X}   {type_str:<6s} {rid:<6d} {rsize:<8d} {name}')

        resources.append({
            'offset': data_offset,
            'type': type_str,
            'id': rid,
            'size': rsize,
            'name': name,
        })

        rsrc_ptr = read32(rom, rsrc_ptr + 8)
        count += 1

    print(f'\n  Total: {len(resources)} resources')
    return resources


def find_trap_table(rom):
    """Analyze the trap dispatch table."""
    trap_tbl_offset = read32(rom, 0x22)
    if trap_tbl_offset == 0 or trap_tbl_offset >= len(rom):
        print('\n  No trap table found.')
        return

    print(f'\n{"="*60}')
    print(f'Trap Dispatch Table (at ${trap_tbl_offset:06X})')
    print(f'{"="*60}')

    # Decode the compressed trap table (same algorithm as find_rom_trap in rom_patches.cpp)
    bp = trap_tbl_offset
    rom_trap = 0xa800
    ofs = 0
    traps = {}

    for pass_num in range(2):
        for i in range(0x400):
            if bp >= len(rom):
                break
            b = rom[bp]
            bp += 1
            unimplemented = False

            if b == 0x80:
                unimplemented = True
            elif b == 0xff:
                if bp + 4 > len(rom):
                    break
                ofs = read32(rom, bp)
                bp += 4
            elif b & 0x80:
                add = (b & 0x7f) << 1
                if add == 0:
                    break
                ofs += add
            else:
                if bp >= len(rom):
                    break
                add = ((b << 8) | rom[bp]) << 1
                bp += 1
                if add == 0:
                    break
                ofs += add

            if not unimplemented:
                traps[rom_trap] = ofs
            rom_trap += 1

        rom_trap = 0xa000

    # Print interesting traps
    interesting = {
        0xa000: 'Open', 0xa001: 'Close', 0xa002: 'Read', 0xa003: 'Write',
        0xa004: 'Control', 0xa005: 'Status',
        0xa017: 'SetTrapAddress', 0xa029: 'HLock', 0xa02e: 'BlockMove',
        0xa053: 'ReadXPRam/ClkNoMem', 0xa058: 'InsTime', 0xa059: 'RmvTime',
        0xa05a: 'PrimeTime', 0xa07c: 'ADBOp',
        0xa093: 'Microseconds', 0xa08d: 'DebugUtil',
        0xa247: 'SetOSTrapAddress', 0xa346: 'GetOSTrapAddress',
        0xa43d: 'DrvrInstallRsrvMem', 0xa53d: 'DrvrInstallRsrvMem (sys)',
        0xa815: 'SCSIDispatch',
        0xa9f0: 'SysError',
    }

    print(f'\n  Key Traps:')
    for trap_num in sorted(interesting.keys()):
        if trap_num in traps:
            print(f'    ${trap_num:04X} ({interesting[trap_num]:<25s}) -> ${traps[trap_num]:06X}')

    print(f'\n  Total decoded traps: {len(traps)}')
    return traps


def find_patch_points(rom):
    """Find standard emulator patch points by pattern matching."""
    print(f'\n{"="*60}')
    print(f'Emulator Patch Points')
    print(f'{"="*60}')

    rom_size = len(rom)

    def find_pattern(pattern, start=0, end=None):
        """Find byte pattern in ROM."""
        if end is None:
            end = rom_size
        pat = bytes(pattern)
        pos = rom.find(pat, start, end)
        return pos if pos >= 0 else None

    def find_all_pattern(pattern, start=0, end=None):
        """Find all occurrences of byte pattern."""
        if end is None:
            end = rom_size
        pat = bytes(pattern)
        results = []
        pos = start
        while pos < end:
            found = rom.find(pat, pos, end)
            if found < 0:
                break
            results.append(found)
            pos = found + 1
        return results

    # --- Patches from patch_rom_32() ---

    # UniversalInfo magic
    universal_dat = [0xdc, 0x00, 0x05, 0x05, 0x3f, 0xff, 0x01, 0x00]
    pos = find_pattern(universal_dat, 0x3400, 0x3c00)
    if pos:
        print(f'  UniversalInfo magic    : ${pos:06X} (UniversalInfo at ${pos - 0x10:06X})')
    else:
        print(f'  UniversalInfo magic    : NOT FOUND (searched $3400-$3C00)')

    # clear_globs: clr (a2)+; sub d2,d3; bne
    clear_globs_dat = [0x42, 0x9a, 0x36, 0x0a, 0x66, 0xfa]
    pos = find_pattern(clear_globs_dat, 0xa00, 0xb00)
    if pos:
        print(f'  ClearBootGlobs         : ${pos:06X}')
    else:
        print(f'  ClearBootGlobs         : NOT FOUND (searched $A00-$B00)')

    # InitMMU pattern
    if rom_size <= 0x80000:
        init_mmu_dat = [0x0c, 0x47, 0x00, 0x03, 0x62, 0x00, 0xfe]
        pos = find_pattern(init_mmu_dat, 0x4000, 0x50000)
    else:
        init_mmu_dat = [0x0c, 0x47, 0x00, 0x04, 0x62, 0x00, 0xfd]
        pos = find_pattern(init_mmu_dat, 0x80000, 0x90000)
    if pos:
        print(f'  InitMMU                : ${pos:06X}')
    else:
        print(f'  InitMMU                : NOT FOUND')

    # InitMMU2 (no RBV)
    init_mmu2_dat = [0x08, 0x06, 0x00, 0x0d, 0x67]
    if rom_size <= 0x80000:
        pos = find_pattern(init_mmu2_dat, 0x4000, 0x50000)
    else:
        pos = find_pattern(init_mmu2_dat, 0x80000, 0x90000)
    if pos:
        print(f'  InitMMU2 (no RBV)      : ${pos:06X}')
    else:
        print(f'  InitMMU2 (no RBV)      : NOT FOUND')

    # InitMMU3
    init_mmu3_dat = [0x0c, 0x2e, 0x00, 0x01, 0xff, 0xe6, 0x66, 0x0c, 0x4c, 0xed, 0x03, 0x87, 0xff, 0xe8]
    if rom_size <= 0x80000:
        pos = find_pattern(init_mmu3_dat, 0x4000, 0x50000)
    else:
        pos = find_pattern(init_mmu3_dat, 0x80000, 0x90000)
    if pos:
        print(f'  InitMMU3               : ${pos:06X}')
    else:
        print(f'  InitMMU3               : NOT FOUND')

    # ReadXPRAM patterns
    read_xpram_dat = [0x26, 0x4e, 0x41, 0xf9, 0x50, 0xf0, 0x00, 0x00, 0x08, 0x90, 0x00, 0x02]
    pos = find_pattern(read_xpram_dat, 0x40000, 0x50000)
    if pos:
        print(f'  ReadXPRAM (ROM10)      : ${pos:06X}')

    read_xpram2_dat = [0x26, 0x4e, 0x08, 0x92, 0x00, 0x02, 0xea, 0x59, 0x02, 0x01, 0x00, 0x07, 0x00, 0x01, 0x00, 0xb8]
    pos = find_pattern(read_xpram2_dat, 0x40000, 0x50000)
    if pos:
        print(f'  ReadXPRAM (ROM11)      : ${pos:06X}')

    if rom_size > 0x80000:
        read_xpram3_dat = [0x48, 0xe7, 0xe0, 0x60, 0x02, 0x01, 0x00, 0x70, 0x0c, 0x01, 0x00, 0x20]
        pos = find_pattern(read_xpram3_dat, 0x80000, 0x90000)
        if pos:
            print(f'  ReadXPRAM3 (ROM15)     : ${pos:06X}')

    # Init SCC
    init_scc_dat = [0x08, 0x38, 0x00, 0x01, 0x0d, 0xd1, 0x67, 0x04]
    pos = find_pattern(init_scc_dat, 0xa00, 0xa80)
    if pos:
        print(f'  InitSCC                : ${pos:06X}')
    else:
        print(f'  InitSCC                : NOT FOUND (searched $A00-$A80)')

    # Init ASC
    init_asc_dat = [0x26, 0x68, 0x00, 0x30, 0x12, 0x00, 0xeb, 0x01]
    pos = find_pattern(init_asc_dat, 0x4000, 0x5000)
    if pos:
        print(f'  InitASC                : ${pos:06X}')
    else:
        print(f'  InitASC                : NOT FOUND (searched $4000-$5000)')

    # NuBus probe
    nubus_dat = [0x45, 0xfa, 0x00, 0x0a, 0x42, 0xa7, 0x10, 0x11]
    pos = find_pattern(nubus_dat, 0x5000, 0x6000)
    if pos:
        print(f'  NuBus Probe            : ${pos:06X}')
    else:
        print(f'  NuBus Probe            : NOT FOUND (searched $5000-$6000)')

    # VIA2 init
    via2_dat = [0x20, 0x78, 0x0c, 0xec, 0x11, 0x7c, 0x00, 0x90]
    pos = find_pattern(via2_dat, 0xa000, 0xa400)
    if pos:
        print(f'  VIA2 Init              : ${pos:06X}')
    else:
        print(f'  VIA2 Init              : NOT FOUND (searched $A000-$A400)')

    # ModelID read from 0x5ffffffc
    model_id_dat = [0x20, 0x7c, 0x5f, 0xff, 0xff, 0xfc, 0x72, 0x07, 0xc2, 0x90]
    pos = find_pattern(model_id_dat, 0x40000, 0x50000)
    if pos:
        print(f'  ModelID read (ROM20)   : ${pos:06X}')

    model_id2_dat = [0x45, 0xf9, 0x5f, 0xff, 0xff, 0xfc, 0x20, 0x12]
    pos = find_pattern(model_id2_dat, 0x4000, 0x5000)
    if pos:
        print(f'  ModelID read (ROM27/32): ${pos:06X}')

    # memdisp
    memdisp_dat = [0x30, 0x3c, 0xa8, 0x9f, 0xa7, 0x46, 0x30, 0x3c, 0xa0, 0x5c, 0xa2, 0x47]
    pos = find_pattern(memdisp_dat, 0x4f100, 0x4f180)
    if pos:
        print(f'  MemoryDispatch         : ${pos:06X}')
    else:
        print(f'  MemoryDispatch         : NOT FOUND (searched $4F100-$4F180)')

    # fix_memsize2
    fix_memsize2_dat = [0x22, 0x30, 0x81, 0xe2, 0x0d, 0xdc, 0xff, 0xba,
                        0xd2, 0xb0, 0x81, 0xe2, 0x0d, 0xdc, 0xff, 0xec,
                        0x21, 0xc1, 0x1e, 0xf8]
    pos = find_pattern(fix_memsize2_dat, 0x4c000, 0x4c080)
    if pos:
        print(f'  FixMemSize2            : ${pos:06X}')
    else:
        print(f'  FixMemSize2            : NOT FOUND (searched $4C000-$4C080)')

    # BlockMove (68040 PTEST workaround)
    if rom_size > 0x80000:
        bmove_dat = [0x20, 0x5f, 0x22, 0x5f, 0x0c, 0x38, 0x00, 0x04, 0x01, 0x2f]
        pos = find_pattern(bmove_dat, 0x87000, 0x87800)
        if pos:
            print(f'  BlockMove (PTEST)      : ${pos:06X}')

    # --- Hardcoded offsets from patch_rom_32() ---
    # These are specific to certain ROM versions. Check what's at them.
    print(f'\n  Hardcoded Offset Checks:')

    hardcoded = {
        0x008c: 'Reset vector patch',
        0x00ba: 'Post-reset jump target',
        0x00c2: 'GetHardwareInfo',
        0x00c6: 'VIA init block',
        0x010e: 'BootGlobs patch',
        0x0190: 'EnableExtCache',
        0x0226: 'EnableOneSecInts',
        0x0230: 'EnableParityPatch/60Hz',
        0x0490: 'CompBootStack',
        0x07c0: 'CPU type test',
        0x0800: 'SetupTimeK (speed test)',
        0x094a: 'HW base address table',
        0x09a0: 'SCSI init',
        0x09c0: 'IWM init',
        0x09f4c: 'DisableIntSources',
        0x1134: '.Sony/.netBOOT open (JSR)',
        0x1142: 'InstallDrivers patch',
        0x1256: '.netBOOT skip (RTS)',
        0x2b3a: 'VIA Level 1 handler',
        0x2be4: '60Hz handler',
        0x4232: 'HW access $50f1a101',
        0x5b78: 'GetDevBase (framebuffer)',
        0xa8a8: 'InitADB VIA check',
        0xb0e2: 'InitTimeMgr VIA write',
        0xccaa: 'ScratchMem fake handle',
    }

    for addr, desc in sorted(hardcoded.items()):
        if addr < len(rom):
            w = read16(rom, addr)
            print(f'    ${addr:06X} ({desc:<35s}): ${w:04X}', end='')
            # Quick disasm
            mnem, ops, _, _ = disasm_one(rom, addr)
            print(f'  [{mnem} {ops}]')
        else:
            print(f'    ${addr:06X} ({desc:<35s}): BEYOND ROM')


def find_byte_pattern(rom, pattern_hex):
    """Find a hex pattern in ROM."""
    pattern = bytes.fromhex(pattern_hex.replace(' ', ''))
    print(f'\n  Searching for pattern: {pattern.hex(" ")}')
    pos = 0
    found = 0
    while pos < len(rom):
        idx = rom.find(pattern, pos)
        if idx < 0:
            break
        print(f'    Found at ${idx:06X}')
        # Show context
        ctx_start = max(0, idx - 8)
        ctx_end = min(len(rom), idx + len(pattern) + 8)
        hex_ctx = ' '.join(f'{rom[i]:02x}' for i in range(ctx_start, ctx_end))
        print(f'      Context: ...{hex_ctx}...')
        found += 1
        pos = idx + 1

    if found == 0:
        print('    Not found.')
    else:
        print(f'    Total: {found} match(es)')


def hex_dump(rom, start, end, base_addr=0):
    """Hex dump a range."""
    for offset in range(start, min(end, len(rom)), 16):
        hex_bytes = ' '.join(f'{rom[offset + i]:02x}' if offset + i < len(rom) else '  '
                             for i in range(16))
        ascii_str = ''.join(chr(rom[offset + i]) if 32 <= rom[offset + i] < 127 else '.'
                            for i in range(min(16, len(rom) - offset)))
        addr = base_addr + offset
        print(f'  {addr:06x}:  {hex_bytes}  {ascii_str}')


def compare_roms(rom1, rom2, path1, path2):
    """Compare two ROMs."""
    print(f'\n{"="*60}')
    print(f'ROM Comparison')
    print(f'{"="*60}')
    print(f'  ROM 1: {path1} ({len(rom1)} bytes)')
    print(f'  ROM 2: {path2} ({len(rom2)} bytes)')

    min_len = min(len(rom1), len(rom2))
    diffs = []
    for i in range(min_len):
        if rom1[i] != rom2[i]:
            diffs.append(i)

    print(f'  Differences: {len(diffs)} bytes')
    if len(diffs) > 0:
        # Show first/last diff regions
        print(f'  First diff at: ${diffs[0]:06X}')
        print(f'  Last diff at:  ${diffs[-1]:06X}')

        # Group into ranges
        ranges = []
        start = diffs[0]
        prev = diffs[0]
        for d in diffs[1:]:
            if d > prev + 16:
                ranges.append((start, prev + 1))
                start = d
            prev = d
        ranges.append((start, prev + 1))

        print(f'  Diff ranges ({len(ranges)}):')
        for s, e in ranges[:20]:
            print(f'    ${s:06X}-${e:06X} ({e - s} bytes)')
        if len(ranges) > 20:
            print(f'    ... and {len(ranges) - 20} more ranges')


def main():
    parser = argparse.ArgumentParser(description='Mac ROM Disassembler and Analysis Tool')
    parser.add_argument('rom', help='ROM file path')
    parser.add_argument('--disasm', nargs=2, metavar=('START', 'END'),
                        help='Disassemble address range (hex)')
    parser.add_argument('--disasm-around', metavar='ADDR',
                        help='Disassemble 64 bytes around address (hex)')
    parser.add_argument('--find-pattern', metavar='HEX',
                        help='Find byte pattern in ROM (hex string)')
    parser.add_argument('--resources', action='store_true',
                        help='List ROM resources')
    parser.add_argument('--universal', action='store_true',
                        help='Dump UniversalInfo tables')
    parser.add_argument('--traps', action='store_true',
                        help='Dump trap dispatch table')
    parser.add_argument('--patch-points', action='store_true',
                        help='Find all standard emulator patch points')
    parser.add_argument('--header', action='store_true',
                        help='Show ROM header info')
    parser.add_argument('--all', action='store_true',
                        help='Run all analyses')
    parser.add_argument('--hex', nargs=2, metavar=('START', 'END'),
                        help='Hex dump address range (hex)')
    parser.add_argument('--compare', metavar='ROM2',
                        help='Compare with another ROM')
    parser.add_argument('--base-addr', default='0',
                        help='Base address for disassembly display (hex, default: 0)')

    args = parser.parse_args()

    # Load ROM
    rom_path = Path(args.rom)
    if not rom_path.exists():
        print(f'Error: ROM file not found: {rom_path}')
        sys.exit(1)

    rom = rom_path.read_bytes()
    base_addr = int(args.base_addr, 16)

    print(f'Loaded: {rom_path.name} ({len(rom)} bytes, {len(rom)//1024}KB)')

    if args.all:
        args.header = True
        args.resources = True
        args.universal = True
        args.traps = True
        args.patch_points = True

    # Default: show header and patch points
    if not any([args.header, args.resources, args.universal, args.traps,
                args.patch_points, args.disasm, args.disasm_around,
                args.find_pattern, args.hex, args.compare]):
        args.header = True
        args.patch_points = True

    if args.header:
        analyze_header(rom)

    if args.universal:
        find_universal_info(rom)

    if args.resources:
        list_resources(rom)

    if args.traps:
        find_trap_table(rom)

    if args.patch_points:
        find_patch_points(rom)

    if args.disasm:
        start = int(args.disasm[0], 16)
        end = int(args.disasm[1], 16)
        print(f'\n  Disassembly ${start:06X}-${end:06X}:')
        print(disassemble(rom, start, end, base_addr))

    if args.disasm_around:
        addr = int(args.disasm_around, 16)
        start = max(0, addr - 32)
        end = min(len(rom), addr + 32)
        print(f'\n  Disassembly around ${addr:06X}:')
        print(disassemble(rom, start, end, base_addr))

    if args.find_pattern:
        find_byte_pattern(rom, args.find_pattern)

    if args.hex:
        start = int(args.hex[0], 16)
        end = int(args.hex[1], 16)
        print(f'\n  Hex dump ${start:06X}-${end:06X}:')
        hex_dump(rom, start, end, base_addr)

    if args.compare:
        rom2_path = Path(args.compare)
        if not rom2_path.exists():
            print(f'Error: ROM file not found: {rom2_path}')
            sys.exit(1)
        rom2 = rom2_path.read_bytes()
        compare_roms(rom, rom2, str(rom_path), str(rom2_path))


if __name__ == '__main__':
    main()
