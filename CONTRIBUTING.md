# Contributing to Pseudocode

Thank you for your interest in contributing to Pseudocode! This guide will help you get started.

## Project Structure

```
Pseudocode/
├── cvm/                    # C Virtual Machine (main implementation)
│   ├── main.c              # Entry point, argument parsing
│   ├── lexer.c             # Tokenizer
│   ├── compiler.c          # Bytecode compiler (Pratt parser)
│   ├── vm.c                # Bytecode interpreter
│   ├── jit.c               # x86-64 JIT compiler
│   ├── jit_x64.c           # JIT machine code generation
│   ├── memory.c            # Garbage collector
│   ├── tensor.c            # SIMD tensor operations
│   └── pseudo.h            # Shared definitions
├── examples/               # Example programs
├── benchmarks/             # Performance benchmarks
├── docs/                   # Documentation website
├── editors/                # Editor plugins
│   ├── vim/                # Vim/Neovim support
│   ├── sublime/            # Sublime Text support
│   └── emacs/              # Emacs support
├── vscode-extension/       # VS Code extension
└── .github/
    └── copilot-instructions.md  # AI coding guidelines
```

## Development Setup

```bash
# Clone the repository
git clone https://github.com/NagusameCS/Pseudocode.git
cd Pseudocode/cvm

# Build with optimizations
make pgo

# Run tests
./pseudo ../examples/hello.pseudo
./pseudo -j ../benchmarks/bench_jit.pseudo
```

## Adding Features

### Adding a New Built-in Function

1. **Define the function** in `vm.c`:
```c
static Value builtin_my_function(int arg_count, Value* args) {
    // Implementation
    return result;
}
```

2. **Register it** in the `init_builtins()` function:
```c
define_builtin("my_function", builtin_my_function);
```

3. **Document it** in:
   - `docs/reference.html`
   - `docs/SPEC.md`
   - `.github/copilot-instructions.md`

### Adding a New Opcode

1. **Add to enum** in `pseudo.h`:
```c
typedef enum {
    // ...
    OP_MY_OPCODE,
    // ...
} OpCode;
```

2. **Generate in compiler** (`compiler.c`):
```c
emit_byte(OP_MY_OPCODE);
```

3. **Handle in VM** (`vm.c`):
```c
CASE(OP_MY_OPCODE): {
    // Implementation
    DISPATCH();
}
```

4. **Handle in JIT** if applicable (`jit_x64.c`)

## Code Style

- **C Standard**: C11
- **Indentation**: 4 spaces
- **Braces**: K&R style
- **Naming**: `snake_case` for functions, `UPPER_CASE` for macros
- **Comments**: `//` for single-line, `/* */` for blocks

## Testing

Run the example programs to verify changes:

```bash
# Basic functionality
./pseudo ../examples/hello.pseudo
./pseudo ../examples/fibonacci.pseudo
./pseudo ../examples/quicksort.pseudo

# JIT performance
./pseudo -j ../benchmarks/bench_jit.pseudo

# Full test suite
for f in ../examples/*.pseudo; do ./pseudo "$f"; done
```

## Pull Request Process

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes with clear commit messages
4. Test thoroughly
5. Submit a pull request

## AI Contribution Guidelines

If you're an AI assistant helping with this codebase:

1. **Read** `.github/copilot-instructions.md` for Pseudocode syntax
2. **Check** `docs/SPEC.md` for language specification
3. **Follow** existing code patterns in the codebase
4. **Test** generated code with the VM before suggesting

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
