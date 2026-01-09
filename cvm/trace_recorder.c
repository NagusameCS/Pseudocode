/*
 * Pseudocode Tracing JIT - Phase 1: Trace Recorder
 *
 * Records bytecode execution into IR while running in the interpreter.
 * Triggered when a loop becomes hot (exceeds threshold).
 *
 * Recording flow:
 * 1. Interpreter detects hot loop
 * 2. Start recording at loop header
 * 3. Each bytecode is translated to IR + guards
 * 4. Recording ends when loop header is reached again
 * 5. IR is compiled to native code
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "pseudo.h"
#include "trace_ir.h"
#include "jit.h"

/* ============================================================
 * IR Buffer Operations
 * ============================================================ */

void ir_init(TraceIR *ir)
{
    memset(ir, 0, sizeof(TraceIR));
    ir->next_vreg = 1; /* vreg 0 is reserved for "no value" */
}

uint16_t ir_vreg(TraceIR *ir, IRType type)
{
    if (ir->next_vreg >= IR_MAX_VREGS)
    {
        return 0; /* Out of vregs */
    }
    uint16_t v = ir->next_vreg++;
    ir->vregs[v].type = type;
    ir->vregs[v].def = -1;
    ir->vregs[v].phys_reg = -1;
    ir->vregs[v].spill_slot = -1;
    return v;
}

uint32_t ir_emit(TraceIR *ir, IROp op, IRType type,
                 uint16_t dst, uint16_t src1, uint16_t src2)
{
    if (ir->nops >= IR_MAX_OPS)
    {
        return 0;
    }
    uint32_t idx = ir->nops++;
    IRIns *ins = &ir->ops[idx];
    ins->op = op;
    ins->type = type;
    ins->dst = dst;
    ins->src1 = src1;
    ins->src2 = src2;
    ins->aux = 0;
    ins->bc_pc = NULL;

    /* Update vreg definition point */
    if (dst > 0 && dst < IR_MAX_VREGS)
    {
        ir->vregs[dst].def = idx;
        ir->vregs[dst].type = type;
    }

    return idx;
}

uint32_t ir_emit_const_int(TraceIR *ir, uint16_t dst, int64_t val)
{
    uint32_t idx = ir_emit(ir, IR_CONST_INT, IR_TYPE_INT64, dst, 0, 0);
    if (idx > 0)
    {
        ir->ops[idx].imm.i64 = val;
    }
    return idx;
}

uint32_t ir_emit_const_double(TraceIR *ir, uint16_t dst, double val)
{
    uint32_t idx = ir_emit(ir, IR_CONST_DOUBLE, IR_TYPE_DOUBLE, dst, 0, 0);
    if (idx > 0)
    {
        ir->ops[idx].imm.f64 = val;
    }
    return idx;
}

uint32_t ir_emit_load(TraceIR *ir, uint16_t dst, uint8_t slot, IRType type)
{
    uint32_t idx = ir_emit(ir, IR_LOAD_LOCAL, type, dst, 0, 0);
    if (idx > 0)
    {
        ir->ops[idx].aux = slot;
    }
    return idx;
}

uint32_t ir_emit_store(TraceIR *ir, uint8_t slot, uint16_t src)
{
    uint32_t idx = ir_emit(ir, IR_STORE_LOCAL, IR_TYPE_UNKNOWN, 0, src, 0);
    if (idx > 0)
    {
        ir->ops[idx].aux = slot;
    }
    return idx;
}

uint32_t ir_emit_guard(TraceIR *ir, IROp guard_op, uint16_t src,
                       uint32_t snapshot_idx, uint8_t *fail_pc)
{
    uint32_t idx = ir_emit(ir, guard_op, IR_TYPE_BOOL, 0, src, 0);
    if (idx > 0)
    {
        ir->ops[idx].imm.snapshot = snapshot_idx;
        ir->ops[idx].bc_pc = fail_pc;
    }
    return idx;
}

uint32_t ir_snapshot(TraceIR *ir, uint8_t *pc,
                     uint8_t *slots, uint16_t *vregs,
                     IRType *types, uint8_t nslots)
{
    if (ir->nsnapshots >= IR_MAX_SNAPSHOTS)
    {
        return 0;
    }
    uint32_t idx = ir->nsnapshots++;
    IRSnapshot *snap = &ir->snapshots[idx];
    snap->pc = pc;
    snap->nslots = nslots;
    for (int i = 0; i < nslots && i < IR_SNAPSHOT_SLOTS; i++)
    {
        snap->slots[i] = slots[i];
        snap->vregs[i] = vregs[i];
        snap->types[i] = types[i];
    }
    return idx;
}

void ir_mark_loop(TraceIR *ir, uint32_t header_idx)
{
    ir->loop_start = header_idx;
    ir->has_loop = true;
}

/* ============================================================
 * Trace Recorder
 * ============================================================ */

void recorder_start(TraceRecorder *rec, TraceIR *ir,
                    uint8_t *pc, uint64_t *bp)
{
    memset(rec, 0, sizeof(TraceRecorder));
    rec->active = true;
    rec->start_pc = pc;
    rec->current_pc = pc;
    rec->loop_header = pc;
    rec->ir = ir;
    rec->sp = 0;
    rec->depth = 0;

    ir_init(ir);
    ir->entry_pc = pc;

    /* Initialize locals from current state */
    /* We'll snapshot types as we encounter them */
    (void)bp;
}

void rec_push(TraceRecorder *rec, uint16_t vreg)
{
    if (rec->sp < 64)
    {
        rec->stack[rec->sp++] = vreg;
    }
}

uint16_t rec_pop(TraceRecorder *rec)
{
    if (rec->sp > 0)
    {
        return rec->stack[--rec->sp];
    }
    return 0;
}

uint16_t rec_peek(TraceRecorder *rec, int n)
{
    if (rec->sp > n)
    {
        return rec->stack[rec->sp - 1 - n];
    }
    return 0;
}

uint16_t rec_get_local(TraceRecorder *rec, uint8_t slot)
{
    return rec->locals[slot];
}

void rec_set_local(TraceRecorder *rec, uint8_t slot, uint16_t vreg, IRType type)
{
    rec->locals[slot] = vreg;
    rec->local_types[slot] = type;
}

/* Infer type from a NaN-boxed value */
static IRType infer_type(uint64_t val)
{
    if ((val & QNAN) != QNAN)
    {
        return IR_TYPE_DOUBLE;
    }
    if (val == VAL_NIL)
    {
        return IR_TYPE_NIL;
    }
    if (val == VAL_TRUE || val == VAL_FALSE)
    {
        return IR_TYPE_BOOL;
    }
    if ((val & (QNAN | 0x7)) == (QNAN | TAG_INT))
    {
        return IR_TYPE_INT32;
    }
    if ((val & TAG_OBJ) == TAG_OBJ)
    {
        /* Could be string, array, function, etc. */
        Obj *obj = (Obj *)(uintptr_t)(val & ~TAG_OBJ);
        switch (obj->type)
        {
        case OBJ_STRING:
            return IR_TYPE_STRING;
        case OBJ_ARRAY:
            return IR_TYPE_ARRAY;
        case OBJ_FUNCTION:
            return IR_TYPE_FUNCTION;
        default:
            return IR_TYPE_BOXED;
        }
    }
    return IR_TYPE_BOXED;
}

/* Create a snapshot of current state for deoptimization */
static uint32_t make_snapshot(TraceRecorder *rec, uint8_t *pc)
{
    uint8_t slots[IR_SNAPSHOT_SLOTS];
    uint16_t vregs[IR_SNAPSHOT_SLOTS];
    IRType types[IR_SNAPSHOT_SLOTS];
    uint8_t nslots = 0;

    /* Capture all live locals */
    for (int i = 0; i < 256 && nslots < IR_SNAPSHOT_SLOTS; i++)
    {
        if (rec->locals[i] != 0)
        {
            slots[nslots] = i;
            vregs[nslots] = rec->locals[i];
            types[nslots] = rec->local_types[i];
            nslots++;
        }
    }

    return ir_snapshot(rec->ir, pc, slots, vregs, types, nslots);
}

/* ============================================================
 * Bytecode Recording - Translate bytecode to IR
 * ============================================================ */

bool recorder_step(TraceRecorder *rec, uint8_t *pc,
                   uint64_t *bp, uint64_t *constants)
{
    if (!rec->active)
        return false;

    rec->current_pc = pc;
    TraceIR *ir = rec->ir;

    /* Check for loop completion */
    if (pc == rec->loop_header && ir->nops > 0)
    {
        /* We've looped back - mark the loop and finish */
        ir_mark_loop(ir, 0);
        rec->loop_count++;
        return false; /* Stop recording */
    }

    /* Check max trace length */
    if (ir->nops >= JIT_MAX_TRACE_LENGTH)
    {
        recorder_abort(rec, "max trace length exceeded");
        return false;
    }

    uint8_t op = *pc++;

    switch (op)
    {
    /* ============ Constants ============ */
    case OP_CONST:
    {
        uint8_t idx = *pc;
        uint64_t val = constants[idx];
        IRType type = infer_type(val);
        uint16_t dst = ir_vreg(ir, type);

        if (type == IR_TYPE_INT32)
        {
            ir_emit_const_int(ir, dst, as_int(val));
        }
        else if (type == IR_TYPE_DOUBLE)
        {
            ir_emit_const_double(ir, dst, as_num(val));
        }
        else
        {
            /* Boxed constant - load from constant pool */
            uint32_t i = ir_emit(ir, IR_LOAD_CONST, IR_TYPE_BOXED, dst, 0, 0);
            ir->ops[i].aux = idx;
        }
        rec_push(rec, dst);
        break;
    }

    case OP_CONST_0:
    {
        uint16_t dst = ir_vreg(ir, IR_TYPE_INT32);
        ir_emit_const_int(ir, dst, 0);
        rec_push(rec, dst);
        break;
    }

    case OP_CONST_1:
    {
        uint16_t dst = ir_vreg(ir, IR_TYPE_INT32);
        ir_emit_const_int(ir, dst, 1);
        rec_push(rec, dst);
        break;
    }

    case OP_CONST_2:
    {
        uint16_t dst = ir_vreg(ir, IR_TYPE_INT32);
        ir_emit_const_int(ir, dst, 2);
        rec_push(rec, dst);
        break;
    }

    case OP_NIL:
    {
        uint16_t dst = ir_vreg(ir, IR_TYPE_NIL);
        ir_emit(ir, IR_CONST_NIL, IR_TYPE_NIL, dst, 0, 0);
        rec_push(rec, dst);
        break;
    }

    case OP_TRUE:
    {
        uint16_t dst = ir_vreg(ir, IR_TYPE_BOOL);
        ir_emit_const_int(ir, dst, 1);
        ir->ops[ir->nops - 1].type = IR_TYPE_BOOL;
        rec_push(rec, dst);
        break;
    }

    case OP_FALSE:
    {
        uint16_t dst = ir_vreg(ir, IR_TYPE_BOOL);
        ir_emit_const_int(ir, dst, 0);
        ir->ops[ir->nops - 1].type = IR_TYPE_BOOL;
        rec_push(rec, dst);
        break;
    }

    /* ============ Local Variables ============ */
    case OP_GET_LOCAL:
    {
        uint8_t slot = *pc;
        uint64_t val = bp[slot];
        IRType type = infer_type(val);

        /* Check if we already have a vreg for this local */
        uint16_t vreg = rec_get_local(rec, slot);
        if (vreg == 0)
        {
            /* First access - load and guard type */
            vreg = ir_vreg(ir, type);
            ir_emit_load(ir, vreg, slot, type);

            /* Insert type guard */
            uint32_t snap = make_snapshot(rec, pc - 1);
            if (type == IR_TYPE_INT32)
            {
                ir_emit_guard(ir, IR_GUARD_INT, vreg, snap, pc - 1);
            }
            else if (type == IR_TYPE_DOUBLE)
            {
                ir_emit_guard(ir, IR_GUARD_DOUBLE, vreg, snap, pc - 1);
            }

            rec_set_local(rec, slot, vreg, type);
        }
        rec_push(rec, vreg);
        break;
    }

    case OP_GET_LOCAL_0:
    case OP_GET_LOCAL_1:
    case OP_GET_LOCAL_2:
    case OP_GET_LOCAL_3:
    {
        uint8_t slot = op - OP_GET_LOCAL_0;
        uint64_t val = bp[slot];
        IRType type = infer_type(val);

        uint16_t vreg = rec_get_local(rec, slot);
        if (vreg == 0)
        {
            vreg = ir_vreg(ir, type);
            ir_emit_load(ir, vreg, slot, type);

            uint32_t snap = make_snapshot(rec, pc - 1);
            if (type == IR_TYPE_INT32)
            {
                ir_emit_guard(ir, IR_GUARD_INT, vreg, snap, pc - 1);
            }
            else if (type == IR_TYPE_DOUBLE)
            {
                ir_emit_guard(ir, IR_GUARD_DOUBLE, vreg, snap, pc - 1);
            }

            rec_set_local(rec, slot, vreg, type);
        }
        rec_push(rec, vreg);
        break;
    }

    case OP_SET_LOCAL:
    {
        uint8_t slot = *pc;
        uint16_t src = rec_pop(rec);
        IRType type = ir->vregs[src].type;
        ir_emit_store(ir, slot, src);
        rec_set_local(rec, slot, src, type);
        break;
    }

    /* ============ Stack Operations ============ */
    case OP_POP:
    {
        rec_pop(rec);
        break;
    }

    case OP_DUP:
    {
        uint16_t v = rec_peek(rec, 0);
        rec_push(rec, v);
        break;
    }

    /* ============ Integer Arithmetic ============ */
    case OP_ADD:
    case OP_ADD_II:
    {
        uint16_t b = rec_pop(rec);
        uint16_t a = rec_pop(rec);
        IRType ta = ir->vregs[a].type;
        IRType tb = ir->vregs[b].type;

        uint16_t dst = ir_vreg(ir, IR_TYPE_INT32);

        if (ta == IR_TYPE_INT32 && tb == IR_TYPE_INT32)
        {
            ir_emit(ir, IR_ADD_INT, IR_TYPE_INT32, dst, a, b);
        }
        else if (ta == IR_TYPE_DOUBLE || tb == IR_TYPE_DOUBLE)
        {
            ir_emit(ir, IR_ADD_DOUBLE, IR_TYPE_DOUBLE, dst, a, b);
            ir->vregs[dst].type = IR_TYPE_DOUBLE;
        }
        else
        {
            /* Unknown types - emit guards and assume int */
            uint32_t snap = make_snapshot(rec, pc - 1);
            ir_emit_guard(ir, IR_GUARD_INT, a, snap, pc - 1);
            ir_emit_guard(ir, IR_GUARD_INT, b, snap, pc - 1);
            ir_emit(ir, IR_ADD_INT, IR_TYPE_INT32, dst, a, b);
        }
        rec_push(rec, dst);
        break;
    }

    case OP_SUB:
    case OP_SUB_II:
    {
        uint16_t b = rec_pop(rec);
        uint16_t a = rec_pop(rec);
        IRType ta = ir->vregs[a].type;
        IRType tb = ir->vregs[b].type;

        uint16_t dst = ir_vreg(ir, IR_TYPE_INT32);

        if (ta == IR_TYPE_INT32 && tb == IR_TYPE_INT32)
        {
            ir_emit(ir, IR_SUB_INT, IR_TYPE_INT32, dst, a, b);
        }
        else if (ta == IR_TYPE_DOUBLE || tb == IR_TYPE_DOUBLE)
        {
            ir_emit(ir, IR_SUB_DOUBLE, IR_TYPE_DOUBLE, dst, a, b);
            ir->vregs[dst].type = IR_TYPE_DOUBLE;
        }
        else
        {
            uint32_t snap = make_snapshot(rec, pc - 1);
            ir_emit_guard(ir, IR_GUARD_INT, a, snap, pc - 1);
            ir_emit_guard(ir, IR_GUARD_INT, b, snap, pc - 1);
            ir_emit(ir, IR_SUB_INT, IR_TYPE_INT32, dst, a, b);
        }
        rec_push(rec, dst);
        break;
    }

    case OP_MUL:
    case OP_MUL_II:
    {
        uint16_t b = rec_pop(rec);
        uint16_t a = rec_pop(rec);
        IRType ta = ir->vregs[a].type;
        IRType tb = ir->vregs[b].type;

        uint16_t dst = ir_vreg(ir, IR_TYPE_INT32);

        if (ta == IR_TYPE_INT32 && tb == IR_TYPE_INT32)
        {
            ir_emit(ir, IR_MUL_INT, IR_TYPE_INT32, dst, a, b);
        }
        else if (ta == IR_TYPE_DOUBLE || tb == IR_TYPE_DOUBLE)
        {
            ir_emit(ir, IR_MUL_DOUBLE, IR_TYPE_DOUBLE, dst, a, b);
            ir->vregs[dst].type = IR_TYPE_DOUBLE;
        }
        else
        {
            uint32_t snap = make_snapshot(rec, pc - 1);
            ir_emit_guard(ir, IR_GUARD_INT, a, snap, pc - 1);
            ir_emit_guard(ir, IR_GUARD_INT, b, snap, pc - 1);
            ir_emit(ir, IR_MUL_INT, IR_TYPE_INT32, dst, a, b);
        }
        rec_push(rec, dst);
        break;
    }

    case OP_DIV:
    case OP_DIV_II:
    case OP_MOD:
    case OP_MOD_II:
        /* Division/modulo have complex register interactions in JIT.
         * Bail out to interpreter for correctness. */
        recorder_abort(rec, "division/modulo not JIT-compiled");
        return false;

    case OP_NEG:
    case OP_NEG_II:
    {
        uint16_t a = rec_pop(rec);
        uint16_t dst = ir_vreg(ir, ir->vregs[a].type);
        if (ir->vregs[a].type == IR_TYPE_DOUBLE)
        {
            ir_emit(ir, IR_NEG_DOUBLE, IR_TYPE_DOUBLE, dst, a, 0);
        }
        else
        {
            ir_emit(ir, IR_NEG_INT, IR_TYPE_INT32, dst, a, 0);
        }
        rec_push(rec, dst);
        break;
    }

    case OP_INC:
    case OP_INC_II:
    {
        uint16_t a = rec_pop(rec);
        uint16_t dst = ir_vreg(ir, IR_TYPE_INT32);
        ir_emit(ir, IR_INC_INT, IR_TYPE_INT32, dst, a, 0);
        rec_push(rec, dst);
        break;
    }

    case OP_DEC:
    case OP_DEC_II:
    {
        uint16_t a = rec_pop(rec);
        uint16_t dst = ir_vreg(ir, IR_TYPE_INT32);
        ir_emit(ir, IR_DEC_INT, IR_TYPE_INT32, dst, a, 0);
        rec_push(rec, dst);
        break;
    }

    /* ============ Comparisons ============ */
    case OP_LT:
    case OP_LT_II:
    {
        uint16_t b = rec_pop(rec);
        uint16_t a = rec_pop(rec);
        uint16_t dst = ir_vreg(ir, IR_TYPE_BOOL);
        ir_emit(ir, IR_LT_INT, IR_TYPE_BOOL, dst, a, b);
        rec_push(rec, dst);
        break;
    }

    case OP_LTE:
    case OP_LTE_II:
    {
        uint16_t b = rec_pop(rec);
        uint16_t a = rec_pop(rec);
        uint16_t dst = ir_vreg(ir, IR_TYPE_BOOL);
        ir_emit(ir, IR_LE_INT, IR_TYPE_BOOL, dst, a, b);
        rec_push(rec, dst);
        break;
    }

    case OP_GT:
    case OP_GT_II:
    {
        uint16_t b = rec_pop(rec);
        uint16_t a = rec_pop(rec);
        uint16_t dst = ir_vreg(ir, IR_TYPE_BOOL);
        ir_emit(ir, IR_GT_INT, IR_TYPE_BOOL, dst, a, b);
        rec_push(rec, dst);
        break;
    }

    case OP_GTE:
    case OP_GTE_II:
    {
        uint16_t b = rec_pop(rec);
        uint16_t a = rec_pop(rec);
        uint16_t dst = ir_vreg(ir, IR_TYPE_BOOL);
        ir_emit(ir, IR_GE_INT, IR_TYPE_BOOL, dst, a, b);
        rec_push(rec, dst);
        break;
    }

    case OP_EQ:
    case OP_EQ_II:
    {
        uint16_t b = rec_pop(rec);
        uint16_t a = rec_pop(rec);
        uint16_t dst = ir_vreg(ir, IR_TYPE_BOOL);
        ir_emit(ir, IR_EQ_INT, IR_TYPE_BOOL, dst, a, b);
        rec_push(rec, dst);
        break;
    }

    case OP_NEQ:
    case OP_NEQ_II:
    {
        uint16_t b = rec_pop(rec);
        uint16_t a = rec_pop(rec);
        uint16_t dst = ir_vreg(ir, IR_TYPE_BOOL);
        ir_emit(ir, IR_NE_INT, IR_TYPE_BOOL, dst, a, b);
        rec_push(rec, dst);
        break;
    }

    /* ============ Control Flow ============ */
    case OP_LOOP:
    {
        /* Backward jump - this is a loop back edge */

        /* If this is a FOR_COUNT loop, emit the counter increment NOW */
        if (rec->has_for_loop)
        {
            /* Increment counter at end of body, before back-edge */
            uint16_t counter = rec->for_counter_vreg;
            uint16_t new_counter = ir_vreg(ir, IR_TYPE_INT32);
            ir_emit(ir, IR_INC_INT, IR_TYPE_INT32, new_counter, counter, 0);
            ir_emit_store(ir, rec->for_counter_slot, new_counter);
            rec_set_local(rec, rec->for_counter_slot, new_counter, IR_TYPE_INT32);

            /* Update the vreg for next iteration */
            rec->for_counter_vreg = new_counter;
        }

        /* Emit the loop back-edge */
        ir_emit(ir, IR_LOOP, IR_TYPE_UNKNOWN, 0, 0, 0);
        return false; /* Stop recording */
    }

    case OP_JMP_FALSE:
    {
        uint16_t cond = rec_pop(rec);
        /* Record the taken branch direction */
        /* Guard that we take the same path */
        uint32_t snap = make_snapshot(rec, pc - 1);
        ir_emit_guard(ir, IR_GUARD_FALSE, cond, snap, pc - 1);
        break;
    }

    case OP_JMP_TRUE:
    {
        uint16_t cond = rec_pop(rec);
        uint32_t snap = make_snapshot(rec, pc - 1);
        ir_emit_guard(ir, IR_GUARD_TRUE, cond, snap, pc - 1);
        break;
    }

    /* ============ FOR_COUNT - The most important loop ============ */
    case OP_FOR_COUNT:
    {
        /* Special handling for counting loops */
        /* This is recorded as a loop structure */
        uint8_t counter_slot = pc[0];
        uint8_t end_slot = pc[1];
        uint8_t var_slot = pc[2];

        /* Load counter and end values */
        uint64_t counter_val = bp[counter_slot];
        uint64_t end_val = bp[end_slot];

        uint16_t counter = ir_vreg(ir, IR_TYPE_INT32);
        uint16_t end = ir_vreg(ir, IR_TYPE_INT32);
        uint16_t var = ir_vreg(ir, IR_TYPE_INT32);

        ir_emit_load(ir, counter, counter_slot, IR_TYPE_INT32);
        ir_emit_load(ir, end, end_slot, IR_TYPE_INT32);

        /* Guard types */
        uint32_t snap = make_snapshot(rec, pc - 1);
        ir_emit_guard(ir, IR_GUARD_INT, counter, snap, pc - 1);
        ir_emit_guard(ir, IR_GUARD_INT, end, snap, pc - 1);

        /* Comparison: counter < end */
        uint16_t cmp_result = ir_vreg(ir, IR_TYPE_BOOL);
        ir_emit(ir, IR_LT_INT, IR_TYPE_BOOL, cmp_result, counter, end);

        /* Guard that we continue (counter < end) */
        ir_emit_guard(ir, IR_GUARD_TRUE, cmp_result, snap, pc - 1);

        /* Set loop variable = counter (BEFORE increment!) */
        ir_emit(ir, IR_MOV, IR_TYPE_INT32, var, counter, 0);
        ir_emit_store(ir, var_slot, var);
        rec_set_local(rec, var_slot, var, IR_TYPE_INT32);

        /* DON'T increment here - save info for OP_LOOP to do it */
        rec->for_counter_slot = counter_slot;
        rec->for_counter_vreg = counter;
        rec->has_for_loop = true;

        /* Track end value for reload at loop back edge */
        rec_set_local(rec, counter_slot, counter, IR_TYPE_INT32);
        rec_set_local(rec, end_slot, end, IR_TYPE_INT32);

        break;
    }

    /* ============ Unsupported - Abort Recording ============ */
    case OP_CALL:
    case OP_RETURN:
    case OP_PRINT:
    case OP_PRINTLN:
        /* These could be supported later with more work */
        recorder_abort(rec, "unsupported opcode in trace");
        return false;

    default:
        /* Unknown opcode - abort */
        recorder_abort(rec, "unknown opcode in trace");
        return false;
    }

    return true; /* Continue recording */
}

bool recorder_finish(TraceRecorder *rec)
{
    if (!rec->active)
        return false;
    if (rec->aborted)
        return false;

    TraceIR *ir = rec->ir;

    /* Emit final store for all modified locals */
    for (int i = 0; i < 256; i++)
    {
        if (rec->locals[i] != 0)
        {
            /* Already stored during recording */
        }
    }

    /* Emit loop back or return */
    if (ir->has_loop)
    {
        ir_emit(ir, IR_LOOP, IR_TYPE_UNKNOWN, 0, 0, 0);
    }
    else
    {
        ir_emit(ir, IR_RET, IR_TYPE_UNKNOWN, 0, 0, 0);
    }

    rec->active = false;
    return true;
}

void recorder_abort(TraceRecorder *rec, const char *reason)
{
    rec->aborted = true;
    rec->abort_reason = reason;
    rec->active = false;
}
