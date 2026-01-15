#!/bin/bash
# Build script for Pseudocode WASM + VS Code Extension
# This script builds everything needed for a cross-platform VS Code extension

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "═══════════════════════════════════════════════════════════════"
echo "  Pseudocode WASM Build Script"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# Step 1: Build WASM package
echo "📦 Building WASM package..."
cd "$PROJECT_ROOT/wasm"

if [ ! -d "node_modules" ]; then
    echo "   Installing dependencies..."
    npm install
fi

echo "   Compiling TypeScript..."
npm run build

echo "   ✓ WASM package built"
echo ""

# Step 2: Build VS Code extension
echo "📦 Building VS Code extension..."
cd "$PROJECT_ROOT/vscode-extension"

if [ ! -d "node_modules" ]; then
    echo "   Installing dependencies..."
    npm install
fi

# Link the local WASM package
echo "   Linking local WASM package..."
npm link ../wasm 2>/dev/null || npm install

echo "   Compiling TypeScript..."
npm run compile

echo "   ✓ VS Code extension built"
echo ""

# Step 3: Package extension
echo "📦 Packaging VS Code extension..."

# Check if vsce is available
if ! command -v vsce &> /dev/null; then
    echo "   Installing vsce..."
    npm install -g @vscode/vsce
fi

echo "   Creating .vsix package..."
vsce package --no-dependencies

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  ✅ Build Complete!"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "  The extension package is ready:"
ls -la *.vsix 2>/dev/null | tail -1
echo ""
echo "  This single .vsix file works on ALL platforms:"
echo "    • Windows (x64, ARM64)"
echo "    • macOS (Intel, Apple Silicon)"
echo "    • Linux (x64, ARM64)"
echo ""
echo "  No native binaries required - WASM runs everywhere!"
echo ""
