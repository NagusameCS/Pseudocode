"use strict";
/*
 * Pseudocode WASM Standard Library - Dictionary Functions
 */
Object.defineProperty(exports, "__esModule", { value: true });
exports.createDictFunctions = createDictFunctions;
const values_1 = require("../runtime/values");
/**
 * Create dictionary functions for the runtime.
 */
function createDictFunctions(memory, callFunction) {
    return {
        /**
         * Create a new empty dictionary.
         */
        dict_new() {
            const ptr = memory.allocDict();
            return (0, values_1.valObject)(ptr);
        },
        /**
         * Get a value from dictionary by key.
         */
        dict_get(dict, key, defaultValue) {
            if (!(0, values_1.isObject)(dict)) {
                throw new Error('dict_get() requires a dictionary');
            }
            const ptr = (0, values_1.asPointer)(dict);
            const keyStr = (0, values_1.isString)(key) ? memory.getString((0, values_1.asPointer)(key)) : (0, values_1.valToString)(key, memory);
            const value = memory.dictGet(ptr, keyStr);
            if (value === undefined) {
                return defaultValue !== undefined ? defaultValue : (0, values_1.valNil)();
            }
            return value;
        },
        /**
         * Set a value in dictionary.
         */
        dict_set(dict, key, value) {
            if (!(0, values_1.isObject)(dict)) {
                throw new Error('dict_set() requires a dictionary');
            }
            const ptr = (0, values_1.asPointer)(dict);
            const keyStr = (0, values_1.isString)(key) ? memory.getString((0, values_1.asPointer)(key)) : (0, values_1.valToString)(key, memory);
            memory.dictSet(ptr, keyStr, value);
            return (0, values_1.valNil)();
        },
        /**
         * Delete a key from dictionary.
         */
        dict_delete(dict, key) {
            if (!(0, values_1.isObject)(dict)) {
                throw new Error('dict_delete() requires a dictionary');
            }
            const ptr = (0, values_1.asPointer)(dict);
            const keyStr = (0, values_1.isString)(key) ? memory.getString((0, values_1.asPointer)(key)) : (0, values_1.valToString)(key, memory);
            const existed = memory.dictDelete(ptr, keyStr);
            return (0, values_1.valBool)(existed);
        },
        /**
         * Check if dictionary has a key.
         */
        has_key(dict, key) {
            if (!(0, values_1.isObject)(dict)) {
                throw new Error('has_key() requires a dictionary');
            }
            const ptr = (0, values_1.asPointer)(dict);
            const keyStr = (0, values_1.isString)(key) ? memory.getString((0, values_1.asPointer)(key)) : (0, values_1.valToString)(key, memory);
            return (0, values_1.valBool)(memory.dictHas(ptr, keyStr));
        },
        /**
         * Get all keys from dictionary.
         */
        keys(dict) {
            if (!(0, values_1.isObject)(dict)) {
                throw new Error('keys() requires a dictionary');
            }
            const ptr = (0, values_1.asPointer)(dict);
            const keys = memory.dictKeys(ptr);
            const arr = memory.allocArray(keys.length);
            for (const key of keys) {
                memory.arrayPush(arr, memory.allocString(key));
            }
            return (0, values_1.valArray)(arr);
        },
        /**
         * Get all values from dictionary.
         */
        values(dict) {
            if (!(0, values_1.isObject)(dict)) {
                throw new Error('values() requires a dictionary');
            }
            const ptr = (0, values_1.asPointer)(dict);
            const values = memory.dictValues(ptr);
            const arr = memory.allocArray(values.length);
            for (const value of values) {
                memory.arrayPush(arr, value);
            }
            return (0, values_1.valArray)(arr);
        },
        /**
         * Get all entries from dictionary as [key, value] pairs.
         */
        entries(dict) {
            if (!(0, values_1.isObject)(dict)) {
                throw new Error('entries() requires a dictionary');
            }
            const ptr = (0, values_1.asPointer)(dict);
            const entries = memory.dictEntries(ptr);
            const arr = memory.allocArray(entries.length);
            for (const [key, value] of entries) {
                const pair = memory.allocArray(2);
                memory.arrayPush(pair, memory.allocString(key));
                memory.arrayPush(pair, value);
                memory.arrayPush(arr, (0, values_1.valArray)(pair));
            }
            return (0, values_1.valArray)(arr);
        },
        /**
         * Get dictionary size.
         */
        dict_size(dict) {
            if (!(0, values_1.isObject)(dict)) {
                throw new Error('dict_size() requires a dictionary');
            }
            const ptr = (0, values_1.asPointer)(dict);
            return (0, values_1.valInt)(memory.dictSize(ptr));
        },
        /**
         * Clear all entries from dictionary.
         */
        dict_clear(dict) {
            if (!(0, values_1.isObject)(dict)) {
                throw new Error('dict_clear() requires a dictionary');
            }
            const ptr = (0, values_1.asPointer)(dict);
            memory.dictClear(ptr);
            return (0, values_1.valNil)();
        },
        /**
         * Merge two dictionaries (second overwrites first).
         */
        dict_merge(dict1, dict2) {
            if (!(0, values_1.isObject)(dict1) || !(0, values_1.isObject)(dict2)) {
                throw new Error('dict_merge() requires dictionaries');
            }
            const ptr1 = (0, values_1.asPointer)(dict1);
            const ptr2 = (0, values_1.asPointer)(dict2);
            // Create new dict with entries from both
            const newPtr = memory.allocDict();
            // Copy from first
            for (const [key, value] of memory.dictEntries(ptr1)) {
                memory.dictSet(newPtr, key, value);
            }
            // Copy from second (overwrites)
            for (const [key, value] of memory.dictEntries(ptr2)) {
                memory.dictSet(newPtr, key, value);
            }
            return (0, values_1.valObject)(newPtr);
        },
        /**
         * Update dictionary in place with entries from another.
         */
        dict_update(dict, other) {
            if (!(0, values_1.isObject)(dict) || !(0, values_1.isObject)(other)) {
                throw new Error('dict_update() requires dictionaries');
            }
            const ptr = (0, values_1.asPointer)(dict);
            const otherPtr = (0, values_1.asPointer)(other);
            for (const [key, value] of memory.dictEntries(otherPtr)) {
                memory.dictSet(ptr, key, value);
            }
            return (0, values_1.valNil)();
        },
        /**
         * Create dictionary from array of [key, value] pairs.
         */
        dict_from_entries(entries) {
            if (!(entries && ((entries & 0x7n) === 5n))) {
                throw new Error('dict_from_entries() requires an array');
            }
            const arrPtr = (0, values_1.asPointer)(entries);
            const count = memory.arrayCount(arrPtr);
            const newPtr = memory.allocDict();
            for (let i = 0; i < count; i++) {
                const pair = memory.arrayGet(arrPtr, i);
                if ((pair & 0x7n) !== 5n) {
                    throw new Error('dict_from_entries() requires array of [key, value] pairs');
                }
                const pairPtr = (0, values_1.asPointer)(pair);
                const key = memory.arrayGet(pairPtr, 0);
                const value = memory.arrayGet(pairPtr, 1);
                const keyStr = (0, values_1.isString)(key) ? memory.getString((0, values_1.asPointer)(key)) : (0, values_1.valToString)(key, memory);
                memory.dictSet(newPtr, keyStr, value);
            }
            return (0, values_1.valObject)(newPtr);
        },
        /**
         * Map function over dictionary values.
         */
        dict_map(dict, func) {
            if (!(0, values_1.isObject)(dict)) {
                throw new Error('dict_map() requires a dictionary');
            }
            if (!(0, values_1.isFunction)(func)) {
                throw new Error('dict_map() requires a function');
            }
            const ptr = (0, values_1.asPointer)(dict);
            const funcIdx = (0, values_1.asPointer)(func);
            const newPtr = memory.allocDict();
            for (const [key, value] of memory.dictEntries(ptr)) {
                const result = callFunction(funcIdx, value, memory.allocString(key));
                memory.dictSet(newPtr, key, result);
            }
            return (0, values_1.valObject)(newPtr);
        },
        /**
         * Filter dictionary by predicate.
         */
        dict_filter(dict, predicate) {
            if (!(0, values_1.isObject)(dict)) {
                throw new Error('dict_filter() requires a dictionary');
            }
            if (!(0, values_1.isFunction)(predicate)) {
                throw new Error('dict_filter() requires a function');
            }
            const ptr = (0, values_1.asPointer)(dict);
            const funcIdx = (0, values_1.asPointer)(predicate);
            const newPtr = memory.allocDict();
            for (const [key, value] of memory.dictEntries(ptr)) {
                const result = callFunction(funcIdx, value, memory.allocString(key));
                if ((0, values_1.isTruthy)(result)) {
                    memory.dictSet(newPtr, key, value);
                }
            }
            return (0, values_1.valObject)(newPtr);
        },
    };
}
//# sourceMappingURL=dict.js.map