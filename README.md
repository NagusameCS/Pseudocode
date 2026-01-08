# ⚡ Pseudocode

A blazingly fast programming language with intuitive pseudocode syntax. Write code that reads like natural language and runs faster than Python.

[![Performance](https://img.shields.io/badge/fib(30)-79ms-brightgreen)](docs/)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

## 🚀 Performance

The C virtual machine uses NaN-boxing and computed gotos to achieve exceptional performance:

| Implementation | fib(30) Time |
|---------------|-------------|
| **Pseudocode (C VM)** | **~79ms** ⚡ |
| Python 3.12 (native) | ~123ms |
| Pseudocode (Python VM) | ~23,000ms |

**~290x faster** than the Python VM and **faster than native Python**!

## ✨ Features

- **Blazing Fast** — C-based VM with computed gotos and NaN-boxing
- **Readable Syntax** — Write code that looks like pseudocode
- **Rich Built-ins** — Math, arrays, strings, and type conversions
- **First-Class Functions** — Recursion and lexical scoping
- **Dynamic Arrays** — With push, pop, and negative indexing

## 📦 Installation

### Build from Source (Recommended)

```bash
# Clone the repository
git clone https://github.com/yourusername/Pseudocode.git
cd Pseudocode/cvm

# Build the C VM (requires GCC or Clang)
make

# Run a program
./pseudo ../examples/fibonacci.pseudo
```

### Python Implementation (Slower)

```bash
python src/pseudocode.py examples/hello.pseudo
```

## 📖 Quick Start

### Hello World

```
print("Hello, World!")
```

### Variables

```
let name = "Alice"
let age = 25
let pi = 3.14159
let active = true
```

### Functions

```
fn factorial(n)
    if n <= 1 then
        return 1
    end
    return n * factorial(n - 1)
end

print(factorial(10))  // 3628800
```

### Control Flow

```
if score >= 90 then
    print("A")
elif score >= 80 then
    print("B")
else
    print("F")
end
```

### Loops

```
// For loop with range
for i in 0..10 do
    print(i)
end

// While loop
let i = 0
while i < 5 do
    print(i)
    i = i + 1
end
```

### Arrays

```
let nums = [1, 2, 3, 4, 5]
print(nums[0])      // 1
print(nums[-1])     // 5 (last element)
print(len(nums))    // 5

push(nums, 6)       // Add to end
let last = pop(nums) // Remove from end
```

### Built-in Functions

```
// Math
print(sqrt(16))     // 4
print(abs(-42))     // 42
print(pow(2, 10))   // 1024
print(min(5, 3))    // 3
print(max(5, 3))    // 5

// Type conversions
print(int("123"))   // 123
print(str(456))     // "456"
print(type([]))     // "array"
```

## 📚 Documentation

See the [full documentation](docs/index.html) for:

- [Language Reference](docs/reference.html) — Complete syntax guide
- [Examples](examples/) — Sample programs
- [Specification](docs/SPEC.md) — Language specification

## ��️ Architecture

The C VM features:

- **NaN-boxing** — All values fit in 64 bits
- **Computed Gotos** — Fast bytecode dispatch (GCC/Clang)
- **Pratt Parser** — Single-pass compilation
- **Stack-based VM** — Simple and efficient

## 🧪 Examples

```bash
./cvm/pseudo examples/fibonacci.pseudo   # Fibonacci benchmark
./cvm/pseudo examples/fizzbuzz.pseudo    # Classic FizzBuzz
./cvm/pseudo examples/factorial.pseudo   # Recursive factorial
./cvm/pseudo examples/primes.pseudo      # Prime number sieve
```

## 📄 License

MIT License — see [LICENSE](LICENSE) for details.

---

Made with ⚡ for speed and 💜 for simplicity
