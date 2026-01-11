# Pseudocode

**A programming language that reads like pseudocode and runs like compiled code.**

<p align="center">
  <img src="vscode-extension/images/icon.png" alt="Pseudocode Logo" width="128" height="128">
</p>

<p align="center">
  <a href="https://nagusamecs.github.io/Pseudocode/">Website</a> |
  <a href="https://nagusamecs.github.io/Pseudocode/reference.html">Reference</a> |
  <a href="#getting-started">Getting Started</a> |
  <a href="#examples">Examples</a>
</p>

---

## Overview

Pseudocode is a compiled language with intuitive, readable syntax designed to look like textbook pseudocode. It features a bytecode VM with an x86-64 tracing JIT compiler for performance-critical loops.

### Key Features

- **Clean Syntax** — No semicolons, no braces, just readable code
- **JIT Compilation** — Tracing JIT generates native x86-64 for hot loops
- **80+ Built-ins** — HTTP, JSON, file I/O, crypto, math, and more
- **First-class Functions** — Closures, lambdas, and higher-order functions
- **Pattern Matching** — Expressive match/case with guards
- **VS Code Extension** — Full IDE support with IntelliSense

---

## Getting Started

### Prerequisites

- GCC or Clang
- Make
- Git

### Installation

```bash
# Clone the repository
git clone https://github.com/NagusameCS/Pseudocode.git
cd Pseudocode

# Build the VM
cd cvm
make

# Run a program
./pseudo ../examples/hello.pseudo
```

### With JIT (faster for loops)

```bash
./pseudo -j ../examples/fibonacci.pseudo
```

---

## Syntax Overview

```pseudo
// Variables
let name = "Pseudocode"
let numbers = [1, 2, 3, 4, 5]

// Functions
fn greet(who)
    print("Hello, " + who + "!")
end

// Control flow
if len(numbers) > 0 then
    for n in numbers do
        print(n)
    end
end

// Pattern matching
fn describe(value)
    match value
        case 0 then
            return "zero"
        case n if n < 0 then
            return "negative"
        case _ then
            return "positive"
    end
end

// Higher-order functions
let doubled = map(numbers, fn(x) return x * 2 end)
let sum = reduce(numbers, fn(a, b) return a + b end, 0)
```

---

## Examples

### Fibonacci

```pseudo
fn fib(n)
    if n <= 1 then
        return n
    end
    return fib(n - 1) + fib(n - 2)
end

print(fib(30))
```

### HTTP API Client

```pseudo
fn fetch_user(username)
    let url = "https://api.github.com/users/" + username
    let response = http_get(url)
    return json_parse(response)
end

let user = fetch_user("octocat")
print(user["name"])
```

### Quicksort

```pseudo
fn quicksort(arr, low, high)
    if low < high then
        let pivot_idx = partition(arr, low, high)
        quicksort(arr, low, pivot_idx - 1)
        quicksort(arr, pivot_idx + 1, high)
    end
end

fn partition(arr, low, high)
    let pivot = arr[high]
    let i = low - 1
    
    for j in low to high - 1 do
        if arr[j] <= pivot then
            i = i + 1
            let temp = arr[i]
            arr[i] = arr[j]
            arr[j] = temp
        end
    end
    
    let temp = arr[i + 1]
    arr[i + 1] = arr[high]
    arr[high] = temp
    
    return i + 1
end
```

---

## Performance

Pseudocode includes a tracing JIT compiler that generates native x86-64 machine code for hot loops. When JIT is enabled (`-j` flag), numeric loops approach C-level performance.

### Benchmark: 10M Increment Loop

| Implementation | Time |
|----------------|------|
| C (gcc -O2) | 33ms |
| Pseudocode (JIT) | 32ms |
| Pseudocode (VM) | 530ms |
| Python 3.11 | 450ms |

The JIT achieves parity with C on simple numeric loops by emitting optimized native code.

---

## VS Code Extension

Full IDE support is available through the [VS Code extension](https://marketplace.visualstudio.com/items?itemName=NagusameCS.pseudocode-lang):

- Syntax highlighting
- IntelliSense and autocompletion
- Hover documentation for 80+ built-ins
- Go to Definition / Find References
- Real-time error diagnostics
- One-click execution
- Debugging support
- 50+ code snippets

Install: `ext install NagusameCS.pseudocode-lang`

---

## Documentation

- [Language Reference](https://nagusamecs.github.io/Pseudocode/reference.html) — Complete syntax and built-in function documentation
- [Examples](examples/) — Sample programs demonstrating various features
- [Benchmarks](benchmarks/) — Performance test programs

---

## Project Structure

```
Pseudocode/
├── cvm/                 # Core VM and compiler
│   ├── main.c          # Entry point
│   ├── lexer.c         # Tokenizer
│   ├── compiler.c      # Bytecode compiler
│   ├── vm.c            # Virtual machine
│   ├── jit.c           # JIT compiler
│   ├── trace_recorder.c # Tracing JIT
│   └── jit_x64.c       # x86-64 codegen
├── docs/               # Website and documentation
├── examples/           # Example programs
├── benchmarks/         # Performance tests
└── vscode-extension/   # VS Code integration
```

---

## Contributing

Contributions are welcome! Please open an issue or submit a pull request.

---

## License

MIT License — see [LICENSE](LICENSE) for details.

---

<p align="center">
  <a href="https://github.com/NagusameCS/Pseudocode">GitHub</a> |
  <a href="https://nagusamecs.github.io/Pseudocode/">Website</a> |
  <a href="https://opencs.dev/NagusameCS">Author</a>
</p>
