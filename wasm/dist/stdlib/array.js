"use strict";
/*
 * Pseudocode WASM Standard Library - Array Functions
 */
Object.defineProperty(exports, "__esModule", { value: true });
exports.createArrayFunctions = createArrayFunctions;
const values_1 = require("../runtime/values");
/**
 * Create array functions for the runtime.
 */
function createArrayFunctions(memory, callFunction) {
    return {
        /**
         * Get array length.
         */
        len(arr) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('len() requires an array');
            }
            const ptr = (0, values_1.asPointer)(arr);
            return (0, values_1.valInt)(memory.arrayCount(ptr));
        },
        /**
         * Push a value onto the array.
         */
        push(arr, value) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('push() requires an array');
            }
            const ptr = (0, values_1.asPointer)(arr);
            memory.arrayPush(ptr, value);
            return (0, values_1.valNil)();
        },
        /**
         * Pop a value from the array.
         */
        pop(arr) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('pop() requires an array');
            }
            const ptr = (0, values_1.asPointer)(arr);
            return memory.arrayPop(ptr);
        },
        /**
         * Get array element at index.
         */
        get(arr, index) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('Array access requires an array');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const idx = Math.trunc((0, values_1.asNumber)(index));
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
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('Array access requires an array');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const idx = Math.trunc((0, values_1.asNumber)(index));
            memory.arraySet(ptr, idx, value);
            return (0, values_1.valNil)();
        },
        /**
         * Get array slice.
         */
        slice(arr, start, end) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('slice() requires an array');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const count = memory.arrayCount(ptr);
            let startIdx = Math.trunc((0, values_1.asNumber)(start));
            let endIdx = end ? Math.trunc((0, values_1.asNumber)(end)) : count;
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
            return (0, values_1.valArray)(newArr);
        },
        /**
         * Reverse array in place.
         */
        reverse(arr) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('reverse() requires an array');
            }
            const ptr = (0, values_1.asPointer)(arr);
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
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('sort() requires an array');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const count = memory.arrayCount(ptr);
            // Extract to JS array for sorting
            const elements = [];
            for (let i = 0; i < count; i++) {
                elements.push({ idx: i, val: memory.arrayGet(ptr, i) });
            }
            // Sort
            if (comparator && (0, values_1.isFunction)(comparator)) {
                const funcIdx = (0, values_1.asPointer)(comparator);
                elements.sort((a, b) => {
                    const result = callFunction(funcIdx, a.val, b.val);
                    return (0, values_1.asNumber)(result);
                });
            }
            else {
                elements.sort((a, b) => {
                    const na = (0, values_1.asNumber)(a.val);
                    const nb = (0, values_1.asNumber)(b.val);
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
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('map() requires an array');
            }
            if (!(0, values_1.isFunction)(func)) {
                throw new Error('map() requires a function');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const funcIdx = (0, values_1.asPointer)(func);
            const count = memory.arrayCount(ptr);
            const newArr = memory.allocArray(count);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                const result = callFunction(funcIdx, elem, (0, values_1.valInt)(i));
                memory.arrayPush(newArr, result);
            }
            return (0, values_1.valArray)(newArr);
        },
        /**
         * Filter array by predicate.
         */
        filter(arr, predicate) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('filter() requires an array');
            }
            if (!(0, values_1.isFunction)(predicate)) {
                throw new Error('filter() requires a function');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const funcIdx = (0, values_1.asPointer)(predicate);
            const count = memory.arrayCount(ptr);
            const newArr = memory.allocArray(0);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                const result = callFunction(funcIdx, elem, (0, values_1.valInt)(i));
                if ((0, values_1.isTruthy)(result)) {
                    memory.arrayPush(newArr, elem);
                }
            }
            return (0, values_1.valArray)(newArr);
        },
        /**
         * Reduce array with accumulator.
         */
        reduce(arr, func, initial) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('reduce() requires an array');
            }
            if (!(0, values_1.isFunction)(func)) {
                throw new Error('reduce() requires a function');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const funcIdx = (0, values_1.asPointer)(func);
            const count = memory.arrayCount(ptr);
            let accumulator = initial;
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                accumulator = callFunction(funcIdx, accumulator, elem, (0, values_1.valInt)(i));
            }
            return accumulator;
        },
        /**
         * Find first element matching predicate.
         */
        find(arr, predicate) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('find() requires an array');
            }
            if (!(0, values_1.isFunction)(predicate)) {
                throw new Error('find() requires a function');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const funcIdx = (0, values_1.asPointer)(predicate);
            const count = memory.arrayCount(ptr);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                const result = callFunction(funcIdx, elem, (0, values_1.valInt)(i));
                if ((0, values_1.isTruthy)(result)) {
                    return elem;
                }
            }
            return (0, values_1.valNil)();
        },
        /**
         * Find index of first element matching predicate.
         */
        find_index(arr, predicate) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('find_index() requires an array');
            }
            if (!(0, values_1.isFunction)(predicate)) {
                throw new Error('find_index() requires a function');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const funcIdx = (0, values_1.asPointer)(predicate);
            const count = memory.arrayCount(ptr);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                const result = callFunction(funcIdx, elem, (0, values_1.valInt)(i));
                if ((0, values_1.isTruthy)(result)) {
                    return (0, values_1.valInt)(i);
                }
            }
            return (0, values_1.valInt)(-1);
        },
        /**
         * Check if any element matches predicate.
         */
        some(arr, predicate) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('some() requires an array');
            }
            if (!(0, values_1.isFunction)(predicate)) {
                throw new Error('some() requires a function');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const funcIdx = (0, values_1.asPointer)(predicate);
            const count = memory.arrayCount(ptr);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                const result = callFunction(funcIdx, elem, (0, values_1.valInt)(i));
                if ((0, values_1.isTruthy)(result)) {
                    return (0, values_1.valBool)(true);
                }
            }
            return (0, values_1.valBool)(false);
        },
        /**
         * Check if all elements match predicate.
         */
        every(arr, predicate) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('every() requires an array');
            }
            if (!(0, values_1.isFunction)(predicate)) {
                throw new Error('every() requires a function');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const funcIdx = (0, values_1.asPointer)(predicate);
            const count = memory.arrayCount(ptr);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                const result = callFunction(funcIdx, elem, (0, values_1.valInt)(i));
                if (!(0, values_1.isTruthy)(result)) {
                    return (0, values_1.valBool)(false);
                }
            }
            return (0, values_1.valBool)(true);
        },
        /**
         * Check if array includes a value.
         */
        includes(arr, value) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('includes() requires an array');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const count = memory.arrayCount(ptr);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                if ((0, values_1.valEquals)(elem, value)) {
                    return (0, values_1.valBool)(true);
                }
            }
            return (0, values_1.valBool)(false);
        },
        /**
         * Find index of value in array.
         */
        index_of(arr, value) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('index_of() requires an array');
            }
            const ptr = (0, values_1.asPointer)(arr);
            const count = memory.arrayCount(ptr);
            for (let i = 0; i < count; i++) {
                const elem = memory.arrayGet(ptr, i);
                if ((0, values_1.valEquals)(elem, value)) {
                    return (0, values_1.valInt)(i);
                }
            }
            return (0, values_1.valInt)(-1);
        },
        /**
         * Concatenate arrays.
         */
        concat(arr1, arr2) {
            if (!(0, values_1.isArray)(arr1) || !(0, values_1.isArray)(arr2)) {
                throw new Error('concat() requires arrays');
            }
            const ptr1 = (0, values_1.asPointer)(arr1);
            const ptr2 = (0, values_1.asPointer)(arr2);
            const count1 = memory.arrayCount(ptr1);
            const count2 = memory.arrayCount(ptr2);
            const newArr = memory.allocArray(count1 + count2);
            for (let i = 0; i < count1; i++) {
                memory.arrayPush(newArr, memory.arrayGet(ptr1, i));
            }
            for (let i = 0; i < count2; i++) {
                memory.arrayPush(newArr, memory.arrayGet(ptr2, i));
            }
            return (0, values_1.valArray)(newArr);
        },
        /**
         * Flatten nested arrays.
         */
        flatten(arr, depth = (0, values_1.valInt)(1)) {
            if (!(0, values_1.isArray)(arr)) {
                throw new Error('flatten() requires an array');
            }
            const maxDepth = Math.trunc((0, values_1.asNumber)(depth));
            function flattenHelper(ptr, currentDepth) {
                const count = memory.arrayCount(ptr);
                const result = [];
                for (let i = 0; i < count; i++) {
                    const elem = memory.arrayGet(ptr, i);
                    if ((0, values_1.isArray)(elem) && currentDepth < maxDepth) {
                        result.push(...flattenHelper((0, values_1.asPointer)(elem), currentDepth + 1));
                    }
                    else {
                        result.push(elem);
                    }
                }
                return result;
            }
            const flattened = flattenHelper((0, values_1.asPointer)(arr), 0);
            const newArr = memory.allocArray(flattened.length);
            for (const elem of flattened) {
                memory.arrayPush(newArr, elem);
            }
            return (0, values_1.valArray)(newArr);
        },
        /**
         * Create a range array.
         */
        range(start, end, step) {
            const startNum = Math.trunc((0, values_1.asNumber)(start));
            const endNum = Math.trunc((0, values_1.asNumber)(end));
            const stepNum = step ? Math.trunc((0, values_1.asNumber)(step)) : 1;
            if (stepNum === 0) {
                throw new Error('range() step cannot be zero');
            }
            const count = Math.max(0, Math.ceil((endNum - startNum) / stepNum));
            const newArr = memory.allocArray(count);
            for (let i = startNum; stepNum > 0 ? i < endNum : i > endNum; i += stepNum) {
                memory.arrayPush(newArr, (0, values_1.valInt)(i));
            }
            return (0, values_1.valArray)(newArr);
        },
        /**
         * Zip multiple arrays together.
         */
        zip(arr1, arr2) {
            if (!(0, values_1.isArray)(arr1) || !(0, values_1.isArray)(arr2)) {
                throw new Error('zip() requires arrays');
            }
            const ptr1 = (0, values_1.asPointer)(arr1);
            const ptr2 = (0, values_1.asPointer)(arr2);
            const count = Math.min(memory.arrayCount(ptr1), memory.arrayCount(ptr2));
            const newArr = memory.allocArray(count);
            for (let i = 0; i < count; i++) {
                const pair = memory.allocArray(2);
                memory.arrayPush(pair, memory.arrayGet(ptr1, i));
                memory.arrayPush(pair, memory.arrayGet(ptr2, i));
                memory.arrayPush(newArr, (0, values_1.valArray)(pair));
            }
            return (0, values_1.valArray)(newArr);
        },
    };
}
//# sourceMappingURL=array.js.map