export declare const TAG_NIL = 0n;
export declare const TAG_BOOL = 1n;
export declare const TAG_INT = 2n;
export declare const TAG_FLOAT = 3n;
export declare const TAG_STRING = 4n;
export declare const TAG_ARRAY = 5n;
export declare const TAG_OBJECT = 6n;
export declare const TAG_FUNCTION = 7n;
export declare const TAG_MASK = 7n;
export declare const PAYLOAD_SHIFT = 3n;
export declare const SMALL_INT_MAX: bigint;
export declare const SMALL_INT_MIN: bigint;
export declare const VAL_NIL = 0n;
export declare const VAL_TRUE: bigint;
export declare const VAL_FALSE: bigint;
/**
 * Represents a Pseudocode value in the WASM runtime.
 * Uses BigInt internally for precise 64-bit manipulation.
 */
export type Value = bigint;
/**
 * Create a nil value.
 */
export declare function valNil(): Value;
/**
 * Create a boolean value.
 */
export declare function valBool(b: boolean): Value;
/**
 * Create an integer value.
 * Small integers are inlined, large integers become heap objects.
 */
export declare function valInt(n: number): Value;
/**
 * Create a float value.
 * The float bits are stored directly in the payload.
 */
export declare function valFloat(n: number): Value;
/**
 * Create a string value from a heap pointer.
 */
export declare function valString(ptr: number): Value;
/**
 * Create an array value from a heap pointer.
 */
export declare function valArray(ptr: number): Value;
/**
 * Create an object value from a heap pointer.
 */
export declare function valObject(ptr: number): Value;
/**
 * Create a function value from a function index.
 */
export declare function valFunction(idx: number): Value;
export declare function isNil(v: Value): boolean;
export declare function isBool(v: Value): boolean;
export declare function isInt(v: Value): boolean;
export declare function isFloat(v: Value): boolean;
export declare function isNumber(v: Value): boolean;
export declare function isString(v: Value): boolean;
export declare function isArray(v: Value): boolean;
export declare function isObject(v: Value): boolean;
export declare function isFunction(v: Value): boolean;
export declare function asBool(v: Value): boolean;
export declare function asInt(v: Value): number;
export declare function asFloat(v: Value): number;
export declare function asNumber(v: Value): number;
export declare function asPointer(v: Value): number;
export declare function isTruthy(v: Value): boolean;
export declare function typeName(v: Value): string;
export declare function valEquals(a: Value, b: Value): boolean;
export declare function valToString(v: Value, memory?: Memory): string;
import type { Memory } from './memory';
//# sourceMappingURL=values.d.ts.map