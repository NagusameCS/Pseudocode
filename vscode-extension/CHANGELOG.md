# Changelog

All notable changes to the Pseudocode Language extension will be documented in this file.

## [2.3.0] - 2026-01-18

### Fixed
- **Removed Native Binary Code** — Completely removed all native VM fallback code that caused libpcre2 errors
- **REPL Now Uses WASM** — Interactive REPL uses the cross-platform WASM runtime
- Cleaned up unused code and imports

## [2.2.0] - 2026-01-16

### Fixed
- **No Native Dependencies** — WASM runtime eliminates libpcre2/dynamic library errors on macOS/Linux
- Removed all platform-specific binary fallbacks

## [2.1.0] - 2026-01-16

### Fixed
- **Walkthrough Media** — Fixed broken walkthrough steps that were treating markdown content as file paths
- Updated documentation to reflect 140+ built-in functions

## [2.0.0] - 2026-01-16

### Changed
- **Major Release** — Production-ready Pseudocode language support
- **Pure WASM Runtime** — No native binaries required, works on any platform
- **Cleaned Repository** — Removed unnecessary build artifacts and archives
- **145 Built-in Functions** — Comprehensive standard library
- **331 VM Opcodes** — Full-featured bytecode instruction set

### Highlights
- Cross-platform compatibility (Windows, macOS, Linux, ARM, x64)
- Syntax highlighting, IntelliSense, diagnostics, formatting
- Code snippets and symbol navigation
- Integrated debugger support
- Bundled with esbuild for optimal package size

## [1.7.0] - 2026-01-16

### Added
- **Cross-Platform WASM Runtime** — Pure TypeScript/WASM implementation that works on all platforms
- **New Built-in Functions**:
  - `randint(min, max)` — Generate random integers in a range
  - `char_at(str, index)` — Get character at position in string
  - `contains(str, substr)` — Check if string contains substring

### Fixed
- **All 33 Examples Pass** — Fixed issues in quicksort, text_analyzer, password_generator
- **VSCode Extension Bundling** — Extension now bundles correctly with esbuild
- **Extended Opcode System** — Opcodes above 255 now use extended opcode mechanism

### Changed
- Extension no longer requires platform-specific binaries
- Updated bundling to use esbuild for smaller package size (244KB)
- Moved base64 encode/decode to extended opcodes for opcode space optimization

## [1.5.0] - 2026-01-11

### Fixed
- **Major Performance Improvements** — Significantly reduced lag while typing:
  - Debounced diagnostics (300ms delay) to prevent parsing on every keystroke
  - Cached completion items with version-aware invalidation
  - Optimized symbol parsing with document version tracking
  - Reduced inlay hints processing for faster response
- **Cross-Platform Compatibility** — Improved VM detection on Windows, Linux, and macOS
- **VM Build Fix** — Added missing `free_object` declaration for proper compilation

### Changed
- Reduced file size thresholds for better performance on large files
- Static completion items (keywords, builtins) are now pre-built once and reused

## [1.4.0] - 2025-01-11

### Added
- **VM Performance Optimizations** — Major runtime improvements:
  - **Function Inlining** — Small functions inlined automatically for reduced call overhead
  - **Polymorphic Inline Caching** — Up to 4x faster method dispatch with shape caching
  - **SIMD Vectorization** — AVX/SSE accelerated array operations (add, sub, mul, div, sum, dot)
  - **Escape Analysis** — Track object lifetimes for future stack allocation
- **New Benchmark Suite** — Comprehensive benchmarks comparing against C and Python
- **Performance Results** — 2-4x faster than Python on typical workloads

### Changed
- Extension now documents all VM optimization capabilities
- Updated syntax highlighting for SIMD array operations

### Fixed
- Various VM stability improvements

## [1.3.0] - 2025-01-10

### Added
- **Inlay Hints** — Show parameter names in function calls for better readability
- **Call Hierarchy** — See incoming/outgoing calls with Ctrl+Shift+H
- **Color Provider** — Visual color picker for hex color strings
- **Smart Selection** — Expand selection intelligently with Shift+Alt+Right
- **Type Definition** — Jump to type definitions (Go to Type Definition)
- **Getting Started Walkthrough** — Interactive tutorial for new users
- **Problem Matcher** — Parse error output for Problems panel integration
- **Command Palette Integration** — All commands available in command palette
- **New Settings**:
  - `pseudocode.inlayHints.parameterNames` — Toggle parameter hints
  - `pseudocode.codeLens.enabled` — Toggle code lens
  - `pseudocode.codeLens.showReferences` — Toggle reference counts
  - `pseudocode.statusBar.enabled` — Toggle status bar

### Changed
- Extended syntax highlighting with more builtin categories

## [1.2.0] - 2025-01-10

### Added
- **Go to Definition** — Jump to function/variable definitions with F12
- **Find All References** — Discover all usages with Shift+F12
- **Rename Symbol** — Safely rename across file with F2
- **Code Lens** — Reference counts and run buttons above functions
- **Document Highlights** — Highlight matching symbols when selected
- **Folding Ranges** — Collapse functions, loops, and conditionals
- **Semantic Tokens** — Enhanced syntax highlighting for better readability
- **Workspace Symbols** — Search functions across all files with Ctrl+T
- **Code Actions** — Quick fixes for common errors and refactoring support
- **Status Bar** — Shows function count, variable count, and line count
- **50+ Code Snippets** — Comprehensive snippets for all language constructs

### Changed
- Completely rewrote snippets to use correct Pseudocode syntax
- Enhanced syntax highlighting with better categorization of builtins
- Improved error diagnostics with more helpful messages

### Fixed
- Snippets now use correct `then/do/end` syntax instead of braces

## [1.1.0] - 2025-01-09

### Added
- **IntelliSense** — Smart autocomplete for keywords, builtins, and local symbols
- **Hover Documentation** — Rich documentation for all 80+ built-in functions
- **Real-time Diagnostics** — Catch errors as you type with typo detection
- **Document Formatting** — Auto-indent with Format Document (Shift+Alt+F)
- **Document Symbols** — Quick navigation via Outline view
- **Signature Help** — Parameter hints for function calls
- **Debugger** — Basic debugging support with breakpoints
- **REPL** — Interactive Pseudocode console

### Changed
- Extended file extension support: `.pseudoh`, `.psch`, `.pseudocode`
- Improved VM detection across different workspace layouts

## [1.0.0] - 2025-01-01

### Added
- Initial release
- Syntax highlighting for `.pseudo` and `.psc` files
- Code snippets for common patterns
- Run command with keyboard shortcut (`Ctrl+Shift+R`)
- Build command for compiling the VM
- Configurable VM path
- Execution time display
