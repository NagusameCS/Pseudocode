# Pseudocode Language Extension

A VS Code extension for **Pseudocode** - a blazingly fast programming language with intuitive syntax.

## Features

### IntelliSense & Productivity
- **Autocomplete** - Smart suggestions for keywords, built-in functions, and your own variables/functions
- **Hover Documentation** - Detailed documentation for all built-in functions on hover
- **Signature Help** - Function parameter hints as you type
- **Document Outline** - Navigate functions and variables in the Outline view
- **Real-time Diagnostics** - Error detection as you type with helpful suggestions
- **Code Formatting** - Auto-indent your code with Format Document (`Shift+Alt+F`)

### Language Support
- **Syntax Highlighting** - Full grammar support for `.pseudo` and `.psc` files
- **Code Snippets** - Quick templates for functions, loops, control flow, and more
- **Bracket Matching** - Automatic bracket pairing and matching

### Execution
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
fn greet(name)
    print("Hello, " + name + "!")
end

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
`print`, `input`, `len`, `str`, `int`, `float`, `type`

### Arrays
`push`, `pop`, `shift`, `unshift`, `slice`, `concat`, `sort`, `reverse`, `range`, `map`, `filter`, `reduce`, `find`, `every`, `some`

### Strings
`split`, `join`, `replace`, `upper`, `lower`, `trim`, `contains`, `starts_with`, `ends_with`, `substr`, `char_at`, `index_of`

### Math
`abs`, `min`, `max`, `floor`, `ceil`, `round`, `sqrt`, `pow`, `sin`, `cos`, `tan`, `log`, `exp`, `random`, `randint`

### File I/O
`read_file`, `write_file`

### HTTP
`http_get`, `http_post`

### JSON
`json_parse`, `json_stringify`

### Time
`time`, `time_ms`, `sleep`

### Type Checking
`is_nil`, `is_int`, `is_float`, `is_string`, `is_array`, `is_function`

### Crypto
`md5`, `sha256`, `base64_encode`, `base64_decode`

## Settings

| Setting | Description | Default |
|---------|-------------|---------|
| `pseudocode.vmPath` | Path to the Pseudocode VM executable | Auto-detected |
| `pseudocode.showExecutionTime` | Show execution time after running | `true` |
| `pseudocode.enableDiagnostics` | Enable real-time error diagnostics | `true` |
| `pseudocode.formatOnSave` | Auto-format on save | `false` |

## Commands

- **Pseudocode: Run Pseudocode File** - Run the current file (`Ctrl+Shift+R`)
- **Pseudocode: Build Pseudocode VM** - Build the VM from source

## Requirements

- The Pseudocode VM (`pseudo`) executable
- For building from source: GCC/Clang and Make

## What's New in v1.1.0

- ✨ **IntelliSense** - Full autocomplete for keywords, builtins, and local symbols
- 📚 **Hover Documentation** - Rich documentation for all built-in functions
- 🔍 **Real-time Diagnostics** - Catch errors as you type
- 📐 **Document Formatting** - Auto-indent and format your code
- 🧭 **Document Symbols** - Quick navigation via Outline view
- ✍️ **Signature Help** - Parameter hints for function calls

## Links

- [Documentation](https://nagusame.me/Pseudocode)
- [GitHub Repository](https://github.com/NagusameCS/Pseudocode)

## License

MIT License
