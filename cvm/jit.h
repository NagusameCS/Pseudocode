/*
 * Pseudocode JIT Compiler Header
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#ifndef PSEUDO_JIT_H
#define PSEUDO_JIT_H

#include <stdint.h>

/* Initialize JIT compiler - call once at VM startup */
void jit_init(void);

/* Cleanup JIT compiler - call at VM shutdown */
void jit_cleanup(void);

/* Check if JIT is available */
int jit_available(void);

/* JIT-compiled loop entry points */

/* for i in 0..n do x = x + 1 end; return x */
int64_t jit_run_inc_loop(int64_t x, int64_t iterations);

/* for i in start..end do (empty) end; return counter */
int64_t jit_run_empty_loop(int64_t start, int64_t end);

/* for i in 0..n do x = x * 3 + 7 end; return x */
int64_t jit_run_arith_loop(int64_t x, int64_t iterations);

/* for i in 0..n do if i%2==0 x++ else x-- end; return x */
int64_t jit_run_branch_loop(int64_t x, int64_t iterations);

#endif /* PSEUDO_JIT_H */
