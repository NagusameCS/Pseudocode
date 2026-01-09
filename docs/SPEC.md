# Pseudocode Language Specification

**Version:** 1.0  
**Date:** January 2026

## 1. Lexical Structure

### 1.1 Character Set
Pseudocode source files are UTF-8 encoded text.

### 1.2 Whitespace
- Spaces and tabs are ignored except for separating tokens
- Newlines are significant as statement terminators
- Indentation is not syntactically significant

### 1.3 Comments
```
// Single-line comment (only supported comment type)
```

### 1.4 Keywords
```
fn      let     if      then    elif    else    end
for     in      to      do      while   return  match
case    true    false   nil     and     or      not
```

### 1.5 Identifiers
```
identifier = letter (letter | digit | "_")*
letter     = "a".."z" | "A".."Z" | "_"
digit      = "0".."9"
```

### 1.6 Literals

**Numbers:**
```
number     = integer | float
integer    = digit+
float      = digit+ "." digit+
```

**Strings:**
```
string     = '"' character* '"' | "'" character* "'"
character  = any_unicode_except_quote | escape_sequence
escape     = "\n" | "\t" | "\r" | "\\" | "\"" | "\'"
```

**Booleans:**
```
boolean    = "true" | "false"
```

**Nil:**
```
nil        = "nil"
```

## 2. Grammar

### 2.1 Program Structure
```
program        = statement*
statement      = declaration | expression_stmt | control_flow | return_stmt
```

### 2.2 Declarations

**Variable Declaration:**
```
var_decl       = "let" identifier "=" expression NEWLINE
```

**Function Declaration:**
```
func_decl      = "fn" identifier "(" parameters? ")" NEWLINE
                 statement*
                 "end" NEWLINE
parameters     = identifier ("," identifier)*
```

### 2.3 Expressions
```
expression     = assignment
assignment     = identifier "=" expression | logic_or

logic_or       = logic_and ("or" logic_and)*
logic_and      = equality ("and" equality)*
equality       = comparison (("==" | "!=") comparison)*
comparison     = term (("<" | ">" | "<=" | ">=") term)*
term           = factor (("+" | "-") factor)*
factor         = unary (("*" | "/" | "%") unary)*
unary          = ("not" | "-") unary | call
call           = primary ("(" arguments? ")" | "[" expression "]")*
primary        = identifier | literal | "(" expression ")" | array | dict

arguments      = expression ("," expression)*
array          = "[" (expression ("," expression)*)? "]"
dict           = "{" (pair ("," pair)*)? "}"
pair           = (string | identifier) ":" expression
```

### 2.4 Control Flow

**If Statement:**
```
if_stmt        = "if" expression "then" NEWLINE
                 statement*
                 elif_clause*
                 else_clause?
                 "end" NEWLINE
elif_clause    = "elif" expression "then" NEWLINE statement*
else_clause    = "else" NEWLINE statement*
```

**For Loop:**
```
for_stmt       = "for" identifier "in" range_or_iter "do" NEWLINE
                 statement*
                 "end" NEWLINE
range_or_iter  = expression "to" expression | expression
```

**While Loop:**
```
while_stmt     = "while" expression "do" NEWLINE
                 statement*
                 "end" NEWLINE
```

**Match Expression:**
```
match_stmt     = "match" expression NEWLINE
                 case_clause+
                 "end" NEWLINE
case_clause    = "case" pattern guard? "then" NEWLINE statement*
pattern        = literal | identifier | "_"
guard          = "if" expression
```

### 2.5 Return Statement
```
return_stmt    = "return" expression? NEWLINE
```

## 3. Type System

### 3.1 Value Types
All values are represented internally as 64-bit NaN-boxed values.

| Type | Description | Examples |
|------|-------------|----------|
| Number | 64-bit IEEE 754 float | `42`, `3.14`, `-17` |
| String | UTF-8 immutable string | `"hello"`, `'world'` |
| Boolean | Logical value | `true`, `false` |
| Nil | Absence of value | `nil` |
| Array | Dynamic array | `[1, 2, 3]` |
| Dictionary | Hash map | `{"a": 1}` |
| Function | First-class function | `fn(x) return x end` |

### 3.2 Type Coercion
- Arithmetic operators require numeric operands
- String `+` concatenates strings
- Comparisons `<`, `>`, `<=`, `>=` require same-type operands
- Equality `==`, `!=` work across types (different types are never equal)
- Truthiness: `false` and `nil` are falsy, all other values are truthy

## 4. Operators

### 4.1 Precedence (lowest to highest)
| Precedence | Operators | Associativity |
|------------|-----------|---------------|
| 1 | `or` | Left |
| 2 | `and` | Left |
| 3 | `==` `!=` | Left |
| 4 | `<` `>` `<=` `>=` | Left |
| 5 | `+` `-` | Left |
| 6 | `*` `/` `%` | Left |
| 7 | `not` `-` (unary) | Right |
| 8 | `()` `[]` | Left |

### 4.2 Arithmetic Operators
- `+` : Addition (numbers) or concatenation (strings)
- `-` : Subtraction
- `*` : Multiplication
- `/` : Division (always floating-point)
- `%` : Modulo (optimized for power-of-2 divisors)

### 4.3 Comparison Operators
- `==` : Equality
- `!=` : Inequality
- `<`, `>`, `<=`, `>=` : Ordering

### 4.4 Logical Operators
- `and` : Short-circuit conjunction
- `or` : Short-circuit disjunction
- `not` : Logical negation

## 5. Built-in Functions

### 5.1 I/O Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `print(value) -> nil` | Output to stdout |
| `input` | `input(prompt?) -> string` | Read line from stdin |

### 5.2 Math Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `abs` | `abs(x) -> number` | Absolute value |
| `sqrt` | `sqrt(x) -> number` | Square root |
| `pow` | `pow(x, y) -> number` | Exponentiation |
| `min` | `min(a, b) -> number` | Minimum |
| `max` | `max(a, b) -> number` | Maximum |
| `floor` | `floor(x) -> number` | Floor |
| `ceil` | `ceil(x) -> number` | Ceiling |
| `round` | `round(x) -> number` | Round to nearest |
| `sin` | `sin(x) -> number` | Sine |
| `cos` | `cos(x) -> number` | Cosine |
| `tan` | `tan(x) -> number` | Tangent |
| `log` | `log(x) -> number` | Natural logarithm |
| `exp` | `exp(x) -> number` | e^x |
| `random` | `random() -> number` | Random [0, 1) |

### 5.3 String Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `len` | `len(s) -> number` | String length |
| `upper` | `upper(s) -> string` | Uppercase |
| `lower` | `lower(s) -> string` | Lowercase |
| `trim` | `trim(s) -> string` | Remove whitespace |
| `split` | `split(s, delim) -> array` | Split string |
| `join` | `join(arr, delim) -> string` | Join array |
| `substr` | `substr(s, start, len) -> string` | Substring |
| `replace` | `replace(s, old, new) -> string` | Replace all |
| `starts_with` | `starts_with(s, prefix) -> bool` | Prefix check |
| `ends_with` | `ends_with(s, suffix) -> bool` | Suffix check |
| `contains` | `contains(s, sub) -> bool` | Substring check |
| `index_of` | `index_of(s, sub) -> number` | Find index |

### 5.4 Array Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `len` | `len(arr) -> number` | Array length |
| `push` | `push(arr, val) -> nil` | Append element |
| `pop` | `pop(arr) -> value` | Remove last |
| `slice` | `slice(arr, start, end) -> array` | Subarray |
| `reverse` | `reverse(arr) -> array` | Reverse |
| `sort` | `sort(arr) -> array` | Sort ascending |
| `map` | `map(arr, fn) -> array` | Transform |
| `filter` | `filter(arr, fn) -> array` | Filter |
| `reduce` | `reduce(arr, fn, init) -> value` | Fold |
| `find` | `find(arr, fn) -> value` | Find first |

### 5.5 Dictionary Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `keys` | `keys(dict) -> array` | All keys |
| `values` | `values(dict) -> array` | All values |
| `has_key` | `has_key(dict, key) -> bool` | Key exists |
| `dict_get` | `dict_get(dict, key, default) -> value` | Get with default |

### 5.6 File Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `read_file` | `read_file(path) -> string` | Read entire file |
| `write_file` | `write_file(path, content) -> nil` | Write file |
| `append_file` | `append_file(path, content) -> nil` | Append to file |
| `file_exists` | `file_exists(path) -> bool` | Check existence |

### 5.7 HTTP Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `http_get` | `http_get(url) -> string` | GET request |
| `http_post` | `http_post(url, body) -> string` | POST request |
| `http_put` | `http_put(url, body) -> string` | PUT request |
| `http_delete` | `http_delete(url) -> string` | DELETE request |

### 5.8 JSON Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `json_parse` | `json_parse(str) -> value` | Parse JSON |
| `json_stringify` | `json_stringify(val) -> string` | Serialize JSON |

### 5.9 Crypto Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `sha256` | `sha256(data) -> string` | SHA-256 hash |
| `md5` | `md5(data) -> string` | MD5 hash |
| `encode_base64` | `encode_base64(data) -> string` | Base64 encode |
| `decode_base64` | `decode_base64(str) -> string` | Base64 decode |

### 5.10 Type Functions
| Function | Signature | Description |
|----------|-----------|-------------|
| `type` | `type(val) -> string` | Type name |
| `int` | `int(x) -> number` | Convert to integer |
| `float` | `float(x) -> number` | Convert to float |
| `str` | `str(x) -> string` | Convert to string |

## 6. Runtime

### 6.1 Execution Modes
- **Interpreter**: Bytecode VM with NaN-boxing and computed gotos
- **JIT**: x86-64 native code generation for numeric loops (enabled with `-j` flag)

### 6.2 Memory Management
- Garbage collected using mark-and-sweep
- Strings are immutable and interned
- Arrays and dictionaries are mutable reference types

### 6.3 Error Handling
Runtime errors terminate execution with an error message and line number.

## 7. Command Line

```
Usage: pseudo [options] <file.pseudo>

Options:
  -j    Enable JIT compilation for numeric loops
  -d    Debug mode (show bytecode and execution trace)
  -v    Print version information
  -h    Show help message
```

## 8. File Extension
Pseudocode source files use the `.pseudo` extension.
