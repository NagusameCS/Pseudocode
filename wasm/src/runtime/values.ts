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

// Type tags (3 bits = 8 types)
export const TAG_NIL = 0n;
export const TAG_BOOL = 1n;
export const TAG_INT = 2n;      // Small integer (inline)
export const TAG_FLOAT = 3n;    // Float (inline as f64 bits)
export const TAG_STRING = 4n;   // String object pointer
export const TAG_ARRAY = 5n;    // Array object pointer
export const TAG_OBJECT = 6n;   // Generic object pointer (dict, class, etc.)
export const TAG_FUNCTION = 7n; // Function pointer

// Masks for extracting parts
export const TAG_MASK = 0x7n;           // Lower 3 bits
export const PAYLOAD_SHIFT = 3n;
export const SMALL_INT_MAX = (1n << 28n) - 1n;
export const SMALL_INT_MIN = -(1n << 28n);

// Special values
export const VAL_NIL = TAG_NIL;
export const VAL_TRUE = TAG_BOOL | (1n << PAYLOAD_SHIFT);
export const VAL_FALSE = TAG_BOOL | (0n << PAYLOAD_SHIFT);

/**
 * Represents a Pseudocode value in the WASM runtime.
 * Uses BigInt internally for precise 64-bit manipulation.
 */
export type Value = bigint;

/**
 * Create a nil value.
 */
export function valNil(): Value {
    return VAL_NIL;
}

/**
 * Create a boolean value.
 */
export function valBool(b: boolean): Value {
    return b ? VAL_TRUE : VAL_FALSE;
}

/**
 * Create an integer value.
 * Small integers are inlined, large integers become heap objects.
 */
export function valInt(n: number): Value {
    const i = BigInt(Math.trunc(n));
    if (i >= SMALL_INT_MIN && i <= SMALL_INT_MAX) {
        // Small integer - inline it
        // Handle negative numbers using two's complement in 29 bits
        const payload = i < 0n ? (i & ((1n << 29n) - 1n)) : i;
        return TAG_INT | (payload << PAYLOAD_SHIFT);
    }
    // Large integer - need to box it (TODO: implement heap allocation)
    throw new Error('Large integers not yet supported');
}

/**
 * Create a float value.
 * The float bits are stored directly in the payload.
 */
export function valFloat(n: number): Value {
    const buffer = new ArrayBuffer(8);
    const view = new DataView(buffer);
    view.setFloat64(0, n, true); // little-endian
    const bits = view.getBigUint64(0, true);
    // Store float bits shifted, with tag
    return TAG_FLOAT | (bits << PAYLOAD_SHIFT);
}

/**
 * Create a string value from a heap pointer.
 */
export function valString(ptr: number): Value {
    return TAG_STRING | (BigInt(ptr) << PAYLOAD_SHIFT);
}

/**
 * Create an array value from a heap pointer.
 */
export function valArray(ptr: number): Value {
    return TAG_ARRAY | (BigInt(ptr) << PAYLOAD_SHIFT);
}

/**
 * Create an object value from a heap pointer.
 */
export function valObject(ptr: number): Value {
    return TAG_OBJECT | (BigInt(ptr) << PAYLOAD_SHIFT);
}

/**
 * Create a function value from a function index.
 */
export function valFunction(idx: number): Value {
    return TAG_FUNCTION | (BigInt(idx) << PAYLOAD_SHIFT);
}

// ============ Type Checking ============

export function isNil(v: Value): boolean {
    return (v & TAG_MASK) === TAG_NIL;
}

export function isBool(v: Value): boolean {
    return (v & TAG_MASK) === TAG_BOOL;
}

export function isInt(v: Value): boolean {
    return (v & TAG_MASK) === TAG_INT;
}

export function isFloat(v: Value): boolean {
    return (v & TAG_MASK) === TAG_FLOAT;
}

export function isNumber(v: Value): boolean {
    const tag = v & TAG_MASK;
    return tag === TAG_INT || tag === TAG_FLOAT;
}

export function isString(v: Value): boolean {
    return (v & TAG_MASK) === TAG_STRING;
}

export function isArray(v: Value): boolean {
    return (v & TAG_MASK) === TAG_ARRAY;
}

export function isObject(v: Value): boolean {
    return (v & TAG_MASK) === TAG_OBJECT;
}

export function isFunction(v: Value): boolean {
    return (v & TAG_MASK) === TAG_FUNCTION;
}

// ============ Value Extraction ============

export function asBool(v: Value): boolean {
    return (v >> PAYLOAD_SHIFT) !== 0n;
}

export function asInt(v: Value): number {
    let payload = v >> PAYLOAD_SHIFT;
    // Sign-extend if negative (bit 28 set)
    if (payload & (1n << 28n)) {
        payload = payload | (~0n << 29n);
    }
    return Number(payload);
}

export function asFloat(v: Value): number {
    const bits = v >> PAYLOAD_SHIFT;
    const buffer = new ArrayBuffer(8);
    const view = new DataView(buffer);
    view.setBigUint64(0, bits, true);
    return view.getFloat64(0, true);
}

export function asNumber(v: Value): number {
    if (isInt(v)) return asInt(v);
    if (isFloat(v)) return asFloat(v);
    throw new Error('Value is not a number');
}

export function asPointer(v: Value): number {
    return Number(v >> PAYLOAD_SHIFT);
}

// ============ Truthiness ============

export function isTruthy(v: Value): boolean {
    const tag = v & TAG_MASK;
    if (tag === TAG_NIL) return false;
    if (tag === TAG_BOOL) return asBool(v);
    if (tag === TAG_INT) return asInt(v) !== 0;
    if (tag === TAG_FLOAT) return asFloat(v) !== 0;
    // Objects, strings, arrays, functions are truthy
    return true;
}

// ============ Type Name ============

export function typeName(v: Value): string {
    const tag = v & TAG_MASK;
    switch (tag) {
        case TAG_NIL: return 'nil';
        case TAG_BOOL: return 'bool';
        case TAG_INT: return 'int';
        case TAG_FLOAT: return 'float';
        case TAG_STRING: return 'string';
        case TAG_ARRAY: return 'array';
        case TAG_OBJECT: return 'object';
        case TAG_FUNCTION: return 'function';
        default: return 'unknown';
    }
}

// ============ Value Equality ============

export function valEquals(a: Value, b: Value): boolean {
    const tagA = a & TAG_MASK;
    const tagB = b & TAG_MASK;
    
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

export function valToString(v: Value, memory?: Memory): string {
    const tag = v & TAG_MASK;
    switch (tag) {
        case TAG_NIL:
            return 'nil';
        case TAG_BOOL:
            return asBool(v) ? 'true' : 'false';
        case TAG_INT:
            return asInt(v).toString();
        case TAG_FLOAT:
            return asFloat(v).toString();
        case TAG_STRING:
            if (memory) {
                return memory.getString(asPointer(v));
            }
            return `<string@${asPointer(v)}>`;
        case TAG_ARRAY:
            return `<array@${asPointer(v)}>`;
        case TAG_OBJECT:
            return `<object@${asPointer(v)}>`;
        case TAG_FUNCTION:
            return `<function#${asPointer(v)}>`;
        default:
            return `<unknown:${v}>`;
    }
}

// Import Memory type (will be defined in memory.ts)
import type { Memory } from './memory';
