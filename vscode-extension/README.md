# Pseudocode Language Extension

<p align="center">
  <img src="images/icon.png" alt="Pseudocode Logo" width="128" height="128">
</p>

<p align="center">
  <strong>Full-featured language support for Pseudocode in Visual Studio Code</strong>
</p>

<p align="center">
  <a href="#features">Features</a> |
  <a href="#installation">Installation</a> |
  <a href="#quick-start">Quick Start</a> |
  <a href="#commands">Commands</a> |
  <a href="#snippets">Snippets</a>
</p>

---

## Features

### IntelliSense

- **Smart Autocompletion** — Suggestions for keywords, 80+ built-in functions, and your variables/functions
- **Hover Documentation** — Detailed docs with examples for every built-in function
- **Signature Help** — Parameter hints as you type function calls
- **Inlay Hints** — See parameter names inline in function calls
- **Go to Definition** — Jump to function/variable definitions with `F12`
- **Find All References** — See every usage with `Shift+F12`
- **Rename Symbol** — Safely rename across your file with `F2`
- **Call Hierarchy** — View incoming/outgoing calls with `Ctrl+Shift+H`

### Editor Integration

- **Rich Syntax Highlighting** — Beautiful coloring with semantic tokens
- **Document Symbols** — Navigate with the Outline view or `Ctrl+Shift+O`
- **Workspace Symbols** — Find functions across all files with `Ctrl+T`
- **Code Lens** — See reference counts and run buttons above functions
- **Folding** — Collapse functions, loops, and conditionals
- **Smart Selection** — Expand selection with `Shift+Alt+Right`
- **Bracket Matching** — Auto-pairing and highlighting
- **Color Picker** — Visual color picker for hex color strings

### Diagnostics

- **Real-time Errors** — Catch typos and syntax errors as you type
- **Quick Fixes** — Auto-fix common mistakes with `Ctrl+.`
- **Typo Detection** — Suggests correct spelling for misspelled builtins

### Formatting

- **Auto-Indentation** — Proper indentation for blocks
- **Format Document** — Clean up your code with `Shift+Alt+F`

### Execution

- **Run File** — Execute with `Ctrl+Shift+R` / `Cmd+Shift+R`
- **Execution Time** — See performance metrics after running
- **Build VM** — Build the Pseudocode VM from source

### Debugging

- **Breakpoints** — Set breakpoints and step through code
- **Variable Inspection** — View variables during debugging
- **Call Stack** — Navigate the call stack

### REPL

- **Interactive Mode** — Test code snippets interactively

---

## Installation

### From VS Code Marketplace

1. Open VS Code
2. Press `Ctrl+P` / `Cmd+P`
3. Type `ext install NagusameCS.pseudocode-lang`

### Platform Support

The extension includes pre-built VM binaries for:
- ✅ macOS ARM64 (Apple Silicon)
- ✅ macOS x64 (Intel)

For other platforms (Windows, Linux), you'll need to build the VM from source:

```bash
cd cvm
make release
```

### From Source

```bash
cd vscode-extension
npm install
npm run compile
```

---

## Quick Start

1. Create a file with `.pseudo` or `.psc` extension
2. Start coding:

```pseudo
// A simple greeting program
fn greet(name)
    print("Hello, " + name + "!")
end

// Main program
let user = input("Enter your name: ")
greet(user)
```

3. Run with `Ctrl+Shift+R` (Windows/Linux) or `Cmd+Shift+R` (Mac)

---

## Commands

| Command | Keybinding | Description |
|---------|------------|-------------|
| Run Pseudocode File | `Ctrl+Shift+R` | Execute the current file |
| Build Pseudocode VM | — | Build the VM from source |
| Open REPL | — | Open interactive REPL |
| Format Document | `Shift+Alt+F` | Format the current file |
| Go to Definition | `F12` | Jump to symbol definition |
| Find All References | `Shift+F12` | Find all references |
| Rename Symbol | `F2` | Rename a symbol |
| Quick Fix | `Ctrl+.` | Show available code fixes |

---

## Snippets

Over 50 snippets for rapid development:

### Basic Constructs

| Prefix | Description |
|--------|-------------|
| `fn` | Function definition |
| `if` | If statement |
| `ife` | If-else statement |
| `ifei` | If-elif-else |
| `while` | While loop |
| `for` | For-in loop |
| `fori` | For loop with range |
| `match` | Pattern matching |
| `let` | Variable declaration |

### Data Structures

| Prefix | Description |
|--------|-------------|
| `arr` | Array literal |
| `dict` | Dictionary literal |
| `stack` | Stack implementation |
| `queue` | Queue implementation |
| `ll` | Linked list node |

### Algorithms

| Prefix | Description |
|--------|-------------|
| `qsort` | Quicksort |
| `bsearch` | Binary search |
| `fib` | Fibonacci |
| `memo` | Memoization wrapper |

### I/O and Network

| Prefix | Description |
|--------|-------------|
| `httpget` | HTTP GET request |
| `httppost` | HTTP POST request |
| `readfile` | Read file contents |
| `writefile` | Write to file |

---

## Built-in Functions (80+)

### Core

`print` `input` `len` `str` `int` `float` `type`

### Strings

`upper` `lower` `trim` `split` `join` `replace` `contains` `starts_with` `ends_with` `substr` `char_at` `index_of` `ord` `chr`

### Arrays

`push` `pop` `shift` `unshift` `slice` `concat` `sort` `reverse` `range` `map` `filter` `reduce` `find` `every` `some` `flat` `unique` `zip`

### Dictionaries

`dict` `dict_get` `dict_set` `dict_has` `dict_keys` `dict_values` `dict_delete` `dict_merge`

### Math

`abs` `min` `max` `floor` `ceil` `round` `sqrt` `pow` `random` `sin` `cos` `tan` `asin` `acos` `atan` `atan2` `log` `log10` `log2` `exp` `hypot`

### Bitwise

`bit_and` `bit_or` `bit_xor` `bit_not` `bit_lshift` `bit_rshift` `popcount` `clz` `ctz`

### File I/O

`read_file` `write_file` `append_file` `file_exists` `delete_file` `list_dir` `mkdir`

### HTTP

`http_get` `http_post` `http_put` `http_delete`

### JSON

`json_parse` `json_stringify`

### Encoding

`encode_base64` `decode_base64` `encode_utf8` `decode_utf8` `md5` `sha256` `hash`

### System

`clock` `sleep` `exec` `env` `set_env` `args` `exit`

### Vector Operations

`vec_add` `vec_sub` `vec_mul` `vec_div` `vec_dot` `vec_sum` `vec_prod` `vec_min` `vec_max` `vec_mean`

---

## Settings

| Setting | Description | Default |
|---------|-------------|---------|
| `pseudocode.vmPath` | Path to the Pseudocode VM | Auto-detected |
| `pseudocode.showExecutionTime` | Show execution time after running | `true` |
| `pseudocode.enableDiagnostics` | Enable real-time error diagnostics | `true` |
| `pseudocode.formatOnSave` | Auto-format on save | `false` |

---

## File Extensions

| Extension | Description |
|-----------|-------------|
| `.pseudo` | Primary source file |
| `.psc` | Short source file |
| `.pseudoh` | Header file |
| `.psch` | Short header |
| `.pseudocode` | Verbose source |

---

## Performance

Pseudocode features a tracing JIT compiler that generates native x86-64 machine code for hot loops:

| Benchmark | Pseudocode | Python | Speedup |
|-----------|------------|--------|---------|
| Fibonacci(30) | 55ms | 136ms | 2.5x |
| Loop 10M | 89ms | 450ms | 5x |
| String ops | 12ms | 28ms | 2.3x |

---

## What's New in v1.4.0

- **JIT Function Inlining** — Small functions are now inlined during JIT compilation
- **Inlay Hints** — See parameter names inline in function calls
- **Call Hierarchy** — View all incoming/outgoing calls
- **Color Picker** — Visual picker for color strings
- **Smart Selection** — Expand selection intelligently
- **Getting Started** — Interactive walkthrough for new users

---

## License

MIT License

---

<p align="center">
  <a href="https://github.com/NagusameCS/Pseudocode">GitHub</a> |
  <a href="https://nagusamecs.github.io/Pseudocode/">Documentation</a> |
  <a href="https://github.com/NagusameCS/Pseudocode/issues">Report Issue</a>
</p>
