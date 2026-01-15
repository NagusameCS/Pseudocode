import { Value } from './values';
export declare const OBJ_STRING = 0;
export declare const OBJ_ARRAY = 1;
export declare const OBJ_DICT = 2;
export declare const OBJ_CLOSURE = 3;
export declare const OBJ_INSTANCE = 4;
export declare const OBJ_GENERATOR = 5;
export declare const OBJ_BYTES = 6;
export declare const OBJ_HEADER_SIZE = 12;
/**
 * Object header structure stored at the beginning of each heap object.
 */
export interface ObjectHeader {
    type: number;
    size: number;
    marked: boolean;
}
/**
 * Memory manager for the WASM runtime.
 * Handles stack, heap, and string interning.
 */
export declare class Memory {
    private buffer;
    private view;
    private bytes;
    private heapStart;
    private heapPtr;
    private stringPoolStart;
    private stringPoolPtr;
    private stackTop;
    private stackPtr;
    private stringTable;
    private objects;
    private bytesAllocated;
    private nextGC;
    constructor(sizeBytes?: number);
    /**
     * Get the underlying ArrayBuffer for WASM access.
     */
    getBuffer(): ArrayBuffer;
    /**
     * Get raw byte access.
     */
    getBytes(): Uint8Array;
    /**
     * Push a value onto the stack.
     */
    stackPush(value: Value): void;
    /**
     * Pop a value from the stack.
     */
    stackPop(): Value;
    /**
     * Peek at the top of the stack without popping.
     */
    stackPeek(offset?: number): Value;
    /**
     * Get a value at a specific stack slot (from frame base).
     */
    stackGet(slot: number, frameBase: number): Value;
    /**
     * Set a value at a specific stack slot.
     */
    stackSet(slot: number, frameBase: number, value: Value): void;
    /**
     * Get current stack pointer.
     */
    getStackPtr(): number;
    /**
     * Set stack pointer (for function calls/returns).
     */
    setStackPtr(ptr: number): void;
    /**
     * Allocate memory on the heap.
     * Returns pointer to the usable area (after header).
     */
    alloc(type: number, dataSize: number): number;
    /**
     * Read object header from a data pointer.
     */
    getHeader(dataPtr: number): ObjectHeader;
    /**
     * Free an object (used internally by GC).
     */
    private free;
    /**
     * Allocate a new string (or return interned string).
     */
    allocString(str: string): Value;
    /**
     * Get a string from a string pointer.
     */
    getString(ptr: number): string;
    /**
     * Get string length from pointer.
     */
    getStringLength(ptr: number): number;
    /**
     * Allocate a new array.
     */
    allocArray(capacity: number): number;
    /**
     * Get array count.
     */
    arrayCount(ptr: number): number;
    /**
     * Get array capacity.
     */
    arrayCapacity(ptr: number): number;
    /**
     * Get array element.
     */
    arrayGet(ptr: number, index: number): Value;
    /**
     * Set array element.
     */
    arraySet(ptr: number, index: number, value: Value): void;
    /**
     * Push element to array.
     */
    arrayPush(ptr: number, value: Value): void;
    /**
     * Pop element from array.
     */
    arrayPop(ptr: number): Value;
    /**
     * Allocate a new dictionary.
     * Uses a JS Map internally for simplicity - the ptr is just an ID.
     */
    private dictStorage;
    private nextDictId;
    allocDict(capacity?: number): number;
    /**
     * Get value from dictionary.
     */
    dictGet(ptr: number, key: string): Value | undefined;
    /**
     * Set value in dictionary.
     */
    dictSet(ptr: number, key: string, value: Value): void;
    /**
     * Check if dictionary has key.
     */
    dictHas(ptr: number, key: string): boolean;
    /**
     * Delete key from dictionary.
     */
    dictDelete(ptr: number, key: string): boolean;
    /**
     * Get all keys from dictionary.
     */
    dictKeys(ptr: number): string[];
    /**
     * Get all values from dictionary.
     */
    dictValues(ptr: number): Value[];
    /**
     * Get all entries from dictionary.
     */
    dictEntries(ptr: number): [string, Value][];
    /**
     * Get dictionary size.
     */
    dictSize(ptr: number): number;
    /**
     * Clear dictionary.
     */
    dictClear(ptr: number): void;
    /**
     * Run garbage collection.
     */
    gc(): void;
    /**
     * Get memory statistics.
     */
    getStats(): {
        allocated: number;
        heapUsed: number;
        stackUsed: number;
    };
    readU8(addr: number): number;
    writeU8(addr: number, value: number): void;
    readU32(addr: number): number;
    writeU32(addr: number, value: number): void;
    readU64(addr: number): bigint;
    writeU64(addr: number, value: bigint): void;
    readF64(addr: number): number;
    writeF64(addr: number, value: number): void;
}
//# sourceMappingURL=memory.d.ts.map