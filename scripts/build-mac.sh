#!/bin/bash
# Build Pseudocode VM for macOS (ARM64 and x64)
# Run this on a Mac

set -e
cd "$(dirname "$0")/.."

echo "=== Building Pseudocode VM for macOS ==="

# Check for PCRE2
if ! pkg-config --exists libpcre2-8 2>/dev/null; then
    echo "Installing PCRE2..."
    brew install pcre2
fi

cd cvm

# Build native (ARM64 on Apple Silicon, x64 on Intel)
echo "Building native binary..."
make clean
make release

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    cp pseudo ../vscode-extension/bin/pseudo-darwin-arm64
    echo "✓ Built pseudo-darwin-arm64"
    
    # Cross-compile for x64 (without PCRE2)
    echo "Cross-compiling for x64..."
    clang -target x86_64-apple-darwin -O3 -DNO_PCRE2 -std=gnu11 \
        -o pseudo-darwin-x64 \
        main.c lexer.c compiler.c vm.c memory.c import.c \
        jit_trace.c trace_recorder.c trace_regalloc.c trace_codegen.c tensor.c \
        -lm
    cp pseudo-darwin-x64 ../vscode-extension/bin/
    echo "✓ Built pseudo-darwin-x64 (no regex)"
else
    cp pseudo ../vscode-extension/bin/pseudo-darwin-x64
    echo "✓ Built pseudo-darwin-x64"
fi

echo ""
echo "=== Done! Binaries in vscode-extension/bin/ ==="
ls -la ../vscode-extension/bin/
