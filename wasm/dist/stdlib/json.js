"use strict";
/*
 * Pseudocode WASM Standard Library - JSON Functions
 */
Object.defineProperty(exports, "__esModule", { value: true });
exports.createJsonFunctions = createJsonFunctions;
const values_1 = require("../runtime/values");
/**
 * Create JSON functions for the runtime.
 */
function createJsonFunctions(memory) {
    /**
     * Convert a JavaScript value to a Pseudocode value.
     */
    const jsToValue = (js) => {
        if (js === null || js === undefined) {
            return (0, values_1.valNil)();
        }
        if (typeof js === 'boolean') {
            return (0, values_1.valBool)(js);
        }
        if (typeof js === 'number') {
            if (Number.isInteger(js) && Math.abs(js) < 2 ** 28) {
                return (0, values_1.valInt)(js);
            }
            return (0, values_1.valFloat)(js);
        }
        if (typeof js === 'string') {
            return memory.allocString(js);
        }
        if (Array.isArray(js)) {
            const arrPtr = memory.allocArray(js.length);
            for (const elem of js) {
                memory.arrayPush(arrPtr, jsToValue(elem));
            }
            return (0, values_1.valArray)(arrPtr);
        }
        if (typeof js === 'object') {
            const dictPtr = memory.allocDict();
            for (const [key, value] of Object.entries(js)) {
                memory.dictSet(dictPtr, key, jsToValue(value));
            }
            return (0, values_1.valObject)(dictPtr);
        }
        // Fallback: stringify
        return memory.allocString(String(js));
    };
    /**
     * Convert a Pseudocode value to a JavaScript value.
     */
    const valueToJs = (val) => {
        if ((0, values_1.isNil)(val)) {
            return null;
        }
        if ((0, values_1.isBool)(val)) {
            return (0, values_1.asBool)(val);
        }
        if ((0, values_1.isInt)(val) || (0, values_1.isFloat)(val)) {
            return (0, values_1.asNumber)(val);
        }
        if ((0, values_1.isString)(val)) {
            return memory.getString((0, values_1.asPointer)(val));
        }
        if ((0, values_1.isArray)(val)) {
            const ptr = (0, values_1.asPointer)(val);
            const count = memory.arrayCount(ptr);
            const arr = [];
            for (let i = 0; i < count; i++) {
                arr.push(valueToJs(memory.arrayGet(ptr, i)));
            }
            return arr;
        }
        if ((0, values_1.isObject)(val)) {
            const ptr = (0, values_1.asPointer)(val);
            const obj = {};
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
        json_parse(jsonStr) {
            if (!(0, values_1.isString)(jsonStr)) {
                throw new Error('json_parse() requires a string');
            }
            const str = memory.getString((0, values_1.asPointer)(jsonStr));
            try {
                const parsed = JSON.parse(str);
                return jsToValue(parsed);
            }
            catch (error) {
                throw new Error(`JSON parse error: ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        /**
         * Convert Pseudocode value to JSON string.
         */
        json_stringify(value, pretty) {
            const js = valueToJs(value);
            const indent = pretty && (0, values_1.asBool)(pretty) ? 2 : undefined;
            try {
                const json = JSON.stringify(js, null, indent);
                return memory.allocString(json);
            }
            catch (error) {
                throw new Error(`JSON stringify error: ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        /**
         * Check if a string is valid JSON.
         */
        json_valid(jsonStr) {
            if (!(0, values_1.isString)(jsonStr)) {
                return (0, values_1.valBool)(false);
            }
            const str = memory.getString((0, values_1.asPointer)(jsonStr));
            try {
                JSON.parse(str);
                return (0, values_1.valBool)(true);
            }
            catch {
                return (0, values_1.valBool)(false);
            }
        },
        /**
         * Get a value from JSON by path (e.g., "user.address.city" or "items[0].name").
         */
        json_get(value, path) {
            if (!(0, values_1.isString)(path)) {
                throw new Error('json_get() requires a string path');
            }
            const pathStr = memory.getString((0, values_1.asPointer)(path));
            const parts = pathStr.split(/\.|\[|\]/).filter(p => p !== '');
            let current = value;
            for (const part of parts) {
                if ((0, values_1.isArray)(current)) {
                    const idx = parseInt(part, 10);
                    if (isNaN(idx)) {
                        return (0, values_1.valNil)();
                    }
                    const ptr = (0, values_1.asPointer)(current);
                    const count = memory.arrayCount(ptr);
                    if (idx < 0 || idx >= count) {
                        return (0, values_1.valNil)();
                    }
                    current = memory.arrayGet(ptr, idx);
                }
                else if ((0, values_1.isObject)(current)) {
                    const ptr = (0, values_1.asPointer)(current);
                    const val = memory.dictGet(ptr, part);
                    if (val === undefined) {
                        return (0, values_1.valNil)();
                    }
                    current = val;
                }
                else {
                    return (0, values_1.valNil)();
                }
            }
            return current;
        },
        /**
         * Set a value in JSON by path.
         */
        json_set(value, path, newValue) {
            if (!(0, values_1.isString)(path)) {
                throw new Error('json_set() requires a string path');
            }
            const pathStr = memory.getString((0, values_1.asPointer)(path));
            const parts = pathStr.split(/\.|\[|\]/).filter(p => p !== '');
            if (parts.length === 0) {
                return newValue;
            }
            // Navigate to parent
            let current = value;
            for (let i = 0; i < parts.length - 1; i++) {
                const part = parts[i];
                if ((0, values_1.isArray)(current)) {
                    const idx = parseInt(part, 10);
                    if (isNaN(idx)) {
                        throw new Error(`Invalid array index: ${part}`);
                    }
                    current = memory.arrayGet((0, values_1.asPointer)(current), idx);
                }
                else if ((0, values_1.isObject)(current)) {
                    const ptr = (0, values_1.asPointer)(current);
                    const val = memory.dictGet(ptr, part);
                    if (val === undefined) {
                        throw new Error(`Path not found: ${part}`);
                    }
                    current = val;
                }
                else {
                    throw new Error(`Cannot navigate into ${(0, values_1.valToString)(current, memory)}`);
                }
            }
            // Set the final key
            const lastPart = parts[parts.length - 1];
            if ((0, values_1.isArray)(current)) {
                const idx = parseInt(lastPart, 10);
                if (isNaN(idx)) {
                    throw new Error(`Invalid array index: ${lastPart}`);
                }
                memory.arraySet((0, values_1.asPointer)(current), idx, newValue);
            }
            else if ((0, values_1.isObject)(current)) {
                memory.dictSet((0, values_1.asPointer)(current), lastPart, newValue);
            }
            else {
                throw new Error(`Cannot set property on ${(0, values_1.valToString)(current, memory)}`);
            }
            return value;
        },
        /**
         * Merge multiple JSON objects.
         */
        json_merge(...values) {
            const result = memory.allocDict();
            for (const value of values) {
                if ((0, values_1.isObject)(value)) {
                    const ptr = (0, values_1.asPointer)(value);
                    for (const [key, val] of memory.dictEntries(ptr)) {
                        memory.dictSet(result, key, val);
                    }
                }
            }
            return (0, values_1.valObject)(result);
        },
        /**
         * Deep clone a JSON value.
         */
        json_clone(value) {
            // Convert to JS and back to create deep clone
            const js = valueToJs(value);
            return jsToValue(js);
        },
        /**
         * Compare two JSON values for deep equality.
         */
        json_equals(a, b) {
            const jsA = valueToJs(a);
            const jsB = valueToJs(b);
            return (0, values_1.valBool)(JSON.stringify(jsA) === JSON.stringify(jsB));
        },
    };
}
//# sourceMappingURL=json.js.map