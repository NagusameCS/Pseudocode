# Pseudocode Language Extension

A VS Code extension for **Pseudocode** - a blazingly fast programming language with intuitive syntax.

## Features

- **Syntax Highlighting** - Full grammar support for `.pseudo` and `.psc` files
- **Code Snippets** - Quick templates for functions, loops, control flow, and more
- **Run Command** - Execute Pseudocode files directly with `Ctrl+Shift+R` / `Cmd+Shift+R`
- **Build Support** - Build the Pseudocode VM from source

## Performance

Pseudocode runs **2.5x faster than Python**. The recursive Fibonacci benchmark (fib(30)) completes in ~55ms compared to Python's ~136ms.

## Quick Start

1. Install the extension
2. Open a `.pseudo` file
3. Press `Ctrl+Shift+R` (or `Cmd+Shift+R` on Mac) to run

```pseudo
// Hello World
fn greet(name) {
    println("Hello, " + name + "!")
}

greet("World")
```

## Snippets

| Prefix | Description |
|--------|-------------|
| `fn` | Function definition |
| `if` | If statement |
| `ife` | If-else statement |
| `while` | While loop |
| `for` | For-in loop |
| `forr` | For loop with range |
| `let` | Variable declaration |
| `print` | Print statement |
| `match` | Pattern matching |
| `httpget` | HTTP GET request |
| `dict` | Dictionary creation |

## Built-in Functions

### Core
`print`, `println`, `input`, `len`, `str`, `int`, `float`, `type`

### Arrays
`push`, `pop`, `slice`, `concat`, `sort`, `reverse`, `range`

### Strings
`split`, `join`, `replace`, `upper`, `lower`, `trim`, `contains`

### Math
`abs`, `min`, `max`, `floor`, `ceil`, `round`, `sqrt`, `pow`, `sin`, `cos`, `tan`, `log`, `exp`

### File I/O
`read_file`, `write_file`, `append_file`, `file_exists`, `list_dir`, `delete_file`, `mkdir`

### HTTP
`http_get`, `http_post`

### JSON
`json_parse`, `json_stringify`

### Dictionaries
`dict`, `dict_get`, `dict_set`, `dict_has`, `dict_keys`, `dict_values`

### Vectors
`vec_add`, `vec_sub`, `vec_mul`, `vec_dot`, `vec_sum`, `vec_mean`, `vec_sort`

### Binary & Encoding
`bytes`, `encode_base64`, `decode_base64`, `hash`

## Settings

| Setting | Description | Default |
|---------|-------------|---------|
| `pseudocode.vmPath` | Path to the Pseudocode VM executable | Auto-detected |
| `pseudocode.showExecutionTime` | Show execution time after running | `true` |

## Commands

- **Pseudocode: Run Pseudocode File** - Run the current file (`Ctrl+Shift+R`)
- **Pseudocode: Build Pseudocode VM** - Build the VM from source

## Requirements

- The Pseudocode VM (`pseudo`) executable
- For building from source: GCC/Clang and Make

## Links

- [Documentation](https://nagusame.me/Pseudocode)
- [GitHub Repository](https://github.com/NagusameCS/Pseudocode)

## License

MIT License
