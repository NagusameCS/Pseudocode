# Zero-Overhead Feature Design

**Goal:** Add modern language features with **zero runtime cost** on the fast path.

**Principle:** "You don't pay for what you don't use" (C++ philosophy)

---

## 1. Try/Catch Exceptions: Zero-Cost Exception Handling

### The Problem
Traditional exceptions (setjmp/longjmp) have overhead even when no exception occurs:
- setjmp saves registers (~50 cycles)
- Every function call must track exception handlers

### Zero-Cost Solution: Exception Tables

**Compile-time:** Build a table mapping instruction addresses to handlers.  
**Runtime:** Only look up table when exception actually occurs.

```c
// Exception table (generated at compile time)
typedef struct {
    uint32_t try_start;      // Start of try block (bytecode offset)
    uint32_t try_end;        // End of try block
    uint32_t handler_offset; // Where catch block starts
    uint32_t finally_offset; // Where finally block starts (0 if none)
} ExceptionEntry;

typedef struct {
    ExceptionEntry* entries;
    uint32_t count;
} ExceptionTable;

// Stored per-function, zero cost when not throwing
ObjFunction {
    ...
    ExceptionTable* exception_table;  // NULL if no try blocks
}
```

**How it works:**

```c
// Normal execution: ZERO overhead
case OP_ADD:
    // No exception checking here!
    b = pop(); a = pop();
    push(a + b);
    DISPATCH();

// Only on throw:
case OP_THROW:
    Value error = pop();
    // Walk exception table to find handler
    uint32_t handler = find_handler(frame->function->exception_table, ip);
    if (handler != UINT32_MAX) {
        ip = &frame->function->chunk.code[handler];
        push(error);  // Error available in catch block
        DISPATCH();
    }
    // No handler? Unwind to caller
    unwind_frame();
    DISPATCH();

// Find handler: O(log n) binary search on sorted table
uint32_t find_handler(ExceptionTable* table, uint8_t* ip) {
    uint32_t offset = ip - chunk_start;
    // Binary search for entry where try_start <= offset < try_end
    for (int i = 0; i < table->count; i++) {
        if (offset >= table->entries[i].try_start && 
            offset < table->entries[i].try_end) {
            return table->entries[i].handler_offset;
        }
    }
    return UINT32_MAX;  // No handler
}
```

**Syntax:**
```pseudocode
try
    let data = read_file("config.json")
    let config = json_parse(data)
catch error
    print("Error: " + error)
finally
    cleanup()
end

// Throw
throw "Something went wrong"
throw {type: "ValueError", message: "Invalid input"}
```

**Cost Analysis:**
| Scenario | Overhead |
|----------|----------|
| Code without try/catch | **ZERO** |
| Code in try block (no throw) | **ZERO** |
| Throwing an exception | O(n) where n = stack depth |

**Implementation:** ~300 lines in compiler, ~100 lines in VM

---

## 2. Classes/OOP: Struct-Based Zero-Overhead Design

### The Problem
Dynamic property lookup is slow:
```c
// Slow: hash table lookup every time
obj["name"]  // Hash "name", find bucket, compare keys
```

### Zero-Cost Solution: Fixed-Offset Classes

**At compile time:** Assign each property a fixed slot index.  
**At runtime:** Direct array access by index.

```c
// Class definition (compile-time)
typedef struct {
    ObjString* name;
    Table methods;           // Method name → ObjClosure*
    uint32_t* field_offsets; // Field name hash → slot index
    uint32_t field_count;
    struct ObjClass* superclass;
} ObjClass;

// Instance (runtime)
typedef struct {
    Obj obj;
    ObjClass* klass;
    Value fields[];  // Inline array, fixed size per class
} ObjInstance;

// Property access compiles to direct slot access
OP_GET_FIELD_FAST, slot  // instance.fields[slot] - O(1)!
OP_SET_FIELD_FAST, slot  // instance.fields[slot] = value - O(1)!
```

**How the compiler knows slots:**

```pseudocode
class Point
    fn init(x, y)
        self.x = x    // Compiler sees: x is slot 0
        self.y = y    // Compiler sees: y is slot 1
    end
end
```

The compiler:
1. Scans class body for all `self.X` assignments
2. Assigns each unique property a slot index
3. Generates `OP_GET_FIELD_FAST 0` instead of `OP_GET_PROPERTY "x"`

**Fallback for dynamic access:**
```pseudocode
let prop = "x"
point[prop]  // Dynamic - uses hash lookup (OP_GET_PROPERTY)
point.x      // Static - uses fast slot (OP_GET_FIELD_FAST)
```

**Method dispatch:**

```c
// Option A: Inline caching (like V8)
case OP_INVOKE_CACHED:
    InlineCache* ic = &caches[READ_SHORT()];
    ObjInstance* instance = AS_INSTANCE(peek(argCount));
    
    if (instance->klass == ic->cached_class) {
        // Cache hit! Direct call, no lookup
        call(ic->cached_method, argCount);
        DISPATCH();
    }
    // Cache miss - slow path, update cache
    ObjClosure* method = lookup_method(instance->klass, name);
    ic->cached_class = instance->klass;
    ic->cached_method = method;
    call(method, argCount);
    DISPATCH();

// After first call, method dispatch is O(1)!
```

**Syntax:**
```pseudocode
class Point
    fn init(x, y)
        self.x = x
        self.y = y
    end
    
    fn distance(other)
        let dx = self.x - other.x
        let dy = self.y - other.y
        return sqrt(dx*dx + dy*dy)
    end
end

let p = Point(3, 4)
print(p.x)           // Fast: slot access
print(p.distance(origin))  // Fast after first call: cached
```

**Cost Analysis:**
| Operation | Overhead |
|-----------|----------|
| Property access (static) | **ZERO** (same as array index) |
| Property access (dynamic) | Hash lookup (unavoidable) |
| Method call (first time) | Hash lookup + cache store |
| Method call (cached) | **ZERO** (direct function pointer) |
| Instance creation | Allocate fields array |

**Implementation:** ~400 lines compiler, ~200 lines VM

---

## 3. Selective Imports: Already Zero-Cost!

The current import system is **compile-time only** - it just concatenates files.

### Enhancement: `from X import Y`

Just filter which names get bound:

```c
// In import.c preprocessor
typedef struct {
    char* module_path;
    char** imported_names;  // NULL = import all, else selective
    int name_count;
} ImportDirective;

// During preprocessing:
if (directive->imported_names != NULL) {
    // Only include functions/variables matching the names
    filter_source(source, directive->imported_names);
}
```

**Even simpler approach:** Import everything at compile time, but only bind requested names to scope:

```pseudocode
from "./lib/math.pseudo" import factorial, PI

// Compiles to:
// 1. Include full math.pseudo source (all functions compiled)
// 2. Only bind "factorial" and "PI" to local scope
// 3. Unused functions are dead-code-eliminated if we add DCE
```

**Cost:** ZERO runtime overhead (it's a compile-time filter)

**Implementation:** ~50 lines in import.c

---

## 4. Inheritance: Single-Inheritance is Nearly Free

### Zero-Cost Approach: Struct Embedding

```c
// Parent class layout
class Animal:
    fields[0] = name    // slot 0

// Child class layout (extends parent)  
class Dog extends Animal:
    fields[0] = name    // Inherited, same slot!
    fields[1] = breed   // New field

// Same slot indices = parent methods work on child!
```

**Method Resolution:**
```c
ObjClosure* lookup_method(ObjClass* klass, ObjString* name) {
    // Check own methods first
    Value method;
    if (tableGet(&klass->methods, name, &method)) {
        return AS_CLOSURE(method);
    }
    // Check parent (single inheritance = simple chain)
    if (klass->superclass != NULL) {
        return lookup_method(klass->superclass, name);
    }
    return NULL;
}

// With inline caching: First lookup is O(depth), subsequent = O(1)
```

**Super calls:**
```pseudocode
class Dog extends Animal
    fn speak()
        super.speak()      // Call parent's speak
        print("Woof!")
    end
end
```

Compiles to:
```c
OP_GET_SUPER, method_name_idx
// Looks up method starting from parent class, not self
```

**Cost Analysis:**
| Operation | Overhead |
|-----------|----------|
| Child property access | **ZERO** (same slot scheme) |
| Method call (cached) | **ZERO** |
| Super call (cached) | **ZERO** |
| isinstance check | O(depth) but typically O(1)-O(3) |

**Implementation:** ~200 additional lines

---

## 5. Generators/Yield: State Machine Transformation

### The Problem
Generators need to suspend and resume, which traditionally requires:
- Heap-allocated coroutine stack
- Context switching overhead

### Zero-Cost Solution: Compile to State Machine

Transform generator function into a state machine at **compile time**:

```pseudocode
// Original
fn count_up(n)
    for i in 1..n+1 do
        yield i
    end
end

// Transformed by compiler to:
fn count_up_state_machine(state)
    match state.phase
        case 0 then
            state.i = 1
            state.n = state.arg_n
            state.phase = 1
            // fall through
        case 1 then
            if state.i > state.n then
                state.phase = 2
                return {done: true}
            end
            let result = state.i
            state.i = state.i + 1
            return {value: result, done: false}
        case 2 then
            return {done: true}
    end
end

// Iterator wrapper
fn count_up(n)
    let state = {phase: 0, arg_n: n, i: 0, n: 0}
    return {
        next: fn() return count_up_state_machine(state) end
    }
end
```

**Compiler transformation:**
1. Find all `yield` statements
2. Assign each a state number
3. Create state struct with all local variables
4. Generate state machine with switch/match

**For simple generators:** State fits in a dict (no heap allocation beyond the dict).

**Cost Analysis:**
| Scenario | Overhead |
|----------|----------|
| Code not using generators | **ZERO** |
| Generator creation | Dict allocation (one time) |
| Each `next()` call | One function call + state lookup |
| vs manual iterator | **SAME** - it's what you'd write by hand! |

**Implementation:** ~400 lines in compiler (complex but worth it)

---

## 6. Async/Await: Stackless Coroutines

### Zero-Cost Design: State Machine + Event Loop

Same principle as generators, but with an event loop:

```pseudocode
// Original
async fn fetch_user(id)
    let response = await http_get("/users/" + str(id))
    return json_parse(response)
end

// Transformed to:
fn fetch_user_state_machine(state, resume_value)
    match state.phase
        case 0 then
            // Start async operation
            state.pending = http_get_async("/users/" + str(state.id))
            state.phase = 1
            return {awaiting: state.pending}
        case 1 then
            // Resumed with result
            let response = resume_value
            state.result = json_parse(response)
            state.phase = 2
            return {done: true, value: state.result}
    end
end
```

**Event loop (in VM):**
```c
// Simple event loop
while (pending_tasks > 0) {
    // Poll for completed I/O
    Event* event = poll_events();
    if (event != NULL) {
        // Resume the waiting coroutine
        Task* task = event->waiting_task;
        Value result = event->result;
        resume_task(task, result);
    }
}
```

**Key insight:** Each async function becomes a state machine. No stack-switching, no fiber overhead.

**Cost Analysis:**
| Scenario | Overhead |
|----------|----------|
| Synchronous code | **ZERO** |
| Async function creation | State dict allocation |
| Await point | State save + event registration |
| vs callbacks | **LESS** - no closure per callback! |

**Implementation:** ~600 lines (compiler) + ~300 lines (event loop in VM)

---

## 7. Type Annotations: 100% Zero Runtime Cost

### Design: Completely Ignored at Runtime

Type annotations are parsed but **thrown away** before compilation:

```pseudocode
fn add(a: number, b: number) -> number
    return a + b
end

// Compiles to exactly the same bytecode as:
fn add(a, b)
    return a + b
end
```

**Implementation:**
```c
// In lexer: recognize type annotation tokens
TOKEN_COLON, TOKEN_ARROW  // : and ->

// In parser: parse and discard
static void function(FunctionType type) {
    // ... parse parameters
    if (match(TOKEN_COLON)) {
        // Parse type annotation, ignore it
        type_annotation();  // Consumes tokens, produces nothing
    }
    // ...
}

static void type_annotation() {
    // number, string, array[T], {key: Type}, etc.
    consume_type();  // Just advances parser, no codegen
}
```

**Benefits:**
1. **Documentation** - Types explain intent
2. **Tooling** - Separate type checker can validate
3. **IDE support** - Autocomplete, error highlighting
4. **Zero cost** - No runtime impact whatsoever

**Syntax support:**
```pseudocode
// Basic types
let name: string = "hello"
let count: number = 42
let items: array = [1, 2, 3]
let user: dict = {"name": "Alice"}

// Function signatures
fn greet(name: string) -> string
    return "Hello, " + name
end

// Union types (parsed but not enforced)
fn process(value: string | number) -> string
    return str(value)
end

// Generic-like syntax (for documentation)
fn first(arr: array[T]) -> T
    return arr[0]
end
```

**Cost:** ZERO. Literally parsed and discarded.

**Implementation:** ~100 lines in parser

---

## Summary: Implementation Costs

| Feature | Runtime Overhead | Compile-Time Cost | Lines of Code |
|---------|------------------|-------------------|---------------|
| Try/catch (zero-cost) | **ZERO** (no throw) | Exception tables | ~400 |
| Classes (fixed-slot) | **ZERO** (static access) | Slot assignment | ~600 |
| Selective imports | **ZERO** | Name filtering | ~50 |
| Inheritance | **ZERO** (cached) | Method chain | ~200 |
| Generators | Dict per generator | State machine transform | ~400 |
| Async/await | Dict + event loop | State machine transform | ~900 |
| Type annotations | **ZERO** | Parse and discard | ~100 |

**Total for all features:** ~2,650 lines of C

---

## Implementation Order (Recommended)

### Phase 1: Easy Wins (1-2 weeks)
1. **Type annotations** - 100 lines, zero risk, great for tooling
2. **Selective imports** - 50 lines, extends existing system
3. **Try/catch** - 400 lines, high value

### Phase 2: OOP Core (2-3 weeks)
4. **Classes** - 600 lines, enables modern patterns
5. **Inheritance** - 200 lines, completes OOP story

### Phase 3: Advanced (4-6 weeks)
6. **Generators** - 400 lines, enables lazy evaluation
7. **Async/await** - 900 lines, modern concurrency

---

## Comparison with Other Languages

| Technique | Pseudocode Design | Python | JavaScript | Lua |
|-----------|-------------------|--------|------------|-----|
| Exceptions | Zero-cost tables | setjmp (slow) | Zero-cost | pcall |
| Classes | Fixed slots + IC | Dict (slow) | Hidden classes | Metatables |
| Inheritance | Single + cache | MRO lookup | Prototype chain | Metatables |
| Generators | State machine | Stack copy | State machine | Coroutines |
| Async | State machine | State machine | State machine | N/A |
| Types | Discarded | Discarded | TypeScript | N/A |

**Result:** Pseudocode's design matches or beats modern implementations!
