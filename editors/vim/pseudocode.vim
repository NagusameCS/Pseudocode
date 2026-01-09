" Vim syntax file
" Language: Pseudocode
" Maintainer: NagusameCS
" Latest Revision: 2026
" License: MIT

if exists("b:current_syntax")
  finish
endif

" Keywords
syn keyword pseudoKeyword let const fn return if then elif else end
syn keyword pseudoKeyword while for in do
syn keyword pseudoKeyword match case
syn keyword pseudoOperator and or not

" Constants
syn keyword pseudoConstant true false nil

" Built-in functions
syn keyword pseudoBuiltin print input len str int float type
syn keyword pseudoBuiltin push pop shift unshift slice concat sort reverse range
syn keyword pseudoBuiltin map filter reduce find every some
syn keyword pseudoBuiltin split join replace upper lower trim contains
syn keyword pseudoBuiltin starts_with ends_with substr char_at index_of
syn keyword pseudoBuiltin abs min max floor ceil round sqrt pow
syn keyword pseudoBuiltin sin cos tan log exp random randint
syn keyword pseudoBuiltin read_file write_file
syn keyword pseudoBuiltin http_get http_post
syn keyword pseudoBuiltin json_parse json_stringify
syn keyword pseudoBuiltin time time_ms sleep
syn keyword pseudoBuiltin is_nil is_int is_float is_string is_array is_function
syn keyword pseudoBuiltin md5 sha256 base64_encode base64_decode
syn keyword pseudoBuiltin assert error exit

" Numbers
syn match pseudoNumber "\<\d\+\>"
syn match pseudoNumber "\<\d\+\.\d*\>"
syn match pseudoNumber "\<\d*\.\d\+\>"
syn match pseudoNumber "\<0x[0-9a-fA-F]\+\>"
syn match pseudoNumber "\<0b[01]\+\>"

" Strings
syn region pseudoString start=+"+ skip=+\\"+ end=+"+ contains=pseudoEscape
syn region pseudoString start=+'+ skip=+\\'+ end=+'+ contains=pseudoEscape
syn match pseudoEscape contained "\\[nrtv\\'\"]"
syn match pseudoEscape contained "\\x[0-9a-fA-F]\{2}"

" Comments
syn match pseudoComment "//.*$"
syn region pseudoComment start="/\*" end="\*/"

" Function definition
syn match pseudoFunction "\<fn\s\+\zs[a-zA-Z_][a-zA-Z0-9_]*"

" Operators
syn match pseudoOperator "[-+*/%=<>!&|^~]"
syn match pseudoOperator "=="
syn match pseudoOperator "!="
syn match pseudoOperator "<="
syn match pseudoOperator ">="
syn match pseudoOperator "&&"
syn match pseudoOperator "||"
syn match pseudoOperator "->"

" Delimiters
syn match pseudoDelimiter "[(),\[\]{}:]"

" Highlighting
hi def link pseudoKeyword     Keyword
hi def link pseudoOperator    Operator
hi def link pseudoConstant    Constant
hi def link pseudoBuiltin     Function
hi def link pseudoNumber      Number
hi def link pseudoString      String
hi def link pseudoEscape      SpecialChar
hi def link pseudoComment     Comment
hi def link pseudoFunction    Function
hi def link pseudoDelimiter   Delimiter

let b:current_syntax = "pseudocode"
