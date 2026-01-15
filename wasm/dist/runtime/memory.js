/*
 * Pseudocode WASM Runtime - Memory Management
 *
 * Manages linear memory for the WASM runtime:
 * - Stack region: for local variables and expression evaluation
 * - Heap region: for dynamically allocated objects
 * - String pool: interned strings
 *
 * Memory Layout:
 * ┌─────────────────────────────────────────────────────┐
 * │                  Stack (grows down)                  │
 * │  [top of memory] ◄─── stack_ptr                     │
 * ├─────────────────────────────────────────────────────┤
 * │                    Free Space                        │
 * ├─────────────────────────────────────────────────────┤
 * │                  Heap (grows up)                     │
 * │  [heap_start] ──► heap_ptr                          │
 * ├─────────────────────────────────────────────────────┤
 * │               String Pool (fixed)                    │
 * │  [string_pool_start]                                │
 * ├─────────────────────────────────────────────────────┤
 * │             Constant Pool (fixed)                    │
 * │  [0]                                                │
 * └─────────────────────────────────────────────────────┘
 */
import { valString } from './values';
// Object type tags for heap objects
export const OBJ_STRING = 0;
export const OBJ_ARRAY = 1;
export const OBJ_DICT = 2;
export const OBJ_CLOSURE = 3;
export const OBJ_INSTANCE = 4;
export const OBJ_GENERATOR = 5;
export const OBJ_BYTES = 6;
// Object header size (type tag + size + GC mark)
export const OBJ_HEADER_SIZE = 12; // 4 + 4 + 4 bytes
// Memory region sizes (in bytes)
const DEFAULT_MEMORY_SIZE = 16 * 1024 * 1024; // 16 MB
const STACK_SIZE = 1 * 1024 * 1024; // 1 MB
const STRING_POOL_SIZE = 1 * 1024 * 1024; // 1 MB
/**
 * Memory manager for the WASM runtime.
 * Handles stack, heap, and string interning.
 */
export class Memory {
    buffer;
    view;
    bytes;
    // Memory region pointers
    heapStart;
    heapPtr;
    stringPoolStart;
    stringPoolPtr;
    stackTop;
    stackPtr;
    // String interning table
    stringTable = new Map();
    // GC tracking
    objects = new Set();
    bytesAllocated = 0;
    nextGC = 1024 * 1024; // 1 MB
    constructor(sizeBytes = DEFAULT_MEMORY_SIZE) {
        this.buffer = new ArrayBuffer(sizeBytes);
        this.view = new DataView(this.buffer);
        this.bytes = new Uint8Array(this.buffer);
        // Initialize memory regions
        // Layout: [const pool | string pool | heap | ... | stack]
        const constPoolSize = 4096; // 4 KB for constants
        this.stringPoolStart = constPoolSize;
        this.stringPoolPtr = this.stringPoolStart;
        this.heapStart = this.stringPoolStart + STRING_POOL_SIZE;
        this.heapPtr = this.heapStart;
        this.stackTop = sizeBytes;
        this.stackPtr = this.stackTop;
    }
    /**
     * Get the underlying ArrayBuffer for WASM access.
     */
    getBuffer() {
        return this.buffer;
    }
    /**
     * Get raw byte access.
     */
    getBytes() {
        return this.bytes;
    }
    // ============ Stack Operations ============
    /**
     * Push a value onto the stack.
     */
    stackPush(value) {
        this.stackPtr -= 8;
        if (this.stackPtr < this.heapPtr) {
            throw new Error('Stack overflow');
        }
        this.view.setBigUint64(this.stackPtr, value, true);
    }
    /**
     * Pop a value from the stack.
     */
    stackPop() {
        if (this.stackPtr >= this.stackTop) {
            throw new Error('Stack underflow');
        }
        const value = this.view.getBigUint64(this.stackPtr, true);
        this.stackPtr += 8;
        return value;
    }
    /**
     * Peek at the top of the stack without popping.
     */
    stackPeek(offset = 0) {
        const addr = this.stackPtr + (offset * 8);
        if (addr >= this.stackTop) {
            throw new Error('Stack underflow');
        }
        return this.view.getBigUint64(addr, true);
    }
    /**
     * Get a value at a specific stack slot (from frame base).
     */
    stackGet(slot, frameBase) {
        const addr = frameBase - ((slot + 1) * 8);
        return this.view.getBigUint64(addr, true);
    }
    /**
     * Set a value at a specific stack slot.
     */
    stackSet(slot, frameBase, value) {
        const addr = frameBase - ((slot + 1) * 8);
        this.view.setBigUint64(addr, value, true);
    }
    /**
     * Get current stack pointer.
     */
    getStackPtr() {
        return this.stackPtr;
    }
    /**
     * Set stack pointer (for function calls/returns).
     */
    setStackPtr(ptr) {
        this.stackPtr = ptr;
    }
    // ============ Heap Allocation ============
    /**
     * Allocate memory on the heap.
     * Returns pointer to the usable area (after header).
     */
    alloc(type, dataSize) {
        const totalSize = OBJ_HEADER_SIZE + dataSize;
        const aligned = (totalSize + 7) & ~7; // 8-byte alignment
        // Check if we need GC
        if (this.bytesAllocated + aligned > this.nextGC) {
            this.gc();
        }
        // Check if we have space
        if (this.heapPtr + aligned > this.stackPtr) {
            // Try GC
            this.gc();
            if (this.heapPtr + aligned > this.stackPtr) {
                throw new Error('Out of memory');
            }
        }
        const ptr = this.heapPtr;
        this.heapPtr += aligned;
        this.bytesAllocated += aligned;
        // Write object header
        this.view.setUint32(ptr, type, true); // type
        this.view.setUint32(ptr + 4, aligned, true); // size
        this.view.setUint32(ptr + 8, 0, true); // marked = false
        // Track object for GC
        this.objects.add(ptr);
        // Return pointer to data area
        return ptr + OBJ_HEADER_SIZE;
    }
    /**
     * Read object header from a data pointer.
     */
    getHeader(dataPtr) {
        const headerPtr = dataPtr - OBJ_HEADER_SIZE;
        return {
            type: this.view.getUint32(headerPtr, true),
            size: this.view.getUint32(headerPtr + 4, true),
            marked: this.view.getUint32(headerPtr + 8, true) !== 0
        };
    }
    /**
     * Free an object (used internally by GC).
     */
    free(headerPtr) {
        // For now, we just track it. Compacting GC would reclaim space.
        this.objects.delete(headerPtr);
    }
    // ============ String Operations ============
    /**
     * Allocate a new string (or return interned string).
     */
    allocString(str) {
        // Check if already interned
        const existing = this.stringTable.get(str);
        if (existing !== undefined) {
            return valString(existing);
        }
        // Encode string as UTF-8
        const encoder = new TextEncoder();
        const encoded = encoder.encode(str);
        // Allocate in string pool or heap
        let ptr;
        const strSize = 4 + encoded.length + 1; // length + data + null
        if (this.stringPoolPtr + strSize <= this.heapStart) {
            // Allocate in string pool (never collected)
            ptr = this.stringPoolPtr;
            this.stringPoolPtr += (strSize + 7) & ~7; // 8-byte align
        }
        else {
            // Allocate on heap
            ptr = this.alloc(OBJ_STRING, strSize) - OBJ_HEADER_SIZE;
        }
        // Write string: [length: u32][data: bytes][null: u8]
        const dataPtr = ptr + (ptr < this.heapStart ? 0 : OBJ_HEADER_SIZE);
        this.view.setUint32(dataPtr, encoded.length, true);
        this.bytes.set(encoded, dataPtr + 4);
        this.bytes[dataPtr + 4 + encoded.length] = 0;
        // Intern the string
        const valuePtr = dataPtr;
        this.stringTable.set(str, valuePtr);
        return valString(valuePtr);
    }
    /**
     * Get a string from a string pointer.
     */
    getString(ptr) {
        const length = this.view.getUint32(ptr, true);
        const decoder = new TextDecoder();
        return decoder.decode(this.bytes.slice(ptr + 4, ptr + 4 + length));
    }
    /**
     * Get string length from pointer.
     */
    getStringLength(ptr) {
        return this.view.getUint32(ptr, true);
    }
    // ============ Array Operations ============
    /**
     * Allocate a new array.
     */
    allocArray(capacity) {
        // Array layout: [count: u32][capacity: u32][values: Value[]]
        const dataSize = 8 + (capacity * 8);
        const ptr = this.alloc(OBJ_ARRAY, dataSize);
        this.view.setUint32(ptr, 0, true); // count = 0
        this.view.setUint32(ptr + 4, capacity, true); // capacity
        return ptr;
    }
    /**
     * Get array count.
     */
    arrayCount(ptr) {
        return this.view.getUint32(ptr, true);
    }
    /**
     * Get array capacity.
     */
    arrayCapacity(ptr) {
        return this.view.getUint32(ptr + 4, true);
    }
    /**
     * Get array element.
     */
    arrayGet(ptr, index) {
        const count = this.arrayCount(ptr);
        if (index < 0 || index >= count) {
            throw new Error(`Array index out of bounds: ${index}`);
        }
        return this.view.getBigUint64(ptr + 8 + (index * 8), true);
    }
    /**
     * Set array element.
     */
    arraySet(ptr, index, value) {
        const count = this.arrayCount(ptr);
        if (index < 0 || index >= count) {
            throw new Error(`Array index out of bounds: ${index}`);
        }
        this.view.setBigUint64(ptr + 8 + (index * 8), value, true);
    }
    /**
     * Push element to array.
     */
    arrayPush(ptr, value) {
        const count = this.arrayCount(ptr);
        const capacity = this.arrayCapacity(ptr);
        if (count >= capacity) {
            throw new Error('Array is full (resize not yet implemented)');
        }
        this.view.setBigUint64(ptr + 8 + (count * 8), value, true);
        this.view.setUint32(ptr, count + 1, true);
    }
    /**
     * Pop element from array.
     */
    arrayPop(ptr) {
        const count = this.arrayCount(ptr);
        if (count === 0) {
            throw new Error('Cannot pop from empty array');
        }
        const value = this.view.getBigUint64(ptr + 8 + ((count - 1) * 8), true);
        this.view.setUint32(ptr, count - 1, true);
        return value;
    }
    // ============ Dictionary Operations ============
    /**
     * Allocate a new dictionary.
     * Uses a JS Map internally for simplicity - the ptr is just an ID.
     */
    dictStorage = new Map();
    nextDictId = 1;
    allocDict(capacity = 16) {
        const id = this.nextDictId++;
        this.dictStorage.set(id, new Map());
        return id;
    }
    /**
     * Get value from dictionary.
     */
    dictGet(ptr, key) {
        const dict = this.dictStorage.get(ptr);
        if (!dict) {
            throw new Error(`Invalid dictionary pointer: ${ptr}`);
        }
        return dict.get(key);
    }
    /**
     * Set value in dictionary.
     */
    dictSet(ptr, key, value) {
        const dict = this.dictStorage.get(ptr);
        if (!dict) {
            throw new Error(`Invalid dictionary pointer: ${ptr}`);
        }
        dict.set(key, value);
    }
    /**
     * Check if dictionary has key.
     */
    dictHas(ptr, key) {
        const dict = this.dictStorage.get(ptr);
        if (!dict) {
            throw new Error(`Invalid dictionary pointer: ${ptr}`);
        }
        return dict.has(key);
    }
    /**
     * Delete key from dictionary.
     */
    dictDelete(ptr, key) {
        const dict = this.dictStorage.get(ptr);
        if (!dict) {
            throw new Error(`Invalid dictionary pointer: ${ptr}`);
        }
        return dict.delete(key);
    }
    /**
     * Get all keys from dictionary.
     */
    dictKeys(ptr) {
        const dict = this.dictStorage.get(ptr);
        if (!dict) {
            throw new Error(`Invalid dictionary pointer: ${ptr}`);
        }
        return Array.from(dict.keys());
    }
    /**
     * Get all values from dictionary.
     */
    dictValues(ptr) {
        const dict = this.dictStorage.get(ptr);
        if (!dict) {
            throw new Error(`Invalid dictionary pointer: ${ptr}`);
        }
        return Array.from(dict.values());
    }
    /**
     * Get all entries from dictionary.
     */
    dictEntries(ptr) {
        const dict = this.dictStorage.get(ptr);
        if (!dict) {
            throw new Error(`Invalid dictionary pointer: ${ptr}`);
        }
        return Array.from(dict.entries());
    }
    /**
     * Get dictionary size.
     */
    dictSize(ptr) {
        const dict = this.dictStorage.get(ptr);
        if (!dict) {
            throw new Error(`Invalid dictionary pointer: ${ptr}`);
        }
        return dict.size;
    }
    /**
     * Clear dictionary.
     */
    dictClear(ptr) {
        const dict = this.dictStorage.get(ptr);
        if (!dict) {
            throw new Error(`Invalid dictionary pointer: ${ptr}`);
        }
        dict.clear();
    }
    // ============ Garbage Collection ============
    /**
     * Run garbage collection.
     */
    gc() {
        // Mark phase - mark all reachable objects
        // (In full implementation, would trace from roots)
        // For now, we don't actually collect since we need
        // proper root tracking. Just grow the GC threshold.
        this.nextGC *= 2;
    }
    /**
     * Get memory statistics.
     */
    getStats() {
        return {
            allocated: this.bytesAllocated,
            heapUsed: this.heapPtr - this.heapStart,
            stackUsed: this.stackTop - this.stackPtr
        };
    }
    // ============ Raw Memory Access ============
    readU8(addr) {
        return this.bytes[addr];
    }
    writeU8(addr, value) {
        this.bytes[addr] = value;
    }
    readU32(addr) {
        return this.view.getUint32(addr, true);
    }
    writeU32(addr, value) {
        this.view.setUint32(addr, value, true);
    }
    readU64(addr) {
        return this.view.getBigUint64(addr, true);
    }
    writeU64(addr, value) {
        this.view.setBigUint64(addr, value, true);
    }
    readF64(addr) {
        return this.view.getFloat64(addr, true);
    }
    writeF64(addr, value) {
        this.view.setFloat64(addr, value, true);
    }
}
//# sourceMappingURL=memory.js.map