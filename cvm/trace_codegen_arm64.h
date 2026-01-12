/*
 * Pseudocode Tracing JIT - ARM64 Code Generator
 *
 * ARM64-specific instruction emission and code generation.
 * Included by trace_codegen.c when compiling for ARM64.
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#ifndef TRACE_CODEGEN_ARM64_H
#define TRACE_CODEGEN_ARM64_H

/* ============================================================
 * ARM64 Register Encoding
 * ============================================================
 * 
 * ARM64 has 31 general-purpose registers (X0-X30) plus SP and ZR.
 * 
 * Calling convention (AAPCS64):
 *   X0-X7:   Arguments and return values
 *   X8:      Indirect result location
 *   X9-X15:  Temporary/scratch registers (caller-saved)
 *   X16-X17: Intra-procedure-call scratch (IP0, IP1)
 *   X18:     Platform register (reserved on some OSes)
 *   X19-X28: Callee-saved registers
 *   X29:     Frame pointer (FP)
 *   X30:     Link register (LR)
 *   SP:      Stack pointer (register 31 in some contexts)
 *   XZR:     Zero register (register 31 in other contexts)
 *
 * For JIT:
 *   X0 = bp (base pointer for locals) - first argument
 *   X1 = globals_values - second argument
 *   X2 = constants - third argument
 *   X9-X15: Allocatable scratch registers
 *   X19-X28: Callee-saved, available but must be preserved
 */

/* Register numbers (same as hardware encoding) */
#define ARM_X0  0
#define ARM_X1  1
#define ARM_X2  2
#define ARM_X3  3
#define ARM_X4  4
#define ARM_X5  5
#define ARM_X6  6
#define ARM_X7  7
#define ARM_X8  8
#define ARM_X9  9
#define ARM_X10 10
#define ARM_X11 11
#define ARM_X12 12
#define ARM_X13 13
#define ARM_X14 14
#define ARM_X15 15
#define ARM_X16 16  /* IP0 - scratch */
#define ARM_X17 17  /* IP1 - scratch */
#define ARM_X18 18  /* Platform register */
#define ARM_X19 19
#define ARM_X20 20
#define ARM_X21 21
#define ARM_X22 22
#define ARM_X23 23
#define ARM_X24 24
#define ARM_X25 25
#define ARM_X26 26
#define ARM_X27 27
#define ARM_X28 28
#define ARM_FP  29  /* Frame pointer */
#define ARM_LR  30  /* Link register */
#define ARM_SP  31  /* Stack pointer (context-dependent) */
#define ARM_XZR 31  /* Zero register (context-dependent) */

/* Map physical register numbers to ARM64 registers
 * We use a subset that's safe for JIT code */
static const int arm64_phys_to_reg[] = {
    ARM_X9,   /* phys 0 -> X9 */
    ARM_X10,  /* phys 1 -> X10 */
    ARM_X11,  /* phys 2 -> X11 */
    ARM_X12,  /* phys 3 -> X12 */
    ARM_X13,  /* phys 4 -> X13 */
    ARM_X14,  /* phys 5 -> X14 */
    ARM_X15,  /* phys 6 -> X15 */
    ARM_X19,  /* phys 7 -> X19 (callee-saved) */
    ARM_X20,  /* phys 8 -> X20 (callee-saved) */
    ARM_X21,  /* phys 9 -> X21 (callee-saved) */
    ARM_X22,  /* phys 10 -> X22 (callee-saved) */
    ARM_X23,  /* phys 11 -> X23 (callee-saved) */
};
#define ARM64_NUM_REGS 12

/* Scratch registers for codegen (not allocatable) */
#define ARM_SCRATCH1 ARM_X16  /* IP0 */
#define ARM_SCRATCH2 ARM_X17  /* IP1 */

/* ============================================================
 * ARM64 Instruction Encoding Helpers
 * ============================================================
 *
 * ARM64 uses fixed 32-bit instructions.
 * Instructions are encoded in little-endian format.
 */

/* Emit a 32-bit ARM64 instruction */
static inline void emit_arm(MCode *mc, uint32_t insn)
{
    emit(mc, insn & 0xFF);
    emit(mc, (insn >> 8) & 0xFF);
    emit(mc, (insn >> 16) & 0xFF);
    emit(mc, (insn >> 24) & 0xFF);
}

/* ============================================================
 * ARM64 Instruction Emission
 * ============================================================ */

/* NOP */
static void arm_emit_nop(MCode *mc)
{
    emit_arm(mc, 0xD503201F);
}

/* RET (return via LR) */
static void arm_emit_ret(MCode *mc)
{
    /* RET: 1101011 0010 11111 0000 00 11110 00000 */
    emit_arm(mc, 0xD65F03C0);
}

/* MOV Xd, Xn (register to register, 64-bit) */
static void arm_emit_mov_rr(MCode *mc, int dst, int src)
{
    /* ORR Xd, XZR, Xn (MOV is an alias) */
    /* 10101010 000 Xm 000000 11111 Xd */
    uint32_t insn = 0xAA0003E0 | (src << 16) | dst;
    emit_arm(mc, insn);
}

/* MOV Xd, #imm16 (move immediate, 64-bit, no shift) */
static void arm_emit_movz(MCode *mc, int dst, uint16_t imm, int shift)
{
    /* MOVZ: 110100101 hw imm16 Rd */
    /* hw = shift / 16 (0, 1, 2, or 3) */
    uint32_t hw = (shift / 16) & 3;
    uint32_t insn = 0xD2800000 | (hw << 21) | ((uint32_t)imm << 5) | dst;
    emit_arm(mc, insn);
}

/* MOVK Xd, #imm16, LSL #shift (keep other bits) */
static void arm_emit_movk(MCode *mc, int dst, uint16_t imm, int shift)
{
    /* MOVK: 111100101 hw imm16 Rd */
    uint32_t hw = (shift / 16) & 3;
    uint32_t insn = 0xF2800000 | (hw << 21) | ((uint32_t)imm << 5) | dst;
    emit_arm(mc, insn);
}

/* Load 64-bit immediate into register */
static void arm_emit_mov_ri64(MCode *mc, int dst, int64_t imm)
{
    uint64_t uimm = (uint64_t)imm;
    
    /* Use MOVZ for first non-zero 16-bit chunk, MOVK for rest */
    int first = 1;
    for (int shift = 0; shift < 64; shift += 16) {
        uint16_t chunk = (uimm >> shift) & 0xFFFF;
        if (chunk != 0 || shift == 0) {
            if (first) {
                arm_emit_movz(mc, dst, chunk, shift);
                first = 0;
            } else if (chunk != 0) {
                arm_emit_movk(mc, dst, chunk, shift);
            }
        }
    }
    
    /* Handle zero case */
    if (first) {
        arm_emit_movz(mc, dst, 0, 0);
    }
}

/* LDR Xt, [Xn, #imm] (load 64-bit from base + offset) */
static void arm_emit_ldr_imm(MCode *mc, int dst, int base, int32_t offset)
{
    /* Offset must be aligned to 8 bytes and in range [0, 32760] for unsigned */
    /* For simplicity, use unscaled if offset is small or negative */
    if (offset >= 0 && offset < 32760 && (offset & 7) == 0) {
        /* LDR (unsigned offset): 11111001 01 imm12 Xn Xt */
        uint32_t imm12 = (offset >> 3) & 0xFFF;
        uint32_t insn = 0xF9400000 | (imm12 << 10) | (base << 5) | dst;
        emit_arm(mc, insn);
    } else if (offset >= -256 && offset <= 255) {
        /* LDUR (unscaled): 11111000 010 imm9 00 Xn Xt */
        uint32_t imm9 = offset & 0x1FF;
        uint32_t insn = 0xF8400000 | (imm9 << 12) | (base << 5) | dst;
        emit_arm(mc, insn);
    } else {
        /* Load offset into scratch, then use register offset */
        arm_emit_mov_ri64(mc, ARM_SCRATCH1, offset);
        /* LDR Xt, [Xn, Xm]: 11111000 011 Xm 011 0 10 Xn Xt */
        uint32_t insn = 0xF8606800 | (ARM_SCRATCH1 << 16) | (base << 5) | dst;
        emit_arm(mc, insn);
    }
}

/* STR Xt, [Xn, #imm] (store 64-bit to base + offset) */
static void arm_emit_str_imm(MCode *mc, int src, int base, int32_t offset)
{
    if (offset >= 0 && offset < 32760 && (offset & 7) == 0) {
        /* STR (unsigned offset): 11111001 00 imm12 Xn Xt */
        uint32_t imm12 = (offset >> 3) & 0xFFF;
        uint32_t insn = 0xF9000000 | (imm12 << 10) | (base << 5) | src;
        emit_arm(mc, insn);
    } else if (offset >= -256 && offset <= 255) {
        /* STUR (unscaled): 11111000 000 imm9 00 Xn Xt */
        uint32_t imm9 = offset & 0x1FF;
        uint32_t insn = 0xF8000000 | (imm9 << 12) | (base << 5) | src;
        emit_arm(mc, insn);
    } else {
        arm_emit_mov_ri64(mc, ARM_SCRATCH1, offset);
        /* STR Xt, [Xn, Xm]: 11111000 001 Xm 011 0 10 Xn Xt */
        uint32_t insn = 0xF8206800 | (ARM_SCRATCH1 << 16) | (base << 5) | src;
        emit_arm(mc, insn);
    }
}

/* STP Xt1, Xt2, [SP, #imm]! (store pair, pre-index, for push) */
static void arm_emit_stp_pre(MCode *mc, int rt1, int rt2, int32_t offset)
{
    /* STP (pre-index): 10101001 11 imm7 Rt2 Rn Rt1 */
    /* imm7 is signed, scaled by 8 */
    int32_t imm7 = (offset >> 3) & 0x7F;
    uint32_t insn = 0xA9800000 | (imm7 << 15) | (rt2 << 10) | (ARM_SP << 5) | rt1;
    /* Set pre-index bit (bit 24) */
    insn |= (1 << 24);
    emit_arm(mc, insn);
}

/* LDP Xt1, Xt2, [SP], #imm (load pair, post-index, for pop) */
static void arm_emit_ldp_post(MCode *mc, int rt1, int rt2, int32_t offset)
{
    /* LDP (post-index): 10101000 11 imm7 Rt2 Rn Rt1 */
    int32_t imm7 = (offset >> 3) & 0x7F;
    uint32_t insn = 0xA8C00000 | (imm7 << 15) | (rt2 << 10) | (ARM_SP << 5) | rt1;
    emit_arm(mc, insn);
}

/* ADD Xd, Xn, Xm (64-bit register add) */
static void arm_emit_add_rrr(MCode *mc, int dst, int src1, int src2)
{
    /* ADD: 10001011 000 Xm 000000 Xn Xd */
    uint32_t insn = 0x8B000000 | (src2 << 16) | (src1 << 5) | dst;
    emit_arm(mc, insn);
}

/* ADD Xd, Xn, #imm (64-bit immediate add) */
static void arm_emit_add_ri(MCode *mc, int dst, int src, int32_t imm)
{
    if (imm >= 0 && imm < 4096) {
        /* ADD (immediate): 10010001 00 imm12 Xn Xd */
        uint32_t insn = 0x91000000 | ((imm & 0xFFF) << 10) | (src << 5) | dst;
        emit_arm(mc, insn);
    } else if (imm < 0 && imm > -4096) {
        /* Use SUB for negative immediate */
        uint32_t insn = 0xD1000000 | (((-imm) & 0xFFF) << 10) | (src << 5) | dst;
        emit_arm(mc, insn);
    } else {
        arm_emit_mov_ri64(mc, ARM_SCRATCH1, imm);
        arm_emit_add_rrr(mc, dst, src, ARM_SCRATCH1);
    }
}

/* SUB Xd, Xn, Xm (64-bit register subtract) */
static void arm_emit_sub_rrr(MCode *mc, int dst, int src1, int src2)
{
    /* SUB: 11001011 000 Xm 000000 Xn Xd */
    uint32_t insn = 0xCB000000 | (src2 << 16) | (src1 << 5) | dst;
    emit_arm(mc, insn);
}

/* MUL Xd, Xn, Xm (64-bit multiply) */
static void arm_emit_mul_rrr(MCode *mc, int dst, int src1, int src2)
{
    /* MUL is alias for MADD with Ra=XZR */
    /* MADD: 10011011 000 Xm 0 11111 Xn Xd */
    uint32_t insn = 0x9B007C00 | (src2 << 16) | (src1 << 5) | dst;
    emit_arm(mc, insn);
}

/* SDIV Xd, Xn, Xm (64-bit signed divide) */
static void arm_emit_sdiv_rrr(MCode *mc, int dst, int src1, int src2)
{
    /* SDIV: 10011010 110 Xm 00001 1 Xn Xd */
    uint32_t insn = 0x9AC00C00 | (src2 << 16) | (src1 << 5) | dst;
    emit_arm(mc, insn);
}

/* MSUB Xd, Xn, Xm, Xa: Xd = Xa - Xn*Xm (for modulo: remainder = dividend - quotient*divisor) */
static void arm_emit_msub(MCode *mc, int dst, int mul1, int mul2, int sub_from)
{
    /* MSUB: 10011011 000 Xm 1 Xa Xn Xd */
    uint32_t insn = 0x9B008000 | (mul2 << 16) | (sub_from << 10) | (mul1 << 5) | dst;
    emit_arm(mc, insn);
}

/* NEG Xd, Xn (negate: Xd = 0 - Xn) */
static void arm_emit_neg(MCode *mc, int dst, int src)
{
    /* NEG is alias for SUB Xd, XZR, Xn */
    uint32_t insn = 0xCB000000 | (src << 16) | (ARM_XZR << 5) | dst;
    emit_arm(mc, insn);
}

/* LSL Xd, Xn, #shift (logical shift left by immediate) */
static void arm_emit_lsl_ri(MCode *mc, int dst, int src, int shift)
{
    /* UBFM Xd, Xn, #(-shift MOD 64), #(63-shift) */
    int immr = (-shift) & 63;
    int imms = 63 - shift;
    uint32_t insn = 0xD3400000 | (immr << 16) | (imms << 10) | (src << 5) | dst;
    emit_arm(mc, insn);
}

/* ASR Xd, Xn, #shift (arithmetic shift right by immediate) */
static void arm_emit_asr_ri(MCode *mc, int dst, int src, int shift)
{
    /* SBFM Xd, Xn, #shift, #63 */
    uint32_t insn = 0x9340FC00 | (shift << 16) | (src << 5) | dst;
    emit_arm(mc, insn);
}

/* ORR Xd, Xn, Xm (bitwise OR) */
static void arm_emit_orr_rrr(MCode *mc, int dst, int src1, int src2)
{
    /* ORR: 10101010 000 Xm 000000 Xn Xd */
    uint32_t insn = 0xAA000000 | (src2 << 16) | (src1 << 5) | dst;
    emit_arm(mc, insn);
}

/* AND Xd, Xn, Xm (bitwise AND) */
static void arm_emit_and_rrr(MCode *mc, int dst, int src1, int src2)
{
    /* AND: 10001010 000 Xm 000000 Xn Xd */
    uint32_t insn = 0x8A000000 | (src2 << 16) | (src1 << 5) | dst;
    emit_arm(mc, insn);
}

/* EOR Xd, Xn, Xm (bitwise XOR) */
static void arm_emit_eor_rrr(MCode *mc, int dst, int src1, int src2)
{
    /* EOR: 11001010 000 Xm 000000 Xn Xd */
    uint32_t insn = 0xCA000000 | (src2 << 16) | (src1 << 5) | dst;
    emit_arm(mc, insn);
}

/* CMP Xn, Xm (compare: sets flags) */
static void arm_emit_cmp_rr(MCode *mc, int src1, int src2)
{
    /* CMP is alias for SUBS XZR, Xn, Xm */
    uint32_t insn = 0xEB000000 | (src2 << 16) | (src1 << 5) | ARM_XZR;
    emit_arm(mc, insn);
}

/* CMP Xn, #imm */
static void arm_emit_cmp_ri(MCode *mc, int src, int32_t imm)
{
    if (imm >= 0 && imm < 4096) {
        /* CMP (immediate): SUBS XZR, Xn, #imm */
        uint32_t insn = 0xF1000000 | ((imm & 0xFFF) << 10) | (src << 5) | ARM_XZR;
        emit_arm(mc, insn);
    } else {
        arm_emit_mov_ri64(mc, ARM_SCRATCH1, imm);
        arm_emit_cmp_rr(mc, src, ARM_SCRATCH1);
    }
}

/* TST Xn, #imm (test bits) */
static void arm_emit_tst_ri(MCode *mc, int src, int64_t imm)
{
    /* For simple cases like testing bit 0 */
    if (imm == 1) {
        /* TST Xn, #1: ANDS XZR, Xn, #1 */
        /* Encoding immediate 1 in logical immediate format */
        uint32_t insn = 0xF2400000 | (src << 5) | ARM_XZR;
        emit_arm(mc, insn);
    } else {
        arm_emit_mov_ri64(mc, ARM_SCRATCH1, imm);
        /* ANDS XZR, Xn, Xm */
        uint32_t insn = 0xEA000000 | (ARM_SCRATCH1 << 16) | (src << 5) | ARM_XZR;
        emit_arm(mc, insn);
    }
}

/* B.cond - conditional branch (returns patch offset) */
static size_t arm_emit_bcond(MCode *mc, int cond)
{
    /* B.cond: 01010100 imm19 0 cond */
    /* cond: EQ=0, NE=1, LT=11, GE=10, LE=13, GT=12 */
    size_t off = mc->length;
    uint32_t insn = 0x54000000 | cond;
    emit_arm(mc, insn);
    return off;
}

/* B - unconditional branch (returns patch offset) */
static size_t arm_emit_b(MCode *mc)
{
    size_t off = mc->length;
    /* B: 000101 imm26 */
    emit_arm(mc, 0x14000000);
    return off;
}

/* Patch a branch instruction */
static void arm_patch_branch(MCode *mc, size_t off, size_t target)
{
    int32_t rel = (int32_t)(target - off) >> 2;  /* Offset in instructions */
    uint32_t insn;
    memcpy(&insn, &mc->code[off], 4);
    
    if ((insn & 0xFF000000) == 0x54000000) {
        /* B.cond: patch imm19 */
        insn = (insn & 0xFF00001F) | ((rel & 0x7FFFF) << 5);
    } else if ((insn & 0xFC000000) == 0x14000000) {
        /* B: patch imm26 */
        insn = (insn & 0xFC000000) | (rel & 0x03FFFFFF);
    }
    
    mc->code[off] = insn & 0xFF;
    mc->code[off + 1] = (insn >> 8) & 0xFF;
    mc->code[off + 2] = (insn >> 16) & 0xFF;
    mc->code[off + 3] = (insn >> 24) & 0xFF;
}

/* Branch conditions */
#define ARM_COND_EQ 0   /* Equal */
#define ARM_COND_NE 1   /* Not equal */
#define ARM_COND_LT 11  /* Signed less than */
#define ARM_COND_GE 10  /* Signed greater or equal */
#define ARM_COND_LE 13  /* Signed less or equal */
#define ARM_COND_GT 12  /* Signed greater than */

/* ============================================================
 * ARM64 Box/Unbox Operations
 * ============================================================ */

/* Unbox integer: dst = src >> 3 (arithmetic shift) */
static void arm_emit_unbox_int(MCode *mc, int dst, int src)
{
    arm_emit_asr_ri(mc, dst, src, 3);
}

/* Box integer: dst = (src << 3) | (QNAN | TAG_INT) */
static void arm_emit_box_int(MCode *mc, int dst, int src)
{
    /* Shift left 3 */
    arm_emit_lsl_ri(mc, dst, src, 3);
    
    /* Load QNAN | TAG_INT into scratch and OR */
    arm_emit_mov_ri64(mc, ARM_SCRATCH1, QNAN | TAG_INT);
    arm_emit_orr_rrr(mc, dst, dst, ARM_SCRATCH1);
}

/* ============================================================
 * ARM64 IR Compiler
 * ============================================================ */

/* Forward declarations */
struct TraceIR;
struct IRIns;

/* Convert physical register from x86-64 numbering to ARM64 */
static inline int arm_phys_reg(int phys)
{
    if (phys < 0 || phys >= ARM64_NUM_REGS) return ARM_X9;
    return arm64_phys_to_reg[phys];
}

/* Compile a single IR operation for ARM64 */
static void arm_compile_ir_op(MCode *mc, TraceIR *ir, IRIns *ins)
{
    int dst = (ins->dst > 0) ? arm_phys_reg(ir->vregs[ins->dst].phys_reg) : -1;
    int src1 = (ins->src1 > 0) ? arm_phys_reg(ir->vregs[ins->src1].phys_reg) : -1;
    int src2 = (ins->src2 > 0) ? arm_phys_reg(ir->vregs[ins->src2].phys_reg) : -1;

    switch (ins->op)
    {
    case IR_NOP:
        break;

    case IR_CONST_INT:
    case IR_CONST_INT64:
        if (dst >= 0) {
            arm_emit_mov_ri64(mc, dst, ins->imm.i64);
        }
        break;

    case IR_CONST_BOOL:
        if (dst >= 0) {
            arm_emit_movz(mc, dst, ins->imm.i64 ? 1 : 0, 0);
        }
        break;

    case IR_CONST_NIL:
        if (dst >= 0) {
            arm_emit_mov_ri64(mc, dst, QNAN | TAG_NIL);
        }
        break;

    case IR_LOAD_CONST:
        if (dst >= 0) {
            /* Constants are in X2 (third argument) */
            arm_emit_ldr_imm(mc, dst, ARM_X2, ins->aux * 8);
        }
        break;

    case IR_COPY:
        if (dst >= 0 && src1 >= 0 && dst != src1) {
            arm_emit_mov_rr(mc, dst, src1);
        }
        break;

    case IR_LOAD_LOCAL:
        if (dst >= 0) {
            /* Load from bp[slot]. bp is in X0 */
            arm_emit_ldr_imm(mc, dst, ARM_X0, ins->aux * 8);
        }
        break;

    case IR_STORE_LOCAL:
        if (src1 >= 0) {
            /* Store to bp[slot]. bp is in X0 */
            arm_emit_str_imm(mc, src1, ARM_X0, ins->aux * 8);
        }
        break;

    case IR_ADD_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0) {
            arm_emit_add_rrr(mc, dst, src1, src2);
        }
        break;

    case IR_SUB_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0) {
            arm_emit_sub_rrr(mc, dst, src1, src2);
        }
        break;

    case IR_MUL_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0) {
            arm_emit_mul_rrr(mc, dst, src1, src2);
        }
        break;

    case IR_DIV_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0) {
            arm_emit_sdiv_rrr(mc, dst, src1, src2);
        }
        break;

    case IR_MOD_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0) {
            /* remainder = dividend - (dividend / divisor) * divisor */
            /* Use SDIV then MSUB */
            arm_emit_sdiv_rrr(mc, ARM_SCRATCH1, src1, src2);
            arm_emit_msub(mc, dst, ARM_SCRATCH1, src2, src1);
        }
        break;

    case IR_NEG_INT:
        if (dst >= 0 && src1 >= 0) {
            arm_emit_neg(mc, dst, src1);
        }
        break;

    case IR_AND:
        if (dst >= 0 && src1 >= 0 && src2 >= 0) {
            arm_emit_and_rrr(mc, dst, src1, src2);
        }
        break;

    case IR_OR:
        if (dst >= 0 && src1 >= 0 && src2 >= 0) {
            arm_emit_orr_rrr(mc, dst, src1, src2);
        }
        break;

    case IR_UNBOX_INT:
        if (dst >= 0 && src1 >= 0) {
            arm_emit_unbox_int(mc, dst, src1);
        }
        break;

    case IR_BOX_INT:
        if (dst >= 0 && src1 >= 0) {
            arm_emit_box_int(mc, dst, src1);
        }
        break;

    case IR_RET:
    case IR_JUMP:
    case IR_BRANCH:
    case IR_LOOP:
        /* Handled in main compile loop */
        break;

    case IR_EXIT:
        /* For now, just emit a return (will be patched) */
        /* TODO: Proper exit stub handling */
        break;

    default:
        /* Unsupported op - emit NOP */
        arm_emit_nop(mc);
        break;
    }
}

/* ARM64 trace compiler */
static bool trace_compile_arm64(TraceIR *ir, void **code_out, size_t *size_out,
                                uint8_t **exit_stubs, uint32_t *num_exits_out)
{
    MCode mc;
    mcode_init(&mc, 16384);
    if (!mc.code)
        return false;

    /* Track exits (unused for now) */
    (void)exit_stubs;
    *num_exits_out = 0;

    /* Prologue: save callee-saved registers we use */
    /* STP X19, X20, [SP, #-16]! */
    arm_emit_stp_pre(&mc, ARM_X19, ARM_X20, -16);
    /* STP X21, X22, [SP, #-16]! */
    arm_emit_stp_pre(&mc, ARM_X21, ARM_X22, -16);
    /* STP X23, X29, [SP, #-16]! - save FP too */
    arm_emit_stp_pre(&mc, ARM_X23, ARM_FP, -16);
    /* STP X30, XZR, [SP, #-16]! - save LR */
    arm_emit_stp_pre(&mc, ARM_LR, ARM_XZR, -16);

    /* Track loop start for back edge */
    size_t loop_start = 0;

    /* Compile each IR instruction */
    for (uint32_t i = 0; i < ir->nops; i++)
    {
        IRIns *ins = &ir->ops[i];

        if (ins->op == IR_LOOP)
        {
            /* Loop back edge */
            int cond_reg = (ins->src1 > 0) ? arm_phys_reg(ir->vregs[ins->src1].phys_reg) : -1;

            if (cond_reg >= 0 && loop_start > 0)
            {
                /* Test condition and branch back if non-zero */
                arm_emit_tst_ri(&mc, cond_reg, 1);
                size_t branch = arm_emit_bcond(&mc, ARM_COND_NE);
                arm_patch_branch(&mc, branch, loop_start);
            }
            else if (loop_start > 0)
            {
                /* Unconditional back edge */
                size_t branch = arm_emit_b(&mc);
                arm_patch_branch(&mc, branch, loop_start);
            }
            continue;
        }

        /* Mark loop start position */
        if (ins->aux > 0 && ins->aux == i)
        {
            loop_start = mc.length;
        }
        if (i == ir->loop_start)
        {
            loop_start = mc.length;
        }

        arm_compile_ir_op(&mc, ir, ins);
    }

    /* Epilogue: restore callee-saved registers */
    /* LDP X30, XZR, [SP], #16 */
    arm_emit_ldp_post(&mc, ARM_LR, ARM_XZR, 16);
    /* LDP X23, X29, [SP], #16 */
    arm_emit_ldp_post(&mc, ARM_X23, ARM_FP, 16);
    /* LDP X21, X22, [SP], #16 */
    arm_emit_ldp_post(&mc, ARM_X21, ARM_X22, 16);
    /* LDP X19, X20, [SP], #16 */
    arm_emit_ldp_post(&mc, ARM_X19, ARM_X20, 16);
    /* RET */
    arm_emit_ret(&mc);

    *code_out = mcode_finalize(&mc);
    *size_out = mc.length;

    return (*code_out != NULL);
}

#endif /* TRACE_CODEGEN_ARM64_H */
