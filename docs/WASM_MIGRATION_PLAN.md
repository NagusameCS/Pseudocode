# WebAssembly Migration Plan for Pseudocode

## Status: ✅ IMPLEMENTED

This migration has been completed. The Pseudocode VS Code extension now uses a WebAssembly-based runtime that works on all platforms without recompilation.

## Overview

This document outlines the migration from the current C-based bytecode VM to a WebAssembly-based runtime. The primary benefits are:

1. **Cross-platform without recompilation** - WASM runs identically on all platforms
2. **VS Code extension portability** - No need to ship platform-specific binaries
3. **Web browser support** - Run Pseudocode directly in browsers
4. **Sandboxed execution** - Better security model

## Current Architecture

```
┌─────────────────────────────────────────────────────┐
│                  Pseudocode Source                   │
│                    (.pseudo files)                   │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│                   Lexer (lexer.c)                    │
│            Token stream generation                   │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│                Compiler (compiler.c)                 │
│        Pratt parser + bytecode emission              │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│              Custom Bytecode (100+ ops)              │
│           NaN-boxed values, computed gotos           │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│                    VM (vm.c)                         │
│    Interpreter + optional x86-64 JIT (jit.c)        │
└─────────────────────────────────────────────────────┘
```

## Target Architecture

```
┌─────────────────────────────────────────────────────┐
│                  Pseudocode Source                   │
│                    (.pseudo files)                   │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│          TypeScript Compiler (new)                   │
│      Lexer + Parser + WASM code generator           │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│               WebAssembly Module                     │
│         Standard WASM bytecode format               │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│           WASM Runtime (browser/Node)               │
│    V8/SpiderMonkey/Wasmtime native execution        │
└─────────────────────────────────────────────────────┘
```

---

## Checkpoints

### Checkpoint 1: WASM Runtime Foundation
**Goal**: Create the basic WASM runtime infrastructure

Files to create:
- `wasm/src/runtime/` - Core runtime in TypeScript
- `wasm/src/runtime/memory.ts` - Memory management (linear memory)
- `wasm/src/runtime/values.ts` - Value representation (tagged values)
- `wasm/src/runtime/gc.ts` - Garbage collector
- `wasm/src/runtime/objects.ts` - Object types (string, array, dict, etc.)

Key decisions:
- Use **tagged values** (simpler than NaN-boxing in WASM)
- Linear memory layout with stack + heap regions
- Simple mark-sweep GC initially

### Checkpoint 2: Bytecode to WASM Compiler
**Goal**: Translate Pseudocode AST to WASM bytecode

Files to create:
- `wasm/src/compiler/lexer.ts` - Tokenizer (port from C)
- `wasm/src/compiler/parser.ts` - AST generation
- `wasm/src/compiler/ast.ts` - AST node types
- `wasm/src/compiler/codegen.ts` - WASM code generation
- `wasm/src/compiler/wasm-builder.ts` - WASM binary format builder

Strategy:
1. Parse Pseudocode to AST (TypeScript)
2. Type inference pass (optional but helpful)
3. Generate WASM module with:
   - Function sections for each Pseudocode function
   - Import section for runtime functions
   - Memory section for heap/stack

### Checkpoint 3: Standard Library in WASM
**Goal**: Implement built-in functions as WASM/JS

Files to create:
- `wasm/src/stdlib/io.ts` - print, input
- `wasm/src/stdlib/math.ts` - Mathematical functions
- `wasm/src/stdlib/string.ts` - String operations
- `wasm/src/stdlib/array.ts` - Array operations
- `wasm/src/stdlib/dict.ts` - Dictionary operations
- `wasm/src/stdlib/file.ts` - File I/O (Node.js only)
- `wasm/src/stdlib/http.ts` - HTTP client
- `wasm/src/stdlib/json.ts` - JSON parsing/serialization

Implementation:
- Core math functions can be pure WASM
- I/O functions need JS imports
- String/Array heavy lifting in WASM, helpers in JS

### Checkpoint 4: VS Code Extension Integration
**Goal**: Replace native binaries with WASM runtime

Files to modify:
- `vscode-extension/src/wasm/` - WASM runtime loader
- `vscode-extension/src/extension.ts` - Use WASM instead of native
- `vscode-extension/package.json` - Remove platform-specific deps

Benefits:
- Single `.vsix` works on all platforms
- No binary signing/notarization issues
- Smaller extension size

### Checkpoint 5: Testing and Validation
**Goal**: Ensure feature parity with C implementation

Tasks:
- Port all example programs
- Run benchmark suite
- Compare output parity
- Performance profiling

---

## Value Representation Strategy

### Current (C): NaN Boxing
```c
typedef uint64_t Value;
// Doubles: normal IEEE 754
// Others: quiet NaN + type tag + payload
```

### Target (WASM): Tagged Values
```typescript
// 32-bit tagged value (fits in WASM i32)
// Bits 0-2: type tag
// Bits 3-31: payload (29 bits)

const TAG_INT = 0;     // Small integers (inline)
const TAG_FLOAT = 1;   // Float index into float table
const TAG_STRING = 2;  // String object pointer
const TAG_ARRAY = 3;   // Array object pointer
const TAG_DICT = 4;    // Dict object pointer
const TAG_FUNC = 5;    // Function pointer
const TAG_NIL = 6;     // Nil value
const TAG_BOOL = 7;    // Boolean (payload = 0 or 1)
```

For larger values (doubles, large ints), use an indirection table.

---

## Opcode Mapping

| Current Opcode | WASM Equivalent |
|----------------|-----------------|
| OP_CONST | i32.const + call $push_const |
| OP_ADD | call $value_add (handles types) |
| OP_SUB | call $value_sub |
| OP_MUL | call $value_mul |
| OP_DIV | call $value_div |
| OP_LT/GT/etc | call $value_compare |
| OP_JMP | br |
| OP_JMP_FALSE | br_if (with condition check) |
| OP_CALL | call_indirect |
| OP_RETURN | return |
| OP_GET_LOCAL | local.get |
| OP_SET_LOCAL | local.set |
| OP_GET_GLOBAL | global.get / call $get_global |
| OP_ARRAY | call $array_new |
| OP_INDEX | call $array_get |

---

## Implementation Order

1. **Week 1-2**: Checkpoint 1 (Runtime foundation)
2. **Week 3-4**: Checkpoint 2 (Compiler core)
3. **Week 5**: Checkpoint 3 (Standard library)
4. **Week 6**: Checkpoint 4 (VS Code integration)
5. **Week 7**: Checkpoint 5 (Testing)

---

## File Structure

```
wasm/
├── package.json
├── tsconfig.json
├── src/
│   ├── index.ts              # Main entry point
│   ├── compiler/
│   │   ├── lexer.ts          # Tokenizer
│   │   ├── parser.ts         # AST parser
│   │   ├── ast.ts            # AST types
│   │   ├── codegen.ts        # WASM code generator
│   │   └── wasm-builder.ts   # WASM binary builder
│   ├── runtime/
│   │   ├── vm.ts             # WASM VM wrapper
│   │   ├── memory.ts         # Memory management
│   │   ├── values.ts         # Value representation
│   │   ├── gc.ts             # Garbage collector
│   │   └── objects.ts        # Object types
│   └── stdlib/
│       ├── index.ts          # Stdlib exports
│       ├── io.ts             # I/O functions
│       ├── math.ts           # Math functions
│       ├── string.ts         # String functions
│       ├── array.ts          # Array functions
│       ├── dict.ts           # Dictionary functions
│       ├── file.ts           # File I/O
│       ├── http.ts           # HTTP client
│       └── json.ts           # JSON functions
├── tests/
│   ├── lexer.test.ts
│   ├── parser.test.ts
│   ├── codegen.test.ts
│   └── runtime.test.ts
└── dist/                     # Compiled output
```

---

## Next Steps

Ready to begin **Checkpoint 1: WASM Runtime Foundation**.

This creates the core value representation and memory management that everything else builds on.
