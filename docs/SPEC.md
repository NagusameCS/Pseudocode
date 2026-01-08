# Pseudocode Language Specification

> Copyright (c) 2026 NagusameCS - Licensed under the MIT License

## Overview
**Pseudocode** is a high-performance language with an x86-64 JIT compiler that achieves C-like speeds. It combines readable pseudocode syntax with a blazingly fast runtime.

## Design Principles
1. **C-speed execution** - JIT compilation for hot loops
2. **Zero-overhead abstractions** - No hidden costs
3. **NaN-boxing VM** - All values fit in 64 bits
4. **Direct bytecode compilation** - Single-pass Pratt parser
5. **Register-cached execution** - SP/BP in CPU registers

## Execution Modes

### JIT Compiler (x86-64)
The JIT compiler generates native machine code for compute-intensive loops:

```
// JIT intrinsics achieve 0.97x C speed
let result = __jit_inc_loop(iterations)       // x = x + 1
let result = __jit_arith_loop(iterations)     // x = x*3 + 7
let result = __jit_branch_loop(iterations)    // if/else branching
```

### Bytecode Interpreter
The VM uses computed gotos and NaN-boxing for 3x faster than Python performance.

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

## Data Science / NumPy-like Features

### Tensors
N-dimensional arrays with SIMD-accelerated operations (AVX2):

```
// Creation
let zeros = tensor_zeros([3, 3])     // 3x3 zeros
let ones = tensor_ones([2, 4])       // 2x4 ones
let rand = tensor_rand([10, 10])     // Uniform random [0,1)
let randn = tensor_randn([5, 5])     // Normal distribution
let t = tensor([1.0, 2.0, 3.0])      // From array
let r = tensor_arange(0, 10, 2)      // [0, 2, 4, 6, 8]

// Arithmetic (element-wise, SIMD accelerated)
let c = tensor_add(a, b)
let d = tensor_sub(a, b)
let e = tensor_mul(a, b)
let f = tensor_div(a, b)

// Reductions
let s = tensor_sum(t)
let m = tensor_mean(t)
let mn = tensor_min(t)
let mx = tensor_max(t)

// Linear algebra
let dot = tensor_dot(a, b)           // Dot product
let mm = tensor_matmul(a, b)         // Matrix multiplication
let n = tensor_norm(t)               // L2 norm

// Math functions
let ex = tensor_exp(t)
let lg = tensor_log(t)
let sq = tensor_sqrt(t)
let pw = tensor_pow(t, 2)
let ab = tensor_abs(t)

// Shape operations
let shape = tensor_shape(t)
let reshaped = tensor_reshape(t, [2, 3])
let transposed = tensor_transpose(t)
let flat = tensor_flatten(t)
```

### Matrices
2D matrices with BLAS-compatible operations:

```
// Creation
let zeros = matrix_zeros(3, 3)
let ones = matrix_ones(2, 4)
let eye = matrix_eye(3)              // Identity matrix
let rand = matrix_rand(4, 4)
let mat = matrix([[1, 2], [3, 4]])   // From 2D array

// Operations
let c = matrix_add(a, b)
let d = matrix_sub(a, b)
let e = matrix_matmul(a, b)          // Matrix multiplication
let f = matrix_scale(a, 2.0)         // Scalar multiplication
let t = matrix_t(a)                  // Transpose

// Linear algebra
let det = matrix_det(mat)            // Determinant
let tr = matrix_trace(mat)           // Trace
let inv = matrix_inv(mat)            // Inverse
let x = matrix_solve(A, b)           // Solve Ax = b
```

### Neural Network Activations
```
let out = relu(logits)               // max(0, x)
let out = sigmoid(logits)            // 1 / (1 + exp(-x))
let out = tanh(logits)               // Hyperbolic tangent
let out = softmax(logits)            // exp(x) / sum(exp(x))
```

### Loss Functions
```
let loss = mse_loss(pred, target)    // Mean squared error
let loss = ce_loss(pred, target)     // Cross-entropy loss
```

### Autograd (Coming Soon)
```
let tape = grad_tape()               // Create gradient tape
tape.start()                         // Start recording
// ... operations ...
tape.stop()
tape.backward(loss)                  // Compute gradients
let grad = t.grad                    // Access gradient tensor
```

## Memory Model
- Stack-allocated locals
- Reference-counted heap objects
- No garbage collection pauses
- SIMD-aligned tensor data (32-byte alignment for AVX2)
