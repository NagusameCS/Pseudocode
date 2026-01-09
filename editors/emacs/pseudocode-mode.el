;;; pseudocode-mode.el --- Major mode for Pseudocode programming language -*- lexical-binding: t; -*-

;; Copyright (c) 2026 NagusameCS
;; Licensed under the MIT License

;; Author: NagusameCS
;; Version: 1.0.0
;; Keywords: languages, pseudocode
;; URL: https://github.com/NagusameCS/Pseudocode

;;; Commentary:

;; This package provides a major mode for editing Pseudocode files.
;; It includes syntax highlighting and basic indentation support.

;;; Code:

(defvar pseudocode-mode-syntax-table
  (let ((table (make-syntax-table)))
    ;; Comments
    (modify-syntax-entry ?/ ". 124" table)
    (modify-syntax-entry ?* ". 23b" table)
    (modify-syntax-entry ?\n ">" table)
    ;; Strings
    (modify-syntax-entry ?\" "\"" table)
    (modify-syntax-entry ?\' "\"" table)
    ;; Operators
    (modify-syntax-entry ?+ "." table)
    (modify-syntax-entry ?- "." table)
    (modify-syntax-entry ?= "." table)
    (modify-syntax-entry ?< "." table)
    (modify-syntax-entry ?> "." table)
    (modify-syntax-entry ?& "." table)
    (modify-syntax-entry ?| "." table)
    table)
  "Syntax table for `pseudocode-mode'.")

(defvar pseudocode-keywords
  '("let" "const" "fn" "return" "if" "then" "elif" "else" "end"
    "while" "for" "in" "do" "match" "case")
  "Keywords in Pseudocode.")

(defvar pseudocode-operators
  '("and" "or" "not")
  "Logical operators in Pseudocode.")

(defvar pseudocode-constants
  '("true" "false" "nil")
  "Constants in Pseudocode.")

(defvar pseudocode-builtins
  '("print" "input" "len" "str" "int" "float" "type"
    "push" "pop" "shift" "unshift" "slice" "concat" "sort" "reverse" "range"
    "map" "filter" "reduce" "find" "every" "some"
    "split" "join" "replace" "upper" "lower" "trim" "contains"
    "starts_with" "ends_with" "substr" "char_at" "index_of"
    "abs" "min" "max" "floor" "ceil" "round" "sqrt" "pow"
    "sin" "cos" "tan" "log" "exp" "random" "randint"
    "read_file" "write_file"
    "http_get" "http_post"
    "json_parse" "json_stringify"
    "time" "time_ms" "sleep"
    "is_nil" "is_int" "is_float" "is_string" "is_array" "is_function"
    "md5" "sha256" "base64_encode" "base64_decode"
    "assert" "error" "exit")
  "Built-in functions in Pseudocode.")

(defvar pseudocode-font-lock-keywords
  `(
    ;; Keywords
    (,(regexp-opt pseudocode-keywords 'words) . font-lock-keyword-face)
    ;; Operators
    (,(regexp-opt pseudocode-operators 'words) . font-lock-keyword-face)
    ;; Constants
    (,(regexp-opt pseudocode-constants 'words) . font-lock-constant-face)
    ;; Built-ins
    (,(regexp-opt pseudocode-builtins 'words) . font-lock-builtin-face)
    ;; Function definitions
    ("\\<fn\\>\\s-+\\([a-zA-Z_][a-zA-Z0-9_]*\\)" 1 font-lock-function-name-face)
    ;; Numbers
    ("\\<[0-9]+\\(\\.[0-9]*\\)?\\>" . font-lock-constant-face)
    ("\\<0x[0-9a-fA-F]+\\>" . font-lock-constant-face)
    ("\\<0b[01]+\\>" . font-lock-constant-face))
  "Font lock keywords for `pseudocode-mode'.")

(defun pseudocode-indent-line ()
  "Indent current line for `pseudocode-mode'."
  (interactive)
  (let ((indent 0)
        (cur-indent (current-indentation))
        (prev-indent 0))
    (save-excursion
      (beginning-of-line)
      (when (not (bobp))
        (forward-line -1)
        (setq prev-indent (current-indentation))
        (setq indent prev-indent)
        ;; Increase indent after block openers
        (when (looking-at ".*\\<\\(fn\\|if\\|elif\\|else\\|while\\|for\\|match\\|case\\)\\>")
          (setq indent (+ indent tab-width)))))
    ;; Decrease indent for block closers
    (save-excursion
      (beginning-of-line)
      (when (looking-at "\\s-*\\<\\(end\\|else\\|elif\\|case\\)\\>")
        (setq indent (max 0 (- indent tab-width)))))
    (indent-line-to indent)))

;;;###autoload
(define-derived-mode pseudocode-mode prog-mode "Pseudocode"
  "Major mode for editing Pseudocode files."
  :syntax-table pseudocode-mode-syntax-table
  (setq-local font-lock-defaults '(pseudocode-font-lock-keywords))
  (setq-local comment-start "// ")
  (setq-local comment-end "")
  (setq-local indent-line-function #'pseudocode-indent-line)
  (setq-local tab-width 4))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.pseudo\\'" . pseudocode-mode))
(add-to-list 'auto-mode-alist '("\\.psc\\'" . pseudocode-mode))

(provide 'pseudocode-mode)

;;; pseudocode-mode.el ends here
