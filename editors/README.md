# Pseudocode Editor Support

This directory contains syntax highlighting and editor support for various editors.

## Vim / Neovim

### Installation

#### Manual Installation
Copy the files to your Vim configuration:

```bash
# Create directories if needed
mkdir -p ~/.vim/syntax ~/.vim/ftdetect ~/.vim/indent

# Copy files
cp vim/pseudocode.vim ~/.vim/syntax/
cp vim/ftdetect/pseudocode.vim ~/.vim/ftdetect/
cp vim/indent/pseudocode.vim ~/.vim/indent/
```

#### Using vim-plug
Add to your `.vimrc` or `init.vim`:
```vim
Plug 'NagusameCS/Pseudocode', { 'rtp': 'editors/vim' }
```

#### Using Packer (Neovim)
```lua
use { 'NagusameCS/Pseudocode', rtp = 'editors/vim' }
```

### Features
- Syntax highlighting for keywords, functions, strings, numbers, comments
- Auto-indentation
- File type detection (`.pseudo`, `.psc`)

---

## Sublime Text

### Installation

1. Open Sublime Text
2. Go to `Preferences > Browse Packages...`
3. Create a folder called `Pseudocode`
4. Copy `sublime/Pseudocode.sublime-syntax` into it

### Features
- Full syntax highlighting
- TextMate-compatible grammar

---

## Emacs

### Installation

Add to your Emacs config (`.emacs` or `init.el`):

```elisp
(load-file "/path/to/editors/emacs/pseudocode-mode.el")
```

Or install manually:
```bash
cp emacs/pseudocode-mode.el ~/.emacs.d/lisp/
```

Then add to your config:
```elisp
(add-to-list 'load-path "~/.emacs.d/lisp/")
(require 'pseudocode-mode)
```

### Features
- Syntax highlighting
- Auto-indentation
- Comment handling

---

## VS Code

The official VS Code extension is available on the Visual Studio Marketplace:

[Pseudocode Language Extension](https://marketplace.visualstudio.com/items?itemName=NagusameCS.pseudocode-lang)

Or install from the command line:
```bash
code --install-extension NagusameCS.pseudocode-lang
```

### Features
- Full IntelliSense (autocomplete, hover docs, signature help)
- Real-time diagnostics
- Code formatting
- Document symbols/outline
- Integrated debugger
- Interactive REPL
- Run with one keystroke (Ctrl+Shift+R)
