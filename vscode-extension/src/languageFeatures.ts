/*
 * Pseudocode Language Features
 * Provides autocomplete, hover, diagnostics, symbols, and signature help
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

import * as vscode from 'vscode';

// ============ Built-in Functions Documentation ============

interface BuiltinInfo {
    signature: string;
    description: string;
    params?: { name: string; description: string }[];
    returns?: string;
    example?: string;
}

const BUILTINS: Record<string, BuiltinInfo> = {
    // I/O Functions
    'print': {
        signature: 'print(value, ...)',
        description: 'Prints values to stdout, followed by a newline.',
        params: [{ name: 'value', description: 'Value(s) to print' }],
        example: 'print("Hello", name, 42)'
    },
    'input': {
        signature: 'input(prompt?) -> string',
        description: 'Reads a line of input from stdin.',
        params: [{ name: 'prompt', description: 'Optional prompt to display' }],
        returns: 'The input string',
        example: 'let name = input("Enter name: ")'
    },
    'read_file': {
        signature: 'read_file(path) -> string',
        description: 'Reads entire file contents as a string.',
        params: [{ name: 'path', description: 'Path to the file' }],
        returns: 'File contents as string'
    },
    'write_file': {
        signature: 'write_file(path, content) -> bool',
        description: 'Writes content to a file.',
        params: [
            { name: 'path', description: 'Path to the file' },
            { name: 'content', description: 'Content to write' }
        ],
        returns: 'true on success'
    },

    // Math Functions
    'abs': {
        signature: 'abs(x) -> number',
        description: 'Returns the absolute value of x.',
        params: [{ name: 'x', description: 'A number' }],
        returns: 'Absolute value'
    },
    'sqrt': {
        signature: 'sqrt(x) -> number',
        description: 'Returns the square root of x.',
        params: [{ name: 'x', description: 'A non-negative number' }],
        returns: 'Square root of x'
    },
    'pow': {
        signature: 'pow(base, exp) -> number',
        description: 'Returns base raised to the power of exp.',
        params: [
            { name: 'base', description: 'The base number' },
            { name: 'exp', description: 'The exponent' }
        ],
        returns: 'base^exp'
    },
    'floor': {
        signature: 'floor(x) -> int',
        description: 'Rounds x down to the nearest integer.',
        params: [{ name: 'x', description: 'A number' }],
        returns: 'Largest integer ≤ x'
    },
    'ceil': {
        signature: 'ceil(x) -> int',
        description: 'Rounds x up to the nearest integer.',
        params: [{ name: 'x', description: 'A number' }],
        returns: 'Smallest integer ≥ x'
    },
    'round': {
        signature: 'round(x) -> int',
        description: 'Rounds x to the nearest integer.',
        params: [{ name: 'x', description: 'A number' }],
        returns: 'Nearest integer to x'
    },
    'min': {
        signature: 'min(a, b, ...) -> number',
        description: 'Returns the smallest of the given values.',
        params: [{ name: 'values', description: 'Two or more numbers' }],
        returns: 'Minimum value'
    },
    'max': {
        signature: 'max(a, b, ...) -> number',
        description: 'Returns the largest of the given values.',
        params: [{ name: 'values', description: 'Two or more numbers' }],
        returns: 'Maximum value'
    },
    'sin': {
        signature: 'sin(x) -> number',
        description: 'Returns the sine of x (in radians).',
        params: [{ name: 'x', description: 'Angle in radians' }],
        returns: 'Sine of x'
    },
    'cos': {
        signature: 'cos(x) -> number',
        description: 'Returns the cosine of x (in radians).',
        params: [{ name: 'x', description: 'Angle in radians' }],
        returns: 'Cosine of x'
    },
    'tan': {
        signature: 'tan(x) -> number',
        description: 'Returns the tangent of x (in radians).',
        params: [{ name: 'x', description: 'Angle in radians' }],
        returns: 'Tangent of x'
    },
    'log': {
        signature: 'log(x) -> number',
        description: 'Returns the natural logarithm of x.',
        params: [{ name: 'x', description: 'A positive number' }],
        returns: 'Natural log of x'
    },
    'exp': {
        signature: 'exp(x) -> number',
        description: 'Returns e raised to the power x.',
        params: [{ name: 'x', description: 'A number' }],
        returns: 'e^x'
    },
    'random': {
        signature: 'random() -> number',
        description: 'Returns a random number between 0 and 1.',
        returns: 'Random float in [0, 1)'
    },
    'randint': {
        signature: 'randint(min, max) -> int',
        description: 'Returns a random integer between min and max (inclusive).',
        params: [
            { name: 'min', description: 'Minimum value' },
            { name: 'max', description: 'Maximum value' }
        ],
        returns: 'Random integer in [min, max]'
    },

    // String Functions
    'len': {
        signature: 'len(value) -> int',
        description: 'Returns the length of a string or array.',
        params: [{ name: 'value', description: 'String or array' }],
        returns: 'Number of elements/characters'
    },
    'str': {
        signature: 'str(value) -> string',
        description: 'Converts a value to its string representation.',
        params: [{ name: 'value', description: 'Any value' }],
        returns: 'String representation'
    },
    'int': {
        signature: 'int(value) -> int',
        description: 'Converts a value to an integer.',
        params: [{ name: 'value', description: 'String or number' }],
        returns: 'Integer value'
    },
    'float': {
        signature: 'float(value) -> number',
        description: 'Converts a value to a floating-point number.',
        params: [{ name: 'value', description: 'String or number' }],
        returns: 'Float value'
    },
    'upper': {
        signature: 'upper(s) -> string',
        description: 'Converts string to uppercase.',
        params: [{ name: 's', description: 'A string' }],
        returns: 'Uppercase string'
    },
    'lower': {
        signature: 'lower(s) -> string',
        description: 'Converts string to lowercase.',
        params: [{ name: 's', description: 'A string' }],
        returns: 'Lowercase string'
    },
    'trim': {
        signature: 'trim(s) -> string',
        description: 'Removes leading and trailing whitespace.',
        params: [{ name: 's', description: 'A string' }],
        returns: 'Trimmed string'
    },
    'split': {
        signature: 'split(s, delimiter) -> array',
        description: 'Splits a string by delimiter.',
        params: [
            { name: 's', description: 'String to split' },
            { name: 'delimiter', description: 'Separator string' }
        ],
        returns: 'Array of substrings'
    },
    'join': {
        signature: 'join(array, delimiter) -> string',
        description: 'Joins array elements with delimiter.',
        params: [
            { name: 'array', description: 'Array of strings' },
            { name: 'delimiter', description: 'Separator string' }
        ],
        returns: 'Joined string'
    },
    'replace': {
        signature: 'replace(s, old, new) -> string',
        description: 'Replaces all occurrences of old with new.',
        params: [
            { name: 's', description: 'Original string' },
            { name: 'old', description: 'Substring to find' },
            { name: 'new', description: 'Replacement string' }
        ],
        returns: 'Modified string'
    },
    'contains': {
        signature: 'contains(s, substr) -> bool',
        description: 'Checks if string contains substring.',
        params: [
            { name: 's', description: 'String to search in' },
            { name: 'substr', description: 'Substring to find' }
        ],
        returns: 'true if found'
    },
    'starts_with': {
        signature: 'starts_with(s, prefix) -> bool',
        description: 'Checks if string starts with prefix.',
        params: [
            { name: 's', description: 'String to check' },
            { name: 'prefix', description: 'Prefix to find' }
        ],
        returns: 'true if starts with prefix'
    },
    'ends_with': {
        signature: 'ends_with(s, suffix) -> bool',
        description: 'Checks if string ends with suffix.',
        params: [
            { name: 's', description: 'String to check' },
            { name: 'suffix', description: 'Suffix to find' }
        ],
        returns: 'true if ends with suffix'
    },
    'substr': {
        signature: 'substr(s, start, len?) -> string',
        description: 'Extracts a substring.',
        params: [
            { name: 's', description: 'Original string' },
            { name: 'start', description: 'Start index (0-based)' },
            { name: 'len', description: 'Optional length' }
        ],
        returns: 'Substring'
    },
    'char_at': {
        signature: 'char_at(s, index) -> string',
        description: 'Returns character at index.',
        params: [
            { name: 's', description: 'A string' },
            { name: 'index', description: 'Character index' }
        ],
        returns: 'Single character string'
    },
    'index_of': {
        signature: 'index_of(s, substr) -> int',
        description: 'Finds first occurrence of substring.',
        params: [
            { name: 's', description: 'String to search' },
            { name: 'substr', description: 'Substring to find' }
        ],
        returns: 'Index or -1 if not found'
    },

    // Array Functions
    'push': {
        signature: 'push(array, value)',
        description: 'Appends value to end of array (in-place).',
        params: [
            { name: 'array', description: 'Target array' },
            { name: 'value', description: 'Value to append' }
        ]
    },
    'pop': {
        signature: 'pop(array) -> value',
        description: 'Removes and returns last element.',
        params: [{ name: 'array', description: 'Target array' }],
        returns: 'Removed element'
    },
    'shift': {
        signature: 'shift(array) -> value',
        description: 'Removes and returns first element.',
        params: [{ name: 'array', description: 'Target array' }],
        returns: 'Removed element'
    },
    'unshift': {
        signature: 'unshift(array, value)',
        description: 'Prepends value to beginning of array.',
        params: [
            { name: 'array', description: 'Target array' },
            { name: 'value', description: 'Value to prepend' }
        ]
    },
    'slice': {
        signature: 'slice(array, start, end?) -> array',
        description: 'Returns a portion of array.',
        params: [
            { name: 'array', description: 'Source array' },
            { name: 'start', description: 'Start index' },
            { name: 'end', description: 'Optional end index' }
        ],
        returns: 'New array with slice'
    },
    'concat': {
        signature: 'concat(a, b) -> array',
        description: 'Concatenates two arrays.',
        params: [
            { name: 'a', description: 'First array' },
            { name: 'b', description: 'Second array' }
        ],
        returns: 'New combined array'
    },
    'reverse': {
        signature: 'reverse(array) -> array',
        description: 'Returns reversed array.',
        params: [{ name: 'array', description: 'Source array' }],
        returns: 'Reversed array'
    },
    'sort': {
        signature: 'sort(array) -> array',
        description: 'Returns sorted array.',
        params: [{ name: 'array', description: 'Source array' }],
        returns: 'Sorted array'
    },
    'map': {
        signature: 'map(array, fn) -> array',
        description: 'Applies function to each element.',
        params: [
            { name: 'array', description: 'Source array' },
            { name: 'fn', description: 'Mapping function' }
        ],
        returns: 'New transformed array',
        example: 'map([1, 2, 3], fn(x) x * 2 end)'
    },
    'filter': {
        signature: 'filter(array, fn) -> array',
        description: 'Filters elements by predicate.',
        params: [
            { name: 'array', description: 'Source array' },
            { name: 'fn', description: 'Predicate function' }
        ],
        returns: 'Filtered array',
        example: 'filter([1, 2, 3, 4], fn(x) x > 2 end)'
    },
    'reduce': {
        signature: 'reduce(array, fn, initial) -> value',
        description: 'Reduces array to single value.',
        params: [
            { name: 'array', description: 'Source array' },
            { name: 'fn', description: 'Reducer function (acc, val)' },
            { name: 'initial', description: 'Initial accumulator value' }
        ],
        returns: 'Reduced value',
        example: 'reduce([1, 2, 3], fn(a, b) a + b end, 0)'
    },
    'find': {
        signature: 'find(array, fn) -> value',
        description: 'Finds first element matching predicate.',
        params: [
            { name: 'array', description: 'Source array' },
            { name: 'fn', description: 'Predicate function' }
        ],
        returns: 'First matching element or nil'
    },
    'every': {
        signature: 'every(array, fn) -> bool',
        description: 'Checks if all elements satisfy predicate.',
        params: [
            { name: 'array', description: 'Source array' },
            { name: 'fn', description: 'Predicate function' }
        ],
        returns: 'true if all match'
    },
    'some': {
        signature: 'some(array, fn) -> bool',
        description: 'Checks if any element satisfies predicate.',
        params: [
            { name: 'array', description: 'Source array' },
            { name: 'fn', description: 'Predicate function' }
        ],
        returns: 'true if any matches'
    },
    'range': {
        signature: 'range(start, end, step?) -> array',
        description: 'Creates array of numbers in range.',
        params: [
            { name: 'start', description: 'Start value' },
            { name: 'end', description: 'End value (exclusive)' },
            { name: 'step', description: 'Optional step (default 1)' }
        ],
        returns: 'Array of numbers',
        example: 'range(0, 10, 2)  // [0, 2, 4, 6, 8]'
    },

    // Type Functions
    'type': {
        signature: 'type(value) -> string',
        description: 'Returns the type of a value.',
        params: [{ name: 'value', description: 'Any value' }],
        returns: '"int", "float", "string", "bool", "array", "function", "nil"'
    },
    'is_nil': {
        signature: 'is_nil(value) -> bool',
        description: 'Checks if value is nil.',
        params: [{ name: 'value', description: 'Any value' }],
        returns: 'true if nil'
    },
    'is_int': {
        signature: 'is_int(value) -> bool',
        description: 'Checks if value is an integer.',
        params: [{ name: 'value', description: 'Any value' }],
        returns: 'true if integer'
    },
    'is_float': {
        signature: 'is_float(value) -> bool',
        description: 'Checks if value is a float.',
        params: [{ name: 'value', description: 'Any value' }],
        returns: 'true if float'
    },
    'is_string': {
        signature: 'is_string(value) -> bool',
        description: 'Checks if value is a string.',
        params: [{ name: 'value', description: 'Any value' }],
        returns: 'true if string'
    },
    'is_array': {
        signature: 'is_array(value) -> bool',
        description: 'Checks if value is an array.',
        params: [{ name: 'value', description: 'Any value' }],
        returns: 'true if array'
    },
    'is_function': {
        signature: 'is_function(value) -> bool',
        description: 'Checks if value is a function.',
        params: [{ name: 'value', description: 'Any value' }],
        returns: 'true if function'
    },

    // Time Functions
    'time': {
        signature: 'time() -> number',
        description: 'Returns current Unix timestamp in seconds.',
        returns: 'Seconds since epoch'
    },
    'time_ms': {
        signature: 'time_ms() -> number',
        description: 'Returns current time in milliseconds.',
        returns: 'Milliseconds since epoch'
    },
    'sleep': {
        signature: 'sleep(ms)',
        description: 'Pauses execution for specified milliseconds.',
        params: [{ name: 'ms', description: 'Milliseconds to sleep' }]
    },

    // System Functions
    'exit': {
        signature: 'exit(code?)',
        description: 'Exits the program with optional exit code.',
        params: [{ name: 'code', description: 'Exit code (default 0)' }]
    },
    'assert': {
        signature: 'assert(condition, message?)',
        description: 'Throws error if condition is false.',
        params: [
            { name: 'condition', description: 'Condition to check' },
            { name: 'message', description: 'Optional error message' }
        ]
    },
    'error': {
        signature: 'error(message)',
        description: 'Throws an error with message.',
        params: [{ name: 'message', description: 'Error message' }]
    },

    // JSON Functions
    'json_parse': {
        signature: 'json_parse(s) -> value',
        description: 'Parses JSON string to value.',
        params: [{ name: 's', description: 'JSON string' }],
        returns: 'Parsed value'
    },
    'json_stringify': {
        signature: 'json_stringify(value) -> string',
        description: 'Converts value to JSON string.',
        params: [{ name: 'value', description: 'Value to stringify' }],
        returns: 'JSON string'
    },

    // HTTP Functions
    'http_get': {
        signature: 'http_get(url) -> string',
        description: 'Makes HTTP GET request.',
        params: [{ name: 'url', description: 'URL to fetch' }],
        returns: 'Response body'
    },
    'http_post': {
        signature: 'http_post(url, body) -> string',
        description: 'Makes HTTP POST request.',
        params: [
            { name: 'url', description: 'URL to post to' },
            { name: 'body', description: 'Request body' }
        ],
        returns: 'Response body'
    },

    // Crypto Functions
    'md5': {
        signature: 'md5(s) -> string',
        description: 'Computes MD5 hash of string.',
        params: [{ name: 's', description: 'String to hash' }],
        returns: 'Hex-encoded hash'
    },
    'sha256': {
        signature: 'sha256(s) -> string',
        description: 'Computes SHA-256 hash of string.',
        params: [{ name: 's', description: 'String to hash' }],
        returns: 'Hex-encoded hash'
    },
    'base64_encode': {
        signature: 'base64_encode(s) -> string',
        description: 'Encodes string to Base64.',
        params: [{ name: 's', description: 'String to encode' }],
        returns: 'Base64 encoded string'
    },
    'base64_decode': {
        signature: 'base64_decode(s) -> string',
        description: 'Decodes Base64 string.',
        params: [{ name: 's', description: 'Base64 string' }],
        returns: 'Decoded string'
    }
};

// ============ Keywords ============

const KEYWORDS = [
    'let', 'const', 'fn', 'return', 'if', 'then', 'elif', 'else', 'end',
    'while', 'for', 'in', 'do', 'and', 'or', 'not', 'true', 'false', 'nil',
    'match', 'case'
];

// ============ Completion Provider ============

export class PseudocodeCompletionProvider implements vscode.CompletionItemProvider {
    provideCompletionItems(
        document: vscode.TextDocument,
        position: vscode.Position
    ): vscode.CompletionItem[] {
        const items: vscode.CompletionItem[] = [];
        const linePrefix = document.lineAt(position).text.substr(0, position.character);

        // Keywords
        for (const keyword of KEYWORDS) {
            const item = new vscode.CompletionItem(keyword, vscode.CompletionItemKind.Keyword);
            item.detail = 'keyword';
            items.push(item);
        }

        // Built-in functions
        for (const [name, info] of Object.entries(BUILTINS)) {
            const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Function);
            item.detail = info.signature;
            item.documentation = new vscode.MarkdownString(info.description);
            if (info.example) {
                item.documentation.appendCodeblock(info.example, 'pseudocode');
            }
            item.insertText = new vscode.SnippetString(`${name}($1)`);
            items.push(item);
        }

        // Local variables and functions from document
        const text = document.getText();

        // Find variable declarations: let x = ... or const x = ...
        const varRegex = /(?:let|const)\s+([a-zA-Z_][a-zA-Z0-9_]*)/g;
        let match;
        const seenVars = new Set<string>();
        while ((match = varRegex.exec(text)) !== null) {
            const varName = match[1];
            if (!seenVars.has(varName)) {
                seenVars.add(varName);
                const item = new vscode.CompletionItem(varName, vscode.CompletionItemKind.Variable);
                item.detail = 'local variable';
                items.push(item);
            }
        }

        // Find function declarations: fn name(...)
        const fnRegex = /fn\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)/g;
        const seenFns = new Set<string>();
        while ((match = fnRegex.exec(text)) !== null) {
            const fnName = match[1];
            const params = match[2];
            if (!seenFns.has(fnName)) {
                seenFns.add(fnName);
                const item = new vscode.CompletionItem(fnName, vscode.CompletionItemKind.Function);
                item.detail = `fn ${fnName}(${params})`;
                item.insertText = new vscode.SnippetString(`${fnName}($1)`);
                items.push(item);
            }
        }

        return items;
    }
}

// ============ Hover Provider ============

export class PseudocodeHoverProvider implements vscode.HoverProvider {
    provideHover(
        document: vscode.TextDocument,
        position: vscode.Position
    ): vscode.Hover | undefined {
        const range = document.getWordRangeAtPosition(position);
        if (!range) return undefined;

        const word = document.getText(range);

        // Check if it's a builtin function
        if (BUILTINS[word]) {
            const info = BUILTINS[word];
            const md = new vscode.MarkdownString();
            md.appendCodeblock(info.signature, 'pseudocode');
            md.appendMarkdown('\n\n' + info.description);

            if (info.params && info.params.length > 0) {
                md.appendMarkdown('\n\n**Parameters:**\n');
                for (const p of info.params) {
                    md.appendMarkdown(`- \`${p.name}\`: ${p.description}\n`);
                }
            }

            if (info.returns) {
                md.appendMarkdown(`\n**Returns:** ${info.returns}`);
            }

            if (info.example) {
                md.appendMarkdown('\n\n**Example:**');
                md.appendCodeblock(info.example, 'pseudocode');
            }

            return new vscode.Hover(md, range);
        }

        // Check if it's a keyword
        if (KEYWORDS.includes(word)) {
            const descriptions: Record<string, string> = {
                'let': 'Declares a mutable variable.',
                'const': 'Declares an immutable constant.',
                'fn': 'Declares a function.',
                'return': 'Returns a value from a function.',
                'if': 'Conditional statement.',
                'then': 'Begins the body of an if statement.',
                'elif': 'Else-if branch of a conditional.',
                'else': 'Else branch of a conditional.',
                'end': 'Ends a block (function, if, while, for, match).',
                'while': 'While loop - repeats while condition is true.',
                'for': 'For loop - iterates over a range or array.',
                'in': 'Used in for loops to specify the iterable.',
                'do': 'Begins the body of a loop.',
                'and': 'Logical AND operator.',
                'or': 'Logical OR operator.',
                'not': 'Logical NOT operator.',
                'true': 'Boolean true value.',
                'false': 'Boolean false value.',
                'nil': 'Null/nil value.',
                'match': 'Pattern matching expression.',
                'case': 'Case branch in a match expression.'
            };

            const md = new vscode.MarkdownString();
            md.appendMarkdown(`**${word}** _(keyword)_\n\n`);
            md.appendMarkdown(descriptions[word] || '');
            return new vscode.Hover(md, range);
        }

        // Check if it's a local function definition
        const text = document.getText();
        const fnRegex = new RegExp(`fn\\s+${word}\\s*\\(([^)]*)\\)`, 'g');
        const fnMatch = fnRegex.exec(text);
        if (fnMatch) {
            const params = fnMatch[1];
            const md = new vscode.MarkdownString();
            md.appendCodeblock(`fn ${word}(${params})`, 'pseudocode');
            md.appendMarkdown('\n\n_User-defined function_');
            return new vscode.Hover(md, range);
        }

        return undefined;
    }
}

// ============ Document Symbol Provider ============

export class PseudocodeDocumentSymbolProvider implements vscode.DocumentSymbolProvider {
    provideDocumentSymbols(document: vscode.TextDocument): vscode.DocumentSymbol[] {
        const symbols: vscode.DocumentSymbol[] = [];
        const text = document.getText();

        // Find functions
        const fnRegex = /fn\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)/g;
        let match;
        while ((match = fnRegex.exec(text)) !== null) {
            const name = match[1];
            const params = match[2];
            const startPos = document.positionAt(match.index);

            // Find matching 'end'
            let depth = 1;
            let endPos = startPos;
            for (let i = match.index + match[0].length; i < text.length; i++) {
                const slice = text.slice(i);
                if (slice.match(/^fn\s/) || slice.match(/^if\s/) ||
                    slice.match(/^while\s/) || slice.match(/^for\s/) ||
                    slice.match(/^match\s/)) {
                    depth++;
                } else if (slice.match(/^end\b/)) {
                    depth--;
                    if (depth === 0) {
                        endPos = document.positionAt(i + 3);
                        break;
                    }
                }
            }

            const range = new vscode.Range(startPos, endPos);
            const symbol = new vscode.DocumentSymbol(
                name,
                `(${params})`,
                vscode.SymbolKind.Function,
                range,
                new vscode.Range(startPos, document.positionAt(match.index + match[0].length))
            );
            symbols.push(symbol);
        }

        // Find top-level variables
        const varRegex = /^(?:let|const)\s+([a-zA-Z_][a-zA-Z0-9_]*)/gm;
        while ((match = varRegex.exec(text)) !== null) {
            const name = match[1];
            const pos = document.positionAt(match.index);
            const endPos = document.positionAt(match.index + match[0].length);
            const kind = match[0].startsWith('const')
                ? vscode.SymbolKind.Constant
                : vscode.SymbolKind.Variable;

            const symbol = new vscode.DocumentSymbol(
                name,
                match[0].startsWith('const') ? 'constant' : 'variable',
                kind,
                new vscode.Range(pos, endPos),
                new vscode.Range(pos, endPos)
            );
            symbols.push(symbol);
        }

        return symbols;
    }
}

// ============ Signature Help Provider ============

export class PseudocodeSignatureHelpProvider implements vscode.SignatureHelpProvider {
    provideSignatureHelp(
        document: vscode.TextDocument,
        position: vscode.Position
    ): vscode.SignatureHelp | undefined {
        const lineText = document.lineAt(position).text;
        const textBefore = lineText.substring(0, position.character);

        // Find the function name before the opening paren
        const match = textBefore.match(/([a-zA-Z_][a-zA-Z0-9_]*)\s*\([^)]*$/);
        if (!match) return undefined;

        const fnName = match[1];
        const info = BUILTINS[fnName];
        if (!info) return undefined;

        const sig = new vscode.SignatureInformation(info.signature, info.description);

        if (info.params) {
            for (const p of info.params) {
                sig.parameters.push(new vscode.ParameterInformation(p.name, p.description));
            }
        }

        // Count commas to determine active parameter
        const argsText = textBefore.substring(textBefore.lastIndexOf('(') + 1);
        const commaCount = (argsText.match(/,/g) || []).length;

        const help = new vscode.SignatureHelp();
        help.signatures = [sig];
        help.activeSignature = 0;
        help.activeParameter = Math.min(commaCount, (info.params?.length || 1) - 1);

        return help;
    }
}

// ============ Diagnostics ============

export function createDiagnostics(
    document: vscode.TextDocument,
    collection: vscode.DiagnosticCollection
): void {
    if (document.languageId !== 'pseudocode') {
        return;
    }

    const diagnostics: vscode.Diagnostic[] = [];
    const text = document.getText();
    const lines = text.split('\n');

    // Track block depth for matching
    let blockStack: { type: string; line: number }[] = [];

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        const trimmed = line.trim();

        // Skip comments and empty lines
        if (trimmed.startsWith('//') || trimmed === '') continue;

        // Check for block openers
        if (trimmed.match(/^fn\s+/)) {
            blockStack.push({ type: 'fn', line: i });
        } else if (trimmed.match(/^if\s+/) && trimmed.includes('then')) {
            blockStack.push({ type: 'if', line: i });
        } else if (trimmed.match(/^while\s+/)) {
            blockStack.push({ type: 'while', line: i });
        } else if (trimmed.match(/^for\s+/)) {
            blockStack.push({ type: 'for', line: i });
        } else if (trimmed.match(/^match\s+/)) {
            blockStack.push({ type: 'match', line: i });
        }

        // Check for 'end'
        if (trimmed === 'end' || trimmed.startsWith('end ') || trimmed.match(/^end\s*$/)) {
            if (blockStack.length === 0) {
                diagnostics.push(new vscode.Diagnostic(
                    new vscode.Range(i, 0, i, line.length),
                    "Unexpected 'end' without matching block opener",
                    vscode.DiagnosticSeverity.Error
                ));
            } else {
                blockStack.pop();
            }
        }

        // Check for common mistakes

        // Missing 'then' after 'if'
        if (trimmed.match(/^if\s+/) && !trimmed.includes('then')) {
            diagnostics.push(new vscode.Diagnostic(
                new vscode.Range(i, 0, i, line.length),
                "Missing 'then' after 'if' condition",
                vscode.DiagnosticSeverity.Error
            ));
        }

        // Missing 'do' after 'for ... in ...'
        if (trimmed.match(/^for\s+/) && trimmed.includes(' in ') && !trimmed.includes(' do')) {
            diagnostics.push(new vscode.Diagnostic(
                new vscode.Range(i, 0, i, line.length),
                "Missing 'do' after 'for ... in ...'",
                vscode.DiagnosticSeverity.Error
            ));
        }

        // Using = instead of == for comparison
        const eqMatch = trimmed.match(/if\s+.*[^=!<>]=[^=]/);
        if (eqMatch && !trimmed.includes('==') && !trimmed.includes('!=') &&
            !trimmed.includes('<=') && !trimmed.includes('>=')) {
            diagnostics.push(new vscode.Diagnostic(
                new vscode.Range(i, 0, i, line.length),
                "Did you mean '==' for comparison? '=' is for assignment.",
                vscode.DiagnosticSeverity.Warning
            ));
        }

        // Check for undefined function calls (common typos)
        const callMatch = trimmed.match(/([a-zA-Z_][a-zA-Z0-9_]*)\s*\(/g);
        if (callMatch) {
            for (const call of callMatch) {
                const fnName = call.replace(/\s*\($/, '');
                // Check if it's a known builtin or keyword
                if (!BUILTINS[fnName] && !['fn', 'if', 'while', 'for'].includes(fnName)) {
                    // Check if it's defined in the document
                    const fnDefRegex = new RegExp(`fn\\s+${fnName}\\s*\\(`);
                    if (!fnDefRegex.test(text)) {
                        // Could be a warning, but might be a global or imported function
                        // Only warn for very common typos
                        const typos: Record<string, string> = {
                            'pritn': 'print',
                            'pirnt': 'print',
                            'prnt': 'print',
                            'lenght': 'len',
                            'legth': 'len'
                        };
                        if (typos[fnName]) {
                            diagnostics.push(new vscode.Diagnostic(
                                new vscode.Range(i, line.indexOf(fnName), i, line.indexOf(fnName) + fnName.length),
                                `Did you mean '${typos[fnName]}'?`,
                                vscode.DiagnosticSeverity.Error
                            ));
                        }
                    }
                }
            }
        }
    }

    // Check for unclosed blocks at end of file
    for (const block of blockStack) {
        diagnostics.push(new vscode.Diagnostic(
            new vscode.Range(block.line, 0, block.line, lines[block.line].length),
            `Unclosed '${block.type}' block - missing 'end'`,
            vscode.DiagnosticSeverity.Error
        ));
    }

    collection.set(document.uri, diagnostics);
}

// ============ Formatter ============

export class PseudocodeDocumentFormatter implements vscode.DocumentFormattingEditProvider {
    provideDocumentFormattingEdits(document: vscode.TextDocument): vscode.TextEdit[] {
        const edits: vscode.TextEdit[] = [];
        const text = document.getText();
        const lines = text.split('\n');

        let indentLevel = 0;
        const indentStr = '    '; // 4 spaces

        for (let i = 0; i < lines.length; i++) {
            const line = lines[i];
            const trimmed = line.trim();

            if (trimmed === '') continue;

            // Decrease indent for 'end', 'else', 'elif', 'case'
            if (trimmed.match(/^(end|else|elif|case)\b/)) {
                indentLevel = Math.max(0, indentLevel - 1);
            }

            const expectedIndent = indentStr.repeat(indentLevel);
            const currentIndent = line.match(/^\s*/)?.[0] || '';

            if (currentIndent !== expectedIndent) {
                edits.push(vscode.TextEdit.replace(
                    new vscode.Range(i, 0, i, currentIndent.length),
                    expectedIndent
                ));
            }

            // Increase indent after block openers
            if (trimmed.match(/^fn\s+/) ||
                (trimmed.match(/^if\s+/) && trimmed.includes('then')) ||
                trimmed.match(/^while\s+.*\s+do\s*$/) ||
                trimmed.match(/^for\s+.*\s+do\s*$/) ||
                trimmed.match(/^match\s+/) ||
                trimmed.match(/^else\s*$/) ||
                trimmed.match(/^elif\s+.*\s+then\s*$/) ||
                trimmed.match(/^case\s+/)) {
                indentLevel++;
            }

            // 'end' already decreased, so re-increase for next line... wait no
            // Actually 'else' and 'elif' should be at same level as 'if', then increase
            if (trimmed.match(/^(else|elif)\b/)) {
                indentLevel++;
            }
        }

        return edits;
    }
}
