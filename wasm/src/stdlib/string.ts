/*
 * Pseudocode WASM Standard Library - String Functions
 */

import { Value, valString, valInt, valBool, valNil, isString, asPointer, asNumber } from '../runtime/values';
import { Memory } from '../runtime/memory';

/**
 * Create string functions for the runtime.
 */
export function createStringFunctions(memory: Memory) {
    return {
        /**
         * Get string length.
         */
        len(s: Value): Value {
            if (!isString(s)) {
                throw new Error('len() requires a string');
            }
            const str = memory.getString(asPointer(s));
            return valInt(str.length);
        },
        
        /**
         * Convert to uppercase.
         */
        upper(s: Value): Value {
            if (!isString(s)) throw new Error('upper() requires a string');
            const str = memory.getString(asPointer(s));
            return memory.allocString(str.toUpperCase());
        },
        
        /**
         * Convert to lowercase.
         */
        lower(s: Value): Value {
            if (!isString(s)) throw new Error('lower() requires a string');
            const str = memory.getString(asPointer(s));
            return memory.allocString(str.toLowerCase());
        },
        
        /**
         * Trim whitespace.
         */
        trim(s: Value): Value {
            if (!isString(s)) throw new Error('trim() requires a string');
            const str = memory.getString(asPointer(s));
            return memory.allocString(str.trim());
        },
        
        /**
         * Trim left whitespace.
         */
        ltrim(s: Value): Value {
            if (!isString(s)) throw new Error('ltrim() requires a string');
            const str = memory.getString(asPointer(s));
            return memory.allocString(str.trimStart());
        },
        
        /**
         * Trim right whitespace.
         */
        rtrim(s: Value): Value {
            if (!isString(s)) throw new Error('rtrim() requires a string');
            const str = memory.getString(asPointer(s));
            return memory.allocString(str.trimEnd());
        },
        
        /**
         * Get substring.
         */
        substr(s: Value, start: Value, length?: Value): Value {
            if (!isString(s)) throw new Error('substr() requires a string');
            const str = memory.getString(asPointer(s));
            const startIdx = Math.trunc(asNumber(start));
            const len = length ? Math.trunc(asNumber(length)) : str.length - startIdx;
            return memory.allocString(str.substr(startIdx, len));
        },
        
        /**
         * Split string by delimiter.
         */
        split(s: Value, delimiter: Value): Value {
            if (!isString(s)) throw new Error('split() requires a string');
            const str = memory.getString(asPointer(s));
            const delim = isString(delimiter) ? memory.getString(asPointer(delimiter)) : String(delimiter);
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
        join(arr: Value, delimiter: Value): Value {
            const arrPtr = Number(arr >> 3n);
            const delim = isString(delimiter) ? memory.getString(asPointer(delimiter)) : String(delimiter);
            
            const count = memory.arrayCount(arrPtr);
            const parts: string[] = [];
            
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(arrPtr, i);
                if (isString(elem)) {
                    parts.push(memory.getString(asPointer(elem)));
                } else {
                    parts.push(String(elem));
                }
            }
            
            return memory.allocString(parts.join(delim));
        },
        
        /**
         * Check if string contains substring.
         */
        contains(s: Value, sub: Value): Value {
            if (!isString(s) || !isString(sub)) {
                throw new Error('contains() requires strings');
            }
            const str = memory.getString(asPointer(s));
            const substr = memory.getString(asPointer(sub));
            return valBool(str.includes(substr));
        },
        
        /**
         * Check if string starts with prefix.
         */
        starts_with(s: Value, prefix: Value): Value {
            if (!isString(s) || !isString(prefix)) {
                throw new Error('starts_with() requires strings');
            }
            const str = memory.getString(asPointer(s));
            const pre = memory.getString(asPointer(prefix));
            return valBool(str.startsWith(pre));
        },
        
        /**
         * Check if string ends with suffix.
         */
        ends_with(s: Value, suffix: Value): Value {
            if (!isString(s) || !isString(suffix)) {
                throw new Error('ends_with() requires strings');
            }
            const str = memory.getString(asPointer(s));
            const suf = memory.getString(asPointer(suffix));
            return valBool(str.endsWith(suf));
        },
        
        /**
         * Find index of substring.
         */
        index_of(s: Value, sub: Value): Value {
            if (!isString(s) || !isString(sub)) {
                throw new Error('index_of() requires strings');
            }
            const str = memory.getString(asPointer(s));
            const substr = memory.getString(asPointer(sub));
            return valInt(str.indexOf(substr));
        },
        
        /**
         * Replace occurrences.
         */
        replace(s: Value, old: Value, newStr: Value): Value {
            if (!isString(s) || !isString(old) || !isString(newStr)) {
                throw new Error('replace() requires strings');
            }
            const str = memory.getString(asPointer(s));
            const oldS = memory.getString(asPointer(old));
            const newS = memory.getString(asPointer(newStr));
            return memory.allocString(str.split(oldS).join(newS));
        },
        
        /**
         * Repeat string.
         */
        repeat(s: Value, count: Value): Value {
            if (!isString(s)) throw new Error('repeat() requires a string');
            const str = memory.getString(asPointer(s));
            const n = Math.trunc(asNumber(count));
            return memory.allocString(str.repeat(n));
        },
        
        /**
         * Reverse string.
         */
        reverse(s: Value): Value {
            if (!isString(s)) throw new Error('reverse() requires a string');
            const str = memory.getString(asPointer(s));
            return memory.allocString([...str].reverse().join(''));
        },
        
        /**
         * Get character at index.
         */
        char_at(s: Value, index: Value): Value {
            if (!isString(s)) throw new Error('char_at() requires a string');
            const str = memory.getString(asPointer(s));
            const idx = Math.trunc(asNumber(index));
            if (idx < 0 || idx >= str.length) {
                return valNil();
            }
            return memory.allocString(str[idx]);
        },
        
        /**
         * Get character code at index.
         */
        char_code(s: Value, index: Value): Value {
            if (!isString(s)) throw new Error('char_code() requires a string');
            const str = memory.getString(asPointer(s));
            const idx = Math.trunc(asNumber(index));
            if (idx < 0 || idx >= str.length) {
                return valInt(-1);
            }
            return valInt(str.charCodeAt(idx));
        },
        
        /**
         * Create string from character code.
         */
        from_char_code(code: Value): Value {
            const c = Math.trunc(asNumber(code));
            return memory.allocString(String.fromCharCode(c));
        },
    };
}
