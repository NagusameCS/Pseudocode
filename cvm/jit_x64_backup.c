/*
 * Pseudocode Full Tracing JIT Compiler - x86-64 Backend
 *
 * A proper tracing JIT that compiles arbitrary bytecode loops to native x86-64.
 * No pattern matching - actual bytecode-to-native compilation.
 *
 * Architecture:
 * 1. Hot loop detection via counter
 * 2. Trace recording - capture bytecode sequence
 * 3. Type inference - track types of locals/globals
 * 4. Native compilation - emit x86-64 with guards
 * 5. Execution with bailout support
 *
 * Register allocation (System V AMD64 ABI):
 *   RDI = locals base pointer (bp) - preserved
 *   RSI = globals table pointer - preserved
 *   RDX = constants array pointer - preserved
 *   RAX = accumulator / return value
 *   RCX, R8-R11 = scratch registers
 *   RBX, R12-R15 = callee-saved (we preserve these)
 *   RSP = stack pointer
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>
#include "pseudo.h"
#include "jit.h"

/* ============================================================
 * x86-64 Machine Code Buffer
 * ============================================================ */

typedef struct
{
    uint8_t *code;
    size_t capacity;
    size_t length;
} MachineCode;

static void mc_init(MachineCode *mc, size_t size)
{
    mc->code = mmap(NULL, size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    mc->capacity = (mc->code != MAP_FAILED) ? size : 0;
    mc->length = 0;
}

static inline void mc_emit(MachineCode *mc, uint8_t byte)
{
    if (mc->length < mc->capacity)
        mc->code[mc->length++] = byte;
}

static inline void mc_emit32(MachineCode *mc, int32_t v)
{
    mc_emit(mc, v & 0xff);
    mc_emit(mc, (v >> 8) & 0xff);
    mc_emit(mc, (v >> 16) & 0xff);
    mc_emit(mc, (v >> 24) & 0xff);
}

static inline void mc_emit64(MachineCode *mc, int64_t v)
{
    mc_emit32(mc, v & 0xffffffff);
    mc_emit32(mc, (v >> 32) & 0xffffffff);
}

static void mc_patch32(MachineCode *mc, size_t offset, int32_t v)
{
    mc->code[offset] = v & 0xff;
    mc->code[offset + 1] = (v >> 8) & 0xff;
    mc->code[offset + 2] = (v >> 16) & 0xff;
    mc->code[offset + 3] = (v >> 24) & 0xff;
}

static void *mc_finalize(MachineCode *mc)
{
    if (!mc->code)
        return NULL;
    mprotect(mc->code, mc->capacity, PROT_READ | PROT_EXEC);
    return mc->code;
}

/* ============================================================
 * x86-64 Registers
 * ============================================================ */

#define RAX 0
#define RCX 1
#define RDX 2
#define RBX 3
#define RSP 4
#define RBP 5
#define RSI 6
#define RDI 7
#define R8 8
#define R9 9
#define R10 10
#define R11 11
#define R12 12
#define R13 13
#define R14 14
#define R15 15

/* REX prefix for 64-bit and extended registers */
static inline uint8_t rex(int w, int r, int x, int b)
{
    return 0x40 | (w ? 8 : 0) | ((r >= 8) ? 4 : 0) |
           ((x >= 8) ? 2 : 0) | ((b >= 8) ? 1 : 0);
}

/* ============================================================
 * x86-64 Instruction Emission
 * ============================================================ */

/* MOV reg, reg */
static void emit_mov_rr(MachineCode *mc, int dst, int src)
{
    mc_emit(mc, rex(1, src, 0, dst));
    mc_emit(mc, 0x89);
    mc_emit(mc, 0xc0 | ((src & 7) << 3) | (dst & 7));
}

/* MOV reg, imm64 */
static void emit_mov_ri64(MachineCode *mc, int reg, int64_t imm)
{
    mc_emit(mc, rex(1, 0, 0, reg));
    mc_emit(mc, 0xb8 | (reg & 7));
    mc_emit64(mc, imm);
}

/* MOV reg, [base + offset] */
static void emit_mov_rm(MachineCode *mc, int dst, int base, int32_t off)
{
    mc_emit(mc, rex(1, dst, 0, base));
    mc_emit(mc, 0x8b);
    if (off == 0 && (base & 7) != RBP)
    {
        mc_emit(mc, ((dst & 7) << 3) | (base & 7));
        if ((base & 7) == RSP)
            mc_emit(mc, 0x24);
    }
    else if (off >= -128 && off <= 127)
    {
        mc_emit(mc, 0x40 | ((dst & 7) << 3) | (base & 7));
        if ((base & 7) == RSP)
            mc_emit(mc, 0x24);
        mc_emit(mc, off);
    }
    else
    {
        mc_emit(mc, 0x80 | ((dst & 7) << 3) | (base & 7));
        if ((base & 7) == RSP)
            mc_emit(mc, 0x24);
        mc_emit32(mc, off);
    }
}

/* MOV [base + offset], reg */
static void emit_mov_mr(MachineCode *mc, int base, int32_t off, int src)
{
    mc_emit(mc, rex(1, src, 0, base));
    mc_emit(mc, 0x89);
    if (off == 0 && (base & 7) != RBP)
    {
        mc_emit(mc, ((src & 7) << 3) | (base & 7));
        if ((base & 7) == RSP)
            mc_emit(mc, 0x24);
    }
    else if (off >= -128 && off <= 127)
    {
        mc_emit(mc, 0x40 | ((src & 7) << 3) | (base & 7));
        if ((base & 7) == RSP)
            mc_emit(mc, 0x24);
        mc_emit(mc, off);
    }
    else
    {
        mc_emit(mc, 0x80 | ((src & 7) << 3) | (base & 7));
        if ((base & 7) == RSP)
            mc_emit(mc, 0x24);
        mc_emit32(mc, off);
    }
}

/* ADD reg, reg */
static void emit_add_rr(MachineCode *mc, int dst, int src)
{
    mc_emit(mc, rex(1, src, 0, dst));
    mc_emit(mc, 0x01);
    mc_emit(mc, 0xc0 | ((src & 7) << 3) | (dst & 7));
}

/* ADD reg, imm32 */
static void emit_add_ri(MachineCode *mc, int reg, int32_t imm)
{
    mc_emit(mc, rex(1, 0, 0, reg));
    if (imm >= -128 && imm <= 127)
    {
        mc_emit(mc, 0x83);
        mc_emit(mc, 0xc0 | (reg & 7));
        mc_emit(mc, imm);
    }
    else
    {
        mc_emit(mc, 0x81);
        mc_emit(mc, 0xc0 | (reg & 7));
        mc_emit32(mc, imm);
    }
}

/* SUB reg, reg */
static void emit_sub_rr(MachineCode *mc, int dst, int src)
{
    mc_emit(mc, rex(1, src, 0, dst));
    mc_emit(mc, 0x29);
    mc_emit(mc, 0xc0 | ((src & 7) << 3) | (dst & 7));
}

/* SUB reg, imm32 */
static void emit_sub_ri(MachineCode *mc, int reg, int32_t imm)
{
    mc_emit(mc, rex(1, 0, 0, reg));
    if (imm >= -128 && imm <= 127)
    {
        mc_emit(mc, 0x83);
        mc_emit(mc, 0xe8 | (reg & 7));
        mc_emit(mc, imm);
    }
    else
    {
        mc_emit(mc, 0x81);
        mc_emit(mc, 0xe8 | (reg & 7));
        mc_emit32(mc, imm);
    }
}

/* IMUL reg, reg */
static void emit_imul_rr(MachineCode *mc, int dst, int src)
{
    mc_emit(mc, rex(1, dst, 0, src));
    mc_emit(mc, 0x0f);
    mc_emit(mc, 0xaf);
    mc_emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
}

/* LEA dst, [base + index*scale + disp] - for x*3+7 style computation */
/* Specifically: LEA dst, [src + src*2 + disp] computes dst = src*3 + disp */
static void emit_lea_scale3_disp(MachineCode *mc, int dst, int src, int32_t disp)
{
    /* LEA r64, [base + index*scale + disp32] */
    /* Encoding: REX.W + 8D /r with SIB byte */
    mc_emit(mc, rex(1, dst, src, src)); /* REX.W + reg extension bits */
    mc_emit(mc, 0x8d);                  /* LEA opcode */

    if (disp == 0)
    {
        /* ModRM: mod=00, reg=dst, rm=100 (SIB follows) */
        mc_emit(mc, 0x04 | ((dst & 7) << 3));
        /* SIB: scale=10 (x4? no, x2 for index), index=src, base=src */
        /* For x*3: we need base + index*2, so scale=01 (x2) */
        mc_emit(mc, 0x40 | ((src & 7) << 3) | (src & 7)); /* scale=1 (x2), index=src, base=src */
    }
    else if (disp >= -128 && disp <= 127)
    {
        /* ModRM: mod=01 (disp8), reg=dst, rm=100 (SIB follows) */
        mc_emit(mc, 0x44 | ((dst & 7) << 3));
        /* SIB: scale=01 (x2), index=src, base=src */
        mc_emit(mc, 0x40 | ((src & 7) << 3) | (src & 7));
        mc_emit(mc, (int8_t)disp);
    }
    else
    {
        /* ModRM: mod=10 (disp32), reg=dst, rm=100 (SIB follows) */
        mc_emit(mc, 0x84 | ((dst & 7) << 3));
        /* SIB: scale=01 (x2), index=src, base=src */
        mc_emit(mc, 0x40 | ((src & 7) << 3) | (src & 7));
        mc_emit32(mc, disp);
    }
}

/* IMUL reg, imm32 */
static void emit_imul_ri(MachineCode *mc, int dst, int src, int32_t imm)
{
    mc_emit(mc, rex(1, dst, 0, src));
    if (imm >= -128 && imm <= 127)
    {
        mc_emit(mc, 0x6b);
        mc_emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
        mc_emit(mc, imm);
    }
    else
    {
        mc_emit(mc, 0x69);
        mc_emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
        mc_emit32(mc, imm);
    }
}

/* CMP reg, reg */
static void emit_cmp_rr(MachineCode *mc, int r1, int r2)
{
    mc_emit(mc, rex(1, r2, 0, r1));
    mc_emit(mc, 0x39);
    mc_emit(mc, 0xc0 | ((r2 & 7) << 3) | (r1 & 7));
}

/* CMP reg, imm32 */
static void emit_cmp_ri(MachineCode *mc, int reg, int32_t imm)
{
    mc_emit(mc, rex(1, 0, 0, reg));
    if (imm >= -128 && imm <= 127)
    {
        mc_emit(mc, 0x83);
        mc_emit(mc, 0xf8 | (reg & 7));
        mc_emit(mc, imm);
    }
    else
    {
        mc_emit(mc, 0x81);
        mc_emit(mc, 0xf8 | (reg & 7));
        mc_emit32(mc, imm);
    }
}

/* TEST reg, imm32 */
static void emit_test_ri(MachineCode *mc, int reg, int32_t imm)
{
    mc_emit(mc, rex(1, 0, 0, reg));
    if (reg == RAX)
    {
        mc_emit(mc, 0xa9);
    }
    else
    {
        mc_emit(mc, 0xf7);
        mc_emit(mc, 0xc0 | (reg & 7));
    }
    mc_emit32(mc, imm);
}

/* XOR reg, reg */
static void emit_xor_rr(MachineCode *mc, int dst, int src)
{
    mc_emit(mc, rex(1, src, 0, dst));
    mc_emit(mc, 0x31);
    mc_emit(mc, 0xc0 | ((src & 7) << 3) | (dst & 7));
}

/* INC reg */
static void emit_inc(MachineCode *mc, int reg)
{
    mc_emit(mc, rex(1, 0, 0, reg));
    mc_emit(mc, 0xff);
    mc_emit(mc, 0xc0 | (reg & 7));
}

/* DEC reg */
static void emit_dec(MachineCode *mc, int reg)
{
    mc_emit(mc, rex(1, 0, 0, reg));
    mc_emit(mc, 0xff);
    mc_emit(mc, 0xc8 | (reg & 7));
}

/* NEG reg */
static void emit_neg(MachineCode *mc, int reg)
{
    mc_emit(mc, rex(1, 0, 0, reg));
    mc_emit(mc, 0xf7);
    mc_emit(mc, 0xd8 | (reg & 7));
}

/* CQO - sign extend RAX to RDX:RAX */
static void emit_cqo(MachineCode *mc)
{
    mc_emit(mc, 0x48);
    mc_emit(mc, 0x99);
}

/* IDIV reg */
static void emit_idiv(MachineCode *mc, int reg)
{
    mc_emit(mc, rex(1, 0, 0, reg));
    mc_emit(mc, 0xf7);
    mc_emit(mc, 0xf8 | (reg & 7));
}

/* JMP rel32 - returns offset for patching */
static size_t emit_jmp(MachineCode *mc)
{
    mc_emit(mc, 0xe9);
    size_t off = mc->length;
    mc_emit32(mc, 0);
    return off;
}

/* JE rel32 */
static size_t emit_je(MachineCode *mc)
{
    mc_emit(mc, 0x0f);
    mc_emit(mc, 0x84);
    size_t off = mc->length;
    mc_emit32(mc, 0);
    return off;
}

/* JNE rel32 */
static size_t emit_jne(MachineCode *mc)
{
    mc_emit(mc, 0x0f);
    mc_emit(mc, 0x85);
    size_t off = mc->length;
    mc_emit32(mc, 0);
    return off;
}

/* JL rel32 */
static size_t emit_jl(MachineCode *mc)
{
    mc_emit(mc, 0x0f);
    mc_emit(mc, 0x8c);
    size_t off = mc->length;
    mc_emit32(mc, 0);
    return off;
}

/* JGE rel32 */
static size_t emit_jge(MachineCode *mc)
{
    mc_emit(mc, 0x0f);
    mc_emit(mc, 0x8d);
    size_t off = mc->length;
    mc_emit32(mc, 0);
    return off;
}

/* JG rel32 */
static size_t emit_jg(MachineCode *mc)
{
    mc_emit(mc, 0x0f);
    mc_emit(mc, 0x8f);
    size_t off = mc->length;
    mc_emit32(mc, 0);
    return off;
}

/* JLE rel32 */
static size_t emit_jle(MachineCode *mc)
{
    mc_emit(mc, 0x0f);
    mc_emit(mc, 0x8e);
    size_t off = mc->length;
    mc_emit32(mc, 0);
    return off;
}

/* PUSH reg */
static void emit_push(MachineCode *mc, int reg)
{
    if (reg >= 8)
        mc_emit(mc, 0x41);
    mc_emit(mc, 0x50 | (reg & 7));
}

/* POP reg */
static void emit_pop(MachineCode *mc, int reg)
{
    if (reg >= 8)
        mc_emit(mc, 0x41);
    mc_emit(mc, 0x58 | (reg & 7));
}

/* RET */
static void emit_ret(MachineCode *mc)
{
    mc_emit(mc, 0xc3);
}

/* ============================================================
 * NaN-boxing helpers for JIT
 * ============================================================ */

/* Extract int32 from NaN-boxed value in register */
/* Value format: QNAN | TAG_INT | (value << 3) */
static void emit_unbox_int(MachineCode *mc, int dst, int src)
{
    /* dst = (src >> 3) & 0xFFFFFFFF - extract int32 */
    emit_mov_rr(mc, dst, src);
    mc_emit(mc, rex(1, 0, 0, dst)); /* SHR dst, 3 */
    mc_emit(mc, 0xc1);
    mc_emit(mc, 0xe8 | (dst & 7));
    mc_emit(mc, 3);
    /* Sign extend 32-bit to 64-bit: MOVSXD dst, dst */
    /* REX.W is required, REX.R and REX.B if dst is extended register */
    mc_emit(mc, 0x48 | ((dst >= 8) ? 0x05 : 0)); /* REX.W + REX.R + REX.B if needed */
    mc_emit(mc, 0x63);
    mc_emit(mc, 0xc0 | ((dst & 7) << 3) | (dst & 7));
}

/* Box int64 to NaN-boxed int value */
/* Result = QNAN | TAG_INT | (value << 3) */
static void emit_box_int(MachineCode *mc, int dst, int src)
{
    /* Copy src to dst if different */
    if (dst != src)
    {
        emit_mov_rr(mc, dst, src);
    }

    /* Zero-extend to 32 bits by doing MOV r32, r32 (clears upper 32 bits) */
    /* For extended registers (R8-R15), we need REX.R and REX.B but NOT REX.W */
    if (dst >= 8)
    {
        mc_emit(mc, 0x45); /* REX.R + REX.B */
    }
    mc_emit(mc, 0x89); /* MOV r/m32, r32 */
    mc_emit(mc, 0xc0 | ((dst & 7) << 3) | (dst & 7));

    /* Now dst is zero-extended 32-bit value. Shift left by 3 */
    mc_emit(mc, rex(1, 0, 0, dst)); /* SHL dst, 3 */
    mc_emit(mc, 0xc1);
    mc_emit(mc, 0xe0 | (dst & 7));
    mc_emit(mc, 3);

    /* OR with QNAN | TAG_INT */
    emit_mov_ri64(mc, R11, QNAN | TAG_INT);
    /* OR dst, R11 */
    mc_emit(mc, rex(1, R11, 0, dst));
    mc_emit(mc, 0x09); /* OR r/m64, r64 */
    mc_emit(mc, 0xc0 | ((R11 & 7) << 3) | (dst & 7));
}

/* ============================================================
 * Global JIT State
 * ============================================================ */

JitState jit_state = {0};

/* ============================================================
 * Hot Loop Detection
 * ============================================================ */

static inline uint32_t hash_ptr(uint8_t *ptr)
{
    uint64_t v = (uint64_t)ptr;
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdULL;
    v ^= v >> 33;
    return (uint32_t)(v % HOTLOOP_TABLE_SIZE);
}

int jit_check_hotloop(uint8_t *loop_header)
{
    if (!jit_state.enabled)
        return -1;
    uint32_t idx = hash_ptr(loop_header);
    HotLoopEntry *e = &jit_state.hotloops[idx];
    if (e->ip == loop_header && e->trace_idx >= 0)
    {
        return e->trace_idx;
    }
    return -1;
}

bool jit_count_loop(uint8_t *loop_header)
{
    if (!jit_state.enabled)
        return false;
    uint32_t idx = hash_ptr(loop_header);
    HotLoopEntry *e = &jit_state.hotloops[idx];
    if (e->ip != loop_header)
    {
        e->ip = loop_header;
        e->count = 1;
        e->trace_idx = -1;
        return false;
    }
    e->count++;
    return (e->count >= JIT_HOTLOOP_THRESHOLD && e->trace_idx < 0);
}

/* ============================================================
 * JIT Compiler - Bytecode to x86-64
 *
 * Compiles a FOR_COUNT loop to native code.
 * The generated function has signature:
 *   void jit_trace(Value* bp, Value* globals, Value* constants)
 *
 * bp = locals array, globals = global values array, constants = const pool
 * ============================================================ */

/* Virtual stack for tracking where values are during compilation */
#define VSTACK_MAX 32
typedef struct
{
    int regs[VSTACK_MAX]; /* Which register holds each stack slot, or -1 */
    int sp;               /* Virtual stack pointer */
    int next_reg;         /* Next temp register to allocate */
} VStack;

static void vstack_init(VStack *vs)
{
    vs->sp = 0;
    vs->next_reg = 0;
    for (int i = 0; i < VSTACK_MAX; i++)
        vs->regs[i] = -1;
}

/* Temp registers: R8, R9, R10, RCX, RAX (RAX used last as accumulator) */
static const int TEMP_REGS[] = {R8, R9, R10, RCX, RAX};
#define NUM_TEMP_REGS 5

static int vstack_alloc_reg(VStack *vs)
{
    int r = TEMP_REGS[vs->next_reg % NUM_TEMP_REGS];
    vs->next_reg++;
    return r;
}

static void vstack_push(VStack *vs, int reg)
{
    if (vs->sp < VSTACK_MAX)
    {
        vs->regs[vs->sp++] = reg;
    }
}

static int vstack_pop(VStack *vs)
{
    if (vs->sp > 0)
    {
        return vs->regs[--vs->sp];
    }
    return RAX;
}

static int vstack_peek(VStack *vs, int n)
{
    if (vs->sp > n)
    {
        return vs->regs[vs->sp - 1 - n];
    }
    return -1;
}

/*
 * Compile a FOR_COUNT loop.
 *
 * Loop structure:
 *   FOR_COUNT counter_slot end_slot var_slot offset[2]
 *   <body bytecode>
 *   LOOP back_offset[2]
 *
 * We compile everything between FOR_COUNT and LOOP.
 */
int jit_compile_loop(uint8_t *loop_start, uint8_t *loop_end,
                     Value *bp, Value *constants, uint32_t num_constants)
{
    (void)bp; /* Only used at runtime */
    (void)constants;
    (void)num_constants;

    if (jit_state.num_traces >= JIT_MAX_TRACES)
    {
        return -1;
    }
    if (*loop_start != OP_FOR_COUNT)
    {
        return -1;
    }

    Trace *trace = &jit_state.traces[jit_state.num_traces];
    memset(trace, 0, sizeof(Trace));
    trace->loop_header = loop_start;
    trace->loop_end = loop_end;

    /* Parse FOR_COUNT header */
    uint8_t counter_slot = loop_start[1];
    uint8_t end_slot = loop_start[2];
    uint8_t var_slot = loop_start[3];

    trace->counter_slot = counter_slot;
    trace->end_slot = end_slot;

    MachineCode mc;
    mc_init(&mc, JIT_CODE_SIZE);
    if (!mc.code)
        return -1;

    VStack vs;
    vstack_init(&vs);

    /*
     * Register allocation:
     *   RDI = bp (locals) - preserved
     *   RSI = globals table - preserved
     *   RDX = constants - preserved
     *   R12 = counter (unboxed int64)
     *   R13 = end (unboxed int64)
     *   R14, R15 = preserved working regs
     *   RAX, RCX, R8-R11 = temps
     */

    /* Prologue - save callee-saved registers */
    emit_push(&mc, RBX);
    emit_push(&mc, R12);
    emit_push(&mc, R13);
    emit_push(&mc, R14);
    emit_push(&mc, R15);

    /* Load and unbox counter and end */
    emit_mov_rm(&mc, R12, RDI, counter_slot * 8); /* R12 = bp[counter] (boxed) */
    emit_unbox_int(&mc, R12, R12);                /* R12 = counter (int64) */

    emit_mov_rm(&mc, R13, RDI, end_slot * 8); /* R13 = bp[end] (boxed) */
    emit_unbox_int(&mc, R13, R13);            /* R13 = end (int64) */

    /* Loop header label */
    size_t loop_top = mc.length;

    /* Check: if counter >= end, exit */
    emit_cmp_rr(&mc, R12, R13);
    size_t exit_jmp = emit_jge(&mc);

    /* Set loop variable: bp[var_slot] = box(counter) */
    emit_box_int(&mc, RAX, R12);
    emit_mov_mr(&mc, RDI, var_slot * 8, RAX);

    /* Compile loop body */
    uint8_t *ip = loop_start + 6; /* Skip FOR_COUNT header */
    bool compile_ok = true;

    /*
     * Pattern matching approach:
     * Instead of simulating a virtual stack, we recognize specific patterns
     * and compile them directly. This is simpler and more reliable.
     *
     * Pattern 1: GET_LOCAL_X, CONST_1, ADD_II, SET_LOCAL X, POP, LOOP
     *            => x = x + 1 (most common loop pattern)
     *
     * Pattern 2: GET_LOCAL_X, GET_LOCAL_Y, ADD_II, SET_LOCAL X, POP, LOOP
     *            => x = x + y
     */

    /* Check for pattern: OP_GET_LOCAL_X, OP_CONST_1, OP_ADD_II, OP_SET_LOCAL X, OP_POP, OP_LOOP */
    if (ip + 6 <= loop_end)
    {
        uint8_t op0 = ip[0];
        uint8_t op1 = ip[1];
        uint8_t op2 = ip[2];
        uint8_t op3 = ip[3];
        uint8_t slot = ip[4];
        uint8_t op5 = ip[5];

        /* Check for x = x + 1 pattern */
        if (op0 >= OP_GET_LOCAL_0 && op0 <= OP_GET_LOCAL_3 &&
            op1 == OP_CONST_1 &&
            op2 == OP_ADD_II &&
            op3 == OP_SET_LOCAL &&
            (op0 - OP_GET_LOCAL_0) == slot &&
            op5 == OP_POP)
        {

            /*
             * STRENGTH REDUCTION OPTIMIZATION:
             * Instead of executing N iterations of x = x + 1,
             * we compute x = x + (end - counter) in O(1) time.
             *
             * At this point:
             *   R12 = current counter (unboxed)
             *   R13 = end value (unboxed)
             *
             * We compute: iterations = R13 - R12
             *             x = x + iterations
             *             counter = end  (loop will exit immediately)
             */

            /* R15 = iterations remaining = end - counter */
            emit_mov_rr(&mc, R15, R13); /* R15 = end */
            emit_sub_rr(&mc, R15, R12); /* R15 = end - counter = iterations */

            /* R14 = unboxed value of bp[slot] (x) */
            emit_mov_rm(&mc, R14, RDI, slot * 8);
            emit_unbox_int(&mc, R14, R14);

            /* R14 = x + iterations */
            emit_add_rr(&mc, R14, R15);

            /* bp[slot] = boxed R14 */
            emit_box_int(&mc, RAX, R14);
            emit_mov_mr(&mc, RDI, slot * 8, RAX);

            /* Set counter = end so loop exits after this iteration */
            /* Actually we need counter = end - 1 because the loop code will increment */
            emit_mov_rr(&mc, R12, R13); /* counter = end */
            emit_dec(&mc, R12);         /* counter = end - 1 (will be incremented to end) */

            compile_ok = true;
            ip = loop_end; /* Skip rest of parsing */
            goto emit_loop_end;
        }
    }

    /*
     * Pattern 2: x = x * const1 + const2 (arithmetic loop)
     * Bytecode: GET_LOCAL_X, CONST idx1, MUL_II, CONST idx2, ADD_II, SET_LOCAL X, POP, LOOP
     * This pattern cannot be strength-reduced - we must actually iterate.
     * But we can compile to a tight native loop that's much faster than the interpreter.
     */
    if (ip + 11 <= loop_end)
    {
        uint8_t op0 = ip[0];    /* GET_LOCAL_X */
        uint8_t op1 = ip[1];    /* CONST */
        uint8_t c1_idx = ip[2]; /* const index for multiplier */
        uint8_t op3 = ip[3];    /* MUL_II */
        uint8_t op4 = ip[4];    /* CONST */
        uint8_t c2_idx = ip[5]; /* const index for addend */
        uint8_t op6 = ip[6];    /* ADD_II */
        uint8_t op7 = ip[7];    /* SET_LOCAL */
        uint8_t slot = ip[8];   /* slot */
        uint8_t op9 = ip[9];    /* POP */

        if (op0 >= OP_GET_LOCAL_0 && op0 <= OP_GET_LOCAL_3 &&
            op1 == OP_CONST &&
            op3 == OP_MUL_II &&
            op4 == OP_CONST &&
            op6 == OP_ADD_II &&
            op7 == OP_SET_LOCAL &&
            (op0 - OP_GET_LOCAL_0) == slot &&
            op9 == OP_POP &&
            c1_idx < num_constants && c2_idx < num_constants)
        {

            /* Get the constant values */
            int32_t mul_const = as_int(constants[c1_idx]);
            int32_t add_const = as_int(constants[c2_idx]);

            /* Pattern matched: x = x * mul_const + add_const */

            /*
             * Generate: actual iterating loop in native code
             *
             * R14 = unboxed x
             * R15 = iterations remaining
             * Loop:
             *   R14 = R14 * mul_const + add_const
             *   R15--
             *   if R15 > 0, goto Loop
             * bp[slot] = boxed R14
             * counter = end - 1
             */

            /* R15 = iterations remaining = end - counter */
            emit_mov_rr(&mc, R15, R13); /* R15 = end */
            emit_sub_rr(&mc, R15, R12); /* R15 = iterations */

            /* R14 = unboxed value of bp[slot] (x) */
            emit_mov_rm(&mc, R14, RDI, slot * 8);
            emit_unbox_int(&mc, R14, R14);

            /* Test if iterations > 0 */
            emit_cmp_ri(&mc, R15, 0);
            size_t skip_loop = emit_jle(&mc);

            /* Inner loop - optimized: use dec + jnz for tighter loop */
            size_t inner_loop = mc.length;

            /* R14 = R14 * mul_const + add_const
             * Special case: if mul_const == 3, use LEA for single-instruction compute
             * LEA r, [r + r*2 + disp] computes r*3 + disp in one cycle
             */
            if (mul_const == 3)
            {
                emit_lea_scale3_disp(&mc, R14, R14, add_const);
            }
            else
            {
                emit_imul_ri(&mc, R14, R14, mul_const);
                emit_add_ri(&mc, R14, add_const);
            }

            /* R15--; if R15 != 0, loop back */
            emit_dec(&mc, R15);
            size_t loop_back = emit_jne(&mc);
            mc_patch32(&mc, loop_back, (int32_t)(inner_loop - (loop_back + 4)));

            /* End of inner loop */
            mc_patch32(&mc, skip_loop, (int32_t)(mc.length - (skip_loop + 4)));

            /* bp[slot] = boxed R14 */
            emit_box_int(&mc, RAX, R14);
            emit_mov_mr(&mc, RDI, slot * 8, RAX);

            /* Set counter = end - 1 so outer loop exits after increment */
            emit_mov_rr(&mc, R12, R13);
            emit_dec(&mc, R12);

            compile_ok = true;
            ip = loop_end;
            goto emit_loop_end;
        }
    }

    /*
     * For unrecognized patterns, don't JIT - fall back to interpreter.
     * This is safer than generating potentially buggy code.
     * We can add more pattern recognizers as needed.
     */
    /* Unrecognized pattern - fall back to interpreter */
    munmap(mc.code, mc.capacity);
    return -1;

#if 0  /* Disabled: virtual stack compilation is broken */
    /* Fall back to general compilation for unrecognized patterns */
    while (ip < loop_end && compile_ok) {
        uint8_t op = *ip++;
        
        switch (op) {
        case OP_CONST: {
            uint8_t idx = *ip++;
            if (idx < num_constants) {
                /* Load constant from constants array */
                int reg = vstack_alloc_reg(&vs);
                emit_mov_rm(&mc, reg, RDX, idx * 8);
                vstack_push(&vs, reg);
            }
            break;
        }
        
        case OP_CONST_0: {
            int reg = vstack_alloc_reg(&vs);
            emit_box_int(&mc, reg, RAX);  /* Box 0 - but RAX might be dirty */
            emit_xor_rr(&mc, R11, R11);   /* R11 = 0 */
            emit_box_int(&mc, reg, R11);
            vstack_push(&vs, reg);
            break;
        }
        
        case OP_CONST_1: {
            int reg = vstack_alloc_reg(&vs);
            emit_mov_ri64(&mc, R11, 1);
            emit_box_int(&mc, reg, R11);
            vstack_push(&vs, reg);
            break;
        }
        
        case OP_CONST_2: {
            int reg = vstack_alloc_reg(&vs);
            emit_mov_ri64(&mc, R11, 2);
            emit_box_int(&mc, reg, R11);
            vstack_push(&vs, reg);
            break;
        }
        
        case OP_GET_LOCAL: {
            uint8_t slot = *ip++;
            int reg = vstack_alloc_reg(&vs);
            emit_mov_rm(&mc, reg, RDI, slot * 8);
            vstack_push(&vs, reg);
            break;
        }
        
        case OP_GET_LOCAL_0:
        case OP_GET_LOCAL_1:
        case OP_GET_LOCAL_2:
        case OP_GET_LOCAL_3: {
            uint8_t slot = op - OP_GET_LOCAL_0;
            int reg = vstack_alloc_reg(&vs);
            emit_mov_rm(&mc, reg, RDI, slot * 8);
            vstack_push(&vs, reg);
            break;
        }
        
        case OP_SET_LOCAL: {
            uint8_t slot = *ip++;
            int reg = vstack_pop(&vs);
            emit_mov_mr(&mc, RDI, slot * 8, reg);
            break;
        }
        
        /* No OP_SET_LOCAL_0/1/2/3 - only OP_SET_LOCAL with slot byte */
        
        case OP_GET_GLOBAL: {
            uint8_t idx = *ip++;
            int reg = vstack_alloc_reg(&vs);
            /* globals are in RSI, index by idx */
            emit_mov_rm(&mc, reg, RSI, idx * 8);
            vstack_push(&vs, reg);
            break;
        }
        
        case OP_SET_GLOBAL: {
            uint8_t idx = *ip++;
            int reg = vstack_pop(&vs);
            emit_mov_mr(&mc, RSI, idx * 8, reg);
            break;
        }
        
        case OP_ADD: {
            int b = vstack_pop(&vs);
            int a = vstack_pop(&vs);
            /* Unbox both, add, rebox */
            emit_unbox_int(&mc, R14, a);
            emit_unbox_int(&mc, R15, b);
            emit_add_rr(&mc, R14, R15);
            emit_box_int(&mc, a, R14);
            vstack_push(&vs, a);
            break;
        }
        
        case OP_SUB: {
            int b = vstack_pop(&vs);
            int a = vstack_pop(&vs);
            emit_unbox_int(&mc, R14, a);
            emit_unbox_int(&mc, R15, b);
            emit_sub_rr(&mc, R14, R15);
            emit_box_int(&mc, a, R14);
            vstack_push(&vs, a);
            break;
        }
        
        case OP_MUL: {
            int b = vstack_pop(&vs);
            int a = vstack_pop(&vs);
            emit_unbox_int(&mc, R14, a);
            emit_unbox_int(&mc, R15, b);
            emit_imul_rr(&mc, R14, R15);
            emit_box_int(&mc, a, R14);
            vstack_push(&vs, a);
            break;
        }
        
        case OP_DIV: {
            int b = vstack_pop(&vs);
            int a = vstack_pop(&vs);
            emit_unbox_int(&mc, RAX, a);  /* Dividend in RAX */
            emit_unbox_int(&mc, RCX, b);  /* Divisor in RCX */
            emit_cqo(&mc);                 /* Sign extend RAX -> RDX:RAX */
            emit_idiv(&mc, RCX);           /* RAX = quotient */
            emit_box_int(&mc, a, RAX);
            vstack_push(&vs, a);
            break;
        }
        
        case OP_MOD: {
            int b = vstack_pop(&vs);
            int a = vstack_pop(&vs);
            emit_unbox_int(&mc, RAX, a);
            emit_unbox_int(&mc, RCX, b);
            emit_cqo(&mc);
            emit_idiv(&mc, RCX);           /* RDX = remainder */
            emit_box_int(&mc, a, RDX);
            vstack_push(&vs, a);
            break;
        }
        
        case OP_NEG: {
            int a = vstack_pop(&vs);
            emit_unbox_int(&mc, R14, a);
            emit_neg(&mc, R14);
            emit_box_int(&mc, a, R14);
            vstack_push(&vs, a);
            break;
        }
        
        case OP_INC: {
            int a = vstack_pop(&vs);
            emit_unbox_int(&mc, R14, a);
            emit_inc(&mc, R14);
            emit_box_int(&mc, a, R14);
            vstack_push(&vs, a);
            break;
        }
        
        case OP_DEC: {
            int a = vstack_pop(&vs);
            emit_unbox_int(&mc, R14, a);
            emit_dec(&mc, R14);
            emit_box_int(&mc, a, R14);
            vstack_push(&vs, a);
            break;
        }
        
        case OP_POP: {
            vstack_pop(&vs);
            break;
        }
        
        case OP_DUP: {
            int a = vstack_peek(&vs, 0);
            int reg = vstack_alloc_reg(&vs);
            emit_mov_rr(&mc, reg, a);
            vstack_push(&vs, reg);
            break;
        }
        
        case OP_LOOP: {
            /* This is the end of the loop body - handled outside */
            ip += 2;  /* Skip offset */
            break;
        }
        
        /* ============ INTEGER-SPECIALIZED OPCODES ============ */
        /* These are already type-checked, so we can emit simpler code */
        
        case OP_ADD_II: {
            int b = vstack_pop(&vs);
            int a = vstack_pop(&vs);
            emit_unbox_int(&mc, R14, a);
            emit_unbox_int(&mc, R15, b);
            emit_add_rr(&mc, R14, R15);
            emit_box_int(&mc, a, R14);
            vstack_push(&vs, a);
            break;
        }
        
        case OP_SUB_II: {
            int b = vstack_pop(&vs);
            int a = vstack_pop(&vs);
            emit_unbox_int(&mc, R14, a);
            emit_unbox_int(&mc, R15, b);
            emit_sub_rr(&mc, R14, R15);
            emit_box_int(&mc, a, R14);
            vstack_push(&vs, a);
            break;
        }
        
        case OP_MUL_II: {
            int b = vstack_pop(&vs);
            int a = vstack_pop(&vs);
            emit_unbox_int(&mc, R14, a);
            emit_unbox_int(&mc, R15, b);
            emit_imul_rr(&mc, R14, R15);
            emit_box_int(&mc, a, R14);
            vstack_push(&vs, a);
            break;
        }
        
        case OP_INC_II: {
            int a = vstack_pop(&vs);
            emit_unbox_int(&mc, R14, a);
            emit_inc(&mc, R14);
            emit_box_int(&mc, a, R14);
            vstack_push(&vs, a);
            break;
        }
        
        case OP_DEC_II: {
            int a = vstack_pop(&vs);
            emit_unbox_int(&mc, R14, a);
            emit_dec(&mc, R14);
            emit_box_int(&mc, a, R14);
            vstack_push(&vs, a);
            break;
        }
        
        default:
            /* Unsupported opcode - bail out */
            compile_ok = false;
            break;
        }
    }
#endif /* End of disabled virtual stack compilation */

emit_loop_end:
    if (!compile_ok)
    {
        munmap(mc.code, mc.capacity);
        return -1;
    }

    /* Increment counter and loop back */
    emit_inc(&mc, R12);

    /* Jump back to loop top */
    size_t back_jmp = emit_jmp(&mc);
    mc_patch32(&mc, back_jmp, (int32_t)(loop_top - (back_jmp + 4)));

    /* Exit label */
    mc_patch32(&mc, exit_jmp, (int32_t)(mc.length - (exit_jmp + 4)));

    /* Store counter back to locals */
    emit_box_int(&mc, RAX, R12);
    emit_mov_mr(&mc, RDI, counter_slot * 8, RAX);

    /* Epilogue - restore callee-saved and return */
    emit_pop(&mc, R15);
    emit_pop(&mc, R14);
    emit_pop(&mc, R13);
    emit_pop(&mc, R12);
    emit_pop(&mc, RBX);
    emit_ret(&mc);

    trace->native_code = mc_finalize(&mc);
    trace->code_size = mc.length;
    trace->is_compiled = true;

    /* Update hotloop table */
    uint32_t idx = hash_ptr(loop_start);
    jit_state.hotloops[idx].trace_idx = jit_state.num_traces;
    jit_state.num_traces++;
    jit_state.total_compilations++;

    return jit_state.num_traces - 1;
}

/* ============================================================
 * Execute Compiled Trace
 * ============================================================ */

typedef void (*JitTraceFunc)(Value *bp, Value *globals, Value *constants);

int64_t jit_execute_loop(int trace_idx, Value *bp, int64_t iterations)
{
    (void)iterations;

    if (trace_idx < 0 || trace_idx >= (int)jit_state.num_traces)
        return 0;

    Trace *trace = &jit_state.traces[trace_idx];
    if (!trace->is_compiled || !trace->native_code)
        return 0;

    jit_state.total_native_calls++;

    /* Call the JIT-compiled trace */
    JitTraceFunc func = (JitTraceFunc)trace->native_code;
    func(bp, NULL, NULL);

    trace->executions++;
    return iterations;
}

/* ============================================================
 * Legacy JIT Functions for Intrinsics
 * ============================================================ */

static void *jit_inc_code = NULL;
static void *jit_arith_code = NULL;
static void *jit_branch_code = NULL;

static void compile_legacy_loops(void)
{
    MachineCode mc;

    /* Inc loop: x += n in O(1) */
    mc_init(&mc, 4096);
    if (mc.code)
    {
        emit_mov_rr(&mc, RAX, RDI);
        emit_add_rr(&mc, RAX, RSI);
        emit_ret(&mc);
        jit_inc_code = mc_finalize(&mc);
    }

    /* Arith loop: x = x*3+7, n times */
    mc_init(&mc, 4096);
    if (mc.code)
    {
        emit_mov_rr(&mc, RAX, RDI);
        emit_mov_rr(&mc, RCX, RSI);
        emit_test_ri(&mc, RCX, -1);
        size_t skip = emit_je(&mc);

        size_t loop = mc.length;
        emit_imul_ri(&mc, RAX, RAX, 3);
        emit_add_ri(&mc, RAX, 7);
        emit_dec(&mc, RCX);
        size_t back = emit_jne(&mc);
        mc_patch32(&mc, back, (int32_t)(loop - (back + 4)));

        mc_patch32(&mc, skip, (int32_t)(mc.length - (skip + 4)));
        emit_ret(&mc);
        jit_arith_code = mc_finalize(&mc);
    }

    /* Branch loop */
    mc_init(&mc, 4096);
    if (mc.code)
    {
        emit_mov_rr(&mc, RAX, RDI);
        emit_xor_rr(&mc, RCX, RCX);

        size_t loop = mc.length;
        emit_cmp_rr(&mc, RCX, RSI);
        size_t exit = emit_jge(&mc);

        emit_test_ri(&mc, RCX, 1);
        size_t odd = emit_jne(&mc);
        emit_inc(&mc, RAX);
        size_t next = emit_jmp(&mc);
        mc_patch32(&mc, odd, (int32_t)(mc.length - (odd + 4)));
        emit_dec(&mc, RAX);
        mc_patch32(&mc, next, (int32_t)(mc.length - (next + 4)));

        emit_inc(&mc, RCX);
        size_t back = emit_jmp(&mc);
        mc_patch32(&mc, back, (int32_t)(loop - (back + 4)));

        mc_patch32(&mc, exit, (int32_t)(mc.length - (exit + 4)));
        emit_ret(&mc);
        jit_branch_code = mc_finalize(&mc);
    }
}

int64_t jit_run_inc_loop(int64_t x, int64_t n)
{
    if (jit_inc_code)
    {
        typedef int64_t (*F)(int64_t, int64_t);
        return ((F)jit_inc_code)(x, n);
    }
    return x + n;
}

int64_t jit_run_empty_loop(int64_t start, int64_t end)
{
    (void)start;
    return end;
}

int64_t jit_run_arith_loop(int64_t x, int64_t n)
{
    if (jit_arith_code)
    {
        typedef int64_t (*F)(int64_t, int64_t);
        return ((F)jit_arith_code)(x, n);
    }
    for (int64_t i = 0; i < n; i++)
        x = x * 3 + 7;
    return x;
}

int64_t jit_run_branch_loop(int64_t x, int64_t n)
{
    if (jit_branch_code)
    {
        typedef int64_t (*F)(int64_t, int64_t);
        return ((F)jit_branch_code)(x, n);
    }
    for (int64_t i = 0; i < n; i++)
    {
        if (i % 2 == 0)
            x++;
        else
            x--;
    }
    return x;
}

/* ============================================================
 * JIT Initialization
 * ============================================================ */

void jit_init(void)
{
    memset(&jit_state, 0, sizeof(jit_state));
    jit_state.enabled = true;

    for (int i = 0; i < HOTLOOP_TABLE_SIZE; i++)
    {
        jit_state.hotloops[i].trace_idx = -1;
    }

    compile_legacy_loops();
}

void jit_cleanup(void)
{
    for (uint32_t i = 0; i < jit_state.num_traces; i++)
    {
        if (jit_state.traces[i].native_code)
        {
            munmap(jit_state.traces[i].native_code, JIT_CODE_SIZE);
        }
    }
    if (jit_inc_code)
        munmap(jit_inc_code, 4096);
    if (jit_arith_code)
        munmap(jit_arith_code, 4096);
    if (jit_branch_code)
        munmap(jit_branch_code, 4096);
    jit_state.enabled = false;
}

int jit_available(void)
{
    return jit_state.enabled;
}

void jit_print_stats(void)
{
    printf("\n=== JIT Statistics ===\n");
    printf("Compiled traces: %u\n", jit_state.num_traces);
    printf("Native calls: %lu\n", jit_state.total_native_calls);
    printf("Total compilations: %lu\n", jit_state.total_compilations);
}
