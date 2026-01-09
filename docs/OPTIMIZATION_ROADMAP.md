# Optimization Roadmap: Closing the Gap to C

**Current Status**: ~15-22x slower than C  
**Target**: 2-5x slower than C (LuaJIT territory)  
**Theoretical Minimum**: 1.5x slower (due to dynamic typing overhead)

---

## Phase 1: Low-Hanging Fruit (Expected: 2-3x improvement)

### 1.1 Constant Folding at Compile Time
**Current**: All arithmetic happens at runtime  
**Goal**: Evaluate `3 + 4` → `7` during compilation

```c
// In compiler.c, during number() parsing
if (current operation is binary && both operands are constants) {
    emit_constant(evaluate(left op right));
} else {
    emit_normal_bytecode();
}
```

### 1.2 Dead Code Elimination
**Current**: Unreachable code is still compiled  
**Goal**: Skip code after unconditional jumps/returns

### 1.3 Strength Reduction
**Current**: `x * 2` uses multiplication  
**Goal**: `x * 2` → `x + x` or `x << 1`

```c
// Powers of 2 multiplication → shift
if (IS_INT(b) && is_power_of_2(as_int(b))) {
    emit_byte(OP_LSHIFT);
    emit_byte(log2(as_int(b)));
}
```

### 1.4 Common Subexpression Elimination
Track computed values, reuse instead of recomputing.

---

## Phase 2: Type Specialization (Expected: 2-4x improvement)

### 2.1 Type Tracking
Track types of local variables during compilation:

```c
typedef enum {
    TYPE_UNKNOWN,    // Could be anything
    TYPE_INT,        // Known to be integer
    TYPE_NUM,        // Known to be float
    TYPE_STRING,     // Known to be string
    TYPE_ARRAY,      // Known to be array
    TYPE_BOOL,       // Known to be boolean
} TrackedType;

typedef struct {
    TrackedType type;
    bool constant;
    Value const_value;
} VarInfo;
```

### 2.2 Specialized Opcodes
Generate different opcodes based on known types:

```c
// Instead of generic OP_ADD:
OP_ADD_II,      // int + int → int
OP_ADD_NN,      // num + num → num  
OP_ADD_IN,      // int + num → num
OP_ADD_NI,      // num + int → num
OP_ADD_SS,      // string + string → string (concat)
```

### 2.3 Integer-Only Loops
When a loop counter is known to be an integer:

```c
// Fast path: no NaN-boxing, no type checks
case OP_FOR_INT:
    int32_t* counter = (int32_t*)(bp + slot);
    if (++(*counter) >= end) ip += jump;
    DISPATCH();
```

### 2.4 Type Guards
Insert type checks at loop entry, optimize loop body:

```c
// Guard at loop entry
if (!IS_INT(counter)) goto slow_path;
// Fast integer-only loop
while (counter < end) { ... }
```

---

## Phase 3: Inline Caching (Expected: 3-5x improvement for OOP code)

### 3.1 Monomorphic Inline Cache
Cache the last looked-up property offset:

```c
typedef struct {
    ObjString* cached_key;
    uint32_t cached_offset;
    uint32_t shape_id;  // Detect object shape changes
} InlineCache;

// Fast path: if same key and shape, use cached offset
case OP_GET_PROPERTY_CACHED:
    InlineCache* ic = &caches[READ_BYTE()];
    if (obj->shape == ic->shape_id && key == ic->cached_key) {
        PUSH(obj->fields[ic->cached_offset]);  // Direct access!
    } else {
        // Slow path: lookup and update cache
        ...
    }
```

### 3.2 Polymorphic Inline Cache
Handle 2-4 common shapes:

```c
typedef struct {
    struct { uint32_t shape_id; uint32_t offset; } entries[4];
    uint8_t count;
} PolyIC;
```

### 3.3 Call-Site Caching
Cache function lookups:

```c
typedef struct {
    ObjString* method_name;
    ObjClass* cached_class;
    ObjClosure* cached_method;
} CallSiteCache;
```

---

## Phase 4: Tracing JIT (Expected: 5-10x improvement)

### 4.1 Trace Recording
Record hot execution paths:

```c
typedef struct {
    uint8_t* start_ip;      // Where trace starts
    uint32_t opcode;        // Recorded opcode
    Value operands[3];      // Recorded operand values
    TrackedType types[3];   // Recorded types
} TraceIR;

typedef struct {
    TraceIR* ops;
    uint32_t length;
    uint32_t capacity;
    void* compiled_code;    // JIT-compiled native code
    uint32_t execution_count;
} Trace;
```

### 4.2 Hot Loop Detection

```c
#define HOT_THRESHOLD 100

typedef struct {
    uint8_t* loop_header;
    uint32_t count;
} LoopCounter;

// At every backward jump
if (++loop_counters[ip].count > HOT_THRESHOLD) {
    start_recording_trace(ip);
}
```

### 4.3 Trace IR Generation
Convert bytecode to SSA-form IR:

```c
typedef enum {
    IR_CONST,      // r1 = 42
    IR_LOAD,       // r2 = LOAD slot
    IR_STORE,      // STORE slot, r2
    IR_ADD_II,     // r3 = r1 + r2 (int + int)
    IR_ADD_NN,     // r4 = r1 + r2 (num + num)
    IR_MUL,        
    IR_CMP_LT,     // r5 = r1 < r2
    IR_GUARD_INT,  // if !IS_INT(r1) exit_trace
    IR_GUARD_TYPE, // if TYPE(r1) != T exit_trace
    IR_JUMP,
    IR_BRANCH,
    IR_CALL,
    IR_RET,
    IR_PHI,        // SSA phi node for loops
} IROpcode;

typedef struct {
    IROpcode op;
    uint16_t dest;     // Destination register
    uint16_t arg1;     // Source 1
    uint16_t arg2;     // Source 2
    TrackedType type;  // Known type of result
} IRInst;
```

### 4.4 IR Optimizations
Apply optimizations to the trace IR:

1. **Loop Invariant Code Motion**: Move unchanging computations out of loops
2. **Dead Code Elimination**: Remove unused computations
3. **Constant Propagation**: Replace variables with known values
4. **Strength Reduction**: `x * 2` → `x << 1`
5. **Allocation Sinking**: Delay allocations until needed
6. **Alias Analysis**: Track which values can alias

### 4.5 Native Code Generation
Compile IR to x86-64:

```c
// Register allocation
typedef struct {
    int32_t vreg;           // Virtual register
    int32_t physical_reg;   // RAX, RBX, etc. (-1 = spilled)
    int32_t spill_slot;     // Stack slot if spilled
    bool dirty;             // Needs writeback?
} RegAlloc;

// Code generation
void jit_compile_trace(Trace* trace) {
    MachineCode mc;
    mc_init(&mc, 65536);
    
    // Prologue
    emit_push_callee_saved(&mc);
    
    for (uint32_t i = 0; i < trace->ir_length; i++) {
        IRInst* inst = &trace->ir[i];
        switch (inst->op) {
            case IR_ADD_II:
                emit_mov_reg_reg(&mc, get_reg(inst->dest), get_reg(inst->arg1));
                emit_add_reg_reg(&mc, get_reg(inst->dest), get_reg(inst->arg2));
                break;
            case IR_GUARD_INT:
                // Check NaN-boxing tag, branch to exit if not int
                emit_test_nan_tag(&mc, get_reg(inst->arg1), TAG_INT);
                emit_jne_to_exit(&mc, trace->exit_stubs[i]);
                break;
            // ... many more cases
        }
    }
    
    // Loop back
    emit_jmp(&mc, mc.code);
    
    trace->compiled_code = mc_finalize(&mc);
}
```

### 4.6 Side Exits and Deoptimization

```c
typedef struct {
    uint8_t* bytecode_ip;   // Where to resume in interpreter
    Value* snapshot;        // Values of all live variables
    uint32_t snapshot_size;
} SideExit;

// When a guard fails, restore state and return to interpreter
void exit_trace(SideExit* exit) {
    // Restore interpreter state
    vm->ip = exit->bytecode_ip;
    memcpy(vm->stack, exit->snapshot, exit->snapshot_size * sizeof(Value));
    // Continue in interpreter
}
```

---

## Phase 5: Escape Analysis (Expected: 1.5-2x for allocation-heavy code)

### 5.1 Object Escape Detection

```c
typedef enum {
    ESCAPE_NONE,      // Never escapes function
    ESCAPE_ARG,       // Passed as argument (may escape)
    ESCAPE_RETURN,    // Returned from function
    ESCAPE_GLOBAL,    // Stored in global
    ESCAPE_UNKNOWN,   // Unknown escape
} EscapeState;

// Track for each allocation
void analyze_escape(ObjAlloc* alloc) {
    for each use of alloc:
        if (use is RETURN) alloc->escape = ESCAPE_RETURN;
        if (use is STORE_GLOBAL) alloc->escape = ESCAPE_GLOBAL;
        if (use is CALL arg) alloc->escape = ESCAPE_ARG;
}
```

### 5.2 Stack Allocation

```c
// For non-escaping objects, allocate on stack
if (alloc->escape == ESCAPE_NONE) {
    // Allocate in function's stack frame
    obj = (Obj*)(bp + frame->stack_alloc_offset);
    frame->stack_alloc_offset += sizeof(ObjInstance);
}
```

### 5.3 Scalar Replacement
Replace object fields with local variables:

```c
// Before:
let p = {x: 1, y: 2}
return p.x + p.y

// After optimization (no allocation!):
let p_x = 1
let p_y = 2
return p_x + p_y
```

---

## Phase 6: Advanced Optimizations (Expected: 1.2-1.5x additional)

### 6.1 Function Inlining

```c
#define INLINE_THRESHOLD 50  // Bytecode length

bool should_inline(ObjFunction* callee) {
    return callee->chunk.count < INLINE_THRESHOLD &&
           callee->arity <= 4 &&
           !callee->has_varargs;
}

// During compilation, copy callee's bytecode into caller
void inline_call(Compiler* c, ObjFunction* callee) {
    // Rename locals to avoid conflicts
    // Copy bytecode, adjusting jump offsets
    // Replace OP_RETURN with jump to continuation
}
```

### 6.2 Loop Unrolling

```c
// Unroll small loops 4x
if (loop_body_size < 20 && iteration_count_known) {
    for (int i = 0; i < 4; i++) {
        emit_loop_body();
    }
    emit_counter_add(4);
    emit_loop_back();
}
```

### 6.3 Tail Call Optimization

```c
// Detect: return f(args)
case OP_RETURN:
    if (previous_op == OP_CALL && call_is_tail_position) {
        // Reuse current frame instead of creating new one
        emit_byte(OP_TAIL_CALL);
    }
```

### 6.4 Hidden Classes / Shapes (for objects)

```c
typedef struct Shape {
    uint32_t id;
    struct Shape* parent;
    ObjString* new_key;
    uint32_t key_offset;
} Shape;

// Objects with same property order share a shape
// Property access becomes indexed array lookup
```

---

## Implementation Priority

| Phase | Effort | Impact | Priority |
|-------|--------|--------|----------|
| 1. Low-hanging fruit | Low | 2-3x | **HIGH** |
| 2. Type specialization | Medium | 2-4x | **HIGH** |
| 3. Inline caching | Medium | 3-5x | Medium |
| 4. Tracing JIT | Very High | 5-10x | **CRITICAL** |
| 5. Escape analysis | High | 1.5-2x | Low |
| 6. Advanced opts | High | 1.2-1.5x | Low |

---

## Expected Cumulative Results

| After Phase | vs C | vs Python |
|-------------|------|-----------|
| Baseline | 15-22x slower | 3-9x faster |
| Phase 1 | 10-15x slower | 5-12x faster |
| Phase 2 | 5-8x slower | 10-20x faster |
| Phase 3 | 4-6x slower | 15-25x faster |
| Phase 4 | 2-4x slower | 25-50x faster |
| Phase 5 | 1.5-3x slower | 30-60x faster |
| Phase 6 | **1.5-2.5x slower** | 40-80x faster |

---

## Reference Implementations

- **LuaJIT**: Best-in-class tracing JIT, 1.5-3x C speed
- **V8**: Hidden classes, inline caching, TurboFan optimizer
- **PyPy**: Meta-tracing JIT, RPython
- **GraalJS**: Partial evaluation, Truffle framework

---

## Files to Create/Modify

1. `cvm/type_info.h` - Type tracking structures
2. `cvm/type_info.c` - Type inference during compilation
3. `cvm/trace.h` - Trace recording structures
4. `cvm/trace.c` - Trace recorder
5. `cvm/ir.h` - Intermediate representation
6. `cvm/ir.c` - IR builder and optimizer
7. `cvm/codegen.c` - x86-64 code generator (rewrite jit.c)
8. `cvm/inline_cache.h` - IC structures
9. `cvm/escape.c` - Escape analysis
10. `cvm/compiler.c` - Add type tracking, inlining
11. `cvm/vm.c` - Add trace-based dispatch, IC handlers

**Estimated total effort: 3-6 months for full implementation**
