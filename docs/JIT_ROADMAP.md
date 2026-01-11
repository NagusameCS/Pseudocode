# JIT Roadmap: Path to Generally Fast

## Current State (January 2026)

**What we have:**
-  Pattern-based strength reduction JIT for ~5 specific loop patterns
-  **General loop JIT compiler** - compiles ANY for-loop to native code
-  **Global variable JIT support** - top-level scripts now JIT-compiled
- Fast bytecode interpreter (~15-20 ns/op)

**Performance Comparison (vs C with volatile):**
| Pattern | C (O3) | Pseudocode JIT | Ratio |
|---------|--------|----------------|-------|
| x=x+1 (100M) | 31ms | 0.02ms | **0.0006x** (strength reduction!) |
| x=x+5 (100M) | 35ms | 216ms | 6.2x slower |
| x=x+i (10M) | 3.2ms | 37ms | ~12x slower |
| x=x+i*2-1 (10M) | 7.7ms | 40ms | ~5x slower |
| if i<5M (10M) | 10ms | 22ms | ~2x slower |
| Global x=x+1 (10M) | - | 20ms |  Working |

**Analysis:**
- Strength-reducible patterns achieve **O(1)** time complexity
- General loops are ~5-12x slower than C due to:
  - NaN-boxing overhead (unbox/box on every memory access)
  - No register-only computation (every op goes through memory)
  - Loop control overhead

**What was implemented:**
- General bytecode-to-IR translator (~500 lines)
- Handles: GET_LOCAL, SET_LOCAL, GET_GLOBAL, SET_GLOBAL, CONST, ADD, SUB, MUL, MOD, comparisons, jumps
- Global variable resolution at compile time with hash table lookup
- Extended JIT calling convention to pass globals pointer
- Proper loop epilogue with counter increment and back-edge
- Fixed var_slot synchronization bug for correct loop variable tracking

---

## Phase 1: Basic Method JIT (2-3 weeks) -  COMPLETE

**Goal:** Compile entire functions to native code, not just recognized patterns.

### 1.1 Extend IR to Cover All Opcodes
```
Current IR ops: ~15 (LOAD_LOCAL, ADD_INT, BOX, UNBOX, etc.)
Needed IR ops: ~50 (all bytecode ops mapped to IR)
```

**New IR operations needed:**
- `IR_LOAD_GLOBAL`, `IR_STORE_GLOBAL`
- `IR_CALL`, `IR_RETURN`
- `IR_ARRAY_GET`, `IR_ARRAY_SET`, `IR_ARRAY_NEW`
- `IR_JMP`, `IR_JMP_IF`, `IR_JMP_IFNOT`
- `IR_LT`, `IR_GT`, `IR_EQ`, `IR_NEQ` (comparisons)
- `IR_MUL_INT`, `IR_DIV_INT`, `IR_MOD_INT`
- `IR_AND`, `IR_OR`, `IR_NOT`
- String operations, type checks, etc.

**Effort:** ~500-800 lines of C

### 1.2 Bytecode-to-IR Translation
Instead of pattern matching, translate ANY bytecode sequence to IR:

```c
// Current: pattern matching
if (body[0] == OP_GET_LOCAL_0 && body[1] == OP_CONST_1 && ...)

// New: general translation
for each opcode in function:
    switch(opcode) {
        case OP_GET_LOCAL: emit(IR_LOAD_LOCAL, slot); break;
        case OP_ADD: emit(IR_ADD, pop(), pop()); break;
        case OP_CALL: emit(IR_CALL, nargs); break;
        ...
    }
```

**Effort:** ~400-600 lines of C

### 1.3 Native Code Generation for All IR
Extend `trace_codegen.c` to emit x64 for all IR ops:

```c
case IR_CALL:
    // Save registers, set up args, call function pointer
    | mov rdi, [rbp + func_slot*8]
    | mov rsi, nargs
    | call ->vm_call_helper
    break;

case IR_ARRAY_GET:
    // Bounds check, load element
    | mov rax, [rbp + arr_slot*8]
    | mov rcx, [rbp + idx_slot*8]
    | cmp rcx, [rax + ARRAY_LEN_OFFSET]
    | jge ->bounds_error
    | mov rax, [rax + rcx*8 + ARRAY_DATA_OFFSET]
    break;
```

**Effort:** ~800-1200 lines of C/DynASM

### Phase 1 Total: ~1700-2600 lines, 2-3 weeks

---

## Phase 2: Type Specialization (2-3 weeks)

**Problem:** Boxing/unboxing overhead dominates runtime.

```
Current: Every value is boxed (8 bytes tag + 8 bytes value)
         add(x, y) requires: unbox(x), unbox(y), add, box(result)
         Overhead: ~10 cycles per operation

Target:  Track types, operate on raw values
         add(x, y) = x + y (1 cycle)
```

### 2.1 Type Inference
Track types through the IR:

```c
// During IR construction
v1 = LOAD_LOCAL 0          // type: unknown
v2 = UNBOX_INT v1          // type: int64
v3 = LOAD_CONST 5          // type: int64 (known at compile time)
v4 = ADD_INT v2, v3        // type: int64
v5 = BOX_INT v4            // type: boxed_int
```

### 2.2 Box Elimination
Remove unnecessary box/unbox pairs:

```c
// Before optimization
v1 = UNBOX_INT arg0
v2 = ADD_INT v1, 1
v3 = BOX_INT v2
v4 = UNBOX_INT v3    // <-- redundant!
v5 = ADD_INT v4, 2

// After optimization
v1 = UNBOX_INT arg0
v2 = ADD_INT v1, 1
v5 = ADD_INT v2, 2   // <-- v3, v4 eliminated
v6 = BOX_INT v5      // <-- box only at end
```

### 2.3 Speculative Specialization
Generate optimized code assuming types, with guards:

```c
// For: x = x + y
GUARD_INT x          // deopt if x is not int
GUARD_INT y          // deopt if y is not int
result = x + y       // raw integer add (1 cycle)
// If guard fails, fall back to interpreter
```

### Phase 2 Total: ~1000-1500 lines, 2-3 weeks

---

## Phase 3: Inline Caching & Call Optimization (1-2 weeks)

**Problem:** Function calls are slow (lookup, frame setup, etc.)

### 3.1 Inline Caching
Cache function lookups at call sites:

```c
// Before: lookup function every call
fn = globals[name_index]  // hash lookup

// After: cache at call site
if (cached_fn != NULL && cached_fn->version == current_version)
    fn = cached_fn        // direct pointer (1 cycle)
else
    fn = globals[name_index]; cache(fn)
```

### 3.2 Inlining Small Functions
For small functions (< 20 bytecodes), inline directly:

```c
// Before:
for i in 0..N:
    x = add_one(x)   // call overhead each iteration

// After (inlined):
for i in 0..N:
    x = x + 1        // no call overhead
```

### 3.3 Tail Call Optimization
Reuse stack frame for tail calls:

```c
fn factorial(n, acc):
    if n <= 1: return acc
    return factorial(n-1, n*acc)  // tail call -> jump, no new frame
```

### Phase 3 Total: ~600-900 lines, 1-2 weeks

---

## Phase 4: Loop Optimizations (1-2 weeks)

### 4.1 Loop-Invariant Code Motion
Move constant computations out of loops:

```c
// Before:
for i in 0..N:
    x = arr[0] + y * 2  // y*2 computed every iteration

// After:
temp = y * 2
for i in 0..N:
    x = arr[0] + temp   // y*2 computed once
```

### 4.2 Bounds Check Elimination
Remove redundant array bounds checks:

```c
// Before:
for i in 0..len(arr):
    check_bounds(arr, i)  // always passes!
    x = arr[i]

// After:
for i in 0..len(arr):
    x = arr[i]            // no check needed
```

### 4.3 Loop Unrolling
Unroll small loops to reduce branch overhead:

```c
// Before:
for i in 0..4: sum += arr[i]

// After:
sum += arr[0]; sum += arr[1]; sum += arr[2]; sum += arr[3]
```

### Phase 4 Total: ~500-800 lines, 1-2 weeks

---

## Summary: Total Effort

| Phase | Description | Lines of Code | Time |
|-------|-------------|---------------|------|
| 1 | Basic Method JIT | 1700-2600 | 2-3 weeks |
| 2 | Type Specialization | 1000-1500 | 2-3 weeks |
| 3 | Call Optimization | 600-900 | 1-2 weeks |
| 4 | Loop Optimizations | 500-800 | 1-2 weeks |
| **Total** | **Full JIT** | **3800-5800** | **6-10 weeks** |

---

##  Recommended Next Steps (Priority Order)

### Immediate Actions ( HIGH Priority)
1. **Function Inlining** - Recursive functions are 400x slower than Python. This is the #1 performance issue to fix.
   - Inline small functions (<50 bytecode ops) at call sites
   - Eliminates function call overhead for recursion
   - Expected: 10-50x improvement for recursive code

2. **Constant Folding in Compiler** - `3 + 4` should compile to `7`
   - Easy to implement in `compiler.c`
   - ~50-100 lines of code
   - Expected: 1.2-1.5x overall improvement

3. **Integer-Specialized Opcodes** - `OP_ADD_II` for int+int
   - Skip NaN-boxing for known integer operations
   - ~200 lines of code
   - Expected: 2-3x improvement for arithmetic-heavy code

### Short Term Actions
4. **Box Elimination Pass** - Remove redundant box/unbox pairs
5. **Dead Code Elimination** - Skip code after unconditional returns
6. **Tail Call Optimization** - Reuse stack frames

### Current Bottleneck Analysis (January 2026)
```
Benchmark: factorial(12) x 100,000 calls

Pseudocode: 27,000ms   Slow (recursive)
Python:        63ms    Fast (has call optimization)
C:              5ms    Fastest

Issue: Each recursive call:
  - Pushes new call frame (~20 ops)
  - Saves/restores registers (~10 ops)
  - Boxes/unboxes return value (~8 ops)
  
With inlining: Would be ~10ms (competitive with Python)
```

---

## Expected Performance After Full Implementation

| Benchmark | Current | After Phase 1 | After Phase 2 | Target |
|-----------|---------|---------------|---------------|--------|
| `x = x + 1` (100M) | 0.02ms | 0.02ms | 0.02ms | 0.02ms |
| `x = x + i` (10M) | 144ms | 20ms | 5ms | 2ms |
| Nested loops (1M) | 0.07ms | 0.07ms | 0.07ms | 0.07ms |
| `if i < N` (1M) | 17ms | 5ms | 2ms | 1ms |
| Array sum (1M) | ~50ms | 15ms | 3ms | 1ms |
| Function calls (1M) | ~100ms | 30ms | 5ms | 2ms |

---

## Quick Wins (Can Do Now)

If full JIT is too much, here are smaller improvements:

### A. Add More Strength Reduction Patterns (~200 lines, 1-2 days)
- `x = x + i` → Gauss formula: `x + n*(n-1)/2`
- `x = x * 2` → `x << iterations`
- Sum patterns, product patterns

### B. Compile Simple Loops Without Strength Reduction (~400 lines, 3-5 days)
- Instead of recognizing patterns, emit actual native loop
- No optimization, just 1:1 native code for loop body
- Gets ~5-10x speedup over interpreter

### C. Improve Interpreter (~100 lines, 1 day)
- Add more superinstructions for common patterns
- Optimize hot paths in existing opcodes
- Gets ~1.5-2x speedup

---

## Recommendation

**For "generally fast" with reasonable effort:**

1. **Phase 1** is the biggest bang-for-buck. A basic method JIT that compiles any function to native code would make everything 5-10x faster.

2. **Phase 2** (type specialization) is what separates V8/LuaJIT from simpler JITs. This is where you go from "faster than interpreter" to "nearly C speed."

3. **Phases 3-4** are polish - important for production but not critical for "generally fast."

**Minimum viable "generally fast":** Phase 1 + basic Phase 2 = ~2500 lines, 4-5 weeks.
