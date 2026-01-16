"use strict";
/*
 * Pseudocode WASM Runtime - Value Representation
 *
 * Uses tagged values for efficient representation in WebAssembly.
 * All values are 64-bit to allow inline doubles and pointers.
 *
 * Value Layout (64 bits):
 * - Bits 0-2: Type tag (8 types)
 * - Bits 3-63: Payload (61 bits for pointers/data)
 *
 * For small integers (<= 2^28), we inline them in the value.
 * For larger numbers and objects, we use heap pointers.
 */
Object.defineProperty(exports, "__esModule", { value: true });
exports.VAL_FALSE = exports.VAL_TRUE = exports.VAL_NIL = exports.SMALL_INT_MIN = exports.SMALL_INT_MAX = exports.PAYLOAD_SHIFT = exports.TAG_MASK = exports.TAG_FUNCTION = exports.TAG_OBJECT = exports.TAG_ARRAY = exports.TAG_STRING = exports.TAG_FLOAT = exports.TAG_INT = exports.TAG_BOOL = exports.TAG_NIL = void 0;
exports.valNil = valNil;
exports.valBool = valBool;
exports.valInt = valInt;
exports.valFloat = valFloat;
exports.valString = valString;
exports.valArray = valArray;
exports.valObject = valObject;
exports.valFunction = valFunction;
exports.isNil = isNil;
exports.isBool = isBool;
exports.isInt = isInt;
exports.isFloat = isFloat;
exports.isNumber = isNumber;
exports.isString = isString;
exports.isArray = isArray;
exports.isObject = isObject;
exports.isFunction = isFunction;
exports.asBool = asBool;
exports.asInt = asInt;
exports.asFloat = asFloat;
exports.asNumber = asNumber;
exports.asPointer = asPointer;
exports.isTruthy = isTruthy;
exports.typeName = typeName;
exports.valEquals = valEquals;
exports.valToString = valToString;
// Type tags (3 bits = 8 types)
exports.TAG_NIL = 0n;
exports.TAG_BOOL = 1n;
exports.TAG_INT = 2n; // Small integer (inline)
exports.TAG_FLOAT = 3n; // Float (inline as f64 bits)
exports.TAG_STRING = 4n; // String object pointer
exports.TAG_ARRAY = 5n; // Array object pointer
exports.TAG_OBJECT = 6n; // Generic object pointer (dict, class, etc.)
exports.TAG_FUNCTION = 7n; // Function pointer
// Masks for extracting parts
exports.TAG_MASK = 0x7n; // Lower 3 bits
exports.PAYLOAD_SHIFT = 3n;
exports.SMALL_INT_MAX = (1n << 28n) - 1n;
exports.SMALL_INT_MIN = -(1n << 28n);
// Special values
exports.VAL_NIL = exports.TAG_NIL;
exports.VAL_TRUE = exports.TAG_BOOL | (1n << exports.PAYLOAD_SHIFT);
exports.VAL_FALSE = exports.TAG_BOOL | (0n << exports.PAYLOAD_SHIFT);
/**
 * Create a nil value.
 */
function valNil() {
    return exports.VAL_NIL;
}
/**
 * Create a boolean value.
 */
function valBool(b) {
    return b ? exports.VAL_TRUE : exports.VAL_FALSE;
}
/**
 * Create an integer value.
 * Small integers are inlined, large integers become heap objects.
 */
function valInt(n) {
    const i = BigInt(Math.trunc(n));
    if (i >= exports.SMALL_INT_MIN && i <= exports.SMALL_INT_MAX) {
        // Small integer - inline it
        // Handle negative numbers using two's complement in 29 bits
        const payload = i < 0n ? (i & ((1n << 29n) - 1n)) : i;
        return exports.TAG_INT | (payload << exports.PAYLOAD_SHIFT);
    }
    // Large integer - need to box it (TODO: implement heap allocation)
    throw new Error('Large integers not yet supported');
}
/**
 * Create a float value.
 * The float bits are stored directly in the payload.
 */
function valFloat(n) {
    const buffer = new ArrayBuffer(8);
    const view = new DataView(buffer);
    view.setFloat64(0, n, true); // little-endian
    const bits = view.getBigUint64(0, true);
    // Store float bits shifted, with tag
    return exports.TAG_FLOAT | (bits << exports.PAYLOAD_SHIFT);
}
/**
 * Create a string value from a heap pointer.
 */
function valString(ptr) {
    return exports.TAG_STRING | (BigInt(ptr) << exports.PAYLOAD_SHIFT);
}
/**
 * Create an array value from a heap pointer.
 */
function valArray(ptr) {
    return exports.TAG_ARRAY | (BigInt(ptr) << exports.PAYLOAD_SHIFT);
}
/**
 * Create an object value from a heap pointer.
 */
function valObject(ptr) {
    return exports.TAG_OBJECT | (BigInt(ptr) << exports.PAYLOAD_SHIFT);
}
/**
 * Create a function value from a function index.
 */
function valFunction(idx) {
    return exports.TAG_FUNCTION | (BigInt(idx) << exports.PAYLOAD_SHIFT);
}
// ============ Type Checking ============
function isNil(v) {
    return (v & exports.TAG_MASK) === exports.TAG_NIL;
}
function isBool(v) {
    return (v & exports.TAG_MASK) === exports.TAG_BOOL;
}
function isInt(v) {
    return (v & exports.TAG_MASK) === exports.TAG_INT;
}
function isFloat(v) {
    return (v & exports.TAG_MASK) === exports.TAG_FLOAT;
}
function isNumber(v) {
    const tag = v & exports.TAG_MASK;
    return tag === exports.TAG_INT || tag === exports.TAG_FLOAT;
}
function isString(v) {
    return (v & exports.TAG_MASK) === exports.TAG_STRING;
}
function isArray(v) {
    return (v & exports.TAG_MASK) === exports.TAG_ARRAY;
}
function isObject(v) {
    return (v & exports.TAG_MASK) === exports.TAG_OBJECT;
}
function isFunction(v) {
    return (v & exports.TAG_MASK) === exports.TAG_FUNCTION;
}
// ============ Value Extraction ============
function asBool(v) {
    return (v >> exports.PAYLOAD_SHIFT) !== 0n;
}
function asInt(v) {
    let payload = v >> exports.PAYLOAD_SHIFT;
    // Sign-extend if negative (bit 28 set)
    if (payload & (1n << 28n)) {
        payload = payload | (~0n << 29n);
    }
    return Number(payload);
}
function asFloat(v) {
    const bits = v >> exports.PAYLOAD_SHIFT;
    const buffer = new ArrayBuffer(8);
    const view = new DataView(buffer);
    view.setBigUint64(0, bits, true);
    return view.getFloat64(0, true);
}
function asNumber(v) {
    if (isInt(v))
        return asInt(v);
    if (isFloat(v))
        return asFloat(v);
    throw new Error('Value is not a number');
}
function asPointer(v) {
    return Number(v >> exports.PAYLOAD_SHIFT);
}
// ============ Truthiness ============
function isTruthy(v) {
    const tag = v & exports.TAG_MASK;
    if (tag === exports.TAG_NIL)
        return false;
    if (tag === exports.TAG_BOOL)
        return asBool(v);
    if (tag === exports.TAG_INT)
        return asInt(v) !== 0;
    if (tag === exports.TAG_FLOAT)
        return asFloat(v) !== 0;
    // Objects, strings, arrays, functions are truthy
    return true;
}
// ============ Type Name ============
function typeName(v) {
    const tag = v & exports.TAG_MASK;
    switch (tag) {
        case exports.TAG_NIL: return 'nil';
        case exports.TAG_BOOL: return 'bool';
        case exports.TAG_INT: return 'int';
        case exports.TAG_FLOAT: return 'float';
        case exports.TAG_STRING: return 'string';
        case exports.TAG_ARRAY: return 'array';
        case exports.TAG_OBJECT: return 'object';
        case exports.TAG_FUNCTION: return 'function';
        default: return 'unknown';
    }
}
// ============ Value Equality ============
function valEquals(a, b) {
    const tagA = a & exports.TAG_MASK;
    const tagB = b & exports.TAG_MASK;
    // Same type - direct comparison
    if (tagA === tagB) {
        return a === b;
    }
    // Numeric comparison (int vs float)
    if (isNumber(a) && isNumber(b)) {
        return asNumber(a) === asNumber(b);
    }
    return false;
}
// ============ Debug String ============
function valToString(v, memory) {
    const tag = v & exports.TAG_MASK;
    switch (tag) {
        case exports.TAG_NIL:
            return 'nil';
        case exports.TAG_BOOL:
            return asBool(v) ? 'true' : 'false';
        case exports.TAG_INT:
            return asInt(v).toString();
        case exports.TAG_FLOAT:
            return asFloat(v).toString();
        case exports.TAG_STRING:
            if (memory) {
                return memory.getString(asPointer(v));
            }
            return `<string@${asPointer(v)}>`;
        case exports.TAG_ARRAY:
            return `<array@${asPointer(v)}>`;
        case exports.TAG_OBJECT:
            return `<object@${asPointer(v)}>`;
        case exports.TAG_FUNCTION:
            return `<function#${asPointer(v)}>`;
        default:
            return `<unknown:${v}>`;
    }
}
//# sourceMappingURL=values.js.map