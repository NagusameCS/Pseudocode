# Pseudocode

A minimalist, high-performance interpreted language with pure pseudocode syntax.

## Features

- **Fast execution** - Stack-based bytecode VM
- **Clean syntax** - Readable pseudocode-like code
- **Static typing** - Type inference with optional annotations
- **Zero dependencies** - Pure Python implementation

## Quick Start

```bash
# Run a file
python src/pseudocode.py examples/hello.pseudo

# Start REPL
python src/pseudocode.py
```

## Syntax Examples

### Variables
```
let x = 10
let name: string = "hello"
const PI = 3.14159
```

### Functions
```
fn add(a: int, b: int) -> int
    return a + b
end

fn factorial(n: int) -> int
    if n <= 1 then
        return 1
    end
    return n * factorial(n - 1)
end
```

### Control Flow
```
if x > 10 then
    print("big")
elif x > 5 then
    print("medium")
else
    print("small")
end

while x > 0 do
    print(x)
    x = x - 1
end

for i in 0..10 do
    print(i)
end
```

### Arrays
```
let nums = [1, 2, 3, 4, 5]
print(nums[0])
print(len(nums))
push(nums, 6)
```

## Built-in Functions

| Function | Description |
|----------|-------------|
| `print(value)` | Output to stdout |
| `len(arr)` | Array/string length |
| `push(arr, val)` | Append to array |
| `pop(arr)` | Remove last element |
| `time()` | Current timestamp (ns) |
| `input()` | Read line from stdin |

## Performance

The bytecode VM provides fast execution through:
- Single-pass compilation
- Stack-based operations
- Minimal memory allocation
- Optimized opcode dispatch

## Project Structure

```
src/
├── lexer.py      # Tokenization
├── ast_nodes.py  # AST definitions
├── parser.py     # Recursive descent parser
├── compiler.py   # Bytecode compiler
├── vm.py         # Virtual machine
├── stdlib.py     # Standard library
└── pseudocode.py # Main entry point

examples/
├── hello.pseudo
├── fibonacci.pseudo
├── factorial.pseudo
├── primes.pseudo
├── arrays.pseudo
└── fizzbuzz.pseudo

docs/
└── SPEC.md       # Language specification
```

## License

MIT License
