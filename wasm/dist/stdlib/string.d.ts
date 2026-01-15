import { Value } from '../runtime/values';
import { Memory } from '../runtime/memory';
/**
 * Create string functions for the runtime.
 */
export declare function createStringFunctions(memory: Memory): {
    /**
     * Get string length.
     */
    len(s: Value): Value;
    /**
     * Convert to uppercase.
     */
    upper(s: Value): Value;
    /**
     * Convert to lowercase.
     */
    lower(s: Value): Value;
    /**
     * Trim whitespace.
     */
    trim(s: Value): Value;
    /**
     * Trim left whitespace.
     */
    ltrim(s: Value): Value;
    /**
     * Trim right whitespace.
     */
    rtrim(s: Value): Value;
    /**
     * Get substring.
     */
    substr(s: Value, start: Value, length?: Value): Value;
    /**
     * Split string by delimiter.
     */
    split(s: Value, delimiter: Value): Value;
    /**
     * Join array with delimiter.
     */
    join(arr: Value, delimiter: Value): Value;
    /**
     * Check if string contains substring.
     */
    contains(s: Value, sub: Value): Value;
    /**
     * Check if string starts with prefix.
     */
    starts_with(s: Value, prefix: Value): Value;
    /**
     * Check if string ends with suffix.
     */
    ends_with(s: Value, suffix: Value): Value;
    /**
     * Find index of substring.
     */
    index_of(s: Value, sub: Value): Value;
    /**
     * Replace occurrences.
     */
    replace(s: Value, old: Value, newStr: Value): Value;
    /**
     * Repeat string.
     */
    repeat(s: Value, count: Value): Value;
    /**
     * Reverse string.
     */
    reverse(s: Value): Value;
    /**
     * Get character at index.
     */
    char_at(s: Value, index: Value): Value;
    /**
     * Get character code at index.
     */
    char_code(s: Value, index: Value): Value;
    /**
     * Create string from character code.
     */
    from_char_code(code: Value): Value;
};
//# sourceMappingURL=string.d.ts.map