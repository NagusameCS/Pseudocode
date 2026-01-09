# Pseudocode Language - AI Coding Guidelines

This repository contains the Pseudocode programming language, a high-performance language with intuitive syntax designed to look like textbook pseudocode.

## Language Overview

Pseudocode is a compiled language that runs on a bytecode VM with an x86-64 JIT compiler. Files use the `.pseudo` extension.

## Syntax Rules

### Basic Structure
- **No semicolons** - statements are newline-terminated
- **No braces** - blocks use keyword delimiters (`end`, `do`, `then`)
- **Indentation** - 4 spaces preferred, not significant for parsing
- **Comments** - single-line only with `//`

### Keywords
```
fn, let, if, then, elif, else, end, for, in, to, do, while, 
return, match, case, true, false, nil, and, or, not
```

### Variable Declaration
```pseudocode
let name = "value"    // Use 'let' for new variables
name = "new value"    // Assignment without 'let' for existing variables
```

### Function Definition
```pseudocode
fn function_name(param1, param2)
    // function body
    return result
end
```

### Control Flow

**If statements:**
```pseudocode
if condition then
    // code
elif other_condition then
    // code
else
    // code
end
```

**For loops (range-based):**
```pseudocode
for i in 1 to 10 do
    // i goes from 1 to 10 inclusive
end

for item in array do
    // iterate over array elements
end
```

**While loops:**
```pseudocode
while condition do
    // code
end
```

### Pattern Matching
```pseudocode
match value
    case 0 then
        // handle zero
    case n if n < 0 then
        // guard clause for negative
    case _ then
        // default case (underscore is wildcard)
end
```

### Data Types
- **Numbers**: `42`, `3.14`, `-17` (all are 64-bit floats internally)
- **Strings**: `"hello"`, `'world'` (double or single quotes)
- **Booleans**: `true`, `false`
- **Nil**: `nil`
- **Arrays**: `[1, 2, 3]`, `["a", "b"]`
- **Dictionaries**: `{"key": "value", "num": 42}`

### Operators
- **Arithmetic**: `+`, `-`, `*`, `/`, `%` (modulo)
- **Comparison**: `==`, `!=`, `<`, `>`, `<=`, `>=`
- **Logical**: `and`, `or`, `not`
- **String concatenation**: `+`

### Built-in Functions (80+)

**I/O:**
- `print(value)` - print to stdout
- `input(prompt)` - read from stdin

**Math:**
- `abs(x)`, `sqrt(x)`, `pow(x, y)`, `min(a, b)`, `max(a, b)`
- `floor(x)`, `ceil(x)`, `round(x)`
- `sin(x)`, `cos(x)`, `tan(x)`, `log(x)`, `exp(x)`
- `random()` - random float 0-1

**Strings:**
- `len(s)`, `upper(s)`, `lower(s)`, `trim(s)`
- `split(s, delim)`, `join(arr, delim)`
- `substr(s, start, len)`, `replace(s, old, new)`
- `starts_with(s, prefix)`, `ends_with(s, suffix)`
- `contains(s, sub)`, `index_of(s, sub)`

**Arrays:**
- `len(arr)`, `push(arr, val)`, `pop(arr)`
- `slice(arr, start, end)`, `reverse(arr)`
- `sort(arr)`, `map(arr, fn)`, `filter(arr, fn)`
- `reduce(arr, fn, init)`, `find(arr, fn)`

**Dictionaries:**
- `keys(dict)`, `values(dict)`, `has_key(dict, key)`
- `dict_get(dict, key, default)`

**File I/O:**
- `read_file(path)`, `write_file(path, content)`
- `append_file(path, content)`, `file_exists(path)`

**HTTP:**
- `http_get(url)`, `http_post(url, body)`
- `http_put(url, body)`, `http_delete(url)`

**JSON:**
- `json_parse(str)`, `json_stringify(obj)`

**Crypto:**
- `sha256(data)`, `md5(data)`
- `encode_base64(data)`, `decode_base64(str)`

**Type Conversion:**
- `int(x)`, `float(x)`, `str(x)`, `type(x)`

## Code Style Guidelines

1. **Use descriptive variable names** - `count` not `c`
2. **Use 4-space indentation** - consistent throughout
3. **Add comments for complex logic** - use `//`
4. **Keep functions small** - single responsibility
5. **Use pattern matching** - prefer `match/case` over long if/elif chains

## Example: Complete Program

```pseudocode
// Quicksort implementation
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

// Main program
let numbers = [64, 34, 25, 12, 22, 11, 90]
print("Before: " + str(numbers))
quicksort(numbers, 0, len(numbers) - 1)
print("After: " + str(numbers))
```

## Running Pseudocode

```bash
# Standard execution
./pseudo program.pseudo

# With JIT enabled (faster for numeric loops)
./pseudo -j program.pseudo

# Debug mode
./pseudo -d program.pseudo
```

## Common Patterns

### Error Handling
```pseudocode
let result = some_operation()
if result == nil then
    print("Operation failed")
    return
end
```

### Functional Programming
```pseudocode
let nums = [1, 2, 3, 4, 5]
let doubled = map(nums, fn(x) return x * 2 end)
let evens = filter(nums, fn(x) return x % 2 == 0 end)
let sum = reduce(nums, fn(acc, x) return acc + x end, 0)
```

### HTTP API Client
```pseudocode
fn fetch_user(username)
    let url = "https://api.example.com/users/" + username
    let response = http_get(url)
    return json_parse(response)
end
```
