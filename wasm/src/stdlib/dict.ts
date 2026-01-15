/*
 * Pseudocode WASM Standard Library - Dictionary Functions
 */

import { 
    Value, valInt, valBool, valNil, valArray, valString, valObject,
    isObject, isString, isFunction, asPointer, isTruthy, valToString
} from '../runtime/values';
import { Memory } from '../runtime/memory';

/**
 * Create dictionary functions for the runtime.
 */
export function createDictFunctions(memory: Memory, callFunction: (funcIdx: number, ...args: Value[]) => Value) {
    return {
        /**
         * Create a new empty dictionary.
         */
        dict_new(): Value {
            const ptr = memory.allocDict();
            return valObject(ptr);
        },
        
        /**
         * Get a value from dictionary by key.
         */
        dict_get(dict: Value, key: Value, defaultValue?: Value): Value {
            if (!isObject(dict)) {
                throw new Error('dict_get() requires a dictionary');
            }
            
            const ptr = asPointer(dict);
            const keyStr = isString(key) ? memory.getString(asPointer(key)) : valToString(key, memory);
            
            const value = memory.dictGet(ptr, keyStr);
            if (value === undefined) {
                return defaultValue !== undefined ? defaultValue : valNil();
            }
            return value;
        },
        
        /**
         * Set a value in dictionary.
         */
        dict_set(dict: Value, key: Value, value: Value): Value {
            if (!isObject(dict)) {
                throw new Error('dict_set() requires a dictionary');
            }
            
            const ptr = asPointer(dict);
            const keyStr = isString(key) ? memory.getString(asPointer(key)) : valToString(key, memory);
            
            memory.dictSet(ptr, keyStr, value);
            return valNil();
        },
        
        /**
         * Delete a key from dictionary.
         */
        dict_delete(dict: Value, key: Value): Value {
            if (!isObject(dict)) {
                throw new Error('dict_delete() requires a dictionary');
            }
            
            const ptr = asPointer(dict);
            const keyStr = isString(key) ? memory.getString(asPointer(key)) : valToString(key, memory);
            
            const existed = memory.dictDelete(ptr, keyStr);
            return valBool(existed);
        },
        
        /**
         * Check if dictionary has a key.
         */
        has_key(dict: Value, key: Value): Value {
            if (!isObject(dict)) {
                throw new Error('has_key() requires a dictionary');
            }
            
            const ptr = asPointer(dict);
            const keyStr = isString(key) ? memory.getString(asPointer(key)) : valToString(key, memory);
            
            return valBool(memory.dictHas(ptr, keyStr));
        },
        
        /**
         * Get all keys from dictionary.
         */
        keys(dict: Value): Value {
            if (!isObject(dict)) {
                throw new Error('keys() requires a dictionary');
            }
            
            const ptr = asPointer(dict);
            const keys = memory.dictKeys(ptr);
            
            const arr = memory.allocArray(keys.length);
            for (const key of keys) {
                memory.arrayPush(arr, memory.allocString(key));
            }
            
            return valArray(arr);
        },
        
        /**
         * Get all values from dictionary.
         */
        values(dict: Value): Value {
            if (!isObject(dict)) {
                throw new Error('values() requires a dictionary');
            }
            
            const ptr = asPointer(dict);
            const values = memory.dictValues(ptr);
            
            const arr = memory.allocArray(values.length);
            for (const value of values) {
                memory.arrayPush(arr, value);
            }
            
            return valArray(arr);
        },
        
        /**
         * Get all entries from dictionary as [key, value] pairs.
         */
        entries(dict: Value): Value {
            if (!isObject(dict)) {
                throw new Error('entries() requires a dictionary');
            }
            
            const ptr = asPointer(dict);
            const entries = memory.dictEntries(ptr);
            
            const arr = memory.allocArray(entries.length);
            for (const [key, value] of entries) {
                const pair = memory.allocArray(2);
                memory.arrayPush(pair, memory.allocString(key));
                memory.arrayPush(pair, value);
                memory.arrayPush(arr, valArray(pair));
            }
            
            return valArray(arr);
        },
        
        /**
         * Get dictionary size.
         */
        dict_size(dict: Value): Value {
            if (!isObject(dict)) {
                throw new Error('dict_size() requires a dictionary');
            }
            
            const ptr = asPointer(dict);
            return valInt(memory.dictSize(ptr));
        },
        
        /**
         * Clear all entries from dictionary.
         */
        dict_clear(dict: Value): Value {
            if (!isObject(dict)) {
                throw new Error('dict_clear() requires a dictionary');
            }
            
            const ptr = asPointer(dict);
            memory.dictClear(ptr);
            return valNil();
        },
        
        /**
         * Merge two dictionaries (second overwrites first).
         */
        dict_merge(dict1: Value, dict2: Value): Value {
            if (!isObject(dict1) || !isObject(dict2)) {
                throw new Error('dict_merge() requires dictionaries');
            }
            
            const ptr1 = asPointer(dict1);
            const ptr2 = asPointer(dict2);
            
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
            
            return valObject(newPtr);
        },
        
        /**
         * Update dictionary in place with entries from another.
         */
        dict_update(dict: Value, other: Value): Value {
            if (!isObject(dict) || !isObject(other)) {
                throw new Error('dict_update() requires dictionaries');
            }
            
            const ptr = asPointer(dict);
            const otherPtr = asPointer(other);
            
            for (const [key, value] of memory.dictEntries(otherPtr)) {
                memory.dictSet(ptr, key, value);
            }
            
            return valNil();
        },
        
        /**
         * Create dictionary from array of [key, value] pairs.
         */
        dict_from_entries(entries: Value): Value {
            if (!(entries && ((entries & 0x7n) === 5n))) {
                throw new Error('dict_from_entries() requires an array');
            }
            
            const arrPtr = asPointer(entries);
            const count = memory.arrayCount(arrPtr);
            const newPtr = memory.allocDict();
            
            for (let i = 0; i < count; i++) {
                const pair = memory.arrayGet(arrPtr, i);
                if ((pair & 0x7n) !== 5n) {
                    throw new Error('dict_from_entries() requires array of [key, value] pairs');
                }
                
                const pairPtr = asPointer(pair);
                const key = memory.arrayGet(pairPtr, 0);
                const value = memory.arrayGet(pairPtr, 1);
                
                const keyStr = isString(key) ? memory.getString(asPointer(key)) : valToString(key, memory);
                memory.dictSet(newPtr, keyStr, value);
            }
            
            return valObject(newPtr);
        },
        
        /**
         * Map function over dictionary values.
         */
        dict_map(dict: Value, func: Value): Value {
            if (!isObject(dict)) {
                throw new Error('dict_map() requires a dictionary');
            }
            if (!isFunction(func)) {
                throw new Error('dict_map() requires a function');
            }
            
            const ptr = asPointer(dict);
            const funcIdx = asPointer(func);
            const newPtr = memory.allocDict();
            
            for (const [key, value] of memory.dictEntries(ptr)) {
                const result = callFunction(funcIdx, value, memory.allocString(key));
                memory.dictSet(newPtr, key, result);
            }
            
            return valObject(newPtr);
        },
        
        /**
         * Filter dictionary by predicate.
         */
        dict_filter(dict: Value, predicate: Value): Value {
            if (!isObject(dict)) {
                throw new Error('dict_filter() requires a dictionary');
            }
            if (!isFunction(predicate)) {
                throw new Error('dict_filter() requires a function');
            }
            
            const ptr = asPointer(dict);
            const funcIdx = asPointer(predicate);
            const newPtr = memory.allocDict();
            
            for (const [key, value] of memory.dictEntries(ptr)) {
                const result = callFunction(funcIdx, value, memory.allocString(key));
                if (isTruthy(result)) {
                    memory.dictSet(newPtr, key, value);
                }
            }
            
            return valObject(newPtr);
        },
    };
}
