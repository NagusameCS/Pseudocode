/*
 * Pseudocode WASM Standard Library - Array Functions
 */
import { valInt, valBool, valNil, valArray, isArray, isFunction, asPointer, asNumber, isTruthy } from '../runtime/values';
/**
 * Create array functions for the runtime.
 */
export function createArrayFunctions(memory, callFunction) {
    return {
        /**
         * Get array length.
         */
        len(arr) {
            if (!isArray(arr)) {
                throw new Error('len() requires an array');
            }
            const ptr = asPointer(arr);
            return valInt(memory.arrayCount(ptr));
        },
        /**
         * Push a value onto the array.
         */
        push(arr, value) {
            if (!isArray(arr)) {
                throw new Error('push() requires an array');
            }
            const ptr = asPointer(arr);
            memory.arrayPush(ptr, value);
            return valNil();
        },
        /**
         * Pop a value from the array.
         */
        pop(arr) {
            if (!isArray(arr)) {
                throw new Error('pop() requires an array');
            }
            const ptr = asPointer(arr);
            return memory.arrayPop(ptr);
        },
        /**
         * Get array element at index.
         */
        get(arr, index) {
            if (!isArray(arr)) {
                throw new Error('Array access requires an array');
            }
            const ptr = asPointer(arr);
            const idx = Math.trunc(asNumber(index));
            const count = memory.arrayCount(ptr);
            // Support negative indices (Python-style)
            const normalizedIdx = idx < 0 ? count + idx : idx;
            if (normalizedIdx < 0 || normalizedIdx >= count) {
                throw new Error(`Index ${idx} out of bounds for array of length ${count}`);
            }
            return memory.arrayGet(ptr, normalizedIdx);
        },
        /**
         * Set array element at index.
         */
        set(arr, index, value) {
            if (!isArray(arr)) {
                throw new Error('Array access requires an array');
            }
            const ptr = asPointer(arr);
            const idx = Math.trunc(asNumber(index));
            memory.arraySet(ptr, idx, value);
            return valNil();
        },
        /**
         * Get array slice.
         */
        slice(arr, start, end) {
            if (!isArray(arr)) {
                throw new Error('slice() requires an array');
            }
            const ptr = asPointer(arr);
            const count = memory.arrayCount(ptr);
            let startIdx = Math.trunc(asNumber(start));
            let endIdx = end ? Math.trunc(asNumber(end)) : count;
            // Normalize negative indices
            if (startIdx < 0)
                startIdx = Math.max(0, count + startIdx);
            if (endIdx < 0)
                endIdx = count + endIdx;
            endIdx = Math.min(endIdx, count);
            // Create new array with slice
            const newArr = memory.allocArray(Math.max(0, endIdx - startIdx));
            for (let i = startIdx; i < endIdx; i++) {
                memory.arrayPush(newArr, memory.arrayGet(ptr, i));
            }
            return valArray(newArr);
        },
        /**
         * Reverse array in place.
         */
        reverse(arr) {
            if (!isArray(arr)) {
                throw new Error('reverse() requires an array');
            }
            const ptr = asPointer(arr);
            const count = memory.arrayCount(ptr);
            // Reverse in place
            for (let i = 0; i < Math.floor(count / 2); i++) {
                const temp = memory.arrayGet(ptr, i);
                memory.arraySet(ptr, i, memory.arrayGet(ptr, count - 1 - i));
                memory.arraySet(ptr, count - 1 - i, temp);
            }
            return arr;
        },
        /**
         * Sort array in place (numeric or string comparison).
         */
        sort(arr, comparator) {
            if (!isArray(arr)) {
                throw new Error('sort() requires an array');
            }
            const ptr = asPointer(arr);
            const count = memory.arrayCount(ptr);
            // Extract to JS array for sorting
            const elements = [];
            for (let i = 0; i < count; i++) {
                elements.push({ idx: i, val: memory.arrayGet(ptr, i) });
            }
            // Sort
            if (comparator && isFunction(comparator)) {
                const funcIdx = asPointer(comparator);
                elements.sort((a, b) => {
                    const result = callFunction(funcIdx, a.val, b.val);
                    return asNumber(result);
                });
            }
            else {
                elements.sort((a, b) => {
                    const na = asNumber(a.val);
                    const nb = asNumber(b.val);
                    return na - nb;
                });
            }
            // Write back
            for (let i = 0; i < elements.length; i++) {
                memory.arraySet(ptr, i, elements[i].val);
            }
            return arr;
        },
        /**
         * Map function over array.
         */
        map(arr, func) {
            if (!isArray(arr)) {
                throw new Error('map() requires an array');
            }
            if (!isFunction(func)) {
                throw new Error('map() requires a function');
            }
            const ptr = asPointer(arr);
            const funcIdx = asPointer(func);
            const count = memory.arrayCount(ptr);
            const newArr = memory.allocArray(count);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                const result = callFunction(funcIdx, elem, valInt(i));
                memory.arrayPush(newArr, result);
            }
            return valArray(newArr);
        },
        /**
         * Filter array by predicate.
         */
        filter(arr, predicate) {
            if (!isArray(arr)) {
                throw new Error('filter() requires an array');
            }
            if (!isFunction(predicate)) {
                throw new Error('filter() requires a function');
            }
            const ptr = asPointer(arr);
            const funcIdx = asPointer(predicate);
            const count = memory.arrayCount(ptr);
            const newArr = memory.allocArray(0);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                const result = callFunction(funcIdx, elem, valInt(i));
                if (isTruthy(result)) {
                    memory.arrayPush(newArr, elem);
                }
            }
            return valArray(newArr);
        },
        /**
         * Reduce array with accumulator.
         */
        reduce(arr, func, initial) {
            if (!isArray(arr)) {
                throw new Error('reduce() requires an array');
            }
            if (!isFunction(func)) {
                throw new Error('reduce() requires a function');
            }
            const ptr = asPointer(arr);
            const funcIdx = asPointer(func);
            const count = memory.arrayCount(ptr);
            let accumulator = initial;
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                accumulator = callFunction(funcIdx, accumulator, elem, valInt(i));
            }
            return accumulator;
        },
        /**
         * Find first element matching predicate.
         */
        find(arr, predicate) {
            if (!isArray(arr)) {
                throw new Error('find() requires an array');
            }
            if (!isFunction(predicate)) {
                throw new Error('find() requires a function');
            }
            const ptr = asPointer(arr);
            const funcIdx = asPointer(predicate);
            const count = memory.arrayCount(ptr);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                const result = callFunction(funcIdx, elem, valInt(i));
                if (isTruthy(result)) {
                    return elem;
                }
            }
            return valNil();
        },
        /**
         * Find index of first element matching predicate.
         */
        find_index(arr, predicate) {
            if (!isArray(arr)) {
                throw new Error('find_index() requires an array');
            }
            if (!isFunction(predicate)) {
                throw new Error('find_index() requires a function');
            }
            const ptr = asPointer(arr);
            const funcIdx = asPointer(predicate);
            const count = memory.arrayCount(ptr);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                const result = callFunction(funcIdx, elem, valInt(i));
                if (isTruthy(result)) {
                    return valInt(i);
                }
            }
            return valInt(-1);
        },
        /**
         * Check if any element matches predicate.
         */
        some(arr, predicate) {
            if (!isArray(arr)) {
                throw new Error('some() requires an array');
            }
            if (!isFunction(predicate)) {
                throw new Error('some() requires a function');
            }
            const ptr = asPointer(arr);
            const funcIdx = asPointer(predicate);
            const count = memory.arrayCount(ptr);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                const result = callFunction(funcIdx, elem, valInt(i));
                if (isTruthy(result)) {
                    return valBool(true);
                }
            }
            return valBool(false);
        },
        /**
         * Check if all elements match predicate.
         */
        every(arr, predicate) {
            if (!isArray(arr)) {
                throw new Error('every() requires an array');
            }
            if (!isFunction(predicate)) {
                throw new Error('every() requires a function');
            }
            const ptr = asPointer(arr);
            const funcIdx = asPointer(predicate);
            const count = memory.arrayCount(ptr);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                const result = callFunction(funcIdx, elem, valInt(i));
                if (!isTruthy(result)) {
                    return valBool(false);
                }
            }
            return valBool(true);
        },
        /**
         * Check if array includes a value.
         */
        includes(arr, value) {
            if (!isArray(arr)) {
                throw new Error('includes() requires an array');
            }
            const ptr = asPointer(arr);
            const count = memory.arrayCount(ptr);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                if (elem === value) {
                    return valBool(true);
                }
            }
            return valBool(false);
        },
        /**
         * Find index of value in array.
         */
        index_of(arr, value) {
            if (!isArray(arr)) {
                throw new Error('index_of() requires an array');
            }
            const ptr = asPointer(arr);
            const count = memory.arrayCount(ptr);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                if (elem === value) {
                    return valInt(i);
                }
            }
            return valInt(-1);
        },
        /**
         * Concatenate arrays.
         */
        concat(arr1, arr2) {
            if (!isArray(arr1) || !isArray(arr2)) {
                throw new Error('concat() requires arrays');
            }
            const ptr1 = asPointer(arr1);
            const ptr2 = asPointer(arr2);
            const count1 = memory.arrayCount(ptr1);
            const count2 = memory.arrayCount(ptr2);
            const newArr = memory.allocArray(count1 + count2);
            for (let i = 0; i < count1; i++) {
                memory.arrayPush(newArr, memory.arrayGet(ptr1, i));
            }
            for (let i = 0; i < count2; i++) {
                memory.arrayPush(newArr, memory.arrayGet(ptr2, i));
            }
            return valArray(newArr);
        },
        /**
         * Flatten nested arrays.
         */
        flatten(arr, depth = valInt(1)) {
            if (!isArray(arr)) {
                throw new Error('flatten() requires an array');
            }
            const maxDepth = Math.trunc(asNumber(depth));
            function flattenHelper(ptr, currentDepth) {
                const count = memory.arrayCount(ptr);
                const result = [];
                for (let i = 0; i < count; i++) {
                    const elem = memory.arrayGet(ptr, i);
                    if (isArray(elem) && currentDepth < maxDepth) {
                        result.push(...flattenHelper(asPointer(elem), currentDepth + 1));
                    }
                    else {
                        result.push(elem);
                    }
                }
                return result;
            }
            const flattened = flattenHelper(asPointer(arr), 0);
            const newArr = memory.allocArray(flattened.length);
            for (const elem of flattened) {
                memory.arrayPush(newArr, elem);
            }
            return valArray(newArr);
        },
        /**
         * Create a range array.
         */
        range(start, end, step) {
            const startNum = Math.trunc(asNumber(start));
            const endNum = Math.trunc(asNumber(end));
            const stepNum = step ? Math.trunc(asNumber(step)) : 1;
            if (stepNum === 0) {
                throw new Error('range() step cannot be zero');
            }
            const count = Math.max(0, Math.ceil((endNum - startNum) / stepNum));
            const newArr = memory.allocArray(count);
            for (let i = startNum; stepNum > 0 ? i < endNum : i > endNum; i += stepNum) {
                memory.arrayPush(newArr, valInt(i));
            }
            return valArray(newArr);
        },
        /**
         * Zip multiple arrays together.
         */
        zip(arr1, arr2) {
            if (!isArray(arr1) || !isArray(arr2)) {
                throw new Error('zip() requires arrays');
            }
            const ptr1 = asPointer(arr1);
            const ptr2 = asPointer(arr2);
            const count = Math.min(memory.arrayCount(ptr1), memory.arrayCount(ptr2));
            const newArr = memory.allocArray(count);
            for (let i = 0; i < count; i++) {
                const pair = memory.allocArray(2);
                memory.arrayPush(pair, memory.arrayGet(ptr1, i));
                memory.arrayPush(pair, memory.arrayGet(ptr2, i));
                memory.arrayPush(newArr, valArray(pair));
            }
            return valArray(newArr);
        },
    };
}
//# sourceMappingURL=array.js.map