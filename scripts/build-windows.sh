#!/bin/bash
# Build Pseudocode VM for Windows x64
# Run this in MSYS2 MinGW64 shell on Windows

set -e
cd "$(dirname "$0")/.."

echo "=== Building Pseudocode VM for Windows ==="

# Check for PCRE2 (optional)
PCRE_FLAGS=""
if pkg-config --exists libpcre2-8 2>/dev/null; then
    PCRE_FLAGS="$(pkg-config --cflags --libs libpcre2-8)"
    echo "Found PCRE2, building with regex support"
else
    PCRE_FLAGS="-DNO_PCRE2"
    echo "Building without PCRE2 (no regex support)"
fi

cd cvm

echo "Compiling..."
gcc -O3 -std=gnu11 $PCRE_FLAGS \
    -o pseudo-win32-x64.exe \
    main.c lexer.c compiler.c vm.c memory.c import.c \
    jit_trace.c trace_recorder.c trace_regalloc.c trace_codegen.c tensor.c \
    -lm

cp pseudo-win32-x64.exe ../vscode-extension/bin/

echo ""
echo "=== Done! ==="
ls -la ../vscode-extension/bin/pseudo-win32-x64.exe
