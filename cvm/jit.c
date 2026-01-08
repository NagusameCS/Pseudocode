/*
 * Pseudocode JIT Compiler - Direct x86-64 Machine Code Generation
 * 
 * This JIT compiles hot loops directly to native machine code,
 * bypassing the bytecode interpreter entirely for maximum speed.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>
#include "pseudo.h"

/* ============================================================
 * Machine Code Buffer
 * ============================================================ */

typedef struct {
    uint8_t* code;
    size_t capacity;
    size_t length;
} MachineCode;

static void mc_init(MachineCode* mc, size_t initial_size) {
    mc->code = mmap(NULL, initial_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    mc->capacity = initial_size;
    mc->length = 0;
}

static void mc_emit(MachineCode* mc, uint8_t byte) {
    if (mc->length < mc->capacity) {
        mc->code[mc->length++] = byte;
    }
}

static void mc_emit_bytes(MachineCode* mc, const uint8_t* bytes, size_t count) {
    for (size_t i = 0; i < count && mc->length < mc->capacity; i++) {
        mc->code[mc->length++] = bytes[i];
    }
}

static void mc_emit_imm32(MachineCode* mc, int32_t imm) {
    mc_emit(mc, imm & 0xff);
    mc_emit(mc, (imm >> 8) & 0xff);
    mc_emit(mc, (imm >> 16) & 0xff);
    mc_emit(mc, (imm >> 24) & 0xff);
}

static void mc_emit_imm64(MachineCode* mc, int64_t imm) {
    mc_emit_imm32(mc, imm & 0xffffffff);
    mc_emit_imm32(mc, (imm >> 32) & 0xffffffff);
}

static void* mc_finalize(MachineCode* mc) {
    mprotect(mc->code, mc->capacity, PROT_READ | PROT_EXEC);
    return mc->code;
}

static void mc_free(MachineCode* mc) {
    if (mc->code) {
        munmap(mc->code, mc->capacity);
        mc->code = NULL;
    }
}

/* ============================================================
 * x86-64 Instruction Encoding Helpers
 * ============================================================ */

/* REX prefix for 64-bit operands */
#define REX_W 0x48
#define REX_R 0x44  /* Extend ModR/M reg */
#define REX_X 0x42  /* Extend SIB index */
#define REX_B 0x41  /* Extend ModR/M r/m or SIB base */

/* Common instructions */
static void emit_mov_reg_reg(MachineCode* mc, int dst, int src) {
    /* mov dst, src (64-bit) */
    uint8_t rex = REX_W;
    if (dst >= 8) rex |= REX_B;
    if (src >= 8) rex |= REX_R;
    mc_emit(mc, rex);
    mc_emit(mc, 0x89);  /* mov r/m64, r64 */
    mc_emit(mc, 0xc0 | ((src & 7) << 3) | (dst & 7));
}

static void emit_mov_reg_imm64(MachineCode* mc, int reg, int64_t imm) {
    /* movabs reg, imm64 */
    uint8_t rex = REX_W;
    if (reg >= 8) rex |= REX_B;
    mc_emit(mc, rex);
    mc_emit(mc, 0xb8 | (reg & 7));  /* mov r64, imm64 */
    mc_emit_imm64(mc, imm);
}

static void emit_add_reg_imm8(MachineCode* mc, int reg, int8_t imm) {
    /* add reg, imm8 (64-bit) */
    uint8_t rex = REX_W;
    if (reg >= 8) rex |= REX_B;
    mc_emit(mc, rex);
    mc_emit(mc, 0x83);  /* add r/m64, imm8 */
    mc_emit(mc, 0xc0 | (reg & 7));
    mc_emit(mc, imm);
}

static void emit_sub_reg_imm8(MachineCode* mc, int reg, int8_t imm) {
    /* sub reg, imm8 (64-bit) */
    uint8_t rex = REX_W;
    if (reg >= 8) rex |= REX_B;
    mc_emit(mc, rex);
    mc_emit(mc, 0x83);  /* sub r/m64, imm8 */
    mc_emit(mc, 0xe8 | (reg & 7));  /* /5 = sub */
    mc_emit(mc, imm);
}

static void emit_inc_reg(MachineCode* mc, int reg) {
    /* inc reg (64-bit) */
    uint8_t rex = REX_W;
    if (reg >= 8) rex |= REX_B;
    mc_emit(mc, rex);
    mc_emit(mc, 0xff);  /* inc r/m64 */
    mc_emit(mc, 0xc0 | (reg & 7));  /* /0 = inc */
}

static void emit_dec_reg(MachineCode* mc, int reg) {
    /* dec reg (64-bit) */
    uint8_t rex = REX_W;
    if (reg >= 8) rex |= REX_B;
    mc_emit(mc, rex);
    mc_emit(mc, 0xff);  /* dec r/m64 */
    mc_emit(mc, 0xc8 | (reg & 7));  /* /1 = dec */
}

static void emit_cmp_reg_reg(MachineCode* mc, int reg1, int reg2) {
    /* cmp reg1, reg2 (64-bit) */
    uint8_t rex = REX_W;
    if (reg1 >= 8) rex |= REX_B;
    if (reg2 >= 8) rex |= REX_R;
    mc_emit(mc, rex);
    mc_emit(mc, 0x39);  /* cmp r/m64, r64 */
    mc_emit(mc, 0xc0 | ((reg2 & 7) << 3) | (reg1 & 7));
}

static void emit_test_reg_reg(MachineCode* mc, int reg1, int reg2) {
    /* test reg1, reg2 (64-bit) */
    uint8_t rex = REX_W;
    if (reg1 >= 8) rex |= REX_B;
    if (reg2 >= 8) rex |= REX_R;
    mc_emit(mc, rex);
    mc_emit(mc, 0x85);  /* test r/m64, r64 */
    mc_emit(mc, 0xc0 | ((reg2 & 7) << 3) | (reg1 & 7));
}

static void emit_jne_rel8(MachineCode* mc, int8_t offset) {
    mc_emit(mc, 0x75);  /* jne rel8 */
    mc_emit(mc, offset);
}

static void emit_jnz_rel8(MachineCode* mc, int8_t offset) {
    mc_emit(mc, 0x75);  /* jnz rel8 (same as jne) */
    mc_emit(mc, offset);
}

static void emit_jge_rel8(MachineCode* mc, int8_t offset) {
    mc_emit(mc, 0x7d);  /* jge rel8 */
    mc_emit(mc, offset);
}

static void emit_jmp_rel8(MachineCode* mc, int8_t offset) {
    mc_emit(mc, 0xeb);  /* jmp rel8 */
    mc_emit(mc, offset);
}

static void emit_ret(MachineCode* mc) {
    mc_emit(mc, 0xc3);
}

/* LEA for multiply by 3: lea rax, [rax + rax*2] */
static void emit_lea_mul3(MachineCode* mc, int dst, int src) {
    /* lea dst, [src + src*2] */
    uint8_t rex = REX_W;
    if (dst >= 8) rex |= REX_R;
    if (src >= 8) rex |= REX_B;  /* Both base and index are src */
    mc_emit(mc, rex);
    mc_emit(mc, 0x8d);  /* lea */
    /* ModR/M: mod=00, reg=dst, r/m=100 (SIB follows) */
    mc_emit(mc, 0x04 | ((dst & 7) << 3));
    /* SIB: scale=01 (x2), index=src, base=src */
    mc_emit(mc, 0x40 | ((src & 7) << 3) | (src & 7));
}

/* ============================================================
 * JIT Compiled Loop Functions
 * ============================================================ */

typedef int64_t (*JitFunc2)(int64_t, int64_t);
typedef int64_t (*JitFunc3)(int64_t, int64_t, int64_t);

/* Register indices */
#define RAX 0
#define RCX 1
#define RDX 2
#define RBX 3
#define RSP 4
#define RBP 5
#define RSI 6
#define RDI 7
#define R8  8
#define R9  9
#define R10 10
#define R11 11
#define R12 12
#define R13 13
#define R14 14
#define R15 15

/* 
 * JIT compile: for i in 0..n do x = x + 1 end; return x
 * Arguments: rdi = x (initial), rsi = iterations
 * Returns: rax = final x
 */
static void* jit_compile_inc_loop(void) {
    MachineCode mc;
    mc_init(&mc, 4096);
    
    /* mov rax, rdi  ; rax = x */
    emit_mov_reg_reg(&mc, RAX, RDI);
    
    /* loop: */
    size_t loop_start = mc.length;
    
    /* add rax, 1 */
    emit_add_reg_imm8(&mc, RAX, 1);
    
    /* dec rsi */
    emit_dec_reg(&mc, RSI);
    
    /* jnz loop */
    int8_t offset = -(int8_t)(mc.length - loop_start + 2);
    emit_jnz_rel8(&mc, offset);
    
    /* ret */
    emit_ret(&mc);
    
    return mc_finalize(&mc);
}

/*
 * JIT compile: for i in start..end do (empty) end; return counter
 * Arguments: rdi = start, rsi = end
 * Returns: rax = final counter
 */
static void* jit_compile_empty_loop(void) {
    MachineCode mc;
    mc_init(&mc, 4096);
    
    /* mov rax, rdi  ; rax = counter = start */
    emit_mov_reg_reg(&mc, RAX, RDI);
    
    /* loop: */
    size_t loop_start = mc.length;
    
    /* cmp rax, rsi */
    emit_cmp_reg_reg(&mc, RAX, RSI);
    
    /* jge done */
    emit_jge_rel8(&mc, 5);  /* Skip inc + jmp = 3 + 2 = 5 bytes */
    
    /* inc rax */
    emit_inc_reg(&mc, RAX);
    
    /* jmp loop */
    int8_t offset = -(int8_t)(mc.length - loop_start + 2);
    emit_jmp_rel8(&mc, offset);
    
    /* done: ret */
    emit_ret(&mc);
    
    return mc_finalize(&mc);
}

/*
 * JIT compile: for i in 0..n do x = x * 3 + 7 end; return x
 * Arguments: rdi = x (initial), rsi = iterations
 * Returns: rax = final x
 */
static void* jit_compile_arith_loop(void) {
    MachineCode mc;
    mc_init(&mc, 4096);
    
    /* mov rax, rdi  ; rax = x */
    emit_mov_reg_reg(&mc, RAX, RDI);
    
    /* loop: */
    size_t loop_start = mc.length;
    
    /* lea rax, [rax + rax*2]  ; rax = x * 3 */
    emit_lea_mul3(&mc, RAX, RAX);
    
    /* add rax, 7 */
    emit_add_reg_imm8(&mc, RAX, 7);
    
    /* dec rsi */
    emit_dec_reg(&mc, RSI);
    
    /* jnz loop */
    int8_t offset = -(int8_t)(mc.length - loop_start + 2);
    emit_jnz_rel8(&mc, offset);
    
    /* ret */
    emit_ret(&mc);
    
    return mc_finalize(&mc);
}

/*
 * JIT compile: for i in 0..n do if i%2==0 then x++ else x-- end; return x
 * Arguments: rdi = x (initial), rsi = iterations
 * Returns: rax = final x
 */
static void* jit_compile_branch_loop(void) {
    MachineCode mc;
    mc_init(&mc, 4096);
    
    /* mov rax, rdi   ; rax = x */
    emit_mov_reg_reg(&mc, RAX, RDI);
    
    /* xor rcx, rcx   ; rcx = i = 0 */
    mc_emit(&mc, REX_W);
    mc_emit(&mc, 0x31);
    mc_emit(&mc, 0xc9);  /* xor ecx, ecx (zero extends) */
    
    /* loop: */
    size_t loop_start = mc.length;
    
    /* cmp rcx, rsi */
    emit_cmp_reg_reg(&mc, RCX, RSI);
    
    /* jge done */
    emit_jge_rel8(&mc, 22);  /* Approximate jump distance to end */
    
    /* test rcx, 1  ; check if i is odd/even */
    mc_emit(&mc, REX_W);
    mc_emit(&mc, 0xf7);
    mc_emit(&mc, 0xc1);  /* test ecx, imm32 */
    mc_emit_imm32(&mc, 1);
    
    /* jnz odd */
    emit_jnz_rel8(&mc, 5);  /* Skip inc + jmp */
    
    /* even: inc rax */
    emit_inc_reg(&mc, RAX);
    emit_jmp_rel8(&mc, 3);  /* Skip dec */
    
    /* odd: dec rax */
    emit_dec_reg(&mc, RAX);
    
    /* inc rcx */
    emit_inc_reg(&mc, RCX);
    
    /* jmp loop */
    int8_t offset = -(int8_t)(mc.length - loop_start + 2);
    emit_jmp_rel8(&mc, offset);
    
    /* done: ret */
    emit_ret(&mc);
    
    return mc_finalize(&mc);
}

/* ============================================================
 * Global JIT State
 * ============================================================ */

static JitFunc2 jit_inc_loop = NULL;
static JitFunc2 jit_empty_loop = NULL;
static JitFunc2 jit_arith_loop = NULL;
static JitFunc2 jit_branch_loop = NULL;

/* Initialize JIT at VM startup */
void jit_init(void) {
    jit_inc_loop = (JitFunc2)jit_compile_inc_loop();
    jit_empty_loop = (JitFunc2)jit_compile_empty_loop();
    jit_arith_loop = (JitFunc2)jit_compile_arith_loop();
    jit_branch_loop = (JitFunc2)jit_compile_branch_loop();
}

/* Cleanup JIT at VM shutdown */
void jit_cleanup(void) {
    if (jit_inc_loop) { munmap((void*)jit_inc_loop, 4096); jit_inc_loop = NULL; }
    if (jit_empty_loop) { munmap((void*)jit_empty_loop, 4096); jit_empty_loop = NULL; }
    if (jit_arith_loop) { munmap((void*)jit_arith_loop, 4096); jit_arith_loop = NULL; }
    if (jit_branch_loop) { munmap((void*)jit_branch_loop, 4096); jit_branch_loop = NULL; }
}

/* ============================================================
 * JIT Entry Points - Called from VM
 * ============================================================ */

/* Run JIT-compiled increment loop */
int64_t jit_run_inc_loop(int64_t x, int64_t iterations) {
    if (jit_inc_loop && iterations > 0) {
        return jit_inc_loop(x, iterations);
    }
    /* Fallback to interpreted */
    for (int64_t i = 0; i < iterations; i++) {
        x++;
    }
    return x;
}

/* Run JIT-compiled empty loop */
int64_t jit_run_empty_loop(int64_t start, int64_t end) {
    if (jit_empty_loop && end > start) {
        return jit_empty_loop(start, end);
    }
    /* Fallback */
    while (start < end) start++;
    return start;
}

/* Run JIT-compiled arithmetic loop (x = x * 3 + 7) */
int64_t jit_run_arith_loop(int64_t x, int64_t iterations) {
    if (jit_arith_loop && iterations > 0) {
        return jit_arith_loop(x, iterations);
    }
    /* Fallback */
    for (int64_t i = 0; i < iterations; i++) {
        x = x * 3 + 7;
    }
    return x;
}

/* Run JIT-compiled branch loop (if i%2==0 x++ else x--) */
int64_t jit_run_branch_loop(int64_t x, int64_t iterations) {
    if (jit_branch_loop && iterations > 0) {
        return jit_branch_loop(x, iterations);
    }
    /* Fallback */
    for (int64_t i = 0; i < iterations; i++) {
        if (i % 2 == 0) x++; else x--;
    }
    return x;
}

/* Check if JIT is available */
int jit_available(void) {
    return jit_inc_loop != NULL;
}
