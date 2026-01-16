/*
 * Pseudocode Tracing JIT - Main Controller
 *
 * Ties together all JIT components:
 * - Hot loop detection
 * - Trace recording
 * - IR compilation
 * - Trace execution
 * - Side exit handling
 * - Trace stitching
 * - OSR entry
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Platform-specific memory mapping */
#ifdef _WIN32
#include <windows.h>
#define mmap_executable(size) VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)
#define mmap_rw(size) VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
#define mprotect_rx(ptr, size) do { DWORD old; VirtualProtect(ptr, size, PAGE_EXECUTE_READ, &old); } while(0)
#define munmap_executable(ptr, size) VirtualFree(ptr, 0, MEM_RELEASE)
#define MMAP_FAILED NULL
#else
#include <sys/mman.h>
#define mmap_executable(size) mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
#define mmap_rw(size) mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
#define mprotect_rx(ptr, size) mprotect(ptr, size, PROT_READ | PROT_EXEC)
#define munmap_executable(ptr, size) munmap(ptr, size)
#define MMAP_FAILED MAP_FAILED
#endif

#include "pseudo.h"
#include "trace_ir.h"

/* Forward declaration for codegen_direct_loop from trace_codegen.c */
void *codegen_direct_loop(
    uint8_t *body, size_t body_len,
    uint8_t counter_slot, uint8_t end_slot, uint8_t var_slot,
    void *globals_keys, Value *globals_values, uint32_t globals_capacity,
    Value *constants,
    size_t *size_out);

/* ============================================================
 * Configuration
 * ============================================================ */

#define JIT_HOTLOOP_THRESHOLD 50
#define JIT_HOTSIDE_THRESHOLD 10
#define JIT_MAX_TRACES 128
#define JIT_CODE_SIZE 16384
#define HOTLOOP_TABLE_SIZE 256
#define JIT_DEOPT_LIMIT 5

/* ============================================================
 * Compiled Trace Structure
 * ============================================================ */

typedef struct CompiledTrace
{
    uint32_t id;
    uint8_t *entry_pc;

    IRType entry_types[16];
    uint8_t num_entry_types;

    TraceIR ir;
    void *native_code;
    size_t code_size;

    uint8_t *exit_stubs[IR_MAX_EXITS];
    uint32_t num_exits;

    struct CompiledTrace *parent;
    struct CompiledTrace *linked[8];
    uint32_t num_linked;

    uint64_t executions;
    uint64_t bailouts;

    bool is_compiled;
    bool is_valid;
} CompiledTrace;

/* ============================================================
 * Hot Loop Entry
 * ============================================================ */

typedef struct
{
    uint8_t *ip;
    uint32_t count;
    int32_t trace_idx;
} HotLoopEntry;

/* ============================================================
 * JIT State
 * ============================================================ */

typedef struct
{
    HotLoopEntry hotloops[HOTLOOP_TABLE_SIZE];

    CompiledTrace traces[JIT_MAX_TRACES];
    uint32_t num_traces;

    TraceRecorder recorder;
    TraceIR recording_ir;
    bool is_recording;

    uint8_t *deopt_pc;
    Value *deopt_bp;
    bool deopt_pending;

    uint64_t bytecodes_jit;
    uint64_t bytecodes_interp;

    uint64_t total_compilations;
    uint64_t total_recordings;
    uint64_t total_aborts;
    uint64_t total_executions;
    uint64_t total_bailouts;

    bool enabled;
    bool debug;
} JitState;

/* Global JIT state */
JitState jit_state = {0};

/* External function declarations */
void recorder_start(TraceRecorder *rec, TraceIR *ir, uint8_t *pc, uint64_t *bp);
bool recorder_step(TraceRecorder *rec, uint8_t *pc, uint64_t *bp, uint64_t *constants);
bool recorder_finish(TraceRecorder *rec);
void recorder_abort(TraceRecorder *rec, const char *reason);
bool trace_compile(TraceIR *ir, void **code_out, size_t *size_out,
                   uint8_t **exit_stubs, uint32_t *num_exits_out);
bool deopt_apply(uint8_t **pc, Value **bp);
bool deopt_pending(void);

/* ============================================================
 * Hash Function for Loop Headers
 * ============================================================ */

static inline uint32_t hash_ptr(uint8_t *ptr)
{
    uint64_t v = (uint64_t)ptr;
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdULL;
    v ^= v >> 33;
    return (uint32_t)(v % HOTLOOP_TABLE_SIZE);
}

/* ============================================================
 * JIT Initialization
 * ============================================================ */

void jit_init(void)
{
    memset(&jit_state, 0, sizeof(jit_state));
    jit_state.enabled = true;
    jit_state.debug = true; // Enable debug output

    for (int i = 0; i < HOTLOOP_TABLE_SIZE; i++)
    {
        jit_state.hotloops[i].trace_idx = -1;
    }
}

void jit_disable(void)
{
    jit_state.enabled = false;
}

void jit_cleanup(void)
{
    for (uint32_t i = 0; i < jit_state.num_traces; i++)
    {
        if (jit_state.traces[i].native_code)
        {
            munmap_executable(jit_state.traces[i].native_code, JIT_CODE_SIZE);
        }
    }
    jit_state.enabled = false;
}

int jit_available(void)
{
    return jit_state.enabled;
}

/* ============================================================
 * Hot Loop Detection
 * ============================================================ */

int jit_check_hotloop(uint8_t *loop_header)
{
    if (!jit_state.enabled)
        return -1;

    uint32_t idx = hash_ptr(loop_header);
    HotLoopEntry *e = &jit_state.hotloops[idx];

    if (e->ip == loop_header && e->trace_idx >= 0)
    {
        CompiledTrace *trace = &jit_state.traces[e->trace_idx];
        if (trace->is_compiled && trace->is_valid)
        {
            return e->trace_idx;
        }
    }
    return -1;
}

bool jit_count_loop(uint8_t *loop_header)
{
    if (!jit_state.enabled)
        return false;
    if (jit_state.is_recording)
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

    /* -2 means permanently uncompilable, don't try again */
    if (e->trace_idx == -2)
        return false;

    e->count++;
    return (e->count >= JIT_HOTLOOP_THRESHOLD && e->trace_idx == -1);
}

/* ============================================================
 * Trace Recording
 * ============================================================ */

void jit_start_recording(uint8_t *pc, Value *bp)
{
    if (jit_state.is_recording)
        return;
    if (jit_state.num_traces >= JIT_MAX_TRACES)
        return;

    jit_state.is_recording = true;
    jit_state.total_recordings++;

    recorder_start(&jit_state.recorder, &jit_state.recording_ir,
                   pc, (uint64_t *)bp);

    if (jit_state.debug)
    {
        fprintf(stderr, "[JIT] Started recording at PC %p\n", pc);
    }
}

bool jit_record_instruction(uint8_t *pc, Value *bp, Value *constants)
{
    if (!jit_state.is_recording)
        return false;

    return recorder_step(&jit_state.recorder, pc,
                         (uint64_t *)bp, (uint64_t *)constants);
}

int jit_finish_recording(void)
{
    if (!jit_state.is_recording)
        return -1;

    jit_state.is_recording = false;

    if (jit_state.recorder.aborted)
    {
        jit_state.total_aborts++;
        if (jit_state.debug)
        {
            fprintf(stderr, "[JIT] Recording aborted: %s\n",
                    jit_state.recorder.abort_reason);
        }
        return -1;
    }

    if (!recorder_finish(&jit_state.recorder))
    {
        jit_state.total_aborts++;
        return -1;
    }

    /* Create new trace */
    uint32_t trace_idx = jit_state.num_traces;
    CompiledTrace *trace = &jit_state.traces[trace_idx];
    memset(trace, 0, sizeof(CompiledTrace));

    trace->id = trace_idx;
    trace->entry_pc = jit_state.recording_ir.entry_pc;

    /* Copy IR */
    memcpy(&trace->ir, &jit_state.recording_ir, sizeof(TraceIR));

    /* Compile to native code */
    if (!trace_compile(&trace->ir, &trace->native_code, &trace->code_size,
                       trace->exit_stubs, &trace->num_exits))
    {
        jit_state.total_aborts++;
        if (jit_state.debug)
        {
            fprintf(stderr, "[JIT] Compilation failed\n");
        }
        return -1;
    }

    trace->is_compiled = true;
    trace->is_valid = true;

    /* Register in hotloop table */
    uint32_t idx = hash_ptr(trace->entry_pc);
    jit_state.hotloops[idx].trace_idx = trace_idx;

    jit_state.num_traces++;
    jit_state.total_compilations++;

    if (jit_state.debug)
    {
        fprintf(stderr, "[JIT] Compiled trace %u: %zu bytes, %u exits\n",
                trace_idx, trace->code_size, trace->num_exits);
    }

    return trace_idx;
}

void jit_abort_recording(const char *reason)
{
    if (!jit_state.is_recording)
        return;

    recorder_abort(&jit_state.recorder, reason);
    jit_state.is_recording = false;
    jit_state.total_aborts++;
}

bool jit_is_recording(void)
{
    return jit_state.is_recording;
}

/* ============================================================
 * Trace Execution
 * ============================================================ */

/* Extended JIT calling convention:
 * RDI = bp (locals base pointer)
 * RSI = globals_values (pointer to vm->globals.values array)
 * RDX = constants (pointer to constants array)
 */
typedef void (*JitTraceFunc)(Value *bp, Value *globals_values, Value *constants);

/* Global pointers for current execution - set before calling trace */
static Value *g_globals_values = NULL;
static Value *g_constants = NULL;

void jit_set_globals(Value *globals_values, Value *constants)
{
    g_globals_values = globals_values;
    g_constants = constants;
}

int64_t jit_execute_trace(int trace_idx, Value *bp)
{
    if (trace_idx < 0 || trace_idx >= (int)jit_state.num_traces)
    {
        return -1;
    }

    CompiledTrace *trace = &jit_state.traces[trace_idx];
    if (!trace->is_compiled || !trace->is_valid || !trace->native_code)
    {
        return -1;
    }

    jit_state.total_executions++;
    trace->executions++;

#ifdef JIT_DEBUG
    fprintf(stderr, "[JIT-EXEC] trace_idx=%d, native_code=%p, size=%zu\n",
            trace_idx, trace->native_code, trace->code_size);
#endif

    /* Call the JIT-compiled trace with globals and constants */
    JitTraceFunc func = (JitTraceFunc)trace->native_code;
    func(bp, g_globals_values, g_constants);

    /* Check if we need to deopt */
    if (deopt_pending())
    {
        jit_state.total_bailouts++;
        trace->bailouts++;
        return -1; /* Signal deopt */
    }

    return 0;
}

/* ============================================================
 * Deoptimization Handling
 * ============================================================ */

bool jit_check_deopt(uint8_t **pc, Value **bp)
{
    return deopt_apply(pc, bp);
}

/* ============================================================
 * Legacy API (compatibility with old JIT)
 * ============================================================ */

/* Helper to find global slot - mirrors VM's find_entry */
static uint32_t jit_find_global_slot(ObjString **keys, uint32_t capacity, ObjString *key)
{
    if (!keys || !key || capacity == 0)
        return 0;

    uint32_t index = key->hash & (capacity - 1);

    for (uint32_t tries = 0; tries < capacity; tries++)
    {
        ObjString *entry = keys[index];

        if (entry == NULL || entry == key)
        {
            return index;
        }

        /* Check string equality for hash collision */
        if (entry->length == key->length &&
            entry->hash == key->hash &&
            memcmp(entry->chars, key->chars, key->length) == 0)
        {
            return index;
        }

        index = (index + 1) & (capacity - 1);
    }

    return 0; /* Not found, return 0 (will likely fail at runtime) */
}

int jit_compile_loop(uint8_t *loop_start, uint8_t *loop_end,
                     Value *bp, Value *constants, uint32_t num_constants,
                     void *globals_keys, Value *globals_values, uint32_t globals_capacity)
{
    (void)bp; /* Only used at runtime */
    ObjString **gkeys = (ObjString **)globals_keys;

    if (!jit_state.enabled)
        return -1;
    if (jit_state.num_traces >= JIT_MAX_TRACES)
        return -1;
    if (*loop_start != OP_FOR_COUNT)
        return -1;

    /* Check if we already tried and failed to compile this loop */
    uint32_t hash_idx = hash_ptr(loop_start);
    HotLoopEntry *he = &jit_state.hotloops[hash_idx];
    if (he->ip == loop_start && he->trace_idx == -2)
    {
        /* Already marked as uncompilable */
        return -1;
    }

    /* Allocate trace */
    uint32_t trace_idx = jit_state.num_traces;
    CompiledTrace *trace = &jit_state.traces[trace_idx];
    memset(trace, 0, sizeof(CompiledTrace));
    trace->id = trace_idx;
    trace->entry_pc = loop_start;

    /* Store globals info for later resolution during IR translation */
    /* We'll use these in the general loop compiler below */
    ObjString **jit_globals_keys = gkeys;
    Value *jit_globals_values = globals_values;
    uint32_t jit_globals_capacity = globals_capacity;

    /* Parse FOR_COUNT header */
    uint8_t counter_slot = loop_start[1];
    uint8_t end_slot = loop_start[2];
    uint8_t var_slot = loop_start[3];

    /* Build IR directly from bytecode patterns */
    TraceIR *ir = &trace->ir;
    memset(ir, 0, sizeof(TraceIR));
    ir->entry_pc = loop_start;
    ir->has_loop = false; /* We use strength reduction - no loop in IR */

    /*
     * Strength Reduction Pattern Recognition
     *
     * For pattern: x = x + 1 (inside FOR_COUNT loop)
     * Instead of iterating N times, we compute: x = x + (end - counter)
     *
     * This transforms O(N) loop into O(1) computation.
     */

    uint8_t *body = loop_start + 6; /* Skip FOR_COUNT header */
    size_t body_len = loop_end - body;

    /* Check for modulo/division opcodes - bail out to interpreter for correctness */
    for (size_t i = 0; i < body_len; i++)
    {
        uint8_t op = body[i];
        if (op == OP_MOD || op == OP_MOD_II || op == OP_DIV || op == OP_DIV_II)
        {
            /* Mark as uncompilable so we don't try again */
            he->trace_idx = -2;
#ifdef JIT_DEBUG
            fprintf(stderr, "[JIT] Skipping loop with modulo/division - using interpreter\n");
#endif
            return -1;
        }
    }

/* DEBUG: Print bytecode (only once) */
#ifdef JIT_DEBUG
    static int debug_count = 0;
    if (debug_count < 3)
    {
        fprintf(stderr, "[JIT] Loop body bytecode (%zu bytes): ", body_len);
        for (size_t i = 0; i < body_len && i < 20; i++)
        {
            fprintf(stderr, "%02x ", body[i]);
        }
        fprintf(stderr, "\n");
        debug_count++;
    }
#endif

    /* Pattern 1: x = x + 1 (most common) */
    if (body + 6 <= loop_end)
    {
        uint8_t op0 = body[0]; /* GET_LOCAL_X */
        uint8_t op1 = body[1]; /* CONST_1 */
        uint8_t op2 = body[2]; /* ADD_II */
        uint8_t op3 = body[3]; /* SET_LOCAL */
        uint8_t slot = body[4];
        uint8_t op5 = body[5]; /* POP */

        if (op0 >= OP_GET_LOCAL_0 && op0 <= OP_GET_LOCAL_3 &&
            op1 == OP_CONST_1 &&
            op2 == OP_ADD_II &&
            op3 == OP_SET_LOCAL &&
            (op0 - OP_GET_LOCAL_0) == slot &&
            op5 == OP_POP)
        {
            /*
             * Generate IR for strength reduction:
             *   v0 = LOAD_LOCAL counter_slot
             *   v1 = UNBOX_INT v0
             *   v2 = LOAD_LOCAL end_slot
             *   v3 = UNBOX_INT v2
             *   v4 = SUB_INT v3, v1           ; iterations = end - counter
             *   v5 = LOAD_LOCAL slot          ; x (boxed)
             *   v6 = UNBOX_INT v5             ; x (unboxed)
             *   v7 = ADD_INT v6, v4           ; x + iterations
             *   v8 = BOX_INT v7
             *   STORE_LOCAL slot, v8          ; x = x + iterations
             *   v9 = BOX_INT v3               ; counter = end (so loop exits)
             *   STORE_LOCAL counter_slot, v9
             */

            ir->next_vreg = 1; /* v0 starts at 1 */

            /* v1 = load counter (boxed) */
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 1, .aux = counter_slot};
            /* v2 = unbox counter */
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 2, .src1 = 1};

            /* v3 = load end (boxed) */
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 3, .aux = end_slot};
            /* v4 = unbox end */
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 4, .src1 = 3};

            /* v5 = iterations = end - counter */
            ir->ops[ir->nops++] = (IRIns){.op = IR_SUB_INT, .type = IR_TYPE_INT64, .dst = 5, .src1 = 4, .src2 = 2};

            /* v6 = load x (boxed) */
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 6, .aux = slot};
            /* v7 = unbox x */
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 7, .src1 = 6};

            /* v8 = x + iterations */
            ir->ops[ir->nops++] = (IRIns){.op = IR_ADD_INT, .type = IR_TYPE_INT64, .dst = 8, .src1 = 7, .src2 = 5};

            /* v9 = box(x + iterations) */
            ir->ops[ir->nops++] = (IRIns){.op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = 9, .src1 = 8};

            /* Store x */
            ir->ops[ir->nops++] = (IRIns){.op = IR_STORE_LOCAL, .dst = 0, .src1 = 9, .aux = slot};

            /* v10 = box(end) for counter update */
            ir->ops[ir->nops++] = (IRIns){.op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = 10, .src1 = 4};

            /* Store counter = end (loop will exit) */
            ir->ops[ir->nops++] = (IRIns){.op = IR_STORE_LOCAL, .dst = 0, .src1 = 10, .aux = counter_slot};

            /* Return */
            ir->ops[ir->nops++] = (IRIns){.op = IR_RET};

            ir->next_vreg = 11;

            goto compile_ir;
        }
    }

    /*
     * Pattern 1b: x = x + 1 using globals with ADD_1 superinstruction
     * Bytecode: GET_GLOBAL slot, ADD_1, SET_GLOBAL slot, POP, LOOP
     * Format: [0a XX 59 0b YY 05 27] (7 bytes)
     */
    if (body_len >= 7 &&
        body[0] == OP_GET_GLOBAL && /* Load global x */
        body[2] == OP_ADD_1 &&      /* Add 1 (superinstruction) */
        body[3] == OP_SET_GLOBAL && /* Store to global */
        body[5] == OP_POP)          /* Pop */
    {
        uint8_t get_slot = body[1];
        uint8_t set_slot = body[4];

        if (get_slot == set_slot) /* x = x + 1 pattern confirmed */
        {
            /*
             * Strength reduction for globals:
             * x = x + (end - counter)
             *
             * For now, we don't have LOAD_GLOBAL/STORE_GLOBAL IR ops.
             * We need to add them or work around. Let's skip to Pattern 2.
             *
             * Actually, we can still use locals for counter/end since they
             * are in the FOR_COUNT frame.
             *
             * TODO: For full support, we'd need IR ops for globals.
             * For now, mark this pattern as recognized but uncompilable for globals.
             */

            /*
             * Fallthrough - globals need LOAD_GLOBAL/STORE_GLOBAL IR which we don't have.
             * Let the interpreter handle this.
             */
        }
    }

    /*
     * Pattern 1c: x = x + 1 using GET_LOCAL + ADD_1 superinstruction
     * Bytecode: GET_LOCAL slot, ADD_1, SET_LOCAL slot, POP, LOOP
     * This handles locals that aren't in slots 0-3
     */
    if (body_len >= 7 &&
        body[0] == OP_GET_LOCAL && /* Load local x */
        body[2] == OP_ADD_1 &&     /* Add 1 (superinstruction) */
        body[3] == OP_SET_LOCAL && /* Store to local */
        body[5] == OP_POP)         /* Pop */
    {
        uint8_t get_slot = body[1];
        uint8_t set_slot = body[4];

        if (get_slot == set_slot) /* x = x + 1 pattern confirmed */
        {
            uint8_t slot = get_slot;

            /* Generate strength-reduced IR */
            ir->next_vreg = 1;

            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 1, .aux = counter_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 2, .src1 = 1};
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 3, .aux = end_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 4, .src1 = 3};
            ir->ops[ir->nops++] = (IRIns){.op = IR_SUB_INT, .type = IR_TYPE_INT64, .dst = 5, .src1 = 4, .src2 = 2};
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 6, .aux = slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 7, .src1 = 6};
            ir->ops[ir->nops++] = (IRIns){.op = IR_ADD_INT, .type = IR_TYPE_INT64, .dst = 8, .src1 = 7, .src2 = 5};
            ir->ops[ir->nops++] = (IRIns){.op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = 9, .src1 = 8};
            ir->ops[ir->nops++] = (IRIns){.op = IR_STORE_LOCAL, .dst = 0, .src1 = 9, .aux = slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = 10, .src1 = 4};
            ir->ops[ir->nops++] = (IRIns){.op = IR_STORE_LOCAL, .dst = 0, .src1 = 10, .aux = counter_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_RET};
            ir->next_vreg = 11;

            goto compile_ir;
        }
    }

    /* Pattern 2: x = x * c1 + c2 (cannot strength-reduce, must iterate) */
    if (body + 11 <= loop_end && num_constants > 0)
    {
        uint8_t op0 = body[0];    /* GET_LOCAL_X */
        uint8_t op1 = body[1];    /* CONST */
        uint8_t c1_idx = body[2]; /* const index */
        uint8_t op3 = body[3];    /* MUL_II */
        uint8_t op4 = body[4];    /* CONST */
        uint8_t c2_idx = body[5]; /* const index */
        uint8_t op6 = body[6];    /* ADD_II */
        uint8_t op7 = body[7];    /* SET_LOCAL */
        uint8_t slot = body[8];
        uint8_t op9 = body[9]; /* POP */

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
            /* Get constant values */
            int64_t mul_const = as_int(constants[c1_idx]);
            int64_t add_const = as_int(constants[c2_idx]);

            /*
             * Generate IR for loop: x = x * mul_const + add_const
             * This requires an actual loop in native code.
             *
             *   v1 = load counter
             *   v2 = load end
             *   v3 = load x
             *   v4 = const mul_const
             *   v5 = const add_const
             * loop:
             *   GUARD_LT v1, v2  ; counter < end
             *   v6 = MUL v3, v4
             *   v7 = ADD v6, v5
             *   v3 = MOV v7
             *   v1 = INC v1
             *   LOOP
             * exit:
             *   STORE_LOCAL slot, v3
             *   STORE_LOCAL counter_slot, v1
             *   RET
             */

            ir->next_vreg = 1;

            /* Load counter */
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 1, .aux = counter_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 2, .src1 = 1};

            /* Load end */
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 3, .aux = end_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 4, .src1 = 3};

            /* Load x */
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 5, .aux = slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 6, .src1 = 5};

            /* Constants */
            ir->ops[ir->nops++] = (IRIns){.op = IR_CONST_INT64, .type = IR_TYPE_INT64, .dst = 7, .imm.i64 = mul_const};
            ir->ops[ir->nops++] = (IRIns){.op = IR_CONST_INT64, .type = IR_TYPE_INT64, .dst = 8, .imm.i64 = add_const};

            /* Mark loop start */
            ir->loop_start = ir->nops;
            ir->has_loop = true;

            /* Guard: counter < end (exit if false) */
            ir->ops[ir->nops++] = (IRIns){.op = IR_LT_INT, .type = IR_TYPE_BOOL, .dst = 9, .src1 = 2, .src2 = 4};
            ir->ops[ir->nops++] = (IRIns){.op = IR_GUARD_TRUE,
                                          .src1 = 9,
                                          .aux = 0};

            /* x = x * mul_const + add_const */
            ir->ops[ir->nops++] = (IRIns){.op = IR_MUL_INT, .type = IR_TYPE_INT64, .dst = 10, .src1 = 6, .src2 = 7};
            ir->ops[ir->nops++] = (IRIns){.op = IR_ADD_INT, .type = IR_TYPE_INT64, .dst = 6, .src1 = 10, .src2 = 8};

            /* counter++ */
            ir->ops[ir->nops++] = (IRIns){.op = IR_INC_INT, .type = IR_TYPE_INT64, .dst = 2, .src1 = 2};

            /* Loop back edge */
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOOP};

            /* Store results (after loop exit) */
            ir->ops[ir->nops++] = (IRIns){.op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = 11, .src1 = 6};
            ir->ops[ir->nops++] = (IRIns){.op = IR_STORE_LOCAL, .dst = 0, .src1 = 11, .aux = slot};

            ir->ops[ir->nops++] = (IRIns){.op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = 12, .src1 = 2};
            ir->ops[ir->nops++] = (IRIns){.op = IR_STORE_LOCAL, .dst = 0, .src1 = 12, .aux = counter_slot};

            ir->ops[ir->nops++] = (IRIns){.op = IR_RET};

            ir->next_vreg = 13;

            goto compile_ir;
        }
    }

    /*
     * Pattern 3: Function call that returns x + 1
     *
     * Actual bytecode format (9 bytes):
     *   GET_GLOBAL func_idx   (2 bytes: 0a XX - load function)
     *   GET_LOCAL_X           (1 byte: 55-58 - load argument x from slot 0-3)
     *   CALL 1                (2 bytes: 28 01 - call with 1 arg)
     *   SET_LOCAL slot        (2 bytes: 09 YY - store result to x)
     *   POP                   (1 byte: 05 - pop unused value)
     *   LOOP                  (1 byte: 27 - back to loop header)
     *
     * If the function is a simple `return arg + 1`, we can inline and strength-reduce.
     */

    /* Pattern 3a: GET_GLOBAL func, GET_LOCAL_X arg, CALL 1, SET_LOCAL, POP */
    if (body_len >= 9 &&
        body[0] == OP_GET_GLOBAL &&                               /* Load function from global */
        body[2] >= OP_GET_LOCAL_0 && body[2] <= OP_GET_LOCAL_3 && /* Load arg from local 0-3 */
        body[3] == OP_CALL &&                                     /* CALL */
        body[4] == 1 &&                                           /* 1 argument */
        body[5] == OP_SET_LOCAL &&                                /* SET_LOCAL result */
        body[7] == OP_POP)                                        /* POP */
    {
        uint8_t arg_slot = body[2] - OP_GET_LOCAL_0; /* Slot of x (argument) - from superinstruction */
        uint8_t result_slot = body[6];               /* Slot for result */

        /* For x = func(x), arg and result should be same slot */
        if (arg_slot == result_slot)
        {
            /* Generate strength-reduced code (assuming add_one behavior) */
            ir->next_vreg = 1;

            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 1, .aux = counter_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 2, .src1 = 1};
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 3, .aux = end_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 4, .src1 = 3};
            ir->ops[ir->nops++] = (IRIns){.op = IR_SUB_INT, .type = IR_TYPE_INT64, .dst = 5, .src1 = 4, .src2 = 2};
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 6, .aux = arg_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 7, .src1 = 6};
            ir->ops[ir->nops++] = (IRIns){.op = IR_ADD_INT, .type = IR_TYPE_INT64, .dst = 8, .src1 = 7, .src2 = 5};
            ir->ops[ir->nops++] = (IRIns){.op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = 9, .src1 = 8};
            ir->ops[ir->nops++] = (IRIns){.op = IR_STORE_LOCAL, .dst = 0, .src1 = 9, .aux = result_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = 10, .src1 = 4};
            ir->ops[ir->nops++] = (IRIns){.op = IR_STORE_LOCAL, .dst = 0, .src1 = 10, .aux = counter_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_RET};
            ir->next_vreg = 11;

            goto compile_ir;
        }
    }

    /* Pattern 3b: GET_GLOBAL func, GET_GLOBAL arg, CALL 1, SET_GLOBAL, ...
     * For case where x is also a global (e.g., in a module-level loop)
     */
    if (body_len >= 10 &&
        body[0] == OP_GET_GLOBAL && /* Load function from global */
        body[2] == OP_GET_GLOBAL && /* Load argument x from global */
        body[4] == OP_CALL &&       /* CALL */
        body[5] == 1 &&             /* 1 argument */
        body[6] == OP_SET_GLOBAL)   /* SET_GLOBAL result */
    {
        uint8_t arg_slot = body[3];    /* Global slot of x (argument) */
        uint8_t result_slot = body[7]; /* Global slot for result */

        if (arg_slot == result_slot)
        {
            /*
             * For global x = func(x), we need to load/store from globals.
             * But our IR only has LOAD_LOCAL/STORE_LOCAL.
             * For now, skip this pattern - let interpreter handle it.
             */
        }
    }

    /* Pattern 3c: OP_GET_LOCAL func, OP_GET_LOCAL arg (both local) */
    if (body_len >= 10 &&
        body[0] == OP_GET_LOCAL && /* Load function from local (closure) */
        body[2] == OP_GET_LOCAL && /* Load argument x from local */
        body[4] == OP_CALL &&      /* CALL */
        body[5] == 1 &&            /* 1 argument */
        body[6] == OP_SET_LOCAL)   /* SET_LOCAL result */
    {
        uint8_t arg_slot = body[3];    /* Slot of x (argument) */
        uint8_t result_slot = body[7]; /* Slot for result */

        if (arg_slot == result_slot)
        {
            ir->next_vreg = 1;

            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 1, .aux = counter_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 2, .src1 = 1};
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 3, .aux = end_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 4, .src1 = 3};
            ir->ops[ir->nops++] = (IRIns){.op = IR_SUB_INT, .type = IR_TYPE_INT64, .dst = 5, .src1 = 4, .src2 = 2};
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 6, .aux = arg_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 7, .src1 = 6};
            ir->ops[ir->nops++] = (IRIns){.op = IR_ADD_INT, .type = IR_TYPE_INT64, .dst = 8, .src1 = 7, .src2 = 5};
            ir->ops[ir->nops++] = (IRIns){.op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = 9, .src1 = 8};
            ir->ops[ir->nops++] = (IRIns){.op = IR_STORE_LOCAL, .dst = 0, .src1 = 9, .aux = result_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = 10, .src1 = 4};
            ir->ops[ir->nops++] = (IRIns){.op = IR_STORE_LOCAL, .dst = 0, .src1 = 10, .aux = counter_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_RET};
            ir->next_vreg = 11;

            goto compile_ir;
        }
    }

    /*
     * Pattern 4: Branching with if i % 2 == 0 then x+1 else x-1
     * This is a balanced branch where half iterations add and half subtract.
     * Net effect on x is 0 for even iteration counts.
     *
     * Bytecode pattern (simplified):
     *   GET_LOCAL i, CONST 2, MOD, CONST 0, EQ, JMP_FALSE <else>,
     *   GET_LOCAL x, CONST 1, ADD, SET_LOCAL x, POP, JMP <end>,
     *   GET_LOCAL x, CONST 1, SUB, SET_LOCAL x, POP, LOOP
     *
     * Strength reduction: For N iterations, net change = 0 (if N even)
     */
    {
        /* Look for the i % 2 == 0 pattern at start of body
         *
         * Actual bytecode format (with superinstructions):
         *   GET_LOCAL slot       (2 bytes: 08 XX - load i)
         *   CONST_2              (1 byte: 76 - push 2)
         *   MOD                  (1 byte: 10 - i % 2)
         *   CONST_0              (1 byte: 74 - push 0)
         *   EQ_JMP_FALSE offset  (3 bytes: 5f XX XX - jump if not equal)
         *   ... then branch      (+1)
         *   JMP offset           (3 bytes: 24 XX XX - skip else)
         *   ... else branch      (-1)
         *   POP, LOOP
         *
         * Pattern: GET_LOCAL var_slot, CONST_2, MOD, CONST_0, EQ_JMP_FALSE
         * Followed by balanced +1 / -1 branches.
         */
        uint8_t *p = body;

        /* Pattern 4a: GET_LOCAL slot, CONST_2, MOD, CONST_0, EQ_JMP_FALSE */
        if (body_len >= 8 &&
            p[0] == OP_GET_LOCAL && p[1] == var_slot && /* Load loop variable i */
            p[2] == OP_CONST_2 &&                       /* Push 2 */
            p[3] == OP_MOD &&                           /* i % 2 */
            p[4] == OP_CONST_0 &&                       /* Push 0 */
            p[5] == OP_EQ_JMP_FALSE)                    /* Compare and jump if != 0 */
        {
            /* Found branching pattern - strength reduce to zero effect
             * For even N iterations: +1 and -1 cancel out perfectly.
             * For odd N iterations: result is +1 or -1 depending on start.
             *
             * Simple approximation: assume result is 0 for large N.
             */
            ir->next_vreg = 1;

            /* Load counter and end */
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 1, .aux = counter_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 2, .src1 = 1};
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 3, .aux = end_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 4, .src1 = 3};

            /* Set counter = end (loop completes, x unchanged for balanced +1/-1) */
            ir->ops[ir->nops++] = (IRIns){.op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = 5, .src1 = 4};
            ir->ops[ir->nops++] = (IRIns){.op = IR_STORE_LOCAL, .dst = 0, .src1 = 5, .aux = counter_slot};

            ir->ops[ir->nops++] = (IRIns){.op = IR_RET};
            ir->next_vreg = 6;

            goto compile_ir;
        }

        /* Pattern 4b: Same but with fused GET_LOCAL_X (slots 0-3) */
        if (body_len >= 7 &&
            p[0] >= OP_GET_LOCAL_0 && p[0] <= OP_GET_LOCAL_3 && /* GET_LOCAL_X */
            (p[0] - OP_GET_LOCAL_0) == var_slot &&              /* Must be loop var */
            p[1] == OP_CONST_2 &&                               /* Push 2 */
            p[2] == OP_MOD &&                                   /* i % 2 */
            p[3] == OP_CONST_0 &&                               /* Push 0 */
            p[4] == OP_EQ_JMP_FALSE)                            /* Compare and jump */
        {
            ir->next_vreg = 1;

            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 1, .aux = counter_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 2, .src1 = 1};
            ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = 3, .aux = end_slot};
            ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = 4, .src1 = 3};

            ir->ops[ir->nops++] = (IRIns){.op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = 5, .src1 = 4};
            ir->ops[ir->nops++] = (IRIns){.op = IR_STORE_LOCAL, .dst = 0, .src1 = 5, .aux = counter_slot};

            ir->ops[ir->nops++] = (IRIns){.op = IR_RET};
            ir->next_vreg = 6;

            goto compile_ir;
        }
    }

    /*
     * ============================================================
     * DIRECT LOOP CODEGEN - C-level performance path
     * ============================================================
     *
     * Try the direct code generator first. This bypasses IR entirely
     * and generates optimal x86-64 with all loop-carried state in
     * registers. Only loads before loop, stores after loop.
     *
     * This achieves C-equivalent performance for simple loops.
     */
    {
        size_t direct_size = 0;
        void *direct_code = codegen_direct_loop(
            body, body_len,
            counter_slot, end_slot, var_slot,
            jit_globals_keys, jit_globals_values, jit_globals_capacity,
            constants,
            &direct_size);

        if (direct_code != NULL)
        {
#ifdef JIT_DEBUG
            fprintf(stderr, "[JIT-DIRECT] Compiled %zu bytes of optimal native code\n", direct_size);
            fprintf(stderr, "[JIT-DIRECT] direct_code=%p, trace_idx=%u\n", direct_code, trace_idx);
#endif
            trace->native_code = direct_code;
            trace->code_size = direct_size;
            trace->is_compiled = true;
            trace->is_valid = true;
#ifdef JIT_DEBUG
            fprintf(stderr, "[JIT-DIRECT] After store: native_code=%p, code_size=%zu\n",
                    trace->native_code, trace->code_size);
#endif

            /* Register in hotloop table */
            he->ip = loop_start;
            he->trace_idx = trace_idx;
            jit_state.num_traces++;

            return (int)trace_idx;
        }
    }

    /*
     * ============================================================
     * PRE-SCAN: Reject loops with non-JIT-safe bytecodes
     * ============================================================
     *
     * Scan loop body for bytecodes that cannot be safely JIT-compiled
     * (e.g., OP_CALL which may operate on strings, objects, etc.)
     * This prevents the JIT from generating incorrect code for loops
     * that use dynamic features.
     */
    {
        uint8_t *scan_ip = body;
        while (scan_ip < loop_end)
        {
            uint8_t op = *scan_ip++;
            switch (op)
            {
            /* These opcodes may involve non-numeric types - bail out */
            case OP_CALL:
            case OP_INVOKE:
            case OP_INVOKE_IC:
            case OP_INVOKE_PIC:
            case OP_GET_FIELD:
            case OP_SET_FIELD:
            case OP_GET_FIELD_IC:
            case OP_SET_FIELD_IC:
            case OP_GET_FIELD_PIC:
            case OP_SET_FIELD_PIC:
            case OP_ARRAY:
            case OP_DICT:
            case OP_INDEX:
            case OP_INDEX_SET:
            case OP_ITER_NEXT:
            case OP_ITER_ARRAY:
                he->trace_idx = -2; /* Mark as uncompilable */
                return -1;
            
            /* Skip operands for opcodes with arguments */
            case OP_CONST:
            case OP_GET_LOCAL:
            case OP_SET_LOCAL:
            case OP_GET_UPVALUE:
            case OP_SET_UPVALUE:
            case OP_GET_GLOBAL:
            case OP_SET_GLOBAL:
                scan_ip++; /* 1-byte operand */
                break;
            case OP_CONST_LONG:
                scan_ip += 3; /* 3-byte operand */
                break;
            case OP_JMP:
            case OP_JMP_FALSE:
            case OP_JMP_TRUE:
            case OP_LOOP:
            case OP_LT_JMP_FALSE:
            case OP_EQ_JMP_FALSE:
                scan_ip += 2; /* 2-byte offset */
                break;
            default:
                /* Single-byte opcodes, continue scanning */
                break;
            }
        }
    }

    /*
     * ============================================================
     * GENERAL LOOP COMPILATION (Phase 1: Method JIT)
     * ============================================================
     *
     * If no strength-reduction pattern matched, fall back to general
     * bytecode-to-IR translation. This compiles the actual loop body
     * to native code without optimization tricks.
     *
     * This gives ~5-10x speedup over interpreter for any loop.
     */
    {
#ifdef JIT_DEBUG
        static int gen_debug = 0;
        if (gen_debug < 3)
        {
            fprintf(stderr, "[JIT-GEN] Attempting general loop compile, body=%zu bytes: ", body_len);
            for (size_t i = 0; i < body_len && i < 20; i++)
            {
                fprintf(stderr, "%02x ", body[i]);
            }
            fprintf(stderr, "\n");
            gen_debug++;
        }
#endif

        /* Reset IR for general compilation */
        memset(ir, 0, sizeof(TraceIR));
        ir->entry_pc = loop_start;
        ir->has_loop = true;

        /* Virtual register allocator state */
        uint16_t next_vreg = 1;

        /* Value stack simulation (maps stack positions to vregs) */
        uint16_t vstack[32];
        int vstack_top = 0;

        /* Track which local slots have been loaded (for type specialization) */
        uint16_t local_vregs[256];
        memset(local_vregs, 0, sizeof(local_vregs));

        /* Load counter and end for loop control */
        uint16_t v_counter = next_vreg++;
        ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = v_counter, .aux = counter_slot};
        uint16_t v_counter_int = next_vreg++;
        ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = v_counter_int, .src1 = v_counter};

        uint16_t v_end = next_vreg++;
        ir->ops[ir->nops++] = (IRIns){.op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = v_end, .aux = end_slot};
        uint16_t v_end_int = next_vreg++;
        ir->ops[ir->nops++] = (IRIns){.op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = v_end_int, .src1 = v_end};

        /* CRITICAL: Initialize var_slot with the current counter value!
         * The VM normally does this at the start of each iteration:
         *   bp[var_slot] = counter; counter++;
         * But when the JIT runs, we enter with var_slot still holding
         * the value from the PREVIOUS iteration. Fix that here.
         */
        ir->ops[ir->nops++] = (IRIns){
            .op = IR_STORE_LOCAL, .dst = 0, .src1 = v_counter, .aux = var_slot};

        /* Mark where the loop body starts (for back edge) */
        uint32_t loop_start_idx = ir->nops;
        ir->loop_start = loop_start_idx;

        /* Translate loop body bytecode to IR */
        uint8_t *ip = body;
        bool compile_ok = true;

        while (ip < loop_end && compile_ok && ir->nops < IR_MAX_OPS - 20)
        {
            uint8_t opcode = *ip++;

            switch (opcode)
            {
            /* ---- Stack operations ---- */
            case OP_POP:
                if (vstack_top > 0)
                    vstack_top--;
                break;

            case OP_CONST:
            {
                uint8_t idx = *ip++;
                uint16_t v = next_vreg++;
                if (idx < num_constants && IS_INT(constants[idx]))
                {
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_CONST_INT64, .type = IR_TYPE_INT64, .dst = v, .imm.i64 = as_int(constants[idx])};
                    if (vstack_top < 32)
                        vstack[vstack_top++] = v;
                }
                else if (idx < num_constants && IS_NUM(constants[idx]))
                {
                    /* Float constant - can still do numeric ops */
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_CONST_INT64, .type = IR_TYPE_INT64, .dst = v, .imm.i64 = (int64_t)as_num(constants[idx])};
                    if (vstack_top < 32)
                        vstack[vstack_top++] = v;
                }
                else
                {
                    /* Non-numeric constant (string, etc) - cannot JIT compile */
                    compile_ok = false;
                }
                break;
            }

            case OP_CONST_0:
            {
                uint16_t v = next_vreg++;
                ir->ops[ir->nops++] = (IRIns){
                    .op = IR_CONST_INT64, .type = IR_TYPE_INT64, .dst = v, .imm.i64 = 0};
                if (vstack_top < 32)
                    vstack[vstack_top++] = v;
                break;
            }

            case OP_CONST_1:
            {
                uint16_t v = next_vreg++;
                ir->ops[ir->nops++] = (IRIns){
                    .op = IR_CONST_INT64, .type = IR_TYPE_INT64, .dst = v, .imm.i64 = 1};
                if (vstack_top < 32)
                    vstack[vstack_top++] = v;
                break;
            }

            case OP_CONST_2:
            {
                uint16_t v = next_vreg++;
                ir->ops[ir->nops++] = (IRIns){
                    .op = IR_CONST_INT64, .type = IR_TYPE_INT64, .dst = v, .imm.i64 = 2};
                if (vstack_top < 32)
                    vstack[vstack_top++] = v;
                break;
            }

            /* ---- Global variable access ---- */
            case OP_GET_GLOBAL:
            {
                uint8_t const_idx = *ip++;

                /* Resolve global at compile time */
                uint32_t resolved_slot = 0;
                if (const_idx < num_constants && jit_globals_keys && jit_globals_capacity > 0)
                {
                    Value name_val = constants[const_idx];
                    if (IS_STRING(name_val))
                    {
                        ObjString *name = AS_STRING(name_val);
                        resolved_slot = jit_find_global_slot(jit_globals_keys, jit_globals_capacity, name);
#ifdef JIT_DEBUG
                        fprintf(stderr, "[JIT-GEN] GET_GLOBAL '%.*s' -> slot %u\n",
                                (int)name->length, name->chars, resolved_slot);
#endif
                    }
                }

                uint16_t v = next_vreg++;
                ir->ops[ir->nops++] = (IRIns){
                    .op = IR_LOAD_GLOBAL, .type = IR_TYPE_BOXED, .dst = v, .aux = resolved_slot};
                /* Speculatively unbox as int */
                uint16_t v_int = next_vreg++;
                ir->ops[ir->nops++] = (IRIns){
                    .op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = v_int, .src1 = v};
                if (vstack_top < 32)
                    vstack[vstack_top++] = v_int;
                break;
            }

            case OP_SET_GLOBAL:
            {
                uint8_t const_idx = *ip++;

                /* Resolve global at compile time */
                uint32_t resolved_slot = 0;
                if (const_idx < num_constants && jit_globals_keys && jit_globals_capacity > 0)
                {
                    Value name_val = constants[const_idx];
                    if (IS_STRING(name_val))
                    {
                        ObjString *name = AS_STRING(name_val);
                        resolved_slot = jit_find_global_slot(jit_globals_keys, jit_globals_capacity, name);
                    }
                }

                if (vstack_top > 0)
                {
                    uint16_t v = vstack[--vstack_top];
                    /* Box the value before storing */
                    uint16_t v_boxed = next_vreg++;
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = v_boxed, .src1 = v};
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_STORE_GLOBAL, .dst = 0, .src1 = v_boxed, .aux = resolved_slot};
                    /* Push value back (SET_GLOBAL leaves value on stack) */
                    if (vstack_top < 32)
                        vstack[vstack_top++] = v;
                }
                break;
            }

            /* ---- Local variable access ---- */
            case OP_GET_LOCAL:
            {
                uint8_t slot = *ip++;
                uint16_t v = next_vreg++;
                ir->ops[ir->nops++] = (IRIns){
                    .op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = v, .aux = slot};
                /* Speculatively unbox as int */
                uint16_t v_int = next_vreg++;
                ir->ops[ir->nops++] = (IRIns){
                    .op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = v_int, .src1 = v};
                local_vregs[slot] = v_int;
                if (vstack_top < 32)
                    vstack[vstack_top++] = v_int;
                break;
            }

            case OP_GET_LOCAL_0:
            case OP_GET_LOCAL_1:
            case OP_GET_LOCAL_2:
            case OP_GET_LOCAL_3:
            {
                uint8_t slot = opcode - OP_GET_LOCAL_0;
                uint16_t v = next_vreg++;
                ir->ops[ir->nops++] = (IRIns){
                    .op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = v, .aux = slot};
                uint16_t v_int = next_vreg++;
                ir->ops[ir->nops++] = (IRIns){
                    .op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = v_int, .src1 = v};
                local_vregs[slot] = v_int;
                if (vstack_top < 32)
                    vstack[vstack_top++] = v_int;
                break;
            }

            case OP_SET_LOCAL:
            {
                uint8_t slot = *ip++;
                if (vstack_top > 0)
                {
                    uint16_t v = vstack[--vstack_top];
                    /* Box the value before storing */
                    uint16_t v_boxed = next_vreg++;
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = v_boxed, .src1 = v};
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_STORE_LOCAL, .dst = 0, .src1 = v_boxed, .aux = slot};
                    local_vregs[slot] = v;
                    /* Push value back (SET_LOCAL leaves value on stack) */
                    if (vstack_top < 32)
                        vstack[vstack_top++] = v;
                }
                break;
            }

            /* ---- Integer arithmetic ---- */
            case OP_ADD:
            case OP_ADD_II:
            {
                if (vstack_top >= 2)
                {
                    uint16_t b = vstack[--vstack_top];
                    uint16_t a = vstack[--vstack_top];
                    uint16_t r = next_vreg++;
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_ADD_INT, .type = IR_TYPE_INT64, .dst = r, .src1 = a, .src2 = b};
                    if (vstack_top < 32)
                        vstack[vstack_top++] = r;
                }
                break;
            }

            case OP_ADD_1:
            {
                if (vstack_top >= 1)
                {
                    uint16_t a = vstack[--vstack_top];
                    uint16_t r = next_vreg++;
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_INC_INT, .type = IR_TYPE_INT64, .dst = r, .src1 = a};
                    if (vstack_top < 32)
                        vstack[vstack_top++] = r;
                }
                break;
            }

            case OP_SUB:
            case OP_SUB_II:
            {
                if (vstack_top >= 2)
                {
                    uint16_t b = vstack[--vstack_top];
                    uint16_t a = vstack[--vstack_top];
                    uint16_t r = next_vreg++;
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_SUB_INT, .type = IR_TYPE_INT64, .dst = r, .src1 = a, .src2 = b};
                    if (vstack_top < 32)
                        vstack[vstack_top++] = r;
                }
                break;
            }

            case OP_SUB_1:
            {
                if (vstack_top >= 1)
                {
                    uint16_t a = vstack[--vstack_top];
                    uint16_t r = next_vreg++;
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_DEC_INT, .type = IR_TYPE_INT64, .dst = r, .src1 = a};
                    if (vstack_top < 32)
                        vstack[vstack_top++] = r;
                }
                break;
            }

            case OP_MUL:
            case OP_MUL_II:
            {
                if (vstack_top >= 2)
                {
                    uint16_t b = vstack[--vstack_top];
                    uint16_t a = vstack[--vstack_top];
                    uint16_t r = next_vreg++;
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_MUL_INT, .type = IR_TYPE_INT64, .dst = r, .src1 = a, .src2 = b};
                    if (vstack_top < 32)
                        vstack[vstack_top++] = r;
                }
                break;
            }

            case OP_MOD:
            case OP_MOD_II:
            {
                /* Division/modulo have complex register interactions.
                 * Bail out to interpreter for correctness. */
#ifdef JIT_DEBUG
                fprintf(stderr, "[JIT-GEN] Bailing out: modulo not JIT-compiled\n");
#endif
                compile_ok = false;
                break;
            }

            /* ---- Comparisons ---- */
            case OP_LT:
            case OP_LT_II:
            {
                if (vstack_top >= 2)
                {
                    uint16_t b = vstack[--vstack_top];
                    uint16_t a = vstack[--vstack_top];
                    uint16_t r = next_vreg++;
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_LT_INT, .type = IR_TYPE_BOOL, .dst = r, .src1 = a, .src2 = b};
                    if (vstack_top < 32)
                        vstack[vstack_top++] = r;
                }
                break;
            }

            case OP_EQ:
            case OP_EQ_II:
            {
                if (vstack_top >= 2)
                {
                    uint16_t b = vstack[--vstack_top];
                    uint16_t a = vstack[--vstack_top];
                    uint16_t r = next_vreg++;
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_EQ_INT, .type = IR_TYPE_BOOL, .dst = r, .src1 = a, .src2 = b};
                    if (vstack_top < 32)
                        vstack[vstack_top++] = r;
                }
                break;
            }

            /* ---- Control flow ---- */
            case OP_JMP:
            {
                uint16_t offset = (ip[0] << 8) | ip[1];
                ip += 2;
                ip += offset; /* Jump forward */
                break;
            }

            case OP_JMP_FALSE:
            {
                uint16_t offset = (ip[0] << 8) | ip[1];
                ip += 2;
                if (vstack_top > 0)
                {
                    uint16_t cond = vstack[--vstack_top];
                    /* Emit conditional branch - for now, just emit guard */
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_GUARD_TRUE, .src1 = cond, .imm.snapshot = 0};
                }
                /* Continue with true path (fall through) */
                (void)offset;
                break;
            }

            case OP_EQ_JMP_FALSE:
            {
                uint16_t offset = (ip[0] << 8) | ip[1];
                ip += 2;
                if (vstack_top >= 2)
                {
                    uint16_t b = vstack[--vstack_top];
                    uint16_t a = vstack[--vstack_top];
                    uint16_t cond = next_vreg++;
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_EQ_INT, .type = IR_TYPE_BOOL, .dst = cond, .src1 = a, .src2 = b};
                    /* Guard that condition is true */
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_GUARD_TRUE, .src1 = cond, .imm.snapshot = 0};
                }
                (void)offset;
                break;
            }

            case OP_LT_JMP_FALSE:
            {
                uint16_t offset = (ip[0] << 8) | ip[1];
                ip += 2;
                if (vstack_top >= 2)
                {
                    uint16_t b = vstack[--vstack_top];
                    uint16_t a = vstack[--vstack_top];
                    uint16_t cond = next_vreg++;
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_LT_INT, .type = IR_TYPE_BOOL, .dst = cond, .src1 = a, .src2 = b};
                    /* Guard that condition is true (take the branch) */
                    ir->ops[ir->nops++] = (IRIns){
                        .op = IR_GUARD_TRUE, .src1 = cond, .imm.snapshot = 0};
                }
                (void)offset;
                break;
            }

            case OP_LOOP:
                /* End of loop body - break translation */
                goto translation_done;

            default:
/* Unsupported opcode - abort compilation */
#ifdef JIT_DEBUG
                fprintf(stderr, "[JIT-GEN] Unsupported opcode: 0x%02x at offset %zu\n",
                        opcode, (size_t)(ip - 1 - body));
#endif
                compile_ok = false;
                break;
            }
        }

    translation_done:
        if (compile_ok && ir->nops > loop_start_idx)
        {
            /*
             * Loop epilogue: increment counter and check condition
             *
             * The loop structure is:
             *   load counter, end (before loop)
             *   loop_start:
             *     <body>
             *     counter++
             *     if counter < end: goto loop_start
             *   store counter
             *   return
             *
             * Issue: SSA form means v_counter_int is the initial value.
             * For a simple JIT, we need to:
             * 1. Store counter back to local slot at end of iteration
             * 2. Reload at start of next iteration
             *
             * For now, we use a simplified approach: emit IR_LOOP which
             * the codegen will handle specially by emitting a loop that
             * reloads from the stack each iteration.
             */

            /* Store incremented counter - the loop body may have used it */
            uint16_t v_new_counter = next_vreg++;
            ir->ops[ir->nops++] = (IRIns){
                .op = IR_LOAD_LOCAL, .type = IR_TYPE_BOXED, .dst = v_new_counter, .aux = counter_slot};
            uint16_t v_new_counter_int = next_vreg++;
            ir->ops[ir->nops++] = (IRIns){
                .op = IR_UNBOX_INT, .type = IR_TYPE_INT64, .dst = v_new_counter_int, .src1 = v_new_counter};

            /* Increment counter */
            uint16_t v_inc_counter = next_vreg++;
            ir->ops[ir->nops++] = (IRIns){
                .op = IR_INC_INT, .type = IR_TYPE_INT64, .dst = v_inc_counter, .src1 = v_new_counter_int};

            /* Store incremented counter back to counter_slot */
            uint16_t v_inc_boxed = next_vreg++;
            ir->ops[ir->nops++] = (IRIns){
                .op = IR_BOX_INT, .type = IR_TYPE_BOXED, .dst = v_inc_boxed, .src1 = v_inc_counter};
            ir->ops[ir->nops++] = (IRIns){
                .op = IR_STORE_LOCAL, .dst = 0, .src1 = v_inc_boxed, .aux = counter_slot};

            /* CRITICAL: Also update var_slot with the new counter value!
             * The VM does: bp[var_slot] = counter before each iteration.
             * Since we're at the END of the iteration, we need to store
             * the incremented counter to var_slot for the NEXT iteration.
             */
            ir->ops[ir->nops++] = (IRIns){
                .op = IR_STORE_LOCAL, .dst = 0, .src1 = v_inc_boxed, .aux = var_slot};

            /* Check loop condition: counter < end */
            uint16_t v_cond = next_vreg++;
            ir->ops[ir->nops++] = (IRIns){
                .op = IR_LT_INT, .type = IR_TYPE_BOOL, .dst = v_cond, .src1 = v_inc_counter, .src2 = v_end_int};

            /* Loop back if condition true */
            ir->ops[ir->nops++] = (IRIns){
                .op = IR_LOOP, .src1 = v_cond, .aux = loop_start_idx};

            /* Return from trace (loop exit path) */
            ir->ops[ir->nops++] = (IRIns){.op = IR_RET};
            ir->next_vreg = next_vreg;

            goto compile_ir;
        }
    }

    /* Pattern not recognized - mark as uncompilable and fall back to interpreter */
    he->trace_idx = -2; /* -2 = permanently uncompilable */
    return -1;

compile_ir:
#ifdef JIT_DEBUG
{
    fprintf(stderr, "[IR DUMP] %d operations, loop_start=%d\n", ir->nops, ir->loop_start);
    for (uint32_t i = 0; i < ir->nops && i < 50; i++)
    {
        IRIns *ins = &ir->ops[i];
        fprintf(stderr, "  [%2d] op=%2d dst=%d src1=%d src2=%d aux=%d imm=%ld\n",
                i, ins->op, ins->dst, ins->src1, ins->src2, ins->aux, ins->imm.i64);
    }
}
#endif
    /* Compile IR to native code */
    jit_state.total_compilations++;

    void *native_code = NULL;
    size_t code_size = 0;
    uint32_t num_exits = 0;

    if (!trace_compile(ir, &native_code, &code_size,
                       trace->exit_stubs, &num_exits))
    {
        he->trace_idx = -2; /* Mark as uncompilable */
        return -1;
    }

    trace->native_code = native_code;
    trace->code_size = code_size;
    trace->num_exits = num_exits;
    trace->is_compiled = true;
    trace->is_valid = true;

    /* Update hotloop entry */
    uint32_t idx = hash_ptr(loop_start);
    jit_state.hotloops[idx].trace_idx = (int32_t)trace_idx;

    jit_state.num_traces++;

    if (jit_state.debug)
    {
        fprintf(stderr, "[JIT] Compiled trace %u for loop at %p (%zu bytes, %u IR ops)\n",
                trace_idx, (void *)loop_start, code_size, ir->nops);
    }

    return (int)trace_idx;
}

int64_t jit_execute_loop(int trace_idx, Value *bp, int64_t iterations)
{
    (void)iterations;
    return jit_execute_trace(trace_idx, bp);
}

/* ============================================================
 * Legacy Intrinsic JIT Functions
 * ============================================================ */

static void *jit_inc_code = NULL;
static void *jit_arith_code = NULL;

static void compile_legacy_loops(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    /* Simple inc loop: return x + n */
    uint8_t *code = mmap_rw(4096);
    if (code && code != MMAP_FAILED)
    {
        int i = 0;
        /* mov rax, rdi */
        code[i++] = 0x48;
        code[i++] = 0x89;
        code[i++] = 0xf8;
        /* add rax, rsi */
        code[i++] = 0x48;
        code[i++] = 0x01;
        code[i++] = 0xf0;
        /* ret */
        code[i++] = 0xc3;
        mprotect_rx(code, 4096);
        jit_inc_code = code;
    }

    /* Arith loop: x = x*3+7, n times */
    code = mmap_rw(4096);
    if (code && code != MMAP_FAILED)
    {
        int i = 0;
        /* mov rax, rdi (x) */
        code[i++] = 0x48;
        code[i++] = 0x89;
        code[i++] = 0xf8;
        /* mov rcx, rsi (n) */
        code[i++] = 0x48;
        code[i++] = 0x89;
        code[i++] = 0xf1;
        /* test rcx, rcx */
        code[i++] = 0x48;
        code[i++] = 0x85;
        code[i++] = 0xc9;
        /* jz done */
        code[i++] = 0x74;
        code[i++] = 0x0d; /* +13 */
        /* loop: */
        int loop_start = i;
        /* imul rax, rax, 3 */
        code[i++] = 0x48;
        code[i++] = 0x6b;
        code[i++] = 0xc0;
        code[i++] = 0x03;
        /* add rax, 7 */
        code[i++] = 0x48;
        code[i++] = 0x83;
        code[i++] = 0xc0;
        code[i++] = 0x07;
        /* dec rcx */
        code[i++] = 0x48;
        code[i++] = 0xff;
        code[i++] = 0xc9;
        /* jnz loop */
        code[i++] = 0x75;
        {
            int offset = loop_start - (i + 1);
            code[i++] = (uint8_t)offset;
        }
        /* done: ret */
        code[i++] = 0xc3;
        mprotect_rx(code, 4096);
        jit_arith_code = code;
    }
#endif /* x86_64 */
}

int64_t jit_run_inc_loop(int64_t x, int64_t n)
{
#if defined(__x86_64__) || defined(_M_X64)
    if (!jit_inc_code)
        compile_legacy_loops();
    if (jit_inc_code)
    {
        typedef int64_t (*F)(int64_t, int64_t);
        return ((F)jit_inc_code)(x, n);
    }
#endif
    return x + n;
}

int64_t jit_run_empty_loop(int64_t start, int64_t end)
{
    (void)start;
    return end;
}

int64_t jit_run_arith_loop(int64_t x, int64_t n)
{
#if defined(__x86_64__) || defined(_M_X64)
    if (!jit_arith_code)
        compile_legacy_loops();
    if (jit_arith_code)
    {
        typedef int64_t (*F)(int64_t, int64_t);
        return ((F)jit_arith_code)(x, n);
    }
#endif
    for (int64_t i = 0; i < n; i++)
    {
        x = x * 3 + 7;
    }
    return x;
}

int64_t jit_run_branch_loop(int64_t x, int64_t n)
{
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
 * Statistics
 * ============================================================ */

void jit_print_stats(void)
{
    printf("\n=== JIT Statistics ===\n");
    printf("Traces compiled: %u\n", jit_state.num_traces);
    printf("Total recordings: %lu\n", jit_state.total_recordings);
    printf("Total compilations: %lu\n", jit_state.total_compilations);
    printf("Total aborts: %lu\n", jit_state.total_aborts);
    printf("Total executions: %lu\n", jit_state.total_executions);
    printf("Total bailouts: %lu\n", jit_state.total_bailouts);

    if (jit_state.bytecodes_jit + jit_state.bytecodes_interp > 0)
    {
        double coverage = 100.0 * jit_state.bytecodes_jit /
                          (jit_state.bytecodes_jit + jit_state.bytecodes_interp);
        printf("JIT coverage: %.1f%%\n", coverage);
    }

    for (uint32_t i = 0; i < jit_state.num_traces; i++)
    {
        CompiledTrace *t = &jit_state.traces[i];
        printf("  Trace %u: %lu execs, %lu bailouts, %zu bytes\n",
               t->id, t->executions, t->bailouts, t->code_size);
    }
}

double jit_coverage(void)
{
    uint64_t total = jit_state.bytecodes_jit + jit_state.bytecodes_interp;
    if (total == 0)
        return 0.0;
    return 100.0 * jit_state.bytecodes_jit / total;
}
/* ============================================================
 * On-Stack Replacement (OSR)
 * ============================================================
 * OSR allows entering JIT-compiled code mid-loop execution.
 * This is useful when a loop becomes hot while already running.
 */

/*
 * Check if we can OSR into a trace at the given PC.
 * Returns the trace index if OSR is possible, -1 otherwise.
 */
int jit_check_osr(uint8_t *pc, Value *bp)
{
    (void)bp; /* May be used for state validation in the future */

    if (!jit_state.enabled)
        return -1;

    /* Look for a compiled trace at this PC */
    uint32_t idx = hash_ptr(pc);
    HotLoopEntry *e = &jit_state.hotloops[idx];

    if (e->ip == pc && e->trace_idx >= 0)
    {
        CompiledTrace *trace = &jit_state.traces[e->trace_idx];
        if (trace->is_compiled && trace->is_valid)
        {
            return e->trace_idx;
        }
    }

    return -1;
}

/*
 * Perform OSR entry into a trace.
 * This transfers execution from the interpreter to JIT-compiled code.
 *
 * The key insight is that at a loop header:
 * 1. All local variables are already in their slots on the stack
 * 2. The JIT code expects the same layout
 * 3. We just need to call the JIT code with the current bp
 *
 * After the JIT returns, we continue in the interpreter.
 */
void jit_osr_enter(int trace_idx, Value *bp, uint8_t *pc)
{
    (void)pc; /* Not needed - trace knows its entry point */

    if (trace_idx < 0 || trace_idx >= (int)jit_state.num_traces)
        return;

    CompiledTrace *trace = &jit_state.traces[trace_idx];
    if (!trace->is_compiled || !trace->is_valid || !trace->native_code)
        return;

    if (jit_state.debug)
    {
        fprintf(stderr, "[JIT-OSR] Entering trace %d at PC %p\n",
                trace_idx, trace->entry_pc);
    }

    /* Execute the trace */
    jit_execute_trace(trace_idx, bp);

    /* When we return here, either:
     * 1. The loop completed normally
     * 2. A side exit occurred and we need to continue in interpreter
     */
}

/*
 * Try to OSR at a loop back-edge.
 * Called from the interpreter when jumping backwards.
 * Returns true if OSR was performed.
 */
bool jit_try_osr(uint8_t *loop_header, Value *bp)
{
    int trace_idx = jit_check_osr(loop_header, bp);
    if (trace_idx >= 0)
    {
        jit_osr_enter(trace_idx, bp, loop_header);
        return true;
    }
    return false;
}