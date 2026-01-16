"use strict";
/*
 * Pseudocode WASM Standard Library - String Functions
 */
Object.defineProperty(exports, "__esModule", { value: true });
exports.createStringFunctions = createStringFunctions;
const values_1 = require("../runtime/values");
/**
 * Create string functions for the runtime.
 */
function createStringFunctions(memory) {
    return {
        /**
         * Get string length.
         */
        len(s) {
            if (!(0, values_1.isString)(s)) {
                throw new Error('len() requires a string');
            }
            const str = memory.getString((0, values_1.asPointer)(s));
            return (0, values_1.valInt)(str.length);
        },
        /**
         * Convert to uppercase.
         */
        upper(s) {
            if (!(0, values_1.isString)(s))
                throw new Error('upper() requires a string');
            const str = memory.getString((0, values_1.asPointer)(s));
            return memory.allocString(str.toUpperCase());
        },
        /**
         * Convert to lowercase.
         */
        lower(s) {
            if (!(0, values_1.isString)(s))
                throw new Error('lower() requires a string');
            const str = memory.getString((0, values_1.asPointer)(s));
            return memory.allocString(str.toLowerCase());
        },
        /**
         * Trim whitespace.
         */
        trim(s) {
            if (!(0, values_1.isString)(s))
                throw new Error('trim() requires a string');
            const str = memory.getString((0, values_1.asPointer)(s));
            return memory.allocString(str.trim());
        },
        /**
         * Trim left whitespace.
         */
        ltrim(s) {
            if (!(0, values_1.isString)(s))
                throw new Error('ltrim() requires a string');
            const str = memory.getString((0, values_1.asPointer)(s));
            return memory.allocString(str.trimStart());
        },
        /**
         * Trim right whitespace.
         */
        rtrim(s) {
            if (!(0, values_1.isString)(s))
                throw new Error('rtrim() requires a string');
            const str = memory.getString((0, values_1.asPointer)(s));
            return memory.allocString(str.trimEnd());
        },
        /**
         * Get substring.
         */
        substr(s, start, length) {
            if (!(0, values_1.isString)(s))
                throw new Error('substr() requires a string');
            const str = memory.getString((0, values_1.asPointer)(s));
            const startIdx = Math.trunc((0, values_1.asNumber)(start));
            const len = length ? Math.trunc((0, values_1.asNumber)(length)) : str.length - startIdx;
            return memory.allocString(str.substr(startIdx, len));
        },
        /**
         * Split string by delimiter.
         */
        split(s, delimiter) {
            if (!(0, values_1.isString)(s))
                throw new Error('split() requires a string');
            const str = memory.getString((0, values_1.asPointer)(s));
            const delim = (0, values_1.isString)(delimiter) ? memory.getString((0, values_1.asPointer)(delimiter)) : String(delimiter);
            const parts = str.split(delim);
            // Create array
            const arrPtr = memory.allocArray(parts.length);
            for (const part of parts) {
                memory.arrayPush(arrPtr, memory.allocString(part));
            }
            // Return array value
            return (BigInt(arrPtr) << 3n) | 5n; // TAG_ARRAY = 5
        },
        /**
         * Join array with delimiter.
         */
        join(arr, delimiter) {
            const arrPtr = Number(arr >> 3n);
            const delim = (0, values_1.isString)(delimiter) ? memory.getString((0, values_1.asPointer)(delimiter)) : String(delimiter);
            const count = memory.arrayCount(arrPtr);
            const parts = [];
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(arrPtr, i);
                if ((0, values_1.isString)(elem)) {
                    parts.push(memory.getString((0, values_1.asPointer)(elem)));
                }
                else {
                    parts.push(String(elem));
                }
            }
            return memory.allocString(parts.join(delim));
        },
        /**
         * Check if string contains substring.
         */
        contains(s, sub) {
            if (!(0, values_1.isString)(s) || !(0, values_1.isString)(sub)) {
                throw new Error('contains() requires strings');
            }
            const str = memory.getString((0, values_1.asPointer)(s));
            const substr = memory.getString((0, values_1.asPointer)(sub));
            return (0, values_1.valBool)(str.includes(substr));
        },
        /**
         * Check if string starts with prefix.
         */
        starts_with(s, prefix) {
            if (!(0, values_1.isString)(s) || !(0, values_1.isString)(prefix)) {
                throw new Error('starts_with() requires strings');
            }
            const str = memory.getString((0, values_1.asPointer)(s));
            const pre = memory.getString((0, values_1.asPointer)(prefix));
            return (0, values_1.valBool)(str.startsWith(pre));
        },
        /**
         * Check if string ends with suffix.
         */
        ends_with(s, suffix) {
            if (!(0, values_1.isString)(s) || !(0, values_1.isString)(suffix)) {
                throw new Error('ends_with() requires strings');
            }
            const str = memory.getString((0, values_1.asPointer)(s));
            const suf = memory.getString((0, values_1.asPointer)(suffix));
            return (0, values_1.valBool)(str.endsWith(suf));
        },
        /**
         * Find index of substring.
         */
        index_of(s, sub) {
            if (!(0, values_1.isString)(s) || !(0, values_1.isString)(sub)) {
                throw new Error('index_of() requires strings');
            }
            const str = memory.getString((0, values_1.asPointer)(s));
            const substr = memory.getString((0, values_1.asPointer)(sub));
            return (0, values_1.valInt)(str.indexOf(substr));
        },
        /**
         * Replace occurrences.
         */
        replace(s, old, newStr) {
            if (!(0, values_1.isString)(s) || !(0, values_1.isString)(old) || !(0, values_1.isString)(newStr)) {
                throw new Error('replace() requires strings');
            }
            const str = memory.getString((0, values_1.asPointer)(s));
            const oldS = memory.getString((0, values_1.asPointer)(old));
            const newS = memory.getString((0, values_1.asPointer)(newStr));
            return memory.allocString(str.split(oldS).join(newS));
        },
        /**
         * Repeat string.
         */
        repeat(s, count) {
            if (!(0, values_1.isString)(s))
                throw new Error('repeat() requires a string');
            const str = memory.getString((0, values_1.asPointer)(s));
            const n = Math.trunc((0, values_1.asNumber)(count));
            return memory.allocString(str.repeat(n));
        },
        /**
         * Reverse string.
         */
        reverse(s) {
            if (!(0, values_1.isString)(s))
                throw new Error('reverse() requires a string');
            const str = memory.getString((0, values_1.asPointer)(s));
            return memory.allocString([...str].reverse().join(''));
        },
        /**
         * Get character at index.
         */
        char_at(s, index) {
            if (!(0, values_1.isString)(s))
                throw new Error('char_at() requires a string');
            const str = memory.getString((0, values_1.asPointer)(s));
            const idx = Math.trunc((0, values_1.asNumber)(index));
            if (idx < 0 || idx >= str.length) {
                return (0, values_1.valNil)();
            }
            return memory.allocString(str[idx]);
        },
        /**
         * Get character code at index.
         */
        char_code(s, index) {
            if (!(0, values_1.isString)(s))
                throw new Error('char_code() requires a string');
            const str = memory.getString((0, values_1.asPointer)(s));
            const idx = Math.trunc((0, values_1.asNumber)(index));
            if (idx < 0 || idx >= str.length) {
                return (0, values_1.valInt)(-1);
            }
            return (0, values_1.valInt)(str.charCodeAt(idx));
        },
        /**
         * Create string from character code.
         */
        from_char_code(code) {
            const c = Math.trunc((0, values_1.asNumber)(code));
            return memory.allocString(String.fromCharCode(c));
        },
    };
}
//# sourceMappingURL=string.js.map