/*
 * Pseudocode Language - Built-in Functions and Keywords
 * 
 * Centralized definitions for syntax highlighting, completion, and documentation.
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

export interface BuiltinInfo {
    signature: string;
    description: string;
    params?: { name: string; description: string }[];
    returns?: string;
    example?: string;
}

// ============ Keywords ============

export const KEYWORDS = [
    'let', 'const', 'fn', 'return', 'if', 'then', 'elif', 'else', 'end',
    'while', 'for', 'in', 'to', 'do', 'and', 'or', 'not', 'true', 'false', 'nil',
    'match', 'case', 'class', 'extends', 'self', 'super', 'static',
    'async', 'await', 'yield', 'module', 'export', 'import', 'from', 'as', 'enum'
];

// ============ Built-in Function Signatures (for inlay hints) ============

export const BUILTIN_SIGNATURES: Record<string, string[]> = {
    // I/O
    'print': ['value', '...args'],
    'input': ['prompt?'],
    'read_file': ['path'],
    'write_file': ['path', 'content'],
    'append_file': ['path', 'content'],
    'file_exists': ['path'],

    // Math
    'abs': ['x'],
    'sqrt': ['x'],
    'pow': ['base', 'exponent'],
    'floor': ['x'],
    'ceil': ['x'],
    'round': ['x'],
    'min': ['a', 'b'],
    'max': ['a', 'b'],
    'sin': ['radians'],
    'cos': ['radians'],
    'tan': ['radians'],
    'asin': ['x'],
    'acos': ['x'],
    'atan': ['x'],
    'atan2': ['y', 'x'],
    'log': ['x'],
    'log10': ['x'],
    'log2': ['x'],
    'exp': ['x'],
    'hypot': ['x', 'y'],
    'random': [],
    'randint': ['min', 'max'],

    // String
    'len': ['value'],
    'str': ['value'],
    'upper': ['string'],
    'lower': ['string'],
    'trim': ['string'],
    'split': ['string', 'delimiter'],
    'join': ['array', 'separator'],
    'replace': ['string', 'old', 'new'],
    'contains': ['string', 'substring'],
    'starts_with': ['string', 'prefix'],
    'ends_with': ['string', 'suffix'],
    'substr': ['string', 'start', 'length?'],
    'char_at': ['string', 'index'],
    'index_of': ['string', 'substring'],
    'ord': ['char'],
    'chr': ['code'],

    // Array
    'push': ['array', 'value'],
    'pop': ['array'],
    'shift': ['array'],
    'unshift': ['array', 'value'],
    'slice': ['array', 'start', 'end?'],
    'concat': ['array1', 'array2'],
    'sort': ['array', 'compareFn?'],
    'reverse': ['array'],
    'range': ['start', 'end', 'step?'],
    'map': ['array', 'fn'],
    'filter': ['array', 'predicate'],
    'reduce': ['array', 'fn', 'initial'],
    'find': ['array', 'predicate'],
    'every': ['array', 'predicate'],
    'some': ['array', 'predicate'],
    'flat': ['array', 'depth?'],
    'unique': ['array'],
    'zip': ['array1', 'array2'],

    // Dict
    'dict': [],
    'dict_get': ['dict', 'key', 'default?'],
    'dict_set': ['dict', 'key', 'value'],
    'dict_has': ['dict', 'key'],
    'dict_keys': ['dict'],
    'dict_values': ['dict'],
    'dict_delete': ['dict', 'key'],
    'dict_merge': ['dict1', 'dict2'],
    'keys': ['dict'],
    'values': ['dict'],
    'has': ['dict', 'key'],
    'has_key': ['dict', 'key'],

    // Type
    'int': ['value'],
    'float': ['value'],
    'type': ['value'],
    'is_nil': ['value'],
    'is_string': ['value'],
    'is_number': ['value'],
    'is_int': ['value'],
    'is_float': ['value'],
    'is_array': ['value'],
    'is_dict': ['value'],
    'is_fn': ['value'],
    'is_function': ['value'],

    // Bitwise
    'bit_and': ['a', 'b'],
    'bit_or': ['a', 'b'],
    'bit_xor': ['a', 'b'],
    'bit_not': ['a'],
    'bit_lshift': ['value', 'shift'],
    'bit_rshift': ['value', 'shift'],
    'popcount': ['value'],
    'clz': ['value'],
    'ctz': ['value'],

    // HTTP
    'http_get': ['url', 'headers?'],
    'http_post': ['url', 'body', 'headers?'],
    'http_put': ['url', 'body', 'headers?'],
    'http_delete': ['url', 'headers?'],

    // JSON
    'json_parse': ['string'],
    'json_stringify': ['value', 'indent?'],

    // System
    'clock': [],
    'time': [],
    'time_ms': [],
    'sleep': ['milliseconds'],
    'exec': ['command'],
    'env': ['name'],
    'set_env': ['name', 'value'],
    'args': [],
    'exit': ['code?'],
    'assert': ['condition', 'message?'],
    'error': ['message'],

    // Encoding
    'encode_base64': ['data'],
    'decode_base64': ['string'],
    'base64_encode': ['data'],
    'base64_decode': ['string'],
    'encode_utf8': ['string'],
    'decode_utf8': ['bytes'],
    'md5': ['data'],
    'sha256': ['data'],
    'hash': ['value'],

    // Vector/Math
    'vec_add': ['a', 'b'],
    'vec_sub': ['a', 'b'],
    'vec_mul': ['a', 'scalar'],
    'vec_div': ['a', 'scalar'],
    'vec_dot': ['a', 'b'],
    'vec_sum': ['vector'],
    'vec_prod': ['vector'],
    'vec_min': ['vector'],
    'vec_max': ['vector'],
    'vec_mean': ['vector'],
};

// ============ Built-in Functions with Documentation ============

export const BUILTINS: Record<string, BuiltinInfo> = {
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
    'append_file': {
        signature: 'append_file(path, content) -> bool',
        description: 'Appends content to a file.',
        params: [
            { name: 'path', description: 'Path to the file' },
            { name: 'content', description: 'Content to append' }
        ],
        returns: 'true on success'
    },
    'file_exists': {
        signature: 'file_exists(path) -> bool',
        description: 'Checks if a file exists.',
        params: [{ name: 'path', description: 'Path to check' }],
        returns: 'true if file exists'
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
    'asin': {
        signature: 'asin(x) -> number',
        description: 'Returns the arcsine of x in radians.',
        params: [{ name: 'x', description: 'Value between -1 and 1' }],
        returns: 'Arcsine in radians'
    },
    'acos': {
        signature: 'acos(x) -> number',
        description: 'Returns the arccosine of x in radians.',
        params: [{ name: 'x', description: 'Value between -1 and 1' }],
        returns: 'Arccosine in radians'
    },
    'atan': {
        signature: 'atan(x) -> number',
        description: 'Returns the arctangent of x in radians.',
        params: [{ name: 'x', description: 'A number' }],
        returns: 'Arctangent in radians'
    },
    'atan2': {
        signature: 'atan2(y, x) -> number',
        description: 'Returns the angle from the x-axis to the point (x, y).',
        params: [
            { name: 'y', description: 'Y coordinate' },
            { name: 'x', description: 'X coordinate' }
        ],
        returns: 'Angle in radians'
    },
    'log': {
        signature: 'log(x) -> number',
        description: 'Returns the natural logarithm of x.',
        params: [{ name: 'x', description: 'A positive number' }],
        returns: 'Natural log of x'
    },
    'log10': {
        signature: 'log10(x) -> number',
        description: 'Returns the base-10 logarithm of x.',
        params: [{ name: 'x', description: 'A positive number' }],
        returns: 'Base-10 log of x'
    },
    'log2': {
        signature: 'log2(x) -> number',
        description: 'Returns the base-2 logarithm of x.',
        params: [{ name: 'x', description: 'A positive number' }],
        returns: 'Base-2 log of x'
    },
    'exp': {
        signature: 'exp(x) -> number',
        description: 'Returns e raised to the power x.',
        params: [{ name: 'x', description: 'A number' }],
        returns: 'e^x'
    },
    'hypot': {
        signature: 'hypot(x, y) -> number',
        description: 'Returns the Euclidean distance sqrt(x² + y²).',
        params: [
            { name: 'x', description: 'First value' },
            { name: 'y', description: 'Second value' }
        ],
        returns: 'Euclidean distance'
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
    'ord': {
        signature: 'ord(char) -> int',
        description: 'Returns the Unicode code point of a character.',
        params: [{ name: 'char', description: 'Single character' }],
        returns: 'Unicode code point'
    },
    'chr': {
        signature: 'chr(code) -> string',
        description: 'Returns the character for a Unicode code point.',
        params: [{ name: 'code', description: 'Unicode code point' }],
        returns: 'Single character string'
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
        example: 'map([1, 2, 3], fn(x) return x * 2 end)'
    },
    'filter': {
        signature: 'filter(array, fn) -> array',
        description: 'Filters elements by predicate.',
        params: [
            { name: 'array', description: 'Source array' },
            { name: 'fn', description: 'Predicate function' }
        ],
        returns: 'Filtered array',
        example: 'filter([1, 2, 3, 4], fn(x) return x > 2 end)'
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
        example: 'reduce([1, 2, 3], fn(a, b) return a + b end, 0)'
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
    'unique': {
        signature: 'unique(array) -> array',
        description: 'Returns array with duplicate elements removed.',
        params: [{ name: 'array', description: 'Source array' }],
        returns: 'Array with unique elements'
    },
    'flat': {
        signature: 'flat(array, depth?) -> array',
        description: 'Flattens nested arrays.',
        params: [
            { name: 'array', description: 'Nested array' },
            { name: 'depth', description: 'Depth to flatten (default 1)' }
        ],
        returns: 'Flattened array'
    },
    'zip': {
        signature: 'zip(a, b) -> array',
        description: 'Combines two arrays into array of pairs.',
        params: [
            { name: 'a', description: 'First array' },
            { name: 'b', description: 'Second array' }
        ],
        returns: 'Array of [a[i], b[i]] pairs'
    },

    // Dictionary Functions
    'keys': {
        signature: 'keys(dict) -> array',
        description: 'Returns all keys of a dictionary.',
        params: [{ name: 'dict', description: 'A dictionary' }],
        returns: 'Array of keys'
    },
    'values': {
        signature: 'values(dict) -> array',
        description: 'Returns all values of a dictionary.',
        params: [{ name: 'dict', description: 'A dictionary' }],
        returns: 'Array of values'
    },
    'has_key': {
        signature: 'has_key(dict, key) -> bool',
        description: 'Checks if dictionary has a key.',
        params: [
            { name: 'dict', description: 'A dictionary' },
            { name: 'key', description: 'Key to check' }
        ],
        returns: 'true if key exists'
    },
    'dict_get': {
        signature: 'dict_get(dict, key, default?) -> value',
        description: 'Gets value from dictionary with optional default.',
        params: [
            { name: 'dict', description: 'A dictionary' },
            { name: 'key', description: 'Key to get' },
            { name: 'default', description: 'Default if key not found' }
        ],
        returns: 'Value or default'
    },

    // Type Functions
    'type': {
        signature: 'type(value) -> string',
        description: 'Returns the type of a value.',
        params: [{ name: 'value', description: 'Any value' }],
        returns: '"number", "string", "bool", "array", "dict", "function", "nil"'
    },
    'is_nil': {
        signature: 'is_nil(value) -> bool',
        description: 'Checks if value is nil.',
        params: [{ name: 'value', description: 'Any value' }],
        returns: 'true if nil'
    },
    'is_number': {
        signature: 'is_number(value) -> bool',
        description: 'Checks if value is a number.',
        params: [{ name: 'value', description: 'Any value' }],
        returns: 'true if number'
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
    'is_dict': {
        signature: 'is_dict(value) -> bool',
        description: 'Checks if value is a dictionary.',
        params: [{ name: 'value', description: 'Any value' }],
        returns: 'true if dict'
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
    'clock': {
        signature: 'clock() -> number',
        description: 'Returns high-resolution timer value.',
        returns: 'Time in seconds'
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
    'exec': {
        signature: 'exec(command) -> string',
        description: 'Executes a shell command and returns output.',
        params: [{ name: 'command', description: 'Command to execute' }],
        returns: 'Command output'
    },
    'env': {
        signature: 'env(name) -> string',
        description: 'Gets an environment variable.',
        params: [{ name: 'name', description: 'Variable name' }],
        returns: 'Variable value or nil'
    },
    'args': {
        signature: 'args() -> array',
        description: 'Returns command-line arguments.',
        returns: 'Array of argument strings'
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
    'http_put': {
        signature: 'http_put(url, body) -> string',
        description: 'Makes HTTP PUT request.',
        params: [
            { name: 'url', description: 'URL to put to' },
            { name: 'body', description: 'Request body' }
        ],
        returns: 'Response body'
    },
    'http_delete': {
        signature: 'http_delete(url) -> string',
        description: 'Makes HTTP DELETE request.',
        params: [{ name: 'url', description: 'URL to delete' }],
        returns: 'Response body'
    },

    // Crypto/Encoding Functions
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
    'encode_base64': {
        signature: 'encode_base64(s) -> string',
        description: 'Encodes string to Base64.',
        params: [{ name: 's', description: 'String to encode' }],
        returns: 'Base64 encoded string'
    },
    'decode_base64': {
        signature: 'decode_base64(s) -> string',
        description: 'Decodes Base64 string.',
        params: [{ name: 's', description: 'Base64 string' }],
        returns: 'Decoded string'
    },

    // Bitwise Functions
    'bit_and': {
        signature: 'bit_and(a, b) -> int',
        description: 'Bitwise AND operation.',
        params: [
            { name: 'a', description: 'First integer' },
            { name: 'b', description: 'Second integer' }
        ],
        returns: 'a AND b'
    },
    'bit_or': {
        signature: 'bit_or(a, b) -> int',
        description: 'Bitwise OR operation.',
        params: [
            { name: 'a', description: 'First integer' },
            { name: 'b', description: 'Second integer' }
        ],
        returns: 'a OR b'
    },
    'bit_xor': {
        signature: 'bit_xor(a, b) -> int',
        description: 'Bitwise XOR operation.',
        params: [
            { name: 'a', description: 'First integer' },
            { name: 'b', description: 'Second integer' }
        ],
        returns: 'a XOR b'
    },
    'bit_not': {
        signature: 'bit_not(a) -> int',
        description: 'Bitwise NOT operation.',
        params: [{ name: 'a', description: 'An integer' }],
        returns: 'NOT a'
    },
    'bit_lshift': {
        signature: 'bit_lshift(value, shift) -> int',
        description: 'Left shift operation.',
        params: [
            { name: 'value', description: 'Value to shift' },
            { name: 'shift', description: 'Number of bits' }
        ],
        returns: 'value << shift'
    },
    'bit_rshift': {
        signature: 'bit_rshift(value, shift) -> int',
        description: 'Right shift operation.',
        params: [
            { name: 'value', description: 'Value to shift' },
            { name: 'shift', description: 'Number of bits' }
        ],
        returns: 'value >> shift'
    }
};
