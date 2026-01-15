/*
 * Pseudocode WASM Standard Library - JSON Functions
 */

import { 
    Value, valString, valInt, valFloat, valBool, valNil, valArray, valObject,
    isString, isInt, isFloat, isArray, isObject, isBool, isNil,
    asPointer, asNumber, asBool, valToString
} from '../runtime/values';
import { Memory } from '../runtime/memory';

/**
 * Create JSON functions for the runtime.
 */
export function createJsonFunctions(memory: Memory) {
    /**
     * Convert a JavaScript value to a Pseudocode value.
     */
    const jsToValue = (js: unknown): Value => {
        if (js === null || js === undefined) {
            return valNil();
        }
        
        if (typeof js === 'boolean') {
            return valBool(js);
        }
        
        if (typeof js === 'number') {
            if (Number.isInteger(js) && Math.abs(js) < 2**28) {
                return valInt(js);
            }
            return valFloat(js);
        }
        
        if (typeof js === 'string') {
            return memory.allocString(js);
        }
        
        if (Array.isArray(js)) {
            const arrPtr = memory.allocArray(js.length);
            for (const elem of js) {
                memory.arrayPush(arrPtr, jsToValue(elem));
            }
            return valArray(arrPtr);
        }
        
        if (typeof js === 'object') {
            const dictPtr = memory.allocDict();
            for (const [key, value] of Object.entries(js)) {
                memory.dictSet(dictPtr, key, jsToValue(value));
            }
            return valObject(dictPtr);
        }
        
        // Fallback: stringify
        return memory.allocString(String(js));
    };
    
    /**
     * Convert a Pseudocode value to a JavaScript value.
     */
    const valueToJs = (val: Value): unknown => {
        if (isNil(val)) {
            return null;
        }
        
        if (isBool(val)) {
            return asBool(val);
        }
        
        if (isInt(val) || isFloat(val)) {
            return asNumber(val);
        }
        
        if (isString(val)) {
            return memory.getString(asPointer(val));
        }
        
        if (isArray(val)) {
            const ptr = asPointer(val);
            const count = memory.arrayCount(ptr);
            const arr: unknown[] = [];
            for (let i = 0; i < count; i++) {
                arr.push(valueToJs(memory.arrayGet(ptr, i)));
            }
            return arr;
        }
        
        if (isObject(val)) {
            const ptr = asPointer(val);
            const obj: Record<string, unknown> = {};
            for (const [key, value] of memory.dictEntries(ptr)) {
                obj[key] = valueToJs(value);
            }
            return obj;
        }
        
        // Function or unknown type
        return null;
    };
    
    return {
        /**
         * Parse JSON string to Pseudocode value.
         */
        json_parse(jsonStr: Value): Value {
            if (!isString(jsonStr)) {
                throw new Error('json_parse() requires a string');
            }
            
            const str = memory.getString(asPointer(jsonStr));
            
            try {
                const parsed = JSON.parse(str);
                return jsToValue(parsed);
            } catch (error) {
                throw new Error(`JSON parse error: ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Convert Pseudocode value to JSON string.
         */
        json_stringify(value: Value, pretty?: Value): Value {
            const js = valueToJs(value);
            const indent = pretty && asBool(pretty) ? 2 : undefined;
            
            try {
                const json = JSON.stringify(js, null, indent);
                return memory.allocString(json);
            } catch (error) {
                throw new Error(`JSON stringify error: ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Check if a string is valid JSON.
         */
        json_valid(jsonStr: Value): Value {
            if (!isString(jsonStr)) {
                return valBool(false);
            }
            
            const str = memory.getString(asPointer(jsonStr));
            
            try {
                JSON.parse(str);
                return valBool(true);
            } catch {
                return valBool(false);
            }
        },
        
        /**
         * Get a value from JSON by path (e.g., "user.address.city" or "items[0].name").
         */
        json_get(value: Value, path: Value): Value {
            if (!isString(path)) {
                throw new Error('json_get() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            const parts = pathStr.split(/\.|\[|\]/).filter(p => p !== '');
            
            let current = value;
            for (const part of parts) {
                if (isArray(current)) {
                    const idx = parseInt(part, 10);
                    if (isNaN(idx)) {
                        return valNil();
                    }
                    const ptr = asPointer(current);
                    const count = memory.arrayCount(ptr);
                    if (idx < 0 || idx >= count) {
                        return valNil();
                    }
                    current = memory.arrayGet(ptr, idx);
                } else if (isObject(current)) {
                    const ptr = asPointer(current);
                    const val = memory.dictGet(ptr, part);
                    if (val === undefined) {
                        return valNil();
                    }
                    current = val;
                } else {
                    return valNil();
                }
            }
            
            return current;
        },
        
        /**
         * Set a value in JSON by path.
         */
        json_set(value: Value, path: Value, newValue: Value): Value {
            if (!isString(path)) {
                throw new Error('json_set() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            const parts = pathStr.split(/\.|\[|\]/).filter(p => p !== '');
            
            if (parts.length === 0) {
                return newValue;
            }
            
            // Navigate to parent
            let current = value;
            for (let i = 0; i < parts.length - 1; i++) {
                const part = parts[i];
                if (isArray(current)) {
                    const idx = parseInt(part, 10);
                    if (isNaN(idx)) {
                        throw new Error(`Invalid array index: ${part}`);
                    }
                    current = memory.arrayGet(asPointer(current), idx);
                } else if (isObject(current)) {
                    const ptr = asPointer(current);
                    const val = memory.dictGet(ptr, part);
                    if (val === undefined) {
                        throw new Error(`Path not found: ${part}`);
                    }
                    current = val;
                } else {
                    throw new Error(`Cannot navigate into ${valToString(current, memory)}`);
                }
            }
            
            // Set the final key
            const lastPart = parts[parts.length - 1];
            if (isArray(current)) {
                const idx = parseInt(lastPart, 10);
                if (isNaN(idx)) {
                    throw new Error(`Invalid array index: ${lastPart}`);
                }
                memory.arraySet(asPointer(current), idx, newValue);
            } else if (isObject(current)) {
                memory.dictSet(asPointer(current), lastPart, newValue);
            } else {
                throw new Error(`Cannot set property on ${valToString(current, memory)}`);
            }
            
            return value;
        },
        
        /**
         * Merge multiple JSON objects.
         */
        json_merge(...values: Value[]): Value {
            const result = memory.allocDict();
            
            for (const value of values) {
                if (isObject(value)) {
                    const ptr = asPointer(value);
                    for (const [key, val] of memory.dictEntries(ptr)) {
                        memory.dictSet(result, key, val);
                    }
                }
            }
            
            return valObject(result);
        },
        
        /**
         * Deep clone a JSON value.
         */
        json_clone(value: Value): Value {
            // Convert to JS and back to create deep clone
            const js = valueToJs(value);
            return jsToValue(js);
        },
        
        /**
         * Compare two JSON values for deep equality.
         */
        json_equals(a: Value, b: Value): Value {
            const jsA = valueToJs(a);
            const jsB = valueToJs(b);
            return valBool(JSON.stringify(jsA) === JSON.stringify(jsB));
        },
    };
}
