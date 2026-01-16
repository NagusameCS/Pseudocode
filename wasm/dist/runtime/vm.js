"use strict";
/*
 * Pseudocode WASM Runtime - Virtual Machine
 *
 * This VM wraps a compiled WebAssembly module and provides:
 * - Memory management
 * - Standard library imports
 * - I/O operations
 * - Garbage collection integration
 */
Object.defineProperty(exports, "__esModule", { value: true });
exports.VM = void 0;
exports.createVM = createVM;
const memory_1 = require("./memory");
const values_1 = require("./values");
/**
 * The Pseudocode WASM Virtual Machine.
 */
class VM {
    memory;
    config;
    // WASM instance
    wasmInstance = null;
    wasmModule = null;
    // Execution state
    frames = [];
    globals = new Map();
    // I/O
    outputBuffer = [];
    constructor(config = {}) {
        this.config = {
            memorySize: 16 * 1024 * 1024,
            stackSize: 1024 * 1024,
            debug: false,
            stdout: console.log,
            stdin: async () => '',
            ...config
        };
        this.memory = new memory_1.Memory(this.config.memorySize);
    }
    /**
     * Load and instantiate a WASM module.
     */
    async loadModule(wasmBytes) {
        const imports = this.createImports();
        this.wasmModule = await WebAssembly.compile(wasmBytes.buffer);
        this.wasmInstance = await WebAssembly.instantiate(this.wasmModule, imports);
        if (this.config.debug) {
            console.log('WASM module loaded');
        }
    }
    /**
     * Load a WASM module from a URL (for browser use).
     */
    async loadModuleFromUrl(url) {
        const response = await fetch(url);
        const bytes = await response.arrayBuffer();
        await this.loadModule(new Uint8Array(bytes));
    }
    /**
     * Run the main function of the loaded module.
     */
    async run() {
        if (!this.wasmInstance) {
            throw new Error('No WASM module loaded');
        }
        const exports = this.wasmInstance.exports;
        // Look for main function
        const main = exports.main || exports._start;
        if (typeof main !== 'function') {
            throw new Error('No main function found in module');
        }
        try {
            const result = main();
            return typeof result === 'bigint' ? result : (0, values_1.valNil)();
        }
        catch (error) {
            if (this.config.debug) {
                console.error('Runtime error:', error);
            }
            throw error;
        }
    }
    /**
     * Call an exported function by name.
     */
    async call(funcName, ...args) {
        if (!this.wasmInstance) {
            throw new Error('No WASM module loaded');
        }
        const func = this.wasmInstance.exports[funcName];
        if (typeof func !== 'function') {
            throw new Error(`Function '${funcName}' not found`);
        }
        const result = func(...args);
        return typeof result === 'bigint' ? result : (0, values_1.valNil)();
    }
    /**
     * Get the memory manager.
     */
    getMemory() {
        return this.memory;
    }
    /**
     * Get global variable.
     */
    getGlobal(name) {
        return this.globals.get(name) ?? (0, values_1.valNil)();
    }
    /**
     * Set global variable.
     */
    setGlobal(name, value) {
        this.globals.set(name, value);
    }
    /**
     * Create the import object for WASM instantiation.
     * This provides all the runtime functions the compiled code needs.
     */
    createImports() {
        const memory = this.memory;
        const vm = this;
        return {
            env: {
                // Memory
                memory: new WebAssembly.Memory({ initial: 256, maximum: 2048 }),
                // ============ Value Operations ============
                value_add: (a, b) => {
                    if ((0, values_1.isInt)(a) && (0, values_1.isInt)(b)) {
                        return (0, values_1.valInt)((0, values_1.asNumber)(a) + (0, values_1.asNumber)(b));
                    }
                    if (((0, values_1.isInt)(a) || (0, values_1.isFloat)(a)) && ((0, values_1.isInt)(b) || (0, values_1.isFloat)(b))) {
                        return (0, values_1.valFloat)((0, values_1.asNumber)(a) + (0, values_1.asNumber)(b));
                    }
                    if ((0, values_1.isString)(a) && (0, values_1.isString)(b)) {
                        const sa = memory.getString((0, values_1.asPointer)(a));
                        const sb = memory.getString((0, values_1.asPointer)(b));
                        return memory.allocString(sa + sb);
                    }
                    throw new Error(`Cannot add ${(0, values_1.typeName)(a)} and ${(0, values_1.typeName)(b)}`);
                },
                value_sub: (a, b) => {
                    return (0, values_1.valFloat)((0, values_1.asNumber)(a) - (0, values_1.asNumber)(b));
                },
                value_mul: (a, b) => {
                    return (0, values_1.valFloat)((0, values_1.asNumber)(a) * (0, values_1.asNumber)(b));
                },
                value_div: (a, b) => {
                    const divisor = (0, values_1.asNumber)(b);
                    if (divisor === 0) {
                        throw new Error('Division by zero');
                    }
                    return (0, values_1.valFloat)((0, values_1.asNumber)(a) / divisor);
                },
                value_mod: (a, b) => {
                    return (0, values_1.valFloat)((0, values_1.asNumber)(a) % (0, values_1.asNumber)(b));
                },
                value_neg: (a) => {
                    return (0, values_1.valFloat)(-(0, values_1.asNumber)(a));
                },
                value_eq: (a, b) => {
                    if ((0, values_1.isInt)(a) && (0, values_1.isInt)(b)) {
                        return (0, values_1.valBool)((0, values_1.asNumber)(a) === (0, values_1.asNumber)(b));
                    }
                    if (((0, values_1.isInt)(a) || (0, values_1.isFloat)(a)) && ((0, values_1.isInt)(b) || (0, values_1.isFloat)(b))) {
                        return (0, values_1.valBool)((0, values_1.asNumber)(a) === (0, values_1.asNumber)(b));
                    }
                    if ((0, values_1.isString)(a) && (0, values_1.isString)(b)) {
                        const sa = memory.getString((0, values_1.asPointer)(a));
                        const sb = memory.getString((0, values_1.asPointer)(b));
                        return (0, values_1.valBool)(sa === sb);
                    }
                    return (0, values_1.valBool)(a === b);
                },
                value_lt: (a, b) => {
                    return (0, values_1.valBool)((0, values_1.asNumber)(a) < (0, values_1.asNumber)(b));
                },
                value_gt: (a, b) => {
                    return (0, values_1.valBool)((0, values_1.asNumber)(a) > (0, values_1.asNumber)(b));
                },
                value_lte: (a, b) => {
                    return (0, values_1.valBool)((0, values_1.asNumber)(a) <= (0, values_1.asNumber)(b));
                },
                value_gte: (a, b) => {
                    return (0, values_1.valBool)((0, values_1.asNumber)(a) >= (0, values_1.asNumber)(b));
                },
                value_not: (a) => {
                    return (0, values_1.valBool)(!(0, values_1.isTruthy)(a));
                },
                value_truthy: (a) => {
                    return (0, values_1.isTruthy)(a) ? 1 : 0;
                },
                // ============ I/O Functions ============
                print: (value) => {
                    const str = (0, values_1.valToString)(value, memory);
                    vm.config.stdout(str);
                },
                println: (value) => {
                    const str = (0, values_1.valToString)(value, memory);
                    vm.config.stdout(str + '\n');
                },
                print_string: (ptr) => {
                    const str = memory.getString(ptr);
                    vm.config.stdout(str);
                },
                // ============ String Functions ============
                string_new: (ptr, len) => {
                    const bytes = memory.getBytes().slice(ptr, ptr + len);
                    const str = new TextDecoder().decode(bytes);
                    return memory.allocString(str);
                },
                string_len: (ptr) => {
                    return memory.getStringLength(ptr);
                },
                string_concat: (a, b) => {
                    const sa = memory.getString(a);
                    const sb = memory.getString(b);
                    return memory.allocString(sa + sb);
                },
                // ============ Array Functions ============
                array_new: (capacity) => {
                    return memory.allocArray(capacity);
                },
                array_len: (ptr) => {
                    return memory.arrayCount(ptr);
                },
                array_get: (ptr, index) => {
                    return memory.arrayGet(ptr, index);
                },
                array_set: (ptr, index, value) => {
                    memory.arraySet(ptr, index, value);
                },
                array_push: (ptr, value) => {
                    memory.arrayPush(ptr, value);
                },
                array_pop: (ptr) => {
                    return memory.arrayPop(ptr);
                },
                // ============ Math Functions ============
                math_sqrt: (x) => Math.sqrt(x),
                math_sin: (x) => Math.sin(x),
                math_cos: (x) => Math.cos(x),
                math_tan: (x) => Math.tan(x),
                math_floor: (x) => Math.floor(x),
                math_ceil: (x) => Math.ceil(x),
                math_round: (x) => Math.round(x),
                math_abs: (x) => Math.abs(x),
                math_pow: (x, y) => Math.pow(x, y),
                math_log: (x) => Math.log(x),
                math_exp: (x) => Math.exp(x),
                math_random: () => Math.random(),
                // ============ Type Functions ============
                type_of: (value) => {
                    return memory.allocString((0, values_1.typeName)(value));
                },
                to_int: (value) => {
                    return (0, values_1.valInt)(Math.trunc((0, values_1.asNumber)(value)));
                },
                to_float: (value) => {
                    return (0, values_1.valFloat)((0, values_1.asNumber)(value));
                },
                to_string: (value) => {
                    return memory.allocString((0, values_1.valToString)(value, memory));
                },
                // ============ Memory Functions ============
                gc_collect: () => {
                    memory.gc();
                },
                // ============ Error Handling ============
                runtime_error: (msgPtr) => {
                    const msg = memory.getString(msgPtr);
                    throw new Error(`Runtime error: ${msg}`);
                },
            },
            // WASI-like interface for basic I/O
            wasi_snapshot_preview1: {
                fd_write: (fd, iovs, iovs_len, nwritten) => {
                    // Simplified fd_write for stdout
                    if (fd !== 1)
                        return 1; // Only stdout supported
                    let totalWritten = 0;
                    const view = new DataView(memory.getBuffer());
                    for (let i = 0; i < iovs_len; i++) {
                        const ptr = view.getUint32(iovs + i * 8, true);
                        const len = view.getUint32(iovs + i * 8 + 4, true);
                        const bytes = memory.getBytes().slice(ptr, ptr + len);
                        const str = new TextDecoder().decode(bytes);
                        vm.config.stdout(str);
                        totalWritten += len;
                    }
                    view.setUint32(nwritten, totalWritten, true);
                    return 0;
                },
                fd_close: (_fd) => 0,
                fd_seek: (_fd, _offset, _whence, _newoffset) => 0,
                proc_exit: (code) => {
                    throw new Error(`Process exit with code ${code}`);
                },
            }
        };
    }
    /**
     * Get collected output.
     */
    getOutput() {
        return this.outputBuffer.join('');
    }
    /**
     * Clear output buffer.
     */
    clearOutput() {
        this.outputBuffer = [];
    }
    /**
     * Get memory statistics.
     */
    getStats() {
        return {
            memory: this.memory.getStats()
        };
    }
}
exports.VM = VM;
/**
 * Create and configure a new VM instance.
 */
function createVM(config) {
    return new VM(config);
}
//# sourceMappingURL=vm.js.map