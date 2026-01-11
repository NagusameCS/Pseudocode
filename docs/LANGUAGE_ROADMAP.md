# Language Features Roadmap

**Version:** 1.0  
**Date:** January 2026  
**Status:** Active Development

---

## Current State (v1.2.0)

### Implemented Features
| Feature | Status | Notes |
|---------|--------|-------|
| Variables (`let`) | [DONE] | Dynamic typing |
| Functions (`fn`) | [DONE] | First-class, closures |
| Control flow | [DONE] | if/elif/else, for, while, match |
| Pattern matching | [DONE] | match/case with guards |
| Arrays | [DONE] | Dynamic, `[1, 2, 3]` |
| Dictionaries | [DONE] | Hash maps via dict(), dict_set(), dict_get() |
| Operators | [DONE] | Arithmetic, logical, comparison |
| String operations | [DONE] | Concatenation, 80+ builtins |
| Import system | [DONE] | Compile-time, zero overhead |
| REPL | [DONE] | Multi-line, .commands |
| CLI | [DONE] | -e, -h, -v, -j, -d flags |
| JIT compiler | [DONE] | Trace-based, x86-64, 64 IR ops |
| Regex | [DONE] | PCRE2-based, via lib/regex.pseudo |
| Classes | [DONE] | Basic classes with init/methods |

### Missing (Compared to Python/JS/Ruby)
| Feature | Difficulty | Priority | Status |
|---------|------------|----------|--------|
| Try/catch exceptions | Medium | HIGH | Not started |
| Class inheritance | Easy | HIGH | Not started |
| Async/await | Hard | MEDIUM | Not started |
| Generators/iterators | Medium | MEDIUM | Not started |
| Decorators | Easy | LOW | Not started |
| Selective imports | Easy | MEDIUM | Not started |
| Optional typing | Hard | LOW | Not started |

### Known Issues
- **VM if-condition bug**: Direct comparisons in `if` statements inside for-loops may not work correctly. Workaround: store comparison result in a variable first.

---

## Phase 1: Error Handling (Priority: HIGH)

### 1.1 Try/Catch/Finally
**Goal:** Structured error handling instead of returning error dicts

```pseudocode
// Proposed syntax
try
    let data = read_file("config.json")
    let config = json_parse(data)
catch error
    print("Failed to load config: " + error.message)
finally
    cleanup()
end

// Throw errors
fn divide(a, b)
    if b == 0 then
        throw "Division by zero"
    end
    return a / b
end

// Error types
throw "simple message"
throw {type: "ValueError", message: "Invalid input", code: 400}
```

**Implementation:**
1. Add `try`, `catch`, `finally`, `throw` keywords to lexer
2. New opcodes: `OP_TRY`, `OP_CATCH`, `OP_THROW`, `OP_FINALLY`
3. Exception stack in VM for unwinding
4. Update compiler for exception blocks

**Effort:** ~500 lines of C

---

### 1.2 Result Type (Alternative)
**Goal:** Explicit error handling without exceptions

```pseudocode
// Built-in result handling
fn safe_divide(a, b)
    if b == 0 then
        return error("Division by zero")
    end
    return ok(a / b)
end

let result = safe_divide(10, 0)
if is_error(result) then
    print("Error: " + result.message)
else
    print("Result: " + unwrap(result))
end

// Or with match
match safe_divide(10, 2)
    case {ok: value} then
        print("Got: " + str(value))
    case {error: msg} then
        print("Error: " + msg)
end
```

**Effort:** ~100 lines (library only)

---

## Phase 2: Object-Oriented Programming (Priority: HIGH)

### 2.1 Basic Classes
**Goal:** Define custom types with methods

```pseudocode
// Class definition
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
    
    fn to_string()
        return "Point(" + str(self.x) + ", " + str(self.y) + ")"
    end
end

// Usage
let p1 = Point(0, 0)
let p2 = Point(3, 4)
print(p1.distance(p2))  // 5
```

**Implementation:**
1. Add `class`, `self` keywords
2. `OP_CLASS`, `OP_INSTANCE`, `OP_GET_PROPERTY`, `OP_SET_PROPERTY`
3. Method dispatch via vtable or inline cache
4. Constructor (`init`) special handling

**Effort:** ~800 lines of C

---

### 2.2 Inheritance
**Goal:** Code reuse through inheritance

```pseudocode
class Animal
    fn init(name)
        self.name = name
    end
    
    fn speak()
        print(self.name + " makes a sound")
    end
end

class Dog extends Animal
    fn init(name, breed)
        super.init(name)
        self.breed = breed
    end
    
    fn speak()
        print(self.name + " barks!")
    end
end

let dog = Dog("Rex", "German Shepherd")
dog.speak()  // "Rex barks!"
```

**Implementation:**
1. Add `extends`, `super` keywords
2. Prototype chain or class hierarchy
3. Method resolution order (MRO)
4. Super call mechanism

**Effort:** ~400 additional lines

---

### 2.3 Static Methods and Properties
```pseudocode
class Math
    static PI = 3.14159
    
    static fn square(x)
        return x * x
    end
end

print(Math.PI)
print(Math.square(5))
```

---

## Phase 3: Module System Enhancement (Priority: MEDIUM)

### 3.1 Selective Imports
**Goal:** Import only what you need

```pseudocode
// Current
import "./lib/math.pseudo"    // Imports everything

// Proposed
from "./lib/math.pseudo" import factorial, gcd, PI
from "./lib/strings.pseudo" import pad_left, center

// With aliases
from "./lib/math.pseudo" import factorial as fact
import "./lib/http.pseudo" as http

// Usage
print(fact(5))
print(http.get("https://api.example.com"))
```

**Implementation:**
1. Parse `from X import Y` syntax
2. Symbol table per module
3. Selective name binding
4. Alias support in import preprocessor

**Effort:** ~300 lines

---

### 3.2 Module Namespaces
**Goal:** Avoid name collisions

```pseudocode
// lib/math.pseudo
module math
    let PI = 3.14159
    
    fn square(x)
        return x * x
    end
end

// main.pseudo
import "math"
print(math.PI)
print(math.square(5))
```

**Implementation:**
1. `module` keyword
2. Namespace scoping
3. Qualified name resolution

**Effort:** ~400 lines

---

## Phase 4: Iteration & Generators (Priority: MEDIUM)

### 4.1 Iterator Protocol
**Goal:** Custom iterable objects

```pseudocode
// Make class iterable
class Range
    fn init(start, stop)
        self.start = start
        self.stop = stop
    end
    
    fn iter()
        return RangeIterator(self.start, self.stop)
    end
end

class RangeIterator
    fn init(current, stop)
        self.current = current
        self.stop = stop
    end
    
    fn next()
        if self.current >= self.stop then
            return {done: true}
        end
        let value = self.current
        self.current = self.current + 1
        return {value: value, done: false}
    end
end

// Usage
for i in Range(1, 10) do
    print(i)
end
```

---

### 4.2 Generators (yield)
**Goal:** Lazy sequences

```pseudocode
fn fibonacci()
    let a = 0
    let b = 1
    while true do
        yield a
        let temp = a
        a = b
        b = temp + b
    end
end

// Usage
for n in take(fibonacci(), 10) do
    print(n)
end
```

**Implementation:**
1. `yield` keyword
2. Generator/coroutine state machine
3. Suspend/resume mechanism
4. Iterator wrapper

**Effort:** ~600 lines (complex)

---

## Phase 5: Async/Concurrency (Priority: FUTURE)

### 5.1 Async/Await
**Goal:** Non-blocking I/O

```pseudocode
async fn fetch_user(id)
    let response = await http_get("/users/" + str(id))
    return json_parse(response)
end

async fn main()
    let user = await fetch_user(123)
    print(user.name)
end

// Concurrent execution
async fn fetch_all()
    let users = await all([
        fetch_user(1),
        fetch_user(2),
        fetch_user(3)
    ])
    return users
end
```

**Implementation:**
1. Event loop in VM
2. Promise/Future type
3. `async`/`await` keywords
4. Non-blocking I/O wrappers
5. Coroutine scheduling

**Effort:** ~2000+ lines (major feature)

---

### 5.2 Parallel Execution
**Goal:** True parallelism for CPU-bound tasks

```pseudocode
// Parallel map
let results = parallel_map(data, fn(x) 
    return expensive_computation(x) 
end)

// Worker pools
let pool = worker_pool(4)  // 4 workers
let futures = []
for task in tasks do
    push(futures, pool.submit(task))
end
let results = pool.join(futures)
```

**Note:** Requires thread-safe VM or actor model

---

## Phase 6: Type System (Priority: LOW)

### 6.1 Optional Type Annotations
**Goal:** Gradual typing for better tooling

```pseudocode
// Optional annotations (ignored at runtime, used by tools)
fn add(a: number, b: number) -> number
    return a + b
end

let name: string = "hello"
let items: array[number] = [1, 2, 3]
let user: {name: string, age: number} = {name: "Alice", age: 30}

// Union types
fn process(value: string | number)
    // ...
end
```

**Implementation:**
1. Type annotation syntax (ignored by compiler)
2. Separate type checker tool
3. IDE integration
4. Runtime optional type checking mode

**Effort:** 1000+ lines for type checker

---

### 6.2 Enums
**Goal:** Named constants

```pseudocode
enum Color
    RED
    GREEN
    BLUE
end

enum HttpStatus
    OK = 200
    NOT_FOUND = 404
    ERROR = 500
end

let status = HttpStatus.OK
match status
    case HttpStatus.OK then print("Success")
    case HttpStatus.NOT_FOUND then print("Not found")
    case _ then print("Other")
end
```

**Effort:** ~200 lines

---

## Phase 7: Metaprogramming (Priority: LOW)

### 7.1 Decorators
**Goal:** Function/class modification

```pseudocode
@memoize
fn fibonacci(n)
    if n <= 1 then return n end
    return fibonacci(n-1) + fibonacci(n-2)
end

@deprecated("Use new_function instead")
fn old_function()
    // ...
end

@trace
fn complex_operation()
    // Automatically logs entry/exit
end
```

**Implementation:**
1. `@decorator` syntax
2. Decorator = function that wraps another function
3. Compile-time or runtime transformation

**Effort:** ~300 lines

---

### 7.2 Reflection
**Goal:** Runtime introspection

```pseudocode
fn inspect(obj)
    print("Type: " + type(obj))
    print("Methods: " + str(methods(obj)))
    print("Properties: " + str(properties(obj)))
end

// Get/set by string name
let prop = get_property(obj, "name")
set_property(obj, "age", 25)
let result = call_method(obj, "greet", ["hello"])
```

---

## Implementation Priority

| Feature | Effort | Impact | Priority |
|---------|--------|--------|----------|
| Try/catch | Medium | HIGH |  |
| Classes | Medium | HIGH |  |
| Selective imports | Low | MEDIUM |  |
| Inheritance | Medium | MEDIUM |  |
| Enums | Low | LOW |  |
| Generators | High | MEDIUM |  |
| Async/await | Very High | HIGH | Future |
| Type annotations | High | MEDIUM | Future |
| Decorators | Medium | LOW | Future |

---

## Recommended Order

### Short Term (1-2 months)
1. **Try/catch exceptions** - Essential for robust programs
2. **Basic classes** - OOP is expected in modern languages
3. **Selective imports** - `from X import Y`

### Medium Term (3-6 months)
4. **Inheritance** - Complete OOP story
5. **Enums** - Simple, useful addition
6. **Iterators** - Custom iterable types
7. **Generators** - Lazy evaluation

### Long Term (6+ months)
8. **Async/await** - Modern concurrency
9. **Type annotations** - Tooling support
10. **Decorators** - Metaprogramming

---

## Comparison with Other Languages

| Feature | Pseudocode | Python | JavaScript | Lua |
|---------|------------|--------|------------|-----|
| Functions |  |  |  |  |
| Closures |  |  |  |  |
| Classes |  |  |  | Tables |
| Inheritance |  |  |  | Metatables |
| Try/catch |  |  |  | pcall |
| Async/await |  |  |  | Coroutines |
| Generators |  |  |  |  |
| Type hints |  |  | TypeScript |  |
| Pattern match |  |  (3.10+) |  |  |
| Imports |  |  |  | require |

---

## Files to Modify

### For Try/Catch
- `cvm/lexer.c` - Add try, catch, finally, throw tokens
- `cvm/compiler.c` - Exception block compilation
- `cvm/vm.c` - Exception handling, stack unwinding
- `cvm/pseudo.h` - New opcodes

### For Classes
- `cvm/lexer.c` - Add class, self, extends, super tokens
- `cvm/compiler.c` - Class compilation
- `cvm/object.c` - ObjClass, ObjInstance types
- `cvm/vm.c` - Method dispatch, property access
- `cvm/memory.c` - GC for class objects

### For Selective Imports
- `cvm/import.c` - Parse `from X import Y`
- `cvm/compiler.c` - Selective binding

---

## Next Steps

1. ⏳ Design and implement try/catch
2. ⏳ Design class syntax and semantics
3. ⏳ Implement selective imports in preprocessor
4. ⏳ Add enums as simple feature
5. ⏳ Document new features in SPEC.md
