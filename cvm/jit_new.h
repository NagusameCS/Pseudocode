/*
 * Pseudocode Tracing JIT Compiler
 *
 * A LuaJIT-style tracing JIT that:
 * 1. Detects hot loops via counters
 * 2. Records traces (bytecode sequences)
 * 3. Lowers traces to SSA IR
 * 4. Compiles IR to native x86-64
 * 5. Executes with deoptimization support
 *
 * Target: 98% of C speed
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#ifndef PSEUDO_JIT_H
#define PSEUDO_JIT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "trace_ir.h"

/* Forward declarations */
typedef uint64_t Value;

/* ============================================================
 * JIT Configuration
 * ============================================================ */

#define JIT_HOTLOOP_THRESHOLD 50 /* Iterations before recording */
#define JIT_HOTSIDE_THRESHOLD 10 /* Side exit hits before retrace */
#define JIT_MAX_TRACE_LENGTH 512 /* Max bytecodes per trace */
#define JIT_MAX_TRACES 128       /* Max compiled traces */
#define JIT_CODE_SIZE 16384      /* Code buffer per trace */
#define HOTLOOP_TABLE_SIZE 256   /* Hash table size */
#define JIT_MAX_INLINE_SIZE 32   /* Max bytecodes to inline */
#define JIT_DEOPT_LIMIT 5        /* Deopts before retrace */

/* ============================================================
 * Compiled Trace
 *
 * A fully compiled trace with native code and metadata.
 * ============================================================ */

typedef struct CompiledTrace
{
    /* Identification */
    uint32_t id;
    uint8_t *entry_pc; /* Bytecode entry point */

    /* Type signature for polymorphic dispatch */
    IRType entry_types[16];
    uint8_t num_entry_types;

    /* Compiled IR and native code */
    TraceIR ir;        /* SSA IR representation */
    void *native_code; /* Executable machine code */
    size_t code_size;

    /* Side exits with native stubs */
    uint8_t *exit_stubs[IR_MAX_EXITS];
    uint32_t num_exits;

    /* Trace linking */
    struct CompiledTrace *parent;    /* Parent trace (if side trace) */
    struct CompiledTrace *linked[8]; /* Linked traces from exits */
    uint32_t num_linked;

    /* Statistics */
    uint64_t executions;
    uint64_t bailouts;
    uint64_t compile_time_ns;

    /* State */
    bool is_compiled;
    bool is_valid; /* False if invalidated */

} CompiledTrace;

/* ============================================================
 * Hot Loop Entry
 *
 * Hash table entry for hot loop detection.
 * ============================================================ */

typedef struct
{
    uint8_t *ip;        /* Loop header bytecode address */
    uint32_t count;     /* Execution count */
    int32_t trace_idx;  /* Index of compiled trace, or -1 */
    uint32_t type_hash; /* Hash of entry types for polymorphic */
} HotLoopEntry;

/* ============================================================
 * Hot Side Exit
 *
 * Tracks side exits that become hot enough for retracing.
 * ============================================================ */

typedef struct
{
    uint32_t trace_idx;  /* Parent trace */
    uint32_t exit_idx;   /* Exit index in parent */
    uint32_t count;      /* Exit count */
    int32_t child_trace; /* Child trace if compiled, or -1 */
} HotExitEntry;

/* ============================================================
 * JIT State
 *
 * Global state for the tracing JIT.
 * ============================================================ */

typedef struct
{
    /* Hot loop detection */
    HotLoopEntry hotloops[HOTLOOP_TABLE_SIZE];

    /* Hot side exit detection */
    HotExitEntry hotexits[HOTLOOP_TABLE_SIZE];
    uint32_t num_hotexits;

    /* Compiled traces */
    CompiledTrace traces[JIT_MAX_TRACES];
    uint32_t num_traces;

    /* Trace recorder (active during recording) */
    TraceRecorder recorder;
    TraceIR recording_ir;
    bool is_recording;

    /* Deoptimization state */
    uint8_t *deopt_pc;  /* PC to resume after deopt */
    Value *deopt_bp;    /* Base pointer after deopt */
    bool deopt_pending; /* Deopt needs processing */

    /* Coverage tracking */
    uint64_t bytecodes_jit;    /* Bytecodes executed in JIT */
    uint64_t bytecodes_interp; /* Bytecodes executed in interpreter */

    /* Statistics */
    uint64_t total_compilations;
    uint64_t total_recordings;
    uint64_t total_aborts;
    uint64_t total_executions;
    uint64_t total_bailouts;
    uint64_t total_side_traces;

    /* Configuration */
    bool enabled;
    bool debug;
    bool trace_recording; /* Print during recording */
    bool trace_codegen;   /* Print during codegen */

} JitState;

/* Global JIT state */
extern JitState jit;

/* ============================================================
 * JIT Initialization
 * ============================================================ */

/* Initialize JIT subsystem */
void jit_init(void);

/* Cleanup JIT subsystem */
void jit_cleanup(void);

/* Check if JIT is available */
int jit_available(void);

/* ============================================================
 * Hot Loop Detection
 * ============================================================ */

/* Check if a loop has a compiled trace */
/* Returns trace index if found, -1 otherwise */
int jit_check_hotloop(uint8_t *loop_header);

/* Count a loop iteration */
/* Returns true if loop became hot and should be recorded */
bool jit_count_loop(uint8_t *loop_header);

/* ============================================================
 * Trace Recording
 * ============================================================ */

/* Start recording a trace at the given PC */
void jit_start_recording(uint8_t *pc, Value *bp);

/* Record one bytecode instruction */
/* Returns: true = continue recording, false = stop */
bool jit_record_instruction(uint8_t *pc, Value *bp, Value *constants);

/* Finish recording and compile the trace */
/* Returns trace index on success, -1 on failure */
int jit_finish_recording(void);

/* Abort recording */
void jit_abort_recording(const char *reason);

/* Check if currently recording */
bool jit_is_recording(void);

/* ============================================================
 * Trace Compilation
 * ============================================================ */

/* Compile a trace from IR to native code */
/* Returns true on success */
bool jit_compile_trace(CompiledTrace *trace);

/* Compile a side exit trace */
int jit_compile_side_trace(uint8_t *exit_pc, Value *bp,
                           CompiledTrace *parent, uint32_t exit_idx);

/* ============================================================
 * Trace Execution
 * ============================================================ */

/* Execute a compiled trace */
/* bp = base pointer (locals) */
/* Returns: number of iterations executed, or -1 on deopt */
int64_t jit_execute_trace(int trace_idx, Value *bp);

/* Handle a side exit */
/* Called from native code when a guard fails */
void jit_side_exit(uint32_t trace_idx, uint32_t exit_idx,
                   Value *bp, void *native_state);

/* ============================================================
 * Deoptimization
 * ============================================================ */

/* Perform deoptimization from a guard failure */
/* Reconstructs interpreter state from snapshot */
void jit_deoptimize(CompiledTrace *trace, uint32_t snapshot_idx,
                    Value *bp, void *native_state);

/* Check if deopt is pending and handle it */
bool jit_check_deopt(uint8_t **pc, Value **bp);

/* ============================================================
 * On-Stack Replacement (OSR)
 * ============================================================ */

/* Check if we can OSR into a trace mid-loop */
int jit_check_osr(uint8_t *pc, Value *bp);

/* Perform OSR entry into a trace */
void jit_osr_enter(int trace_idx, Value *bp, uint8_t *pc);

/* ============================================================
 * Trace Cache & Linking
 * ============================================================ */

/* Find a trace by entry PC and type signature */
int jit_find_trace(uint8_t *pc, IRType *types, int ntypes);

/* Link two traces (parent exit -> child entry) */
void jit_link_traces(CompiledTrace *parent, uint32_t exit_idx,
                     CompiledTrace *child);

/* Invalidate a trace (e.g., on code modification) */
void jit_invalidate_trace(int trace_idx);

/* ============================================================
 * Statistics & Debugging
 * ============================================================ */

/* Print JIT statistics */
void jit_print_stats(void);

/* Get coverage percentage */
double jit_coverage(void);

/* Dump trace IR (for debugging) */
void jit_dump_ir(CompiledTrace *trace);

/* Dump native code (for debugging) */
void jit_dump_native(CompiledTrace *trace);

/* ============================================================
 * Legacy API (for compatibility)
 * ============================================================ */

int jit_compile_loop(uint8_t *loop_start, uint8_t *loop_end,
                     Value *bp, Value *constants, uint32_t num_constants);
int64_t jit_execute_loop(int trace_idx, Value *bp, int64_t iterations);

/* Intrinsic JIT functions */
int64_t jit_run_inc_loop(int64_t x, int64_t iterations);
int64_t jit_run_empty_loop(int64_t start, int64_t end);
int64_t jit_run_arith_loop(int64_t x, int64_t iterations);
int64_t jit_run_branch_loop(int64_t x, int64_t iterations);

#endif /* PSEUDO_JIT_H */
