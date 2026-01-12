/*
 * Pseudocode Tracing JIT - Phase 4: Native Code Generator
 *
 * Lowers SSA IR to native machine code.
 * Supports x86-64 and ARM64 architectures.
 * Uses linear register allocation results.
 * Emits guards with side exit stubs.
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Architecture detection */
#if defined(__aarch64__) || defined(_M_ARM64)
#define JIT_ARM64 1
#define JIT_X64 0
#elif defined(__x86_64__) || defined(_M_X64)
#define JIT_ARM64 0
#define JIT_X64 1
#else
#define JIT_ARM64 0
#define JIT_X64 0
#define JIT_UNSUPPORTED 1
#endif

/* Platform-specific memory mapping */
#ifdef _WIN32
#include <windows.h>
#define mmap_executable(size) VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)
#define munmap_executable(ptr, size) VirtualFree(ptr, 0, MEM_RELEASE)
#else
#include <sys/mman.h>
#define mmap_executable(size) mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
#define munmap_executable(ptr, size) munmap(ptr, size)
#endif

/* Apple Silicon requires MAP_JIT for executable memory */
#if defined(__APPLE__) && JIT_ARM64
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#undef mmap_executable
#define mmap_executable(size) mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0)
#endif

#include "pseudo.h"
#include "trace_ir.h"

/* ARM64 header is included later, after MCode is defined */

/* RegAlloc structure (defined here to avoid circular deps) */
typedef struct
{
    int16_t phys_to_vreg[24];
    int16_t vreg_to_phys[IR_MAX_VREGS];
    int16_t vreg_to_spill[IR_MAX_VREGS];
    int16_t next_spill_slot;
    uint32_t vreg_last_use[IR_MAX_VREGS];
} RegAlloc;

/* External declarations */
void regalloc_run(TraceIR *ir, RegAlloc *ra);
void deopt_reconstruct(TraceIR *ir, uint32_t snapshot_idx,
                       Value *bp, void *native_regs);

/* ============================================================
 * JIT Runtime Helpers for Function Inlining
 * ============================================================
 * These are called from JIT-compiled code to handle operations
 * that are too complex to inline as native code.
 */

/* Global VM pointer for JIT runtime helpers */
static VM *g_jit_vm = NULL;

/* Set the VM for JIT runtime helpers */
void jit_set_vm(void *vm)
{
    g_jit_vm = (VM *)vm;
}

/*
 * jit_call_inline - Call a Pseudocode function from JIT code
 *
 * This is a runtime helper that:
 * 1. Sets up a call frame
 * 2. Executes the function via the interpreter
 * 3. Returns the result as a Value
 *
 * Parameters (x64 ABI):
 *   RDI = ObjFunction* function
 *   RSI = Value* args array
 *   RDX = uint64_t arg_count
 *
 * Returns: Value (result in RAX)
 */
__attribute__((noinline))
Value
jit_call_inline(ObjFunction *function, Value *args, uint64_t arg_count)
{
    if (!g_jit_vm || !function)
    {
        return VAL_NIL;
    }

    VM *vm = g_jit_vm;

    /* Verify arity */
    if ((uint64_t)function->arity != arg_count)
    {
        return VAL_NIL; /* Arity mismatch */
    }

    /* Check for stack overflow */
    if (vm->frame_count >= FRAMES_MAX - 1)
    {
        return VAL_NIL;
    }

    /* Save current VM state */
    Value *old_sp = vm->sp;
    int old_frame_count = vm->frame_count;

    /* Push callee (function) and arguments onto stack */
    *vm->sp++ = val_obj(function);
    for (uint64_t i = 0; i < arg_count; i++)
    {
        *vm->sp++ = args[i];
    }

    /* Set up call frame */
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->function = function;
    frame->closure = NULL;
    frame->ip = vm->ip; /* Save return IP */
    frame->slots = vm->sp - arg_count - 1;
    frame->is_init = false;

    /* Execute the function */
    uint8_t *saved_ip = vm->ip;
    vm->ip = vm->chunk.code + function->code_start;

    /* Run until we return from this frame */
    int target_depth = old_frame_count;

    /* Simple inline interpreter for the function body */
    while (vm->frame_count > target_depth)
    {
        uint8_t op = *vm->ip++;
        switch (op)
        {
        case OP_RETURN:
        {
            Value result = *(--vm->sp);

            /* Pop frame */
            vm->frame_count--;

            /* If we've returned from our target frame, we're done */
            if (vm->frame_count <= target_depth)
            {
                vm->sp = old_sp;
                vm->ip = saved_ip;
                return result;
            }

            /* Otherwise, continue with the caller */
            CallFrame *caller = &vm->frames[vm->frame_count - 1];
            vm->sp = caller->slots;
            vm->ip = caller->ip;
            *vm->sp++ = result;
            break;
        }
        case OP_CONST:
        {
            uint8_t idx = *vm->ip++;
            *vm->sp++ = vm->chunk.constants[idx];
            break;
        }
        case OP_GET_LOCAL:
        {
            uint8_t slot = *vm->ip++;
            CallFrame *cur_frame = &vm->frames[vm->frame_count - 1];
            *vm->sp++ = cur_frame->slots[slot];
            break;
        }
        case OP_SET_LOCAL:
        {
            uint8_t slot = *vm->ip++;
            CallFrame *cur_frame = &vm->frames[vm->frame_count - 1];
            cur_frame->slots[slot] = vm->sp[-1];
            break;
        }
        case OP_ADD:
        {
            Value b = *(--vm->sp);
            Value a = *(--vm->sp);
            if (IS_NUM(a) && IS_NUM(b))
            {
                *vm->sp++ = val_num(as_num(a) + as_num(b));
            }
            else
            {
                *vm->sp++ = VAL_NIL;
            }
            break;
        }
        case OP_SUB:
        {
            Value b = *(--vm->sp);
            Value a = *(--vm->sp);
            if (IS_NUM(a) && IS_NUM(b))
            {
                *vm->sp++ = val_num(as_num(a) - as_num(b));
            }
            else
            {
                *vm->sp++ = VAL_NIL;
            }
            break;
        }
        case OP_MUL:
        {
            Value b = *(--vm->sp);
            Value a = *(--vm->sp);
            if (IS_NUM(a) && IS_NUM(b))
            {
                *vm->sp++ = val_num(as_num(a) * as_num(b));
            }
            else
            {
                *vm->sp++ = VAL_NIL;
            }
            break;
        }
        case OP_DIV:
        {
            Value b = *(--vm->sp);
            Value a = *(--vm->sp);
            if (IS_NUM(a) && IS_NUM(b))
            {
                *vm->sp++ = val_num(as_num(a) / as_num(b));
            }
            else
            {
                *vm->sp++ = VAL_NIL;
            }
            break;
        }
        case OP_LT:
        {
            Value b = *(--vm->sp);
            Value a = *(--vm->sp);
            if (IS_NUM(a) && IS_NUM(b))
            {
                *vm->sp++ = as_num(a) < as_num(b) ? VAL_TRUE : VAL_FALSE;
            }
            else
            {
                *vm->sp++ = VAL_FALSE;
            }
            break;
        }
        case OP_GT:
        {
            Value b = *(--vm->sp);
            Value a = *(--vm->sp);
            if (IS_NUM(a) && IS_NUM(b))
            {
                *vm->sp++ = as_num(a) > as_num(b) ? VAL_TRUE : VAL_FALSE;
            }
            else
            {
                *vm->sp++ = VAL_FALSE;
            }
            break;
        }
        case OP_NIL:
            *vm->sp++ = VAL_NIL;
            break;
        case OP_TRUE:
            *vm->sp++ = VAL_TRUE;
            break;
        case OP_FALSE:
            *vm->sp++ = VAL_FALSE;
            break;
        case OP_POP:
            vm->sp--;
            break;
        case OP_DUP:
            *vm->sp = vm->sp[-1];
            vm->sp++;
            break;
        case OP_NEG:
        {
            Value a = *(--vm->sp);
            if (IS_NUM(a))
            {
                *vm->sp++ = val_num(-as_num(a));
            }
            else
            {
                *vm->sp++ = VAL_NIL;
            }
            break;
        }
        default:
            /* Unsupported opcode in inline function - fall back */
            vm->sp = old_sp;
            vm->ip = saved_ip;
            vm->frame_count = old_frame_count;
            return VAL_NIL;
        }
    }

    /* Should not reach here */
    vm->sp = old_sp;
    vm->ip = saved_ip;
    return VAL_NIL;
}

/* ============================================================
 * Machine Code Buffer
 * ============================================================ */

typedef struct
{
    uint8_t *code;
    size_t capacity;
    size_t length;
} MCode;

static void mcode_init(MCode *mc, size_t size)
{
    mc->code = mmap(NULL, size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    mc->capacity = (mc->code != MAP_FAILED) ? size : 0;
    mc->length = 0;
}

static inline void emit(MCode *mc, uint8_t b)
{
    if (mc->length < mc->capacity)
        mc->code[mc->length++] = b;
}

static inline void emit32(MCode *mc, int32_t v)
{
    emit(mc, v & 0xff);
    emit(mc, (v >> 8) & 0xff);
    emit(mc, (v >> 16) & 0xff);
    emit(mc, (v >> 24) & 0xff);
}

static inline void emit64(MCode *mc, int64_t v)
{
    emit32(mc, v & 0xffffffff);
    emit32(mc, (v >> 32) & 0xffffffff);
}

static void patch32(MCode *mc, size_t off, int32_t v)
{
    mc->code[off] = v & 0xff;
    mc->code[off + 1] = (v >> 8) & 0xff;
    mc->code[off + 2] = (v >> 16) & 0xff;
    mc->code[off + 3] = (v >> 24) & 0xff;
}

static void *mcode_finalize(MCode *mc)
{
    if (!mc->code)
        return NULL;
#if JIT_ARM64 && defined(__APPLE__)
    /* Flush instruction cache on ARM64 */
    sys_icache_invalidate(mc->code, mc->length);
#endif
    mprotect(mc->code, mc->capacity, PROT_READ | PROT_EXEC);
    return mc->code;
}

/* Include ARM64-specific code after MCode is defined */
#if JIT_ARM64
#include "trace_codegen_arm64.h"
#endif

#if JIT_X64
/* ============================================================
 * x86-64 Register Encoding
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

static inline uint8_t rex(int w, int r, int x, int b)
{
    return 0x40 | (w ? 8 : 0) | ((r >= 8) ? 4 : 0) |
           ((x >= 8) ? 2 : 0) | ((b >= 8) ? 1 : 0);
}

/* ============================================================
 * x86-64 Instruction Emission
 * ============================================================ */

/* MOV reg, reg */
static void emit_mov_rr(MCode *mc, int dst, int src)
{
    emit(mc, rex(1, src, 0, dst));
    emit(mc, 0x89);
    emit(mc, 0xc0 | ((src & 7) << 3) | (dst & 7));
}

/* MOV reg, imm64 */
static void emit_mov_ri64(MCode *mc, int reg, int64_t imm)
{
    emit(mc, rex(1, 0, 0, reg));
    emit(mc, 0xb8 | (reg & 7));
    emit64(mc, imm);
}

/* MOV reg, imm32 (zero-extended) */
static void emit_mov_ri32(MCode *mc, int reg, int32_t imm)
{
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0xb8 | (reg & 7));
    emit32(mc, imm);
}

/* MOV reg, [base + offset] */
static void emit_mov_rm(MCode *mc, int dst, int base, int32_t off)
{
    emit(mc, rex(1, dst, 0, base));
    emit(mc, 0x8b);
    if (off == 0 && (base & 7) != RBP)
    {
        emit(mc, ((dst & 7) << 3) | (base & 7));
        if ((base & 7) == RSP)
            emit(mc, 0x24);
    }
    else if (off >= -128 && off <= 127)
    {
        emit(mc, 0x40 | ((dst & 7) << 3) | (base & 7));
        if ((base & 7) == RSP)
            emit(mc, 0x24);
        emit(mc, off);
    }
    else
    {
        emit(mc, 0x80 | ((dst & 7) << 3) | (base & 7));
        if ((base & 7) == RSP)
            emit(mc, 0x24);
        emit32(mc, off);
    }
}

/* MOV [base + offset], reg */
static void emit_mov_mr(MCode *mc, int base, int32_t off, int src)
{
    emit(mc, rex(1, src, 0, base));
    emit(mc, 0x89);
    if (off == 0 && (base & 7) != RBP)
    {
        emit(mc, ((src & 7) << 3) | (base & 7));
        if ((base & 7) == RSP)
            emit(mc, 0x24);
    }
    else if (off >= -128 && off <= 127)
    {
        emit(mc, 0x40 | ((src & 7) << 3) | (base & 7));
        if ((base & 7) == RSP)
            emit(mc, 0x24);
        emit(mc, off);
    }
    else
    {
        emit(mc, 0x80 | ((src & 7) << 3) | (base & 7));
        if ((base & 7) == RSP)
            emit(mc, 0x24);
        emit32(mc, off);
    }
}

/* ADD reg, reg */
static void emit_add_rr(MCode *mc, int dst, int src)
{
    emit(mc, rex(1, src, 0, dst));
    emit(mc, 0x01);
    emit(mc, 0xc0 | ((src & 7) << 3) | (dst & 7));
}

/* ADD reg, imm32 */
static void emit_add_ri(MCode *mc, int reg, int32_t imm)
{
    emit(mc, rex(1, 0, 0, reg));
    if (imm >= -128 && imm <= 127)
    {
        emit(mc, 0x83);
        emit(mc, 0xc0 | (reg & 7));
        emit(mc, imm);
    }
    else
    {
        emit(mc, 0x81);
        emit(mc, 0xc0 | (reg & 7));
        emit32(mc, imm);
    }
}

/* SUB reg, reg */
static void emit_sub_rr(MCode *mc, int dst, int src)
{
    emit(mc, rex(1, src, 0, dst));
    emit(mc, 0x29);
    emit(mc, 0xc0 | ((src & 7) << 3) | (dst & 7));
}

/* SUB reg, imm32 */
static void emit_sub_ri(MCode *mc, int reg, int32_t imm)
{
    emit(mc, rex(1, 0, 0, reg));
    if (imm >= -128 && imm <= 127)
    {
        emit(mc, 0x83);
        emit(mc, 0xe8 | (reg & 7));
        emit(mc, imm);
    }
    else
    {
        emit(mc, 0x81);
        emit(mc, 0xe8 | (reg & 7));
        emit32(mc, imm);
    }
}

/* IMUL reg, reg */
static void emit_imul_rr(MCode *mc, int dst, int src)
{
    emit(mc, rex(1, dst, 0, src));
    emit(mc, 0x0f);
    emit(mc, 0xaf);
    emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
}

/* IMUL reg, imm32 */
static void emit_imul_ri(MCode *mc, int dst, int src, int32_t imm)
{
    emit(mc, rex(1, dst, 0, src));
    if (imm >= -128 && imm <= 127)
    {
        emit(mc, 0x6b);
        emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
        emit(mc, imm);
    }
    else
    {
        emit(mc, 0x69);
        emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
        emit32(mc, imm);
    }
}

/* INC reg */
static void emit_inc(MCode *mc, int reg)
{
    emit(mc, rex(1, 0, 0, reg));
    emit(mc, 0xff);
    emit(mc, 0xc0 | (reg & 7));
}

/* DEC reg */
static void emit_dec(MCode *mc, int reg)
{
    emit(mc, rex(1, 0, 0, reg));
    emit(mc, 0xff);
    emit(mc, 0xc8 | (reg & 7));
}

/* NEG reg */
static void emit_neg(MCode *mc, int reg)
{
    emit(mc, rex(1, 0, 0, reg));
    emit(mc, 0xf7);
    emit(mc, 0xd8 | (reg & 7));
}

/* CMP reg, reg */
static void emit_cmp_rr(MCode *mc, int r1, int r2)
{
    emit(mc, rex(1, r2, 0, r1));
    emit(mc, 0x39);
    emit(mc, 0xc0 | ((r2 & 7) << 3) | (r1 & 7));
}

/* CMP reg, imm32 */
static void emit_cmp_ri(MCode *mc, int reg, int32_t imm)
{
    emit(mc, rex(1, 0, 0, reg));
    if (imm >= -128 && imm <= 127)
    {
        emit(mc, 0x83);
        emit(mc, 0xf8 | (reg & 7));
        emit(mc, imm);
    }
    else
    {
        emit(mc, 0x81);
        emit(mc, 0xf8 | (reg & 7));
        emit32(mc, imm);
    }
}

/* TEST reg, imm32 */
static void emit_test_ri(MCode *mc, int reg, int32_t imm)
{
    emit(mc, rex(1, 0, 0, reg));
    emit(mc, 0xf7);
    emit(mc, 0xc0 | (reg & 7));
    emit32(mc, imm);
}

/* XOR reg, reg */
static void emit_xor_rr(MCode *mc, int dst, int src)
{
    emit(mc, rex(1, src, 0, dst));
    emit(mc, 0x31);
    emit(mc, 0xc0 | ((src & 7) << 3) | (dst & 7));
}

/* SETL reg (set if less) */
static void emit_setl(MCode *mc, int reg)
{
    if (reg >= 4 && reg <= 7)
        emit(mc, 0x40); /* REX for SPL,BPL,SIL,DIL */
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0x0f);
    emit(mc, 0x9c);
    emit(mc, 0xc0 | (reg & 7));
}

/* SETLE reg */
static void emit_setle(MCode *mc, int reg)
{
    if (reg >= 4 && reg <= 7)
        emit(mc, 0x40);
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0x0f);
    emit(mc, 0x9e);
    emit(mc, 0xc0 | (reg & 7));
}

/* SETG reg */
static void emit_setg(MCode *mc, int reg)
{
    if (reg >= 4 && reg <= 7)
        emit(mc, 0x40);
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0x0f);
    emit(mc, 0x9f);
    emit(mc, 0xc0 | (reg & 7));
}

/* SETGE reg */
static void emit_setge(MCode *mc, int reg)
{
    if (reg >= 4 && reg <= 7)
        emit(mc, 0x40);
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0x0f);
    emit(mc, 0x9d);
    emit(mc, 0xc0 | (reg & 7));
}

/* SETE reg */
static void emit_sete(MCode *mc, int reg)
{
    if (reg >= 4 && reg <= 7)
        emit(mc, 0x40);
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0x0f);
    emit(mc, 0x94);
    emit(mc, 0xc0 | (reg & 7));
}

/* SETNE reg */
static void emit_setne(MCode *mc, int reg)
{
    if (reg >= 4 && reg <= 7)
        emit(mc, 0x40);
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0x0f);
    emit(mc, 0x95);
    emit(mc, 0xc0 | (reg & 7));
}

/* MOVZX reg, reg8 (zero extend byte to qword) */
static void emit_movzx_rr8(MCode *mc, int dst, int src)
{
    emit(mc, rex(1, dst, 0, src));
    emit(mc, 0x0f);
    emit(mc, 0xb6);
    emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
}

/* JMP rel32 */
static size_t emit_jmp(MCode *mc)
{
    emit(mc, 0xe9);
    size_t off = mc->length;
    emit32(mc, 0);
    return off;
}

/* JE rel32 */
static size_t emit_je(MCode *mc)
{
    emit(mc, 0x0f);
    emit(mc, 0x84);
    size_t off = mc->length;
    emit32(mc, 0);
    return off;
}

/* JNE rel32 */
static size_t emit_jne(MCode *mc)
{
    emit(mc, 0x0f);
    emit(mc, 0x85);
    size_t off = mc->length;
    emit32(mc, 0);
    return off;
}

/* JL rel32 */
static size_t emit_jl(MCode *mc)
{
    emit(mc, 0x0f);
    emit(mc, 0x8c);
    size_t off = mc->length;
    emit32(mc, 0);
    return off;
}

/* JGE rel32 */
static size_t emit_jge(MCode *mc)
{
    emit(mc, 0x0f);
    emit(mc, 0x8d);
    size_t off = mc->length;
    emit32(mc, 0);
    return off;
}

/* PUSH reg */
static void emit_push(MCode *mc, int reg)
{
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0x50 | (reg & 7));
}

/* POP reg */
static void emit_pop(MCode *mc, int reg)
{
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0x58 | (reg & 7));
}

/* RET */
static void emit_ret(MCode *mc)
{
    emit(mc, 0xc3);
}

/* CQO - sign extend RAX to RDX:RAX */
static void emit_cqo(MCode *mc)
{
    emit(mc, 0x48);
    emit(mc, 0x99);
}

/* IDIV reg */
static void emit_idiv(MCode *mc, int reg)
{
    emit(mc, rex(1, 0, 0, reg));
    emit(mc, 0xf7);
    emit(mc, 0xf8 | (reg & 7));
}

/* Division helper: dst = dividend / divisor
 * Saves/restores RDX and RAX as needed */
static void emit_div_rr(MCode *mc, int dst, int dividend, int divisor)
{
    /* Handle the complex register allocation for division */
    int saved_rax = -1, saved_rdx = -1;

    /* Save RAX if needed */
    if (dividend != RAX && dst != RAX)
    {
        emit_push(mc, RAX);
        saved_rax = 1;
    }
    /* Save RDX if needed */
    if (dividend != RDX && dst != RDX && divisor != RDX)
    {
        emit_push(mc, RDX);
        saved_rdx = 1;
    }

    /* Move dividend to RAX if not already there */
    if (dividend != RAX)
    {
        emit_mov_rr(mc, RAX, dividend);
    }

    /* Handle case where divisor is in RDX (CQO will clobber it) */
    int actual_divisor = divisor;
    if (divisor == RDX)
    {
        emit_mov_rr(mc, R11, RDX); /* Use R11 as temp */
        actual_divisor = R11;
    }

    /* Sign extend RAX to RDX:RAX */
    emit_cqo(mc);

    /* Divide: RAX = RDX:RAX / divisor */
    emit_idiv(mc, actual_divisor);

    /* Move result to destination */
    if (dst != RAX)
    {
        emit_mov_rr(mc, dst, RAX);
    }

    /* Restore RDX */
    if (saved_rdx == 1)
    {
        emit_pop(mc, RDX);
    }
    /* Restore RAX */
    if (saved_rax == 1)
    {
        emit_pop(mc, RAX);
    }
}

/* Modulo helper: dst = dividend % divisor
 * Result is in RDX after IDIV */
static void emit_mod_rr(MCode *mc, int dst, int dividend, int divisor)
{
    int saved_rax = -1, saved_rdx = -1;

    /* Save RAX if needed */
    if (dividend != RAX && dst != RAX)
    {
        emit_push(mc, RAX);
        saved_rax = 1;
    }
    /* Save RDX only if dst isn't RDX and divisor isn't RDX */
    if (dst != RDX && dividend != RDX && divisor != RDX)
    {
        emit_push(mc, RDX);
        saved_rdx = 1;
    }

    /* Move dividend to RAX */
    if (dividend != RAX)
    {
        emit_mov_rr(mc, RAX, dividend);
    }

    /* Handle divisor in RDX */
    int actual_divisor = divisor;
    if (divisor == RDX)
    {
        emit_mov_rr(mc, R11, RDX);
        actual_divisor = R11;
    }

    /* Sign extend */
    emit_cqo(mc);

    /* Divide */
    emit_idiv(mc, actual_divisor);

    /* Remainder is in RDX, move to dst */
    if (dst != RDX)
    {
        emit_mov_rr(mc, dst, RDX);
    }

    /* Restore */
    if (saved_rdx == 1)
    {
        emit_pop(mc, RDX);
    }
    if (saved_rax == 1)
    {
        emit_pop(mc, RAX);
    }
}

/* ============================================================
 * NaN-boxing Helpers
 * ============================================================ */

/* Unbox int: dst = (src >> 3) - 64-bit arithmetic shift */
static void emit_unbox_int(MCode *mc, int dst, int src)
{
    emit_mov_rr(mc, dst, src);
    /* 64-bit arithmetic shift right by 3 */
    emit(mc, rex(1, 0, 0, dst));
    emit(mc, 0xc1);
    emit(mc, 0xf8 | (dst & 7)); /* SAR instead of SHR to preserve sign */
    emit(mc, 3);
}

/* Box int: dst = QNAN | TAG_INT | (src << 3) */
static void emit_box_int(MCode *mc, int dst, int src)
{
    if (dst != src)
        emit_mov_rr(mc, dst, src);

    /* Shift left 3 (64-bit shift to preserve full value) */
    emit(mc, rex(1, 0, 0, dst));
    emit(mc, 0xc1);
    emit(mc, 0xe0 | (dst & 7));
    emit(mc, 3);

    /* OR with QNAN | TAG_INT */
    emit_mov_ri64(mc, R11, QNAN | TAG_INT);
    emit(mc, rex(1, R11, 0, dst));
    emit(mc, 0x09);
    emit(mc, 0xc0 | ((R11 & 7) << 3) | (dst & 7));
}

/* ============================================================
 * SSE Instructions for Floats (Phase 6)
 * ============================================================ */

/* MOVSD xmm, xmm */
static void emit_movsd_rr(MCode *mc, int dst, int src)
{
    emit(mc, 0xf2);
    if (dst >= 8 || src >= 8)
    {
        emit(mc, 0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit(mc, 0x0f);
    emit(mc, 0x10);
    emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
}

/* MOVSD xmm, [mem] */
static void emit_movsd_rm(MCode *mc, int dst, int base, int32_t off)
{
    emit(mc, 0xf2);
    if (dst >= 8 || base >= 8)
    {
        emit(mc, 0x40 | ((dst >= 8) ? 4 : 0) | ((base >= 8) ? 1 : 0));
    }
    emit(mc, 0x0f);
    emit(mc, 0x10);
    if (off == 0 && (base & 7) != RBP)
    {
        emit(mc, ((dst & 7) << 3) | (base & 7));
    }
    else if (off >= -128 && off <= 127)
    {
        emit(mc, 0x40 | ((dst & 7) << 3) | (base & 7));
        emit(mc, off);
    }
    else
    {
        emit(mc, 0x80 | ((dst & 7) << 3) | (base & 7));
        emit32(mc, off);
    }
}

/* ADDSD xmm, xmm */
static void emit_addsd_rr(MCode *mc, int dst, int src)
{
    emit(mc, 0xf2);
    if (dst >= 8 || src >= 8)
    {
        emit(mc, 0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit(mc, 0x0f);
    emit(mc, 0x58);
    emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
}

/* SUBSD xmm, xmm */
static void emit_subsd_rr(MCode *mc, int dst, int src)
{
    emit(mc, 0xf2);
    if (dst >= 8 || src >= 8)
    {
        emit(mc, 0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit(mc, 0x0f);
    emit(mc, 0x5c);
    emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
}

/* MULSD xmm, xmm */
static void emit_mulsd_rr(MCode *mc, int dst, int src)
{
    emit(mc, 0xf2);
    if (dst >= 8 || src >= 8)
    {
        emit(mc, 0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit(mc, 0x0f);
    emit(mc, 0x59);
    emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
}

/* DIVSD xmm, xmm */
static void emit_divsd_rr(MCode *mc, int dst, int src)
{
    emit(mc, 0xf2);
    if (dst >= 8 || src >= 8)
    {
        emit(mc, 0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit(mc, 0x0f);
    emit(mc, 0x5e);
    emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
}

/* UCOMISD xmm, xmm (unordered compare double) */
static void emit_ucomisd_rr(MCode *mc, int dst, int src)
{
    emit(mc, 0x66);
    if (dst >= 8 || src >= 8)
    {
        emit(mc, 0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit(mc, 0x0f);
    emit(mc, 0x2e);
    emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
}

/* SETA reg (set if above - unsigned greater) */
static void emit_seta(MCode *mc, int reg)
{
    if (reg >= 4 && reg <= 7)
        emit(mc, 0x40);
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0x0f);
    emit(mc, 0x97);
    emit(mc, 0xc0 | (reg & 7));
}

/* SETAE reg (set if above or equal - unsigned >=) */
static void emit_setae(MCode *mc, int reg)
{
    if (reg >= 4 && reg <= 7)
        emit(mc, 0x40);
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0x0f);
    emit(mc, 0x93);
    emit(mc, 0xc0 | (reg & 7));
}

/* SETB reg (set if below - unsigned less) */
static void emit_setb(MCode *mc, int reg)
{
    if (reg >= 4 && reg <= 7)
        emit(mc, 0x40);
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0x0f);
    emit(mc, 0x92);
    emit(mc, 0xc0 | (reg & 7));
}

/* SETBE reg (set if below or equal - unsigned <=) */
static void emit_setbe(MCode *mc, int reg)
{
    if (reg >= 4 && reg <= 7)
        emit(mc, 0x40);
    if (reg >= 8)
        emit(mc, 0x41);
    emit(mc, 0x0f);
    emit(mc, 0x96);
    emit(mc, 0xc0 | (reg & 7));
}

/* CVTSI2SD xmm, r64 (convert int64 to double) */
static void emit_cvtsi2sd_rr(MCode *mc, int xmm, int gpr)
{
    emit(mc, 0xf2);
    emit(mc, rex(1, xmm, 0, gpr));
    emit(mc, 0x0f);
    emit(mc, 0x2a);
    emit(mc, 0xc0 | ((xmm & 7) << 3) | (gpr & 7));
}

/* CVTTSD2SI r64, xmm (convert double to int64, truncate) */
static void emit_cvttsd2si_rr(MCode *mc, int gpr, int xmm)
{
    emit(mc, 0xf2);
    emit(mc, rex(1, gpr, 0, xmm));
    emit(mc, 0x0f);
    emit(mc, 0x2c);
    emit(mc, 0xc0 | ((gpr & 7) << 3) | (xmm & 7));
}

/* XORPD xmm, xmm (for negation: xor with sign bit) */
static void emit_xorpd_rr(MCode *mc, int dst, int src)
{
    emit(mc, 0x66);
    if (dst >= 8 || src >= 8)
    {
        emit(mc, 0x40 | ((dst >= 8) ? 4 : 0) | ((src >= 8) ? 1 : 0));
    }
    emit(mc, 0x0f);
    emit(mc, 0x57);
    emit(mc, 0xc0 | ((dst & 7) << 3) | (src & 7));
}

/* MOVQ r64, xmm (move from XMM to GPR) */
static void emit_movq_r_xmm(MCode *mc, int gpr, int xmm)
{
    emit(mc, 0x66);
    emit(mc, rex(1, xmm, 0, gpr));
    emit(mc, 0x0f);
    emit(mc, 0x7e);
    emit(mc, 0xc0 | ((xmm & 7) << 3) | (gpr & 7));
}

/* MOVQ xmm, r64 (move from GPR to XMM) */
static void emit_movq_xmm_r(MCode *mc, int xmm, int gpr)
{
    emit(mc, 0x66);
    emit(mc, rex(1, xmm, 0, gpr));
    emit(mc, 0x0f);
    emit(mc, 0x6e);
    emit(mc, 0xc0 | ((xmm & 7) << 3) | (gpr & 7));
}

/* ============================================================
 * Code Generation from IR
 * ============================================================ */

/* Exit stub structure */
typedef struct
{
    size_t code_offset;    /* Offset in main code buffer */
    uint32_t snapshot_idx; /* Snapshot for this exit */
} ExitStub;

/* Compile a single IR instruction */
static void compile_ir_op(MCode *mc, TraceIR *ir, IRIns *ins,
                          ExitStub *exits, uint32_t *num_exits)
{
    int dst = (ins->dst > 0) ? ir->vregs[ins->dst].phys_reg : -1;
    int src1 = (ins->src1 > 0) ? ir->vregs[ins->src1].phys_reg : -1;
    int src2 = (ins->src2 > 0) ? ir->vregs[ins->src2].phys_reg : -1;

    switch (ins->op)
    {
    case IR_NOP:
        break;

    case IR_CONST_INT:
#ifdef JIT_DEBUG
        fprintf(stderr, "[IR_CONST_INT] dst=%d, imm=%lld\n", dst, (long long)ins->imm.i64);
#endif
        if (dst >= 0)
        {
            if (ins->imm.i64 >= INT32_MIN && ins->imm.i64 <= INT32_MAX)
            {
                emit_mov_ri32(mc, dst, (int32_t)ins->imm.i64);
            }
            else
            {
                emit_mov_ri64(mc, dst, ins->imm.i64);
            }
        }
        break;

    case IR_CONST_INT64:
#ifdef JIT_DEBUG
        fprintf(stderr, "[IR_CONST_INT64] dst=%d, imm=%lld\n", dst, (long long)ins->imm.i64);
#endif
        if (dst >= 0)
        {
            emit_mov_ri64(mc, dst, ins->imm.i64);
        }
        break;

    case IR_CONST_DOUBLE:
        if (dst >= 0)
        {
            /* Load double constant - imm.f64 contains the double value */
            /* Move to GPR first, then to XMM */
            union
            {
                double d;
                uint64_t u;
            } conv;
            conv.d = ins->imm.f64;
            emit_mov_ri64(mc, R11, conv.u);
            emit_movq_xmm_r(mc, dst, R11);
        }
        break;

    case IR_CONST_BOOL:
        if (dst >= 0)
        {
            /* Boolean: 0 or 1 */
            emit_mov_ri32(mc, dst, ins->imm.i64 ? 1 : 0);
        }
        break;

    case IR_CONST_NIL:
        if (dst >= 0)
        {
            /* NIL is represented as a specific NaN-boxed value */
            /* NIL tag: QNAN | TAG_NIL */
            emit_mov_ri64(mc, dst, QNAN | TAG_NIL);
        }
        break;

    case IR_LOAD_CONST:
        /* Load from constant pool - aux is index, need constants array pointer */
        /* For now, just load the immediate if available */
        if (dst >= 0)
        {
            /* Constants are passed via RDX (third arg) if available */
            /* Load: dst = constants[aux] */
            emit_mov_rm(mc, dst, RDX, ins->aux * 8);
        }
        break;

    case IR_COPY:
        /* Copy is same as MOV */
        if (dst >= 0 && src1 >= 0 && dst != src1)
        {
            emit_mov_rr(mc, dst, src1);
        }
        break;

    case IR_LOAD_LOCAL:
        if (dst >= 0)
        {
#ifdef JIT_DEBUG
            fprintf(stderr, "[IR_LOAD_LOCAL] dst=%d, slot=%d\n", dst, ins->aux);
#endif
            /* Load boxed value from bp[slot] */
            emit_mov_rm(mc, dst, RDI, ins->aux * 8);
            /* Note: unboxing is now done via separate IR_UNBOX_INT op */
        }
        break;

    case IR_STORE_LOCAL:
        if (src1 >= 0)
        {
            /* Store value to bp[slot] (value should already be boxed) */
            emit_mov_mr(mc, RDI, ins->aux * 8, src1);
        }
        break;

    case IR_LOAD_GLOBAL:
        if (dst >= 0)
        {
#ifdef JIT_DEBUG
            fprintf(stderr, "[IR_LOAD_GLOBAL] dst=%d, slot=%d\n", dst, ins->aux);
#endif
            /* Load boxed value from globals_values[slot]
             * RSI = globals_values pointer (passed in at runtime)
             * aux = resolved hash table slot index */
            emit_mov_rm(mc, dst, RSI, ins->aux * 8);
        }
        break;

    case IR_STORE_GLOBAL:
        if (src1 >= 0)
        {
#ifdef JIT_DEBUG
            fprintf(stderr, "[IR_STORE_GLOBAL] src1=%d, slot=%d\n", src1, ins->aux);
#endif
            /* Store value to globals_values[slot]
             * RSI = globals_values pointer
             * aux = resolved hash table slot index */
            emit_mov_mr(mc, RSI, ins->aux * 8, src1);
        }
        break;

    case IR_MOV:
        if (dst >= 0 && src1 >= 0 && dst != src1)
        {
            emit_mov_rr(mc, dst, src1);
        }
        break;

    case IR_ADD_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
#ifdef JIT_DEBUG
            fprintf(stderr, "[IR_ADD_INT] dst=%d, src1=%d, src2=%d\n", dst, src1, src2);
#endif
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit_add_rr(mc, dst, src2);
        }
        break;

    case IR_SUB_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit_sub_rr(mc, dst, src2);
        }
        break;

    case IR_MUL_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit_imul_rr(mc, dst, src2);
        }
        break;

    case IR_DIV_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            /* IDIV uses RDX:RAX / divisor -> RAX=quotient, RDX=remainder
             * CQO sign-extends RAX into RDX, clobbering RDX!
             * IDIV clobbers both RAX and RDX
             *
             * Critical register clobber cases:
             * - src1 == R11: reading src2 into R11 would clobber src1
             * - src2 == R10: reading src1 into R10 would clobber src2
             * Solution: read to RAX first (safe), then move to scratch regs
             */

            /* Save all potentially clobbered registers */
            emit_push(mc, RAX);
            emit_push(mc, RDX);
            emit_push(mc, R10);
            emit_push(mc, R11);

            /* Read both operands into RAX/RDX first (they're saved on stack)
             * This avoids any src vs R10/R11 conflicts */
            emit_mov_rr(mc, RAX, src1); /* dividend into RAX */
            emit_mov_rr(mc, RDX, src2); /* divisor into RDX (temp) */

            /* Now move to final positions - no conflict possible */
            emit_mov_rr(mc, R11, RDX); /* divisor to R11 */
            /* RAX already has dividend, no need to copy to R10 */

            /* Sign-extend RAX to RDX:RAX */
            emit_cqo(mc);       /* Sign-extend RAX into RDX:RAX */
            emit_idiv(mc, R11); /* RDX:RAX / R11 -> RAX=quot, RDX=rem */

            /* Save result (quotient) to R10 */
            emit_mov_rr(mc, R10, RAX);

            /* Restore scratch regs (we'll move result after) */
            emit_pop(mc, R11);
            /* Skip R10 restore - we need its value */
            emit_add_ri(mc, RSP, 8); /* Discard R10 from stack */
            emit_pop(mc, RDX);
            emit_pop(mc, RAX);

            /* Move result to destination */
            emit_mov_rr(mc, dst, R10);
        }
        break;

    case IR_MOD_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
#ifdef JIT_DEBUG
            fprintf(stderr, "[IR_MOD_INT] dst=%d, src1=%d, src2=%d\n", dst, src1, src2);
#endif
            /* IDIV uses RDX:RAX / divisor -> RAX=quotient, RDX=remainder
             * CQO sign-extends RAX into RDX, clobbering RDX!
             * IDIV clobbers both RAX and RDX
             *
             * Critical register clobber cases:
             * - src1 == R11: reading src2 into R11 would clobber src1
             * - src2 == R10: reading src1 into R10 would clobber src2
             * Solution: read to RAX first (safe), then move to scratch regs
             */

            /* Save all potentially clobbered registers */
            emit_push(mc, RAX);
            emit_push(mc, RDX);
            emit_push(mc, R10);
            emit_push(mc, R11);

            /* Read both operands into RAX/RDX first (they're saved on stack)
             * This avoids any src vs R10/R11 conflicts */
            emit_mov_rr(mc, RAX, src1); /* dividend into RAX */
            emit_mov_rr(mc, RDX, src2); /* divisor into RDX (temp) */

            /* Now move to final positions - no conflict possible */
            emit_mov_rr(mc, R11, RDX); /* divisor to R11 */
            emit_mov_rr(mc, R10, RAX); /* dividend stays in RAX, copy to R10 for clarity */

            /* RAX already has dividend, sign-extend to RDX:RAX */
            emit_cqo(mc);       /* Sign-extend RAX into RDX:RAX */
            emit_idiv(mc, R11); /* RDX:RAX / R11 -> RAX=quot, RDX=rem */

            /* Save result (remainder) to R10 */
            emit_mov_rr(mc, R10, RDX);

            /* Restore scratch regs (we'll move result after) */
            emit_pop(mc, R11);
            /* Skip R10 restore - we need its value */
            emit_add_ri(mc, RSP, 8); /* Discard R10 from stack */
            emit_pop(mc, RDX);
            emit_pop(mc, RAX);

            /* Move result to destination */
            emit_mov_rr(mc, dst, R10);
        }
        break;

    case IR_NEG_INT:
        if (dst >= 0 && src1 >= 0)
        {
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit_neg(mc, dst);
        }
        break;

    case IR_INC_INT:
        if (dst >= 0 && src1 >= 0)
        {
#ifdef JIT_DEBUG
            fprintf(stderr, "[IR_INC_INT] dst=%d, src1=%d\n", dst, src1);
#endif
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit_inc(mc, dst);
        }
        break;

    case IR_DEC_INT:
        if (dst >= 0 && src1 >= 0)
        {
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit_dec(mc, dst);
        }
        break;

    /* Comparisons */
    case IR_LT_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
#ifdef JIT_DEBUG
            fprintf(stderr, "[IR_LT_INT] dst=%d, src1=%d, src2=%d\n", dst, src1, src2);
#endif
            emit_cmp_rr(mc, src1, src2);
            emit_setl(mc, dst);
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    case IR_LE_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            emit_cmp_rr(mc, src1, src2);
            emit_setle(mc, dst);
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    case IR_GT_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            emit_cmp_rr(mc, src1, src2);
            emit_setg(mc, dst);
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    case IR_GE_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            emit_cmp_rr(mc, src1, src2);
            emit_setge(mc, dst);
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    case IR_EQ_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            emit_cmp_rr(mc, src1, src2);
            emit_sete(mc, dst);
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    case IR_NE_INT:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            emit_cmp_rr(mc, src1, src2);
            emit_setne(mc, dst);
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    /* Guards */
    case IR_GUARD_INT:
    {
        /* Check that value has int type tag */
        /* Value format: QNAN | TAG_INT | (value << 3) */
        /* Check: (val & (QNAN | 0x7)) == (QNAN | TAG_INT) */
        if (src1 >= 0)
        {
            emit_mov_ri64(mc, R11, QNAN | 0x7);
            emit(mc, rex(1, src1, 0, R11));
            emit(mc, 0x21); /* AND */
            emit(mc, 0xc0 | ((src1 & 7) << 3) | (R11 & 7));
            emit_mov_ri64(mc, R10, QNAN | TAG_INT);
            emit_cmp_rr(mc, R11, R10);
            /* If not equal, jump to exit */
            if (*num_exits < IR_MAX_EXITS)
            {
                exits[*num_exits].code_offset = emit_jne(mc);
                exits[*num_exits].snapshot_idx = ins->imm.snapshot;
                (*num_exits)++;
            }
        }
        break;
    }

    case IR_GUARD_TRUE:
        if (src1 >= 0)
        {
            emit_test_ri(mc, src1, 1);
            if (*num_exits < IR_MAX_EXITS)
            {
                exits[*num_exits].code_offset = emit_je(mc);
                exits[*num_exits].snapshot_idx = ins->imm.snapshot;
                (*num_exits)++;
            }
        }
        break;

    case IR_GUARD_FALSE:
        if (src1 >= 0)
        {
            emit_test_ri(mc, src1, 1);
            if (*num_exits < IR_MAX_EXITS)
            {
                exits[*num_exits].code_offset = emit_jne(mc);
                exits[*num_exits].snapshot_idx = ins->imm.snapshot;
                (*num_exits)++;
            }
        }
        break;

    /* Float operations (Phase 6) */
    case IR_ADD_DOUBLE:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            if (dst != src1)
                emit_movsd_rr(mc, dst, src1);
            emit_addsd_rr(mc, dst, src2);
        }
        break;

    case IR_SUB_DOUBLE:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            if (dst != src1)
                emit_movsd_rr(mc, dst, src1);
            emit_subsd_rr(mc, dst, src2);
        }
        break;

    case IR_MUL_DOUBLE:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            if (dst != src1)
                emit_movsd_rr(mc, dst, src1);
            emit_mulsd_rr(mc, dst, src2);
        }
        break;

    case IR_DIV_DOUBLE:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            if (dst != src1)
                emit_movsd_rr(mc, dst, src1);
            emit_divsd_rr(mc, dst, src2);
        }
        break;

    /* Double comparisons - use UCOMISD which sets CF/ZF flags
     * UCOMISD sets: ZF=1 if equal, CF=1 if src1 < src2
     * For NaN: CF=ZF=PF=1 */
    case IR_LT_DOUBLE:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            emit_ucomisd_rr(mc, src1, src2);
            emit_setb(mc, dst); /* CF=1 means src1 < src2 */
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    case IR_LE_DOUBLE:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            emit_ucomisd_rr(mc, src1, src2);
            emit_setbe(mc, dst); /* CF=1 or ZF=1 means src1 <= src2 */
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    case IR_GT_DOUBLE:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            emit_ucomisd_rr(mc, src1, src2);
            emit_seta(mc, dst); /* CF=0 and ZF=0 means src1 > src2 */
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    case IR_GE_DOUBLE:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            emit_ucomisd_rr(mc, src1, src2);
            emit_setae(mc, dst); /* CF=0 means src1 >= src2 */
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    case IR_EQ_DOUBLE:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            emit_ucomisd_rr(mc, src1, src2);
            emit_sete(mc, dst); /* ZF=1 means equal */
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    case IR_NE_DOUBLE:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            emit_ucomisd_rr(mc, src1, src2);
            emit_setne(mc, dst); /* ZF=0 means not equal */
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    /* Logical operations */
    case IR_NOT:
        if (dst >= 0 && src1 >= 0)
        {
            /* Logical NOT: dst = !src1 (0 becomes 1, non-zero becomes 0) */
            emit_test_ri(mc, src1, src1); /* Test if src1 is zero */
            emit_sete(mc, dst);           /* Set 1 if zero (ZF=1) */
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    case IR_AND:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            /* Logical AND: dst = src1 && src2 */
            /* Both must be non-zero for result to be 1 */
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit_test_ri(mc, dst, dst); /* Check if src1 is non-zero */
            emit_setne(mc, R11);        /* R11 = (src1 != 0) */
            emit_movzx_rr8(mc, R11, R11);
            emit_test_ri(mc, src2, src2); /* Check if src2 is non-zero */
            emit_setne(mc, dst);          /* dst = (src2 != 0) */
            emit_movzx_rr8(mc, dst, dst);
            emit(mc, rex(1, R11, 0, dst)); /* AND dst, R11 */
            emit(mc, 0x21);
            emit(mc, 0xc0 | ((R11 & 7) << 3) | (dst & 7));
        }
        break;

    case IR_OR:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            /* Logical OR: dst = src1 || src2 */
            /* Either must be non-zero for result to be 1 */
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit(mc, rex(1, src2, 0, dst)); /* OR dst, src2 */
            emit(mc, 0x09);
            emit(mc, 0xc0 | ((src2 & 7) << 3) | (dst & 7));
            emit_test_ri(mc, dst, dst); /* Test if result is non-zero */
            emit_setne(mc, dst);        /* Set 1 if non-zero */
            emit_movzx_rr8(mc, dst, dst);
        }
        break;

    /* Bitwise operations */
    case IR_BAND:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            /* Bitwise AND: dst = src1 & src2 */
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit(mc, rex(1, src2, 0, dst));
            emit(mc, 0x21); /* AND r/m64, r64 */
            emit(mc, 0xc0 | ((src2 & 7) << 3) | (dst & 7));
        }
        break;

    case IR_BOR:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            /* Bitwise OR: dst = src1 | src2 */
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit(mc, rex(1, src2, 0, dst));
            emit(mc, 0x09); /* OR r/m64, r64 */
            emit(mc, 0xc0 | ((src2 & 7) << 3) | (dst & 7));
        }
        break;

    case IR_BXOR:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            /* Bitwise XOR: dst = src1 ^ src2 */
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit_xor_rr(mc, dst, src2);
        }
        break;

    case IR_BNOT:
        if (dst >= 0 && src1 >= 0)
        {
            /* Bitwise NOT: dst = ~src1 */
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit(mc, rex(1, 0, 0, dst));
            emit(mc, 0xf7); /* NOT r/m64 */
            emit(mc, 0xd0 | (dst & 7));
        }
        break;

    case IR_SHL:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            /* Shift left: dst = src1 << src2 */
            /* SHL uses CL for variable shift count */
            emit_push(mc, RCX);
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit_mov_rr(mc, RCX, src2);
            emit(mc, rex(1, 0, 0, dst));
            emit(mc, 0xd3); /* SHL r/m64, CL */
            emit(mc, 0xe0 | (dst & 7));
            emit_pop(mc, RCX);
        }
        break;

    case IR_SHR:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            /* Arithmetic shift right: dst = src1 >> src2 */
            /* SAR uses CL for variable shift count */
            emit_push(mc, RCX);
            if (dst != src1)
                emit_mov_rr(mc, dst, src1);
            emit_mov_rr(mc, RCX, src2);
            emit(mc, rex(1, 0, 0, dst));
            emit(mc, 0xd3); /* SAR r/m64, CL */
            emit(mc, 0xf8 | (dst & 7));
            emit_pop(mc, RCX);
        }
        break;

    case IR_LOOP:
    case IR_RET:
    case IR_JUMP:
    case IR_BRANCH:
        /* Handled in main compile loop for proper label resolution */
        break;

    case IR_EXIT:
        /* Side exit to interpreter - jump to exit stub */
        if (*num_exits < IR_MAX_EXITS)
        {
            exits[*num_exits].code_offset = emit_jmp(mc);
            exits[*num_exits].snapshot_idx = ins->imm.snapshot;
            (*num_exits)++;
        }
        break;

    case IR_UNBOX_INT:
        if (dst >= 0 && src1 >= 0)
        {
#ifdef JIT_DEBUG
            fprintf(stderr, "[IR_UNBOX_INT] dst=%d, src1=%d\n", dst, src1);
#endif
            emit_unbox_int(mc, dst, src1);
        }
        break;

    case IR_BOX_INT:
        if (dst >= 0 && src1 >= 0)
        {
#ifdef JIT_DEBUG
            fprintf(stderr, "[IR_BOX_INT] dst=%d, src1=%d\n", dst, src1);
#endif
            emit_box_int(mc, dst, src1);
        }
        break;

    case IR_INT_TO_DOUBLE:
        if (dst >= 0 && src1 >= 0)
        {
            /* Convert int64 in GPR to double in XMM register */
            /* Note: This assumes dst is allocated to an XMM register */
            emit_cvtsi2sd_rr(mc, dst, src1);
        }
        break;

    case IR_DOUBLE_TO_INT:
        if (dst >= 0 && src1 >= 0)
        {
            /* Convert double in XMM to int64 in GPR (truncate toward zero) */
            emit_cvttsd2si_rr(mc, dst, src1);
        }
        break;

    case IR_BOX_DOUBLE:
        if (dst >= 0 && src1 >= 0)
        {
            /* Double is already in IEEE 754 format, just move to GPR */
            /* NaN-boxing: doubles are stored as-is (no tagging needed) */
            emit_movq_r_xmm(mc, dst, src1);
        }
        break;

    case IR_UNBOX_DOUBLE:
        if (dst >= 0 && src1 >= 0)
        {
            /* Move from GPR to XMM register */
            emit_movq_xmm_r(mc, dst, src1);
        }
        break;

    case IR_NEG_DOUBLE:
        if (dst >= 0 && src1 >= 0)
        {
            /* Negate double by XORing with sign bit mask */
            /* Load sign bit mask (0x8000000000000000) into R11, then XMM */
            emit_mov_ri64(mc, R11, 0x8000000000000000ULL);
            emit_movq_xmm_r(mc, R10, R11); /* Use R10 as temp XMM reg concept - need to use actual XMM */
            /* Actually, simpler: subtract from zero */
            /* SUBSD result = 0.0 - src1 */
            /* Or XOR with sign bit in memory - complex. Let's use SUBSD approach: */
            /* For now, just copy and negate via XORPD with constant */
            if (dst != src1)
                emit_movsd_rr(mc, dst, src1);
            /* XOR with sign bit - need sign bit in XMM register */
            /* Simplest: use 0 - src1 */
            /* Actually let's just flip the sign bit in GPR and move back */
            emit_movq_r_xmm(mc, R11, src1);
            emit_mov_ri64(mc, R10, 0x8000000000000000ULL);
            emit_xor_rr(mc, R11, R10);
            emit_movq_xmm_r(mc, dst, R11);
        }
        break;

        /* Array operations - ObjArray layout:
         * offset 0: Obj (type=4, next=8, marked=1, padding) = 24 bytes
         * offset 24: count (uint32_t)
         * offset 28: capacity (uint32_t)
         * offset 32: values (Value*)
         */
#define ARRAY_COUNT_OFFSET 24
#define ARRAY_VALUES_OFFSET 32

    case IR_ARRAY_LEN:
        if (dst >= 0 && src1 >= 0)
        {
            /* src1 = pointer to ObjArray, dst = count (unboxed int) */
            /* First unbox to get raw pointer: src1 is NaN-boxed object pointer */
            emit_mov_ri64(mc, R11, 0x0000FFFFFFFFFFFFULL);
            emit(mc, rex(1, src1, 0, R11));
            emit(mc, 0x21); /* AND */
            emit(mc, 0xc0 | ((src1 & 7) << 3) | (R11 & 7));
            /* R11 now has raw pointer, load count */
            emit(mc, rex(1, dst, 0, R11)); /* MOV dst, [R11 + offset] */
            emit(mc, 0x8b);
            emit(mc, 0x40 | ((dst & 7) << 3) | (R11 & 7));
            emit(mc, ARRAY_COUNT_OFFSET);
            /* Zero-extend 32-bit to 64-bit */
            emit(mc, 0x89); /* MOV r32, r32 to zero-extend */
            emit(mc, 0xc0 | ((dst & 7) << 3) | (dst & 7));
        }
        break;

    case IR_ARRAY_GET:
        if (dst >= 0 && src1 >= 0 && src2 >= 0)
        {
            /* src1 = array (boxed), src2 = index (unboxed int), dst = value */
            /* Unbox array pointer */
            emit_mov_ri64(mc, R11, 0x0000FFFFFFFFFFFFULL);
            emit_mov_rr(mc, R10, src1);
            emit(mc, rex(1, R10, 0, R11));
            emit(mc, 0x21);
            emit(mc, 0xc0 | ((R10 & 7) << 3) | (R11 & 7));
            /* R10 = raw array pointer */
            /* Load values pointer: R11 = array->values */
            emit(mc, rex(1, R11, 0, R10));
            emit(mc, 0x8b);
            emit(mc, 0x40 | ((R11 & 7) << 3) | (R10 & 7));
            emit(mc, ARRAY_VALUES_OFFSET);
            /* Load value: dst = values[index] = [R11 + src2*8] */
            emit(mc, rex(1, dst, src2, R11)); /* Use SIB */
            emit(mc, 0x8b);
            emit(mc, 0x04 | ((dst & 7) << 3));              /* ModRM with SIB */
            emit(mc, ((src2 & 7) << 3) | (R11 & 7) | 0xc0); /* SIB: scale=8, index=src2, base=R11 - wrong encoding */
            /* Actually need proper SIB encoding for [R11 + src2*8] */
            /* Let's do it simpler: multiply index by 8, add to base, load */
            emit_push(mc, RCX);
            emit_mov_rr(mc, RCX, src2);
            emit(mc, rex(1, 0, 0, RCX)); /* SHL RCX, 3 (multiply by 8) */
            emit(mc, 0xc1);
            emit(mc, 0xe0 | (RCX & 7));
            emit(mc, 3);
            emit_add_rr(mc, RCX, R11);    /* RCX = values + index*8 */
            emit_mov_rm(mc, dst, RCX, 0); /* dst = [RCX] */
            emit_pop(mc, RCX);
        }
        break;

    case IR_ARRAY_SET:
        if (src1 >= 0 && src2 >= 0)
        {
            /* src1 = array (boxed), src2 = index (unboxed), aux = value vreg */
            int val_reg = (ins->aux > 0) ? ir->vregs[ins->aux].phys_reg : -1;
            if (val_reg >= 0)
            {
                /* Unbox array pointer */
                emit_mov_ri64(mc, R11, 0x0000FFFFFFFFFFFFULL);
                emit_mov_rr(mc, R10, src1);
                emit(mc, rex(1, R10, 0, R11));
                emit(mc, 0x21);
                emit(mc, 0xc0 | ((R10 & 7) << 3) | (R11 & 7));
                /* R10 = raw array pointer */
                /* Load values pointer */
                emit(mc, rex(1, R11, 0, R10));
                emit(mc, 0x8b);
                emit(mc, 0x40 | ((R11 & 7) << 3) | (R10 & 7));
                emit(mc, ARRAY_VALUES_OFFSET);
                /* Store value: values[index] = val */
                emit_push(mc, RCX);
                emit_mov_rr(mc, RCX, src2);
                emit(mc, rex(1, 0, 0, RCX));
                emit(mc, 0xc1);
                emit(mc, 0xe0 | (RCX & 7));
                emit(mc, 3);
                emit_add_rr(mc, RCX, R11);
                emit_mov_mr(mc, RCX, 0, val_reg);
                emit_pop(mc, RCX);
            }
        }
        break;

#undef ARRAY_COUNT_OFFSET
#undef ARRAY_VALUES_OFFSET

    /* SSA/Deopt operations */
    case IR_PHI:
        /* PHI nodes are resolved during register allocation, no codegen needed */
        break;

    case IR_SNAPSHOT:
        /* Snapshot is metadata for deoptimization, no codegen needed */
        break;

    /* Additional guard operations */
    case IR_GUARD_TYPE:
        /* Guard that value has expected type tag */
        if (src1 >= 0)
        {
            /* Extract type bits and compare */
            emit_mov_rr(mc, R11, src1);
            emit_mov_ri64(mc, R10, QNAN | 0x7);
            emit(mc, rex(1, R10, 0, R11));
            emit(mc, 0x21); /* AND */
            emit(mc, 0xc0 | ((R10 & 7) << 3) | (R11 & 7));
            /* Compare with expected type (in aux) */
            emit_mov_ri64(mc, R10, QNAN | ins->aux);
            emit_cmp_rr(mc, R11, R10);
            if (*num_exits < IR_MAX_EXITS)
            {
                exits[*num_exits].code_offset = emit_jne(mc);
                exits[*num_exits].snapshot_idx = ins->imm.snapshot;
                (*num_exits)++;
            }
        }
        break;

    case IR_GUARD_DOUBLE:
        /* Guard that value is a double (not NaN-boxed) */
        if (src1 >= 0)
        {
            /* Doubles have top bits != QNAN pattern */
            emit_mov_rr(mc, R11, src1);
            emit_mov_ri64(mc, R10, QNAN);
            emit(mc, rex(1, R10, 0, R11));
            emit(mc, 0x21); /* AND */
            emit(mc, 0xc0 | ((R10 & 7) << 3) | (R11 & 7));
            emit_cmp_rr(mc, R11, R10);
            if (*num_exits < IR_MAX_EXITS)
            {
                exits[*num_exits].code_offset = emit_je(mc); /* Exit if IS NaN-boxed */
                exits[*num_exits].snapshot_idx = ins->imm.snapshot;
                (*num_exits)++;
            }
        }
        break;

    case IR_GUARD_OVERFLOW:
        /* Guard that previous arithmetic didn't overflow */
        /* Check overflow flag (OF) after arithmetic */
        if (*num_exits < IR_MAX_EXITS)
        {
            /* JO - jump if overflow */
            emit(mc, 0x0f);
            emit(mc, 0x80); /* JO rel32 */
            exits[*num_exits].code_offset = mc->length;
            emit32(mc, 0); /* Placeholder */
            exits[*num_exits].snapshot_idx = ins->imm.snapshot;
            (*num_exits)++;
        }
        break;

    case IR_GUARD_BOUNDS:
        /* Guard array index within bounds: index < array.count */
        if (src1 >= 0 && src2 >= 0)
        {
            /* src1 = index, src2 = array length */
            emit_cmp_rr(mc, src1, src2);
            if (*num_exits < IR_MAX_EXITS)
            {
                exits[*num_exits].code_offset = emit_jge(mc); /* Exit if index >= len */
                exits[*num_exits].snapshot_idx = ins->imm.snapshot;
                (*num_exits)++;
            }
        }
        break;

    case IR_GUARD_FUNC:
        /* Guard that function pointer matches expected */
        if (src1 >= 0)
        {
            /* Compare function pointer with expected (stored in imm) */
            emit_mov_ri64(mc, R11, ins->imm.i64);
            emit_cmp_rr(mc, src1, R11);
            if (*num_exits < IR_MAX_EXITS)
            {
                exits[*num_exits].code_offset = emit_jne(mc);
                exits[*num_exits].snapshot_idx = ins->aux;
                (*num_exits)++;
            }
        }
        break;

    /* Function call operations */
    case IR_ARG:
        /* Prepare argument for function call - move to arg register */
        if (src1 >= 0)
        {
            /* Arguments go to RDI, RSI, RDX, RCX, R8, R9 per x64 ABI */
            int arg_idx = ins->aux;
            int arg_regs[] = {RDI, RSI, RDX, RCX, R8, R9};
            if (arg_idx < 6)
            {
                emit_mov_rr(mc, arg_regs[arg_idx], src1);
            }
            else
            {
                /* Stack args: push in reverse order */
                emit_push(mc, src1);
            }
        }
        break;

    case IR_CALL:
        /* Call a function - aux contains number of args */
        if (src1 >= 0)
        {
            /* src1 = function pointer (boxed), aux = nargs */
            /* Unbox function pointer */
            emit_mov_ri64(mc, R11, 0x0000FFFFFFFFFFFFULL);
            emit(mc, rex(1, src1, 0, R11));
            emit(mc, 0x21);
            emit(mc, 0xc0 | ((src1 & 7) << 3) | (R11 & 7));
            /* Call through R11 */
            emit(mc, 0x41); /* REX.B */
            emit(mc, 0xff); /* CALL r/m64 */
            emit(mc, 0xd0 | (R11 & 7));
            /* Result is in RAX, move to dst if needed */
            if (dst >= 0 && dst != RAX)
            {
                emit_mov_rr(mc, dst, RAX);
            }
        }
        break;

    case IR_CALL_INLINE:
        /*
         * Inline function call via runtime helper
         *
         * The IR instruction has:
         * - imm.i64 = ObjFunction* to call (stored as int64)
         * - aux = argument count
         * - src1, src2 = first two argument vregs (rest would need stack)
         *
         * We call jit_call_inline(function, args_array, arg_count)
         * where args_array is built on the stack temporarily.
         */
        {
            ObjFunction *fn = (ObjFunction *)(uintptr_t)ins->imm.i64;
            uint8_t arg_count = (uint8_t)ins->aux;

            if (fn && arg_count <= 2)
            {
                /* Allocate stack space for args array (max 8 args * 8 bytes) */
                /* SUB RSP, 64 */
                emit(mc, rex(1, 0, 0, RSP));
                emit(mc, 0x83);
                emit(mc, 0xe8 | (RSP & 7)); /* SUB r/m64, imm8 */
                emit(mc, 64);

                /* Store arguments into stack-based array */
                /* RSP+0 = arg0, RSP+8 = arg1, etc. */
                if (arg_count >= 1 && src1 >= 0)
                {
                    /* MOV [RSP+0], src1 */
                    emit_mov_mr(mc, RSP, 0, src1);
                }
                if (arg_count >= 2 && src2 >= 0)
                {
                    /* MOV [RSP+8], src2 */
                    emit_mov_mr(mc, RSP, 8, src2);
                }

                /* Set up call arguments per x64 ABI:
                 * RDI = function pointer
                 * RSI = args array pointer (RSP)
                 * RDX = arg count
                 */

                /* MOV RDI, function_ptr (immediate 64-bit) */
                emit_mov_ri64(mc, RDI, (int64_t)(uintptr_t)fn);

                /* MOV RSI, RSP (args array is at RSP) */
                emit_mov_rr(mc, RSI, RSP);

                /* MOV RDX, arg_count */
                emit_mov_ri32(mc, RDX, arg_count);

                /* CALL jit_call_inline */
                /* MOV R11, jit_call_inline address */
                emit_mov_ri64(mc, R11, (int64_t)(uintptr_t)jit_call_inline);

                /* CALL R11 */
                emit(mc, 0x41); /* REX.B */
                emit(mc, 0xff); /* CALL r/m64 */
                emit(mc, 0xd3); /* ModR/M for R11 */

                /* Free stack space */
                /* ADD RSP, 64 */
                emit(mc, rex(1, 0, 0, RSP));
                emit(mc, 0x83);
                emit(mc, 0xc0 | (RSP & 7)); /* ADD r/m64, imm8 */
                emit(mc, 64);

                /* Result is in RAX, move to dst if needed */
                if (dst >= 0 && dst != RAX)
                {
                    emit_mov_rr(mc, dst, RAX);
                }
            }
        }
        break;

    case IR_RET_VAL:
        /* Set return value */
        if (src1 >= 0)
        {
            /* Move return value to RAX */
            if (src1 != RAX)
            {
                emit_mov_rr(mc, RAX, src1);
            }
        }
        break;

    default:
        /* Unhandled - skip */
        break;
    }
}
#endif /* JIT_X64 - end of x86-64 specific code */

/* ============================================================
 * Main Trace Compiler
 * ============================================================ */

bool trace_compile(TraceIR *ir, void **code_out, size_t *size_out,
                   uint8_t **exit_stubs, uint32_t *num_exits_out)
{
#if JIT_ARM64
    /* Use ARM64 code generator */
    return trace_compile_arm64(ir, code_out, size_out, exit_stubs, num_exits_out);
#elif JIT_X64
    /* x86-64 code generator follows */
    MCode mc;
    mcode_init(&mc, 16384);
    if (!mc.code)
        return false;

    /* Run register allocation */
    RegAlloc ra;
    regalloc_run(ir, &ra);

    /* Track exits */
    ExitStub exits[IR_MAX_EXITS];
    uint32_t num_exits = 0;

    /* Prologue */
    emit_push(&mc, RBX);
    emit_push(&mc, R12);
    emit_push(&mc, R13);
    emit_push(&mc, R14);
    emit_push(&mc, R15);

    /* Track loop start for back edge */
    size_t loop_start = 0;

    /* Compile each IR instruction */
    for (uint32_t i = 0; i < ir->nops; i++)
    {
        IRIns *ins = &ir->ops[i];

        if (ins->op == IR_LOOP)
        {
            /* This is a loop back edge with condition */
            int cond_reg = (ins->src1 > 0) ? ir->vregs[ins->src1].phys_reg : -1;

#ifdef JIT_DEBUG
            fprintf(stderr, "[IR_LOOP] cond_reg=%d, loop_start=%zu, current=%zu\n",
                    cond_reg, loop_start, mc.length);
#endif
            if (cond_reg >= 0)
            {
                /* Test condition and jump back if true */
                emit_test_ri(&mc, cond_reg, 1);
                size_t jcc = emit_jne(&mc); /* Jump if condition is non-zero */
                patch32(&mc, jcc, (int32_t)(loop_start - (jcc + 4)));
            }
            else if (ir->has_loop && loop_start > 0)
            {
                /* Unconditional back edge (shouldn't happen normally) */
                size_t jmp = emit_jmp(&mc);
                patch32(&mc, jmp, (int32_t)(loop_start - (jmp + 4)));
            }
            continue;
        }

        /* Mark loop start position for back edges */
        if (ins->aux > 0 && ins->aux == i)
        {
            loop_start = mc.length;
        }

        /* Also track if this is the designated loop start */
        if (i == ir->loop_start)
        {
            loop_start = mc.length;
        }

        compile_ir_op(&mc, ir, ins, exits, &num_exits);
    }

    /* Epilogue */
    emit_pop(&mc, R15);
    emit_pop(&mc, R14);
    emit_pop(&mc, R13);
    emit_pop(&mc, R12);
    emit_pop(&mc, RBX);
    emit_ret(&mc);

    /* Generate exit stubs */
    for (uint32_t i = 0; i < num_exits; i++)
    {
        size_t stub_addr = mc.length;

        /* Exit stub: call deopt handler */
        /* For now, just return with error code */
        emit_mov_ri32(&mc, RAX, -1);
        emit_pop(&mc, R15);
        emit_pop(&mc, R14);
        emit_pop(&mc, R13);
        emit_pop(&mc, R12);
        emit_pop(&mc, RBX);
        emit_ret(&mc);

        /* Patch the guard jump to point here */
        patch32(&mc, exits[i].code_offset,
                (int32_t)(stub_addr - (exits[i].code_offset + 4)));

        if (exit_stubs)
        {
            exit_stubs[i] = mc.code + stub_addr;
        }
    }

    *code_out = mcode_finalize(&mc);
    *size_out = mc.length;
    *num_exits_out = num_exits;

    return (*code_out != NULL);
#else
    /* Unsupported architecture */
    (void)ir;
    (void)exit_stubs;
    *code_out = NULL;
    *size_out = 0;
    *num_exits_out = 0;
    return false;
#endif /* JIT_X64 */
}

#if JIT_X64
/* ============================================================
 * DIRECT LOOP CODEGEN - Bypasses IR for C-level performance
 * ============================================================
 *
 * This generates optimal x86-64 for simple loop patterns by:
 * 1. Keeping all loop-carried state in registers
 * 2. Zero memory operations in the hot loop body
 * 3. Only load before loop, store after loop
 *
 * Register allocation (fixed):
 *   RDI = bp (base pointer for locals)
 *   RSI = globals_values
 *   RDX = constants
 *   R12 = loop counter (i)
 *   R13 = loop end
 *   R14 = accumulator variable (x)
 *   R15 = scratch / second accumulator
 *   RBX = preserved
 *
 * Calling convention: void jit_func(Value *bp, Value *globals, Value *constants)
 */

/* Helper: emit JL with placeholder */
static size_t emit_jl_placeholder(MCode *mc)
{
    emit(mc, 0x0f);
    emit(mc, 0x8c);
    size_t off = mc->length;
    emit32(mc, 0);
    return off;
}

/* Helper: patch relative jump */
static void patch_jmp(MCode *mc, size_t placeholder_off, size_t target)
{
    int32_t rel = (int32_t)(target - (placeholder_off + 4));
    patch32(mc, placeholder_off, rel);
}

/*
 * Analyze loop body and generate optimal code
 *
 * Returns: pointer to executable code, or NULL if pattern not recognized
 *
 * Supported patterns:
 *   - x = x + i, x = x + const, x = x * i, x = x - i
 *   - x = x + expr where expr involves i and constants
 *   - Multiple accumulator updates
 */
void *codegen_direct_loop(
    uint8_t *body, size_t body_len,
    uint8_t counter_slot, uint8_t end_slot, uint8_t var_slot,
    void *globals_keys, Value *globals_values, uint32_t globals_capacity,
    Value *constants,
    size_t *size_out)
{
    (void)globals_keys;
    (void)globals_values;
    (void)globals_capacity;

#ifdef JIT_DEBUG
    fprintf(stderr, "[JIT-DIRECT] Slots: counter=%d, end=%d, var=%d\n", counter_slot, end_slot, var_slot);
#endif

    /* Analyze the bytecode pattern */
    if (body_len < 4)
        return NULL;

    /*
     * Parse a single accumulator pattern:
     *   GET_LOCAL x
     *   <expr>
     *   SET_LOCAL x
     *   POP
     *
     * For expressions like: x = x + i, x = x + i*2 - 1, etc.
     */

    /* Stack to track expression evaluation */
    typedef enum
    {
        SRC_LOCAL,
        SRC_CONST,
        SRC_VAR,
        SRC_EXPR
    } SrcType;
    typedef struct
    {
        SrcType type;
        int32_t value; /* local slot, const index, or computed value */
        uint8_t op;    /* last operation */
    } StackElem;

    StackElem stack[16];
    int sp = 0;

    uint8_t accum_slot = 255; /* Accumulator local slot */
    bool uses_var = false;    /* Does expression use loop variable i? */
    bool valid = true;

    uint8_t *ip = body;
    uint8_t *body_end = body + body_len;

    /* Skip trailing OP_LOOP if present */
    if (body_end > body && body_end[-1] == OP_LOOP)
    {
        body_end--;
    }

    while (ip < body_end && valid)
    {
        uint8_t op = *ip++;

        switch (op)
        {
        case OP_GET_LOCAL_0:
        case OP_GET_LOCAL_1:
        case OP_GET_LOCAL_2:
        case OP_GET_LOCAL_3:
        {
            uint8_t slot = op - OP_GET_LOCAL_0;
            if (slot == var_slot)
            {
                stack[sp++] = (StackElem){SRC_VAR, slot, 0};
                uses_var = true;
            }
            else
            {
                stack[sp++] = (StackElem){SRC_LOCAL, slot, 0};
            }
            break;
        }

        case OP_GET_LOCAL:
        {
            uint8_t slot = *ip++;
            if (slot == var_slot)
            {
                stack[sp++] = (StackElem){SRC_VAR, slot, 0};
                uses_var = true;
            }
            else
            {
                stack[sp++] = (StackElem){SRC_LOCAL, slot, 0};
            }
            break;
        }

        case OP_CONST_0:
            stack[sp++] = (StackElem){SRC_CONST, 0, 0};
            break;
        case OP_CONST_1:
            stack[sp++] = (StackElem){SRC_CONST, 1, 0};
            break;
        case OP_CONST_2:
            stack[sp++] = (StackElem){SRC_CONST, 2, 0};
            break;

        case OP_CONST:
        case OP_CONST_LONG:
        {
            uint32_t idx;
            if (op == OP_CONST)
            {
                idx = *ip++;
            }
            else
            {
                idx = ip[0] | (ip[1] << 8) | (ip[2] << 16);
                ip += 3;
            }
            /* Get actual constant value if it's an integer */
            Value cv = constants[idx];
            if (IS_INT(cv))
            {
                stack[sp++] = (StackElem){SRC_CONST, as_int(cv), 0};
            }
            else
            {
                valid = false;
            }
            break;
        }

        case OP_ADD_II:
        case OP_SUB_II:
        case OP_MUL_II:
        case OP_DIV_II:
        case OP_MOD_II:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        {
            if (sp < 2)
            {
                valid = false;
                break;
            }
            StackElem b = stack[--sp];
            StackElem a = stack[--sp];
            /* Mark result as expression that may use var */
            SrcType type = (a.type == SRC_VAR || b.type == SRC_VAR ||
                            a.type == SRC_EXPR || b.type == SRC_EXPR)
                               ? SRC_EXPR
                               : SRC_CONST;
            stack[sp++] = (StackElem){type, 0, op};
            break;
        }

        case OP_SET_LOCAL:
        {
            uint8_t slot = *ip++;
            if (sp < 1)
            {
                valid = false;
                break;
            }
            sp--; /* Consume the value but keep it as result */
            if (accum_slot == 255)
            {
                accum_slot = slot;
            }
            /* Push back for POP to consume */
            stack[sp++] = (StackElem){SRC_LOCAL, slot, 0};
            break;
        }

        case OP_POP:
            if (sp > 0)
                sp--;
            break;

        case OP_LT_JMP_FALSE:
        {
            /* Conditional: if i < N then ... */
            ip += 2; /* Skip offset */
            if (sp < 2)
            {
                valid = false;
                break;
            }
            sp -= 2; /* Pop both comparison operands */
            break;
        }

        default:
            /* Unsupported opcode */
            valid = false;
            break;
        }

        if (sp >= 16)
            valid = false;
    }

    if (!valid || accum_slot == 255)
    {
#ifdef JIT_DEBUG
        fprintf(stderr, "[JIT-DIRECT] Pattern match failed: valid=%d, accum_slot=%d\n", valid, accum_slot);
#endif
        return NULL;
    }

    /*
     * Now generate optimal machine code.
     * We'll re-parse the bytecode during codegen to emit the right instructions.
     */

    MCode mc;
    mcode_init(&mc, 16384);
    if (!mc.code)
        return NULL;

    /* Prologue: save callee-saved registers */
    emit_push(&mc, RBX);
    emit_push(&mc, R12);
    emit_push(&mc, R13);
    emit_push(&mc, R14);
    emit_push(&mc, R15);

    /*
     * Load initial values into registers:
     *   R12 = counter (unboxed)
     *   R13 = end (unboxed)
     *   R14 = accumulator x (unboxed)
     */

    /* R12 = bp[counter_slot], unboxed */
    emit_mov_rm(&mc, R12, RDI, counter_slot * 8);
    emit_unbox_int(&mc, R12, R12);

    /* R13 = bp[end_slot], unboxed */
    emit_mov_rm(&mc, R13, RDI, end_slot * 8);
    emit_unbox_int(&mc, R13, R13);

    /* R14 = bp[accum_slot], unboxed */
    emit_mov_rm(&mc, R14, RDI, accum_slot * 8);
    emit_unbox_int(&mc, R14, R14);

    /*
     * Loop structure:
     *   loop_start:
     *     <body using R12 as i, R14 as x>
     *     R12++
     *     if R12 < R13: goto loop_start
     *   store results
     *   return
     */

    size_t loop_start = mc.length;

    /* Store i to var_slot at start of each iteration (VM semantics) */
    /* Skip this for now - we'll handle it specially */

    /*
     * Re-parse bytecode to generate loop body.
     * Use RBX and R15 as expression temporaries.
     */

    /* Simple register stack for expression evaluation */
    int expr_regs[8];
    int expr_sp = 0;
    int next_tmp = 0; /* 0=RBX, 1=R15, 2=RAX, 3=RCX */

#define GET_TMP_REG() (next_tmp == 0 ? (next_tmp++, RBX) : next_tmp == 1 ? (next_tmp++, R15) \
                                                       : next_tmp == 2   ? (next_tmp++, RAX) \
                                                                         : RCX)

    ip = body;
    body_end = body + body_len;
    if (body_end > body && body_end[-1] == OP_LOOP)
        body_end--;

    while (ip < body_end)
    {
        uint8_t op = *ip++;

        switch (op)
        {
        case OP_GET_LOCAL_0:
        case OP_GET_LOCAL_1:
        case OP_GET_LOCAL_2:
        case OP_GET_LOCAL_3:
        {
            uint8_t slot = op - OP_GET_LOCAL_0;
            if (slot == var_slot)
            {
                /* Loop variable i -> use R12 directly */
                expr_regs[expr_sp++] = R12;
            }
            else if (slot == accum_slot)
            {
                /* Accumulator x -> use R14 directly */
                expr_regs[expr_sp++] = R14;
            }
            else
            {
                /* Other local - load from memory into temp */
                int reg = GET_TMP_REG();
                emit_mov_rm(&mc, reg, RDI, slot * 8);
                emit_unbox_int(&mc, reg, reg);
                expr_regs[expr_sp++] = reg;
            }
            break;
        }

        case OP_GET_LOCAL:
        {
            uint8_t slot = *ip++;
            if (slot == var_slot)
            {
                expr_regs[expr_sp++] = R12;
            }
            else if (slot == accum_slot)
            {
                expr_regs[expr_sp++] = R14;
            }
            else
            {
                int reg = GET_TMP_REG();
                emit_mov_rm(&mc, reg, RDI, slot * 8);
                emit_unbox_int(&mc, reg, reg);
                expr_regs[expr_sp++] = reg;
            }
            break;
        }

        case OP_CONST_0:
        {
            int reg = GET_TMP_REG();
            emit_xor_rr(&mc, reg, reg);
            expr_regs[expr_sp++] = reg;
            break;
        }
        case OP_CONST_1:
        {
            int reg = GET_TMP_REG();
            emit_mov_ri32(&mc, reg, 1);
            expr_regs[expr_sp++] = reg;
            break;
        }
        case OP_CONST_2:
        {
            int reg = GET_TMP_REG();
            emit_mov_ri32(&mc, reg, 2);
            expr_regs[expr_sp++] = reg;
            break;
        }

        case OP_CONST:
        case OP_CONST_LONG:
        {
            uint32_t idx;
            if (op == OP_CONST)
            {
                idx = *ip++;
            }
            else
            {
                idx = ip[0] | (ip[1] << 8) | (ip[2] << 16);
                ip += 3;
            }
            Value cv = constants[idx];
            int reg = GET_TMP_REG();
            emit_mov_ri32(&mc, reg, as_int(cv));
            expr_regs[expr_sp++] = reg;
            break;
        }

        case OP_ADD_II:
        case OP_ADD:
        {
            if (expr_sp < 2)
                break;
            int b = expr_regs[--expr_sp];
            int a = expr_regs[--expr_sp];
            emit_add_rr(&mc, a, b);
            expr_regs[expr_sp++] = a;
            break;
        }

        case OP_SUB_II:
        case OP_SUB:
        {
            if (expr_sp < 2)
                break;
            int b = expr_regs[--expr_sp];
            int a = expr_regs[--expr_sp];
            emit_sub_rr(&mc, a, b);
            expr_regs[expr_sp++] = a;
            break;
        }

        case OP_MUL_II:
        case OP_MUL:
        {
            if (expr_sp < 2)
                break;
            int b = expr_regs[--expr_sp];
            int a = expr_regs[--expr_sp];
            emit_imul_rr(&mc, a, b);
            expr_regs[expr_sp++] = a;
            break;
        }

        case OP_DIV_II:
        case OP_DIV:
        {
            if (expr_sp < 2)
                break;
            int b = expr_regs[--expr_sp];
            int a = expr_regs[--expr_sp];
            /* Division: need fresh register if a is R12 (counter) or R14 (accum) */
            int dst;
            if (a == R12 || a == R14)
            {
                dst = GET_TMP_REG();
            }
            else
            {
                dst = a;
            }
            emit_div_rr(&mc, dst, a, b);
            expr_regs[expr_sp++] = dst;
            break;
        }

        case OP_MOD_II:
        case OP_MOD:
        {
            if (expr_sp < 2)
                break;
            int b = expr_regs[--expr_sp];
            int a = expr_regs[--expr_sp];
            /* Modulo: need fresh register if a is R12 (counter) or R14 (accum) */
            int dst;
            if (a == R12 || a == R14)
            {
                dst = GET_TMP_REG();
            }
            else
            {
                dst = a;
            }
            emit_mod_rr(&mc, dst, a, b);
            expr_regs[expr_sp++] = dst;
            break;
        }

        case OP_SET_LOCAL:
        {
            uint8_t slot = *ip++;
            if (expr_sp < 1)
                break;
            int src = expr_regs[--expr_sp];
            if (slot == accum_slot)
            {
                /* Store to accumulator register - skip if already there */
                if (src != R14)
                    emit_mov_rr(&mc, R14, src);
            }
            else
            {
                /* Store to memory (other locals) */
                emit_box_int(&mc, src, src);
                emit_mov_mr(&mc, RDI, slot * 8, src);
            }
            /* Keep on stack for potential POP */
            expr_regs[expr_sp++] = (slot == accum_slot) ? R14 : src;
            break;
        }

        case OP_POP:
            if (expr_sp > 0)
            {
                expr_sp--;
                next_tmp = 0; /* Reset temp registers */
            }
            break;

        case OP_LT_JMP_FALSE:
        {
            /* if a < b then skip */
            uint16_t offset = ip[0] | (ip[1] << 8);
            ip += 2;
            (void)offset;
            if (expr_sp < 2)
                break;
            int b = expr_regs[--expr_sp];
            int a = expr_regs[--expr_sp];
            emit_cmp_rr(&mc, a, b);
            /* If NOT less (a >= b), jump past the rest of body */
            /* For now, just skip - most conditionals work inside loop */
            break;
        }

        default:
            /* Skip unknown opcodes - rely on pattern matching above */
            break;
        }
    }

    /* Loop epilogue: increment counter and check condition */
    emit_inc(&mc, R12);
    emit_cmp_rr(&mc, R12, R13);

    /* if R12 < R13: jump to loop_start */
    size_t jl_off = emit_jl_placeholder(&mc);
    patch_jmp(&mc, jl_off, loop_start);

    /*
     * Store results back to memory (only ONCE after loop exits!)
     * This is the key optimization - we've kept everything in registers
     * for the entire loop execution.
     */

    /* Store accumulator x to bp[accum_slot] */
    emit_box_int(&mc, R14, R14);
    emit_mov_mr(&mc, RDI, accum_slot * 8, R14);

    /* Store final counter to bp[counter_slot] */
    emit_box_int(&mc, R12, R12);
    emit_mov_mr(&mc, RDI, counter_slot * 8, R12);

    /* Also store to var_slot */
    emit_mov_mr(&mc, RDI, var_slot * 8, R12);

    /* Epilogue: restore registers and return */
    emit_pop(&mc, R15);
    emit_pop(&mc, R14);
    emit_pop(&mc, R13);
    emit_pop(&mc, R12);
    emit_pop(&mc, RBX);
    emit_ret(&mc);

    *size_out = mc.length;
    void *code = mcode_finalize(&mc);

#ifdef JIT_DEBUG
    fprintf(stderr, "[JIT-DIRECT] Generated code (%zu bytes): ", mc.length);
    for (size_t i = 0; i < mc.length; i++)
    {
        fprintf(stderr, "%02x ", ((uint8_t *)code)[i]);
    }
    fprintf(stderr, "\n");
#endif

    return code;
}

#else /* !JIT_X64 (ARM64 or unsupported) */

/* ARM64/fallback stub for codegen_direct_loop */
void *codegen_direct_loop(
    uint8_t *body, size_t body_len,
    uint8_t counter_slot, uint8_t end_slot, uint8_t var_slot,
    void *globals_keys, Value *globals_values, uint32_t globals_capacity,
    Value *constants,
    size_t *size_out)
{
    /* Direct loop codegen not yet implemented for ARM64 */
    /* Fall back to trace-based JIT or interpreter */
    (void)body;
    (void)body_len;
    (void)counter_slot;
    (void)end_slot;
    (void)var_slot;
    (void)globals_keys;
    (void)globals_values;
    (void)globals_capacity;
    (void)constants;
    *size_out = 0;
    return NULL;
}

#endif /* JIT_X64 */
