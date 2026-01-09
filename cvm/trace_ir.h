/*
 * Pseudocode Tracing JIT - SSA IR Definition
 *
 * A minimal SSA intermediate representation for trace compilation.
 * No global analysis - IR is per-trace only.
 *
 * Design principles:
 * - Single-entry, multi-exit traces
 * - Speculative typing with guards
 * - Deoptimization on guard failure
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#ifndef TRACE_IR_H
#define TRACE_IR_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * Configuration
 * ============================================================ */

#define IR_MAX_OPS 512       /* Max IR ops per trace */
#define IR_MAX_VREGS 256     /* Max virtual registers */
#define IR_MAX_GUARDS 64     /* Max guard points */
#define IR_MAX_EXITS 32      /* Max side exits */
#define IR_MAX_CONSTANTS 64  /* Max constants in trace */
#define IR_MAX_SNAPSHOTS 64  /* Max deopt snapshots */
#define IR_SNAPSHOT_SLOTS 32 /* Max slots in a snapshot */

/* ============================================================
 * Type Tags for Speculative Typing
 * ============================================================ */

typedef enum
{
    IR_TYPE_UNKNOWN = 0,
    IR_TYPE_INT32,    /* 32-bit signed integer */
    IR_TYPE_INT64,    /* 64-bit signed integer */
    IR_TYPE_DOUBLE,   /* IEEE 754 double */
    IR_TYPE_BOOL,     /* Boolean */
    IR_TYPE_NIL,      /* Nil */
    IR_TYPE_STRING,   /* String object */
    IR_TYPE_ARRAY,    /* Array object */
    IR_TYPE_FUNCTION, /* Function object */
    IR_TYPE_BOXED,    /* Any boxed NaN-tagged value */
} IRType;

/* ============================================================
 * IR Opcodes
 * ============================================================ */

typedef enum
{
    /* Pseudo-ops (no code generation) */
    IR_NOP = 0,
    IR_PHI,      /* SSA phi node (loop merge) */
    IR_SNAPSHOT, /* Deopt snapshot point */

    /* Load/Store */
    IR_LOAD_LOCAL,   /* Load from local slot: vreg = bp[slot] */
    IR_STORE_LOCAL,  /* Store to local slot: bp[slot] = vreg */
    IR_LOAD_CONST,   /* Load constant: vreg = constants[idx] */
    IR_LOAD_GLOBAL,  /* Load global by index */
    IR_STORE_GLOBAL, /* Store global by index */

    /* Constants */
    IR_CONST_INT,    /* 32-bit integer constant */
    IR_CONST_INT64,  /* 64-bit integer constant */
    IR_CONST_DOUBLE, /* Double constant */
    IR_CONST_BOOL,   /* Boolean constant */
    IR_CONST_NIL,    /* Nil constant */

    /* Integer Arithmetic */
    IR_ADD_INT, /* Integer add: c = a + b */
    IR_SUB_INT, /* Integer sub: c = a - b */
    IR_MUL_INT, /* Integer mul: c = a * b */
    IR_DIV_INT, /* Integer div: c = a / b */
    IR_MOD_INT, /* Integer mod: c = a % b */
    IR_NEG_INT, /* Integer neg: b = -a */
    IR_INC_INT, /* Increment: b = a + 1 */
    IR_DEC_INT, /* Decrement: b = a - 1 */

    /* Float Arithmetic */
    IR_ADD_DOUBLE, /* Double add */
    IR_SUB_DOUBLE, /* Double sub */
    IR_MUL_DOUBLE, /* Double mul */
    IR_DIV_DOUBLE, /* Double div */
    IR_NEG_DOUBLE, /* Double negate */

    /* Comparison (result is bool) */
    IR_LT_INT, /* Integer less than */
    IR_LE_INT, /* Integer less or equal */
    IR_GT_INT, /* Integer greater than */
    IR_GE_INT, /* Integer greater or equal */
    IR_EQ_INT, /* Integer equal */
    IR_NE_INT, /* Integer not equal */

    IR_LT_DOUBLE, /* Double comparisons */
    IR_LE_DOUBLE,
    IR_GT_DOUBLE,
    IR_GE_DOUBLE,
    IR_EQ_DOUBLE,
    IR_NE_DOUBLE,

    /* Logical */
    IR_NOT, /* Logical not */
    IR_AND, /* Logical and */
    IR_OR,  /* Logical or */

    /* Bitwise */
    IR_BAND, /* Bitwise and */
    IR_BOR,  /* Bitwise or */
    IR_BXOR, /* Bitwise xor */
    IR_BNOT, /* Bitwise not */
    IR_SHL,  /* Shift left */
    IR_SHR,  /* Shift right (arithmetic) */

    /* Type conversions */
    IR_INT_TO_DOUBLE, /* int -> double */
    IR_DOUBLE_TO_INT, /* double -> int (truncate) */
    IR_BOX_INT,       /* int -> NaN-boxed value */
    IR_UNBOX_INT,     /* NaN-boxed -> int */
    IR_BOX_DOUBLE,    /* double -> NaN-boxed value */
    IR_UNBOX_DOUBLE,  /* NaN-boxed -> double */

    /* Control flow */
    IR_JUMP,   /* Unconditional jump to label */
    IR_BRANCH, /* Conditional branch: if (cond) goto label */
    IR_LOOP,   /* Loop back edge */
    IR_EXIT,   /* Side exit to interpreter */
    IR_RET,    /* Return from trace */

    /* Guards (emit check + deopt) */
    IR_GUARD_TYPE,     /* Guard value type matches */
    IR_GUARD_INT,      /* Guard value is integer */
    IR_GUARD_DOUBLE,   /* Guard value is double */
    IR_GUARD_TRUE,     /* Guard condition is true */
    IR_GUARD_FALSE,    /* Guard condition is false */
    IR_GUARD_OVERFLOW, /* Guard no overflow occurred */
    IR_GUARD_BOUNDS,   /* Guard array bounds */
    IR_GUARD_FUNC,     /* Guard function identity */

    /* Function calls */
    IR_CALL,        /* Call function */
    IR_CALL_INLINE, /* Inlined function call marker */
    IR_ARG,         /* Function argument */
    IR_RET_VAL,     /* Return value */

    /* Array operations */
    IR_ARRAY_GET, /* array[index] */
    IR_ARRAY_SET, /* array[index] = value */
    IR_ARRAY_LEN, /* array.length */

    /* Move/Copy */
    IR_MOV,  /* Move between vregs */
    IR_COPY, /* Copy value */

    IR_OP_COUNT
} IROp;

/* ============================================================
 * IR Instruction
 * ============================================================ */

typedef struct
{
    uint16_t op;   /* IROp */
    uint16_t type; /* IRType - result type */
    uint16_t dst;  /* Destination vreg (0 = none) */
    uint16_t src1; /* Source vreg 1 */
    uint16_t src2; /* Source vreg 2 (or immediate index) */
    uint16_t aux;  /* Auxiliary data (slot, const idx, etc.) */

    /* For constants and guards */
    union
    {
        int64_t i64;       /* Integer constant */
        double f64;        /* Double constant */
        uint32_t snapshot; /* Snapshot index for guards */
        uint8_t *pc;       /* Bytecode PC for guards */
    } imm;

    /* Original bytecode location for debugging/deopt */
    uint8_t *bc_pc;
} IRIns;

/* ============================================================
 * Deoptimization Snapshot
 *
 * A snapshot captures the interpreter state at a guard point.
 * On guard failure, we restore this state and jump to the
 * interpreter at the recorded PC.
 * ============================================================ */

typedef struct
{
    uint8_t *pc;                       /* Bytecode PC to resume at */
    uint8_t nslots;                    /* Number of slots to restore */
    uint8_t slots[IR_SNAPSHOT_SLOTS];  /* Local slot indices */
    uint16_t vregs[IR_SNAPSHOT_SLOTS]; /* Corresponding vregs */
    IRType types[IR_SNAPSHOT_SLOTS];   /* Types of each slot */
} IRSnapshot;

/* ============================================================
 * Side Exit
 *
 * When a guard fails, we jump to a side exit which:
 * 1. Reconstructs interpreter state from snapshot
 * 2. Jumps to interpreter at the recorded PC
 * ============================================================ */

typedef struct
{
    uint32_t guard_idx;    /* Index of failing guard in IR */
    uint32_t snapshot_idx; /* Snapshot to restore */
    uint32_t count;        /* Number of times this exit was taken */
    uint8_t *native_addr;  /* Address of exit stub in native code */
} SideExit;

/* ============================================================
 * Virtual Register Info
 * ============================================================ */

typedef struct
{
    IRType type;        /* Inferred type */
    int16_t def;        /* IR instruction that defines this vreg */
    int16_t phys_reg;   /* Assigned physical register (-1 = spilled) */
    int16_t spill_slot; /* Spill slot if spilled */
    bool is_const;      /* True if constant */
    bool is_loop_var;   /* True if modified in loop */
} VRegInfo;

/* ============================================================
 * Trace IR Buffer
 *
 * Complete IR representation of a trace, ready for code gen.
 * ============================================================ */

typedef struct
{
    /* IR instructions */
    IRIns ops[IR_MAX_OPS];
    uint32_t nops;

    /* Virtual registers */
    VRegInfo vregs[IR_MAX_VREGS];
    uint32_t next_vreg;

    /* Snapshots for deoptimization */
    IRSnapshot snapshots[IR_MAX_SNAPSHOTS];
    uint32_t nsnapshots;

    /* Side exits */
    SideExit exits[IR_MAX_EXITS];
    uint32_t nexits;

    /* Constants referenced in trace */
    union
    {
        int64_t i64;
        double f64;
    } constants[IR_MAX_CONSTANTS];
    IRType const_types[IR_MAX_CONSTANTS];
    uint32_t nconsts;

    /* Loop info */
    uint32_t loop_start; /* IR index of loop header */
    bool has_loop;       /* True if trace contains a loop */

    /* Entry point */
    uint8_t *entry_pc; /* Bytecode entry point */

    /* Type signature at entry (for trace cache lookup) */
    IRType entry_types[16];
    uint8_t num_entry_types;

} TraceIR;

/* ============================================================
 * Trace Recorder State
 *
 * State maintained while recording bytecode into IR.
 * ============================================================ */

typedef struct
{
    /* Recording state */
    bool active;         /* Currently recording? */
    uint8_t *start_pc;   /* PC where recording started */
    uint8_t *current_pc; /* Current bytecode PC */
    uint32_t depth;      /* Call depth (for inlining) */

    /* IR buffer being built */
    TraceIR *ir;

    /* Stack simulation (maps stack slots to vregs) */
    uint16_t stack[64]; /* vreg for each stack slot */
    int32_t sp;         /* Simulated stack pointer */

    /* Local variable tracking (maps local slots to vregs) */
    uint16_t locals[256];    /* vreg for each local slot */
    IRType local_types[256]; /* Type of each local */

    /* Loop detection */
    uint8_t *loop_header; /* Detected loop header */
    uint32_t loop_count;  /* Iterations seen */

    /* Abort conditions */
    bool aborted;
    const char *abort_reason;

} TraceRecorder;

/* ============================================================
 * IR Builder API
 * ============================================================ */

/* Initialize a new trace IR buffer */
void ir_init(TraceIR *ir);

/* Allocate a new virtual register */
uint16_t ir_vreg(TraceIR *ir, IRType type);

/* Emit IR instructions */
uint32_t ir_emit(TraceIR *ir, IROp op, IRType type,
                 uint16_t dst, uint16_t src1, uint16_t src2);
uint32_t ir_emit_const_int(TraceIR *ir, uint16_t dst, int64_t val);
uint32_t ir_emit_const_double(TraceIR *ir, uint16_t dst, double val);
uint32_t ir_emit_load(TraceIR *ir, uint16_t dst, uint8_t slot, IRType type);
uint32_t ir_emit_store(TraceIR *ir, uint8_t slot, uint16_t src);
uint32_t ir_emit_guard(TraceIR *ir, IROp guard_op, uint16_t src,
                       uint32_t snapshot_idx, uint8_t *fail_pc);

/* Create a deopt snapshot */
uint32_t ir_snapshot(TraceIR *ir, uint8_t *pc,
                     uint8_t *slots, uint16_t *vregs,
                     IRType *types, uint8_t nslots);

/* Mark loop header */
void ir_mark_loop(TraceIR *ir, uint32_t header_idx);

/* ============================================================
 * Trace Recorder API
 * ============================================================ */

/* Start recording a trace */
void recorder_start(TraceRecorder *rec, TraceIR *ir,
                    uint8_t *pc, uint64_t *bp);

/* Record one bytecode instruction */
/* Returns: true = continue, false = stop recording */
bool recorder_step(TraceRecorder *rec, uint8_t *pc,
                   uint64_t *bp, uint64_t *constants);

/* Finish recording */
bool recorder_finish(TraceRecorder *rec);

/* Abort recording */
void recorder_abort(TraceRecorder *rec, const char *reason);

/* Push/pop from simulated stack */
void rec_push(TraceRecorder *rec, uint16_t vreg);
uint16_t rec_pop(TraceRecorder *rec);
uint16_t rec_peek(TraceRecorder *rec, int n);

/* Get/set local variable vreg */
uint16_t rec_get_local(TraceRecorder *rec, uint8_t slot);
void rec_set_local(TraceRecorder *rec, uint8_t slot, uint16_t vreg, IRType type);

#endif /* TRACE_IR_H */
