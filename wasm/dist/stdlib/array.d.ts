import { Value } from '../runtime/values';
import { Memory } from '../runtime/memory';
/**
 * Create array functions for the runtime.
 */
export declare function createArrayFunctions(memory: Memory, callFunction: (funcIdx: number, ...args: Value[]) => Value): {
    /**
     * Get array length.
     */
    len(arr: Value): Value;
    /**
     * Push a value onto the array.
     */
    push(arr: Value, value: Value): Value;
    /**
     * Pop a value from the array.
     */
    pop(arr: Value): Value;
    /**
     * Get array element at index.
     */
    get(arr: Value, index: Value): Value;
    /**
     * Set array element at index.
     */
    set(arr: Value, index: Value, value: Value): Value;
    /**
     * Get array slice.
     */
    slice(arr: Value, start: Value, end?: Value): Value;
    /**
     * Reverse array in place.
     */
    reverse(arr: Value): Value;
    /**
     * Sort array in place (numeric or string comparison).
     */
    sort(arr: Value, comparator?: Value): Value;
    /**
     * Map function over array.
     */
    map(arr: Value, func: Value): Value;
    /**
     * Filter array by predicate.
     */
    filter(arr: Value, predicate: Value): Value;
    /**
     * Reduce array with accumulator.
     */
    reduce(arr: Value, func: Value, initial: Value): Value;
    /**
     * Find first element matching predicate.
     */
    find(arr: Value, predicate: Value): Value;
    /**
     * Find index of first element matching predicate.
     */
    find_index(arr: Value, predicate: Value): Value;
    /**
     * Check if any element matches predicate.
     */
    some(arr: Value, predicate: Value): Value;
    /**
     * Check if all elements match predicate.
     */
    every(arr: Value, predicate: Value): Value;
    /**
     * Check if array includes a value.
     */
    includes(arr: Value, value: Value): Value;
    /**
     * Find index of value in array.
     */
    index_of(arr: Value, value: Value): Value;
    /**
     * Concatenate arrays.
     */
    concat(arr1: Value, arr2: Value): Value;
    /**
     * Flatten nested arrays.
     */
    flatten(arr: Value, depth?: Value): Value;
    /**
     * Create a range array.
     */
    range(start: Value, end: Value, step?: Value): Value;
    /**
     * Zip multiple arrays together.
     */
    zip(arr1: Value, arr2: Value): Value;
};
//# sourceMappingURL=array.d.ts.map