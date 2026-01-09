/*
 * Pseudocode Tracing JIT - Phase 2-3: Register Allocation & Deopt
 *
 * Simple linear-scan register allocator for trace IR.
 * Maps virtual registers to physical x86-64 registers.
 * Handles spilling at trace boundaries.
 *
 * Deoptimization support: reconstruct interpreter state from snapshots.
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

/* ============================================================
 * x86-64 Physical Registers
 * ============================================================ */

#define PHYS_RAX 0
#define PHYS_RCX 1
#define PHYS_RDX 2
#define PHYS_RBX 3
#define PHYS_RSP 4 /* Stack pointer - never allocate */
#define PHYS_RBP 5 /* Frame pointer - never allocate */
#define PHYS_RSI 6
#define PHYS_RDI 7
#define PHYS_R8 8
#define PHYS_R9 9
#define PHYS_R10 10
#define PHYS_R11 11
#define PHYS_R12 12
#define PHYS_R13 13
#define PHYS_R14 14
#define PHYS_R15 15

/* SSE registers for floats */
#define PHYS_XMM0 16
#define PHYS_XMM1 17
#define PHYS_XMM2 18
#define PHYS_XMM3 19
#define PHYS_XMM4 20
#define PHYS_XMM5 21
#define PHYS_XMM6 22
#define PHYS_XMM7 23

#define NUM_INT_REGS 16
#define NUM_XMM_REGS 8
#define NUM_PHYS_REGS 24

/* Allocatable integer registers (excluding RSP, RBP, RDI for bp) */
static const int ALLOC_INT_REGS[] = {
    PHYS_RAX, PHYS_RCX, PHYS_RDX,
    PHYS_R8, PHYS_R9, PHYS_R10, PHYS_R11,
    PHYS_R14, PHYS_R15 /* Callee-saved, good for loop vars */
};
#define NUM_ALLOC_INT_REGS 9

/* Allocatable SSE registers */
static const int ALLOC_XMM_REGS[] = {
    PHYS_XMM0, PHYS_XMM1, PHYS_XMM2, PHYS_XMM3,
    PHYS_XMM4, PHYS_XMM5, PHYS_XMM6, PHYS_XMM7};
#define NUM_ALLOC_XMM_REGS 8

/* ============================================================
 * Register Allocator State
 * ============================================================ */

typedef struct
{
    /* Which vreg is in each physical register (-1 = free) */
    int16_t phys_to_vreg[NUM_PHYS_REGS];

    /* Physical register for each vreg (-1 = spilled/unallocated) */
    int16_t vreg_to_phys[IR_MAX_VREGS];

    /* Spill slots */
    int16_t vreg_to_spill[IR_MAX_VREGS];
    int16_t next_spill_slot;

    /* Liveness info */
    uint32_t vreg_last_use[IR_MAX_VREGS];

} RegAlloc;

/* ============================================================
 * Liveness Analysis
 * ============================================================ */

static void compute_liveness(TraceIR *ir, RegAlloc *ra)
{
    /* Simple backwards pass to find last use of each vreg */
    for (int i = 0; i < IR_MAX_VREGS; i++)
    {
        ra->vreg_last_use[i] = 0;
    }

    for (uint32_t i = ir->nops; i > 0; i--)
    {
        IRIns *ins = &ir->ops[i - 1];

        /* Mark uses */
        if (ins->src1 > 0 && ra->vreg_last_use[ins->src1] == 0)
        {
            ra->vreg_last_use[ins->src1] = i;
        }
        if (ins->src2 > 0 && ra->vreg_last_use[ins->src2] == 0)
        {
            ra->vreg_last_use[ins->src2] = i;
        }

        /* dst defines the vreg, so last_use should be after this */
        if (ins->dst > 0 && ra->vreg_last_use[ins->dst] == 0)
        {
            ra->vreg_last_use[ins->dst] = i;
        }
    }
}

/* ============================================================
 * Register Allocation
 * ============================================================ */

void regalloc_init(RegAlloc *ra)
{
    memset(ra, 0, sizeof(RegAlloc));
    for (int i = 0; i < NUM_PHYS_REGS; i++)
    {
        ra->phys_to_vreg[i] = -1;
    }
    for (int i = 0; i < IR_MAX_VREGS; i++)
    {
        ra->vreg_to_phys[i] = -1;
        ra->vreg_to_spill[i] = -1;
    }
    ra->next_spill_slot = 0;
}

/* Find a free integer register */
static int find_free_int_reg(RegAlloc *ra)
{
    for (int i = 0; i < NUM_ALLOC_INT_REGS; i++)
    {
        int reg = ALLOC_INT_REGS[i];
        if (ra->phys_to_vreg[reg] == -1)
        {
            return reg;
        }
    }
    return -1;
}

/* Find a free XMM register */
static int find_free_xmm_reg(RegAlloc *ra)
{
    for (int i = 0; i < NUM_ALLOC_XMM_REGS; i++)
    {
        int reg = ALLOC_XMM_REGS[i];
        if (ra->phys_to_vreg[reg] == -1)
        {
            return reg;
        }
    }
    return -1;
}

/* Evict the least recently used register */
static int evict_int_reg(RegAlloc *ra, TraceIR *ir, uint32_t current_pos)
{
    int best_reg = -1;
    uint32_t best_dist = 0;

    for (int i = 0; i < NUM_ALLOC_INT_REGS; i++)
    {
        int reg = ALLOC_INT_REGS[i];
        int vreg = ra->phys_to_vreg[reg];
        if (vreg >= 0)
        {
            uint32_t last_use = ra->vreg_last_use[vreg];
            uint32_t dist = (last_use > current_pos) ? last_use - current_pos : 0;
            if (dist > best_dist || best_reg < 0)
            {
                best_dist = dist;
                best_reg = reg;
            }
        }
    }

    if (best_reg >= 0)
    {
        int vreg = ra->phys_to_vreg[best_reg];
        /* Spill this vreg */
        ra->vreg_to_spill[vreg] = ra->next_spill_slot++;
        ra->vreg_to_phys[vreg] = -1;
        ra->phys_to_vreg[best_reg] = -1;
    }

    return best_reg;
}

/* Allocate a physical register for a virtual register */
int regalloc_alloc(RegAlloc *ra, TraceIR *ir, uint16_t vreg, uint32_t pos)
{
    if (vreg == 0)
        return -1;

    /* Already allocated? */
    if (ra->vreg_to_phys[vreg] >= 0)
    {
        return ra->vreg_to_phys[vreg];
    }

    IRType type = ir->vregs[vreg].type;
    int phys;

    if (type == IR_TYPE_DOUBLE)
    {
        phys = find_free_xmm_reg(ra);
        /* TODO: evict XMM if needed */
    }
    else
    {
        phys = find_free_int_reg(ra);
        if (phys < 0)
        {
            phys = evict_int_reg(ra, ir, pos);
        }
    }

    if (phys >= 0)
    {
        ra->vreg_to_phys[vreg] = phys;
        ra->phys_to_vreg[phys] = vreg;
        ir->vregs[vreg].phys_reg = phys;
    }

    return phys;
}

/* Free a register (vreg is dead) */
void regalloc_free(RegAlloc *ra, uint16_t vreg)
{
    if (vreg == 0)
        return;

    int phys = ra->vreg_to_phys[vreg];
    if (phys >= 0)
    {
        ra->phys_to_vreg[phys] = -1;
        ra->vreg_to_phys[vreg] = -1;
    }
}

/* Allocate registers for entire trace */
void regalloc_run(TraceIR *ir, RegAlloc *ra)
{
    regalloc_init(ra);
    compute_liveness(ir, ra);

    /* Reserve RDI for base pointer */
    ra->phys_to_vreg[PHYS_RDI] = -2; /* -2 = reserved */

    /* Reserve callee-saved for loop variables */
    /* R12 = counter, R13 = end */
    ra->phys_to_vreg[PHYS_R12] = -2;
    ra->phys_to_vreg[PHYS_R13] = -2;

    /* Linear scan allocation */
    for (uint32_t i = 0; i < ir->nops; i++)
    {
        IRIns *ins = &ir->ops[i];

        /* Allocate sources first */
        if (ins->src1 > 0)
        {
            regalloc_alloc(ra, ir, ins->src1, i);
        }
        if (ins->src2 > 0)
        {
            regalloc_alloc(ra, ir, ins->src2, i);
        }

        /* Then allocate destination */
        if (ins->dst > 0)
        {
            regalloc_alloc(ra, ir, ins->dst, i);
        }

        /* Free dead vregs */
        for (uint16_t v = 1; v < ir->next_vreg; v++)
        {
            if (ra->vreg_last_use[v] == i + 1)
            {
                regalloc_free(ra, v);
            }
        }
    }
}

/* ============================================================
 * Deoptimization
 *
 * On guard failure, we need to:
 * 1. Restore interpreter state from snapshot
 * 2. Jump back to interpreter at recorded PC
 * ============================================================ */

typedef struct
{
    uint8_t *pc;                       /* PC to resume at */
    Value *bp;                         /* Base pointer */
    int64_t values[IR_SNAPSHOT_SLOTS]; /* Unboxed values to restore */
    uint8_t slots[IR_SNAPSHOT_SLOTS];  /* Slot indices */
    IRType types[IR_SNAPSHOT_SLOTS];   /* Types */
    uint8_t nslots;
} DeoptState;

/* Global deopt state (set by exit stub, read by interpreter) */
static DeoptState g_deopt_state;
static bool g_deopt_pending = false;

/* Reconstruct interpreter state from a snapshot */
void deopt_reconstruct(TraceIR *ir, uint32_t snapshot_idx,
                       Value *bp, void *native_regs)
{
    if (snapshot_idx >= ir->nsnapshots)
        return;

    IRSnapshot *snap = &ir->snapshots[snapshot_idx];

    /* Save deopt state for interpreter to pick up */
    g_deopt_state.pc = snap->pc;
    g_deopt_state.bp = bp;
    g_deopt_state.nslots = snap->nslots;

    /* For each slot in snapshot, get the value and rebox it */
    for (int i = 0; i < snap->nslots; i++)
    {
        uint16_t vreg = snap->vregs[i];
        IRType type = snap->types[i];
        int phys = ir->vregs[vreg].phys_reg;

        /* Get value from physical register or spill slot */
        int64_t val = 0;
        if (phys >= 0 && native_regs != NULL)
        {
            /* Read from saved register state */
            int64_t *regs = (int64_t *)native_regs;
            val = regs[phys];
        }
        else
        {
            /* Value was spilled - would need to read from stack */
            /* For now, use the current bp value */
            val = as_int(bp[snap->slots[i]]);
        }

        g_deopt_state.slots[i] = snap->slots[i];
        g_deopt_state.values[i] = val;
        g_deopt_state.types[i] = type;
    }

    g_deopt_pending = true;
}

/* Called by interpreter to apply deopt state */
bool deopt_apply(uint8_t **pc, Value **bp)
{
    if (!g_deopt_pending)
        return false;

    /* Restore locals */
    for (int i = 0; i < g_deopt_state.nslots; i++)
    {
        uint8_t slot = g_deopt_state.slots[i];
        int64_t val = g_deopt_state.values[i];
        IRType type = g_deopt_state.types[i];

        /* Rebox the value */
        if (type == IR_TYPE_INT32 || type == IR_TYPE_INT64)
        {
            (*bp)[slot] = val_int((int32_t)val);
        }
        else if (type == IR_TYPE_DOUBLE)
        {
            union
            {
                int64_t i;
                double d;
            } u;
            u.i = val;
            (*bp)[slot] = val_num(u.d);
        }
        else if (type == IR_TYPE_BOOL)
        {
            (*bp)[slot] = val ? VAL_TRUE : VAL_FALSE;
        }
        else if (type == IR_TYPE_NIL)
        {
            (*bp)[slot] = VAL_NIL;
        }
        else
        {
            /* Boxed value - restore as-is */
            (*bp)[slot] = (Value)val;
        }
    }

    /* Set PC to resume point */
    *pc = g_deopt_state.pc;

    g_deopt_pending = false;
    return true;
}

/* Check if deopt is pending */
bool deopt_pending(void)
{
    return g_deopt_pending;
}

/* Clear deopt state */
void deopt_clear(void)
{
    g_deopt_pending = false;
}
