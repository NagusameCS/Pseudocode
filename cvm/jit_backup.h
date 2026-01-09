/*
 * Pseudocode Tracing JIT Compiler
 *
 * Full tracing JIT that automatically detects hot loops,
 * records traces, and compiles them to native x86-64.
 *
 * Target: 98% of C speed
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#ifndef PSEUDO_JIT_H
#define PSEUDO_JIT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward declarations */
typedef uint64_t Value;

/* ============================================================
 * Tracing JIT Configuration - Compact sizes to avoid large globals
 * ============================================================ */

#define JIT_HOTLOOP_THRESHOLD 50 /* Iterations before compiling */
#define JIT_MAX_TRACE_LENGTH 64  /* Max bytecodes in a trace */
#define JIT_MAX_TRACES 16        /* Max compiled traces */
#define JIT_CODE_SIZE 4096       /* Code buffer per trace */
#define HOTLOOP_TABLE_SIZE 64    /* Hash table for hot loops */

/* ============================================================
 * Trace Recording
 * ============================================================ */

/* Types for trace specialization */
typedef enum
{
    TRACE_TYPE_UNKNOWN = 0,
    TRACE_TYPE_INT32 = 1,  /* 32-bit integer */
    TRACE_TYPE_INT64 = 2,  /* 64-bit integer */
    TRACE_TYPE_DOUBLE = 3, /* Double precision float */
    TRACE_TYPE_BOOL = 4,   /* Boolean */
} TraceType;

/* Recorded operation in a trace */
typedef struct
{
    uint8_t opcode;
    uint8_t arg1;
    uint8_t arg2;
    uint8_t arg3;
    int32_t imm32;              /* Immediate value if applicable */
    TraceType operand_types[2]; /* Types of operands (for specialization) */
} TraceOp;

/* A recorded trace ready for compilation */
typedef struct
{
    uint8_t *loop_header; /* Bytecode address of loop start */
    uint8_t *loop_end;    /* Bytecode address after loop */
    TraceOp ops[JIT_MAX_TRACE_LENGTH];
    uint32_t length; /* Number of recorded ops */

    /* Type information for locals used in trace */
    TraceType local_types[16]; /* Inferred types for first 16 locals */
    uint8_t num_locals;        /* Number of locals used */

    /* Compiled code */
    void *native_code; /* Compiled native function */
    size_t code_size;
    bool is_compiled;

    /* Loop metadata */
    uint8_t counter_slot; /* Local slot for loop counter */
    uint8_t end_slot;     /* Local slot for loop end */
    uint8_t body_slot;    /* Primary working variable slot */
    bool is_int_loop;     /* True if all ops are integer-only */

    /* Statistics */
    uint64_t executions;
    uint64_t compile_time_ns;
} Trace;

/* ============================================================
 * Hot Loop Detection
 * ============================================================ */

typedef struct
{
    uint8_t *ip;       /* Loop header IP */
    uint32_t count;    /* Execution count */
    int32_t trace_idx; /* Index into trace table, or -1 */
} HotLoopEntry;

/* ============================================================
 * JIT Compiler State
 * ============================================================ */

typedef struct
{
    /* Hot loop detection */
    HotLoopEntry hotloops[HOTLOOP_TABLE_SIZE];

    /* Compiled traces */
    Trace traces[JIT_MAX_TRACES];
    uint32_t num_traces;

    /* Current trace being recorded */
    Trace *recording_trace;
    bool is_recording;
    uint8_t *recording_start;
    uint8_t *recording_ip; /* Current IP during recording */

    /* Value stack snapshot at trace start (for type inference) */
    Value *trace_bp;

    /* Statistics */
    uint64_t total_compilations;
    uint64_t total_native_calls;
    uint64_t total_bailouts;
    uint64_t total_iterations_jit;
    uint64_t total_iterations_interp;

    /* Enabled flag */
    bool enabled;
    bool debug;
} JitState;

/* Global JIT state */
extern JitState jit_state;

/* ============================================================
 * JIT API
 * ============================================================ */

/* Initialize JIT compiler */
void jit_init(void);

/* Cleanup JIT compiler */
void jit_cleanup(void);

/* Check if JIT is available */
int jit_available(void);

/* Check if a loop has been compiled */
/* Returns trace index if compiled, -1 otherwise */
int jit_check_hotloop(uint8_t *loop_header);

/* Count a loop iteration, returns true if should compile */
bool jit_count_loop(uint8_t *loop_header);

/* Compile a loop to native code */
/* Returns trace index on success, -1 on failure */
int jit_compile_loop(uint8_t *loop_start, uint8_t *loop_end,
                     Value *bp, Value *constants, uint32_t num_constants);

/* Execute a compiled trace */
/* bp = base pointer (locals), slot = primary result slot */
/* Returns: iterations executed by JIT, updates bp[slot] */
int64_t jit_execute_loop(int trace_idx, Value *bp, int64_t iterations);

/* Specialized loop runners (direct native code) */
typedef int64_t (*JitLoopFunc)(int64_t *locals);

/* ============================================================
 * Legacy JIT Functions (for explicit intrinsics)
 * ============================================================ */

int64_t jit_run_inc_loop(int64_t x, int64_t iterations);
int64_t jit_run_empty_loop(int64_t start, int64_t end);
int64_t jit_run_arith_loop(int64_t x, int64_t iterations);
int64_t jit_run_branch_loop(int64_t x, int64_t iterations);

/* ============================================================
 * JIT Statistics
 * ============================================================ */

void jit_print_stats(void);

#endif /* PSEUDO_JIT_H */
