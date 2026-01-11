# Changelog

All notable changes to the Pseudocode Language extension will be documented in this file.

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
