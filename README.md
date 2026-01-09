# ⚡ Pseudocode

A high-performance programming language with intuitive pseudocode-like syntax. Write code that reads like natural language and runs at **C-like speeds**.

[![VS Code](https://img.shields.io/badge/VS%20Code-Marketplace-blue?logo=visualstudiocode)](https://marketplace.visualstudio.com/items?itemName=NagusameCS.pseudocode-lang)
[![Performance](https://img.shields.io/badge/JIT-1.0x_C_speed-brightgreen)](docs/)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

## 🚀 Performance

### x86-64 JIT Compiler — C-Level Speed

The JIT compiler generates native machine code for compute-intensive loops:

| Benchmark (1e8 iterations) | JIT Time | C Time | vs C |
|---------------------------|----------|--------|------|
| Increment Loop (`x = x + 1`) | **32ms** | 33ms | **1.0x C** |
| Arithmetic Loop (`x*3 + 7`) | **63ms** | 64ms | **0.98x C** |
| Branch Loop (if/else) | **76ms** | 64ms | **0.84x C** |

JIT intrinsics provide **30-50x speedup** over the interpreted VM.

### Bytecode Interpreter — 3x Faster than Python

| Implementation | fib(30) Time |
|---------------|-------------|
| **Pseudocode (C VM)** | **~44ms** ⚡ |
| Python 3.12 | ~136ms |
| Ruby 3.2 | ~180ms |

## ✨ Features

- **C-Speed JIT** — x86-64 native code generation for hot loops
- **Fast Interpreter** — NaN-boxing VM with computed gotos
- **Readable Syntax** — Keywords like `if/then/end`, `for/in/do`, `match/case`
- **80+ Built-ins** — Math, arrays, HTTP, JSON, file I/O, crypto hashing
- **Pattern Matching** — Expressive `match/case` with guards
- **First-Class Functions** — Closures and higher-order functions
- **Tensor Operations** — SIMD-accelerated matrix/vector math

## 📦 Installation

```bash
# Clone and build with PGO optimizations
git clone https://github.com/NagusameCS/Pseudocode.git
cd Pseudocode/cvm && make pgo

# Run a program
./pseudo ../examples/hello.pseudo

# Run with JIT enabled
./pseudo -j ../examples/fibonacci.pseudo
```

## 🔌 Editor Support

| Editor | Features | Install |
|--------|----------|---------|
| **VS Code** | IntelliSense, debugger, REPL, diagnostics | [Marketplace](https://marketplace.visualstudio.com/items?itemName=NagusameCS.pseudocode-lang) |
| **Vim/Neovim** | Syntax highlighting, indentation | Copy `editors/vim/` to `~/.vim/` |
| **Sublime Text** | Syntax highlighting | Copy `editors/sublime/` to Packages |
| **Emacs** | Major mode with highlighting | Load `editors/emacs/pseudocode-mode.el` |

## 📖 Quick Start

### Hello World

```pseudocode
print("Hello, World!")
```

### Variables

```pseudocode
let name = "Alice"
let age = 25
let pi = 3.14159
let active = true
```

### Functions

```pseudocode
fn factorial(n)
    if n <= 1 then
        return 1
    end
    return n * factorial(n - 1)
end

print(factorial(10))  // 3628800
```

### Control Flow

```pseudocode
if score >= 90 then
    print("A")
elif score >= 80 then
    print("B")
else
    print("F")
end
```

### Loops

```pseudocode
// Range-based for loop
for i in 1 to 10 do
    print(i)
end

// While loop
let i = 0
while i < 5 do
    print(i)
    i = i + 1
end
```

### Pattern Matching

```pseudocode
fn describe(value)
    match value
        case 0 then return "zero"
        case n if n < 0 then return "negative"
        case n if n > 100 then return "large"
        case _ then return "normal"
    end
end
```

### Arrays & Dictionaries

```pseudocode
let nums = [1, 2, 3, 4, 5]
push(nums, 6)
print(nums[-1])  // 6 (last element)

let user = {"name": "Alice", "age": 25}
print(user["name"])  // Alice
```

### HTTP & JSON

```pseudocode
let response = http_get("https://api.github.com/users/octocat")
let data = json_parse(response)
print(data["login"])  // octocat
```

### JIT Intrinsics

```pseudocode
// Use -j flag for C-speed numeric loops
let sum = 0
for i in 1 to 100000000 do
    sum = sum + i
end
print(sum)
```

## 🧪 Example Programs

| Example | Description | Run |
|---------|-------------|-----|
| [quicksort.pseudo](examples/quicksort.pseudo) | In-place quicksort | `./pseudo examples/quicksort.pseudo` |
| [neural_network.pseudo](examples/neural_network.pseudo) | MLP from scratch | `./pseudo examples/neural_network.pseudo` |
| [http_client.pseudo](examples/http_client.pseudo) | GitHub API client | `./pseudo examples/http_client.pseudo` |
| [dynamic_programming.pseudo](examples/dynamic_programming.pseudo) | DP algorithms | `./pseudo examples/dynamic_programming.pseudo` |
| [primes.pseudo](examples/primes.pseudo) | Sieve of Eratosthenes | `./pseudo examples/primes.pseudo` |

See all examples in the [examples/](examples/) directory.

## 📚 Documentation

- **[Website](https://nagusame.com/pseudocode/)** — Main site with interactive examples
- **[Language Reference](docs/reference.html)** — Complete syntax and built-in functions
- **[Language Specification](docs/SPEC.md)** — Formal grammar and semantics

## 🏗️ Architecture

### x86-64 JIT Compiler
- Direct machine code emission (no LLVM)
- Loop variables kept in CPU registers
- LEA-based fast multiplication
- Intrinsics: `__jit_inc_loop()`, `__jit_arith_loop()`, `__jit_branch_loop()`

### Bytecode Interpreter
- NaN-boxing: all values in 64 bits
- Computed gotos for fast dispatch
- Register-cached stack/frame pointers
- Profile-guided optimization (PGO)

## 🤖 AI/LLM Integration

Pseudocode is designed to be AI-friendly:

- Syntax mirrors natural language pseudocode used in textbooks
- Clear, unambiguous keywords (`if/then/end`, `for/in/do`)
- No special symbols or operator overloading
- See [.github/copilot-instructions.md](.github/copilot-instructions.md) for AI coding guidelines

## 📄 License

MIT License — see [LICENSE](LICENSE) for details.

---

Made with ⚡ for speed and 💜 for simplicity
