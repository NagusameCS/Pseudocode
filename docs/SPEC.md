# Pseudocode Language Specification

## Overview
**Pseudocode** is a minimalist, high-performance interpreted language designed for maximum execution speed while maintaining readable, pseudocode-like syntax.

## Design Principles
1. **Zero-overhead abstractions** - No hidden costs
2. **Static typing with inference** - Catch errors early, write less
3. **Stack-based VM** - Minimal memory allocation
4. **Direct bytecode compilation** - No intermediate representations
5. **Native integer arithmetic** - 64-bit operations

## Syntax

### Variables
```
let x = 10
let name: string = "hello"
const PI = 3.14159
```

### Types
- `int` - 64-bit signed integer
- `float` - 64-bit floating point
- `bool` - boolean (true/false)
- `string` - UTF-8 string
- `array<T>` - typed array
- `fn` - function type

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
if condition then
    // code
elif other then
    // code
else
    // code
end

while condition do
    // code
end

for i in 0..10 do
    // code
end

for item in array do
    // code
end
```

### Operators
| Operator | Description |
|----------|-------------|
| `+` `-` `*` `/` | Arithmetic |
| `%` | Modulo |
| `==` `!=` `<` `>` `<=` `>=` | Comparison |
| `and` `or` `not` | Logical |
| `<<` `>>` `&` `\|` `^` | Bitwise |

### Arrays
```
let nums = [1, 2, 3, 4, 5]
let first = nums[0]
let length = len(nums)
push(nums, 6)
```

### Comments
```
// Single line comment

/* 
   Multi-line
   comment 
*/
```

## Built-in Functions
- `print(value)` - Output to stdout
- `len(array)` - Array/string length
- `push(array, value)` - Append to array
- `pop(array)` - Remove last element
- `time()` - Current timestamp (nanoseconds)
- `input()` - Read line from stdin

## Memory Model
- Stack-allocated locals
- Reference-counted heap objects
- No garbage collection pauses
