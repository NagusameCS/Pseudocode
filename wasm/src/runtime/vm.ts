/*
 * Pseudocode WASM Runtime - Virtual Machine
 * 
 * This VM wraps a compiled WebAssembly module and provides:
 * - Memory management
 * - Standard library imports
 * - I/O operations
 * - Garbage collection integration
 */

import { Memory } from './memory';
import {
    Value, valNil, valBool, valInt, valFloat, valString,
    isNil, isBool, isInt, isFloat, isString, isArray, isObject, isFunction,
    asNumber, asBool, asPointer, isTruthy, valToString, typeName
} from './values';

/**
 * Configuration for the VM.
 */
export interface VMConfig {
    memorySize?: number;      // Memory size in bytes (default: 16MB)
    stackSize?: number;       // Stack size in bytes (default: 1MB)
    debug?: boolean;          // Enable debug output
    stdout?: (msg: string) => void;  // Custom stdout
    stdin?: () => Promise<string>;   // Custom stdin
}

/**
 * Call frame for function calls.
 */
interface CallFrame {
    funcIndex: number;    // Function table index
    returnAddr: number;   // Return instruction pointer
    frameBase: number;    // Stack frame base
    localCount: number;   // Number of locals
}

/**
 * The Pseudocode WASM Virtual Machine.
 */
export class VM {
    private memory: Memory;
    private config: VMConfig;
    
    // WASM instance
    private wasmInstance: WebAssembly.Instance | null = null;
    private wasmModule: WebAssembly.Module | null = null;
    
    // Execution state
    private frames: CallFrame[] = [];
    private globals: Map<string, Value> = new Map();
    
    // I/O
    private outputBuffer: string[] = [];
    
    constructor(config: VMConfig = {}) {
        this.config = {
            memorySize: 16 * 1024 * 1024,
            stackSize: 1024 * 1024,
            debug: false,
            stdout: console.log,
            stdin: async () => '',
            ...config
        };
        
        this.memory = new Memory(this.config.memorySize);
    }
    
    /**
     * Load and instantiate a WASM module.
     */
    async loadModule(wasmBytes: Uint8Array): Promise<void> {
        const imports = this.createImports();
        
        this.wasmModule = await WebAssembly.compile(wasmBytes.buffer as ArrayBuffer);
        this.wasmInstance = await WebAssembly.instantiate(this.wasmModule, imports);
        
        if (this.config.debug) {
            console.log('WASM module loaded');
        }
    }
    
    /**
     * Load a WASM module from a URL (for browser use).
     */
    async loadModuleFromUrl(url: string): Promise<void> {
        const response = await fetch(url);
        const bytes = await response.arrayBuffer();
        await this.loadModule(new Uint8Array(bytes));
    }
    
    /**
     * Run the main function of the loaded module.
     */
    async run(): Promise<Value> {
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
            return typeof result === 'bigint' ? result : valNil();
        } catch (error) {
            if (this.config.debug) {
                console.error('Runtime error:', error);
            }
            throw error;
        }
    }
    
    /**
     * Call an exported function by name.
     */
    async call(funcName: string, ...args: Value[]): Promise<Value> {
        if (!this.wasmInstance) {
            throw new Error('No WASM module loaded');
        }
        
        const func = this.wasmInstance.exports[funcName];
        if (typeof func !== 'function') {
            throw new Error(`Function '${funcName}' not found`);
        }
        
        const result = func(...args);
        return typeof result === 'bigint' ? result : valNil();
    }
    
    /**
     * Get the memory manager.
     */
    getMemory(): Memory {
        return this.memory;
    }
    
    /**
     * Get global variable.
     */
    getGlobal(name: string): Value {
        return this.globals.get(name) ?? valNil();
    }
    
    /**
     * Set global variable.
     */
    setGlobal(name: string, value: Value): void {
        this.globals.set(name, value);
    }
    
    /**
     * Create the import object for WASM instantiation.
     * This provides all the runtime functions the compiled code needs.
     */
    private createImports(): WebAssembly.Imports {
        const memory = this.memory;
        const vm = this;
        
        return {
            env: {
                // Memory
                memory: new WebAssembly.Memory({ initial: 256, maximum: 2048 }),
                
                // ============ Value Operations ============
                
                value_add: (a: bigint, b: bigint): bigint => {
                    if (isInt(a) && isInt(b)) {
                        return valInt(asNumber(a) + asNumber(b));
                    }
                    if ((isInt(a) || isFloat(a)) && (isInt(b) || isFloat(b))) {
                        return valFloat(asNumber(a) + asNumber(b));
                    }
                    if (isString(a) && isString(b)) {
                        const sa = memory.getString(asPointer(a));
                        const sb = memory.getString(asPointer(b));
                        return memory.allocString(sa + sb);
                    }
                    throw new Error(`Cannot add ${typeName(a)} and ${typeName(b)}`);
                },
                
                value_sub: (a: bigint, b: bigint): bigint => {
                    return valFloat(asNumber(a) - asNumber(b));
                },
                
                value_mul: (a: bigint, b: bigint): bigint => {
                    return valFloat(asNumber(a) * asNumber(b));
                },
                
                value_div: (a: bigint, b: bigint): bigint => {
                    const divisor = asNumber(b);
                    if (divisor === 0) {
                        throw new Error('Division by zero');
                    }
                    return valFloat(asNumber(a) / divisor);
                },
                
                value_mod: (a: bigint, b: bigint): bigint => {
                    return valFloat(asNumber(a) % asNumber(b));
                },
                
                value_neg: (a: bigint): bigint => {
                    return valFloat(-asNumber(a));
                },
                
                value_eq: (a: bigint, b: bigint): bigint => {
                    if (isInt(a) && isInt(b)) {
                        return valBool(asNumber(a) === asNumber(b));
                    }
                    if ((isInt(a) || isFloat(a)) && (isInt(b) || isFloat(b))) {
                        return valBool(asNumber(a) === asNumber(b));
                    }
                    if (isString(a) && isString(b)) {
                        const sa = memory.getString(asPointer(a));
                        const sb = memory.getString(asPointer(b));
                        return valBool(sa === sb);
                    }
                    return valBool(a === b);
                },
                
                value_lt: (a: bigint, b: bigint): bigint => {
                    return valBool(asNumber(a) < asNumber(b));
                },
                
                value_gt: (a: bigint, b: bigint): bigint => {
                    return valBool(asNumber(a) > asNumber(b));
                },
                
                value_lte: (a: bigint, b: bigint): bigint => {
                    return valBool(asNumber(a) <= asNumber(b));
                },
                
                value_gte: (a: bigint, b: bigint): bigint => {
                    return valBool(asNumber(a) >= asNumber(b));
                },
                
                value_not: (a: bigint): bigint => {
                    return valBool(!isTruthy(a));
                },
                
                value_truthy: (a: bigint): number => {
                    return isTruthy(a) ? 1 : 0;
                },
                
                // ============ I/O Functions ============
                
                print: (value: bigint): void => {
                    const str = valToString(value, memory);
                    vm.config.stdout!(str);
                },
                
                println: (value: bigint): void => {
                    const str = valToString(value, memory);
                    vm.config.stdout!(str + '\n');
                },
                
                print_string: (ptr: number): void => {
                    const str = memory.getString(ptr);
                    vm.config.stdout!(str);
                },
                
                // ============ String Functions ============
                
                string_new: (ptr: number, len: number): bigint => {
                    const bytes = memory.getBytes().slice(ptr, ptr + len);
                    const str = new TextDecoder().decode(bytes);
                    return memory.allocString(str);
                },
                
                string_len: (ptr: number): number => {
                    return memory.getStringLength(ptr);
                },
                
                string_concat: (a: number, b: number): bigint => {
                    const sa = memory.getString(a);
                    const sb = memory.getString(b);
                    return memory.allocString(sa + sb);
                },
                
                // ============ Array Functions ============
                
                array_new: (capacity: number): number => {
                    return memory.allocArray(capacity);
                },
                
                array_len: (ptr: number): number => {
                    return memory.arrayCount(ptr);
                },
                
                array_get: (ptr: number, index: number): bigint => {
                    return memory.arrayGet(ptr, index);
                },
                
                array_set: (ptr: number, index: number, value: bigint): void => {
                    memory.arraySet(ptr, index, value);
                },
                
                array_push: (ptr: number, value: bigint): void => {
                    memory.arrayPush(ptr, value);
                },
                
                array_pop: (ptr: number): bigint => {
                    return memory.arrayPop(ptr);
                },
                
                // ============ Math Functions ============
                
                math_sqrt: (x: number): number => Math.sqrt(x),
                math_sin: (x: number): number => Math.sin(x),
                math_cos: (x: number): number => Math.cos(x),
                math_tan: (x: number): number => Math.tan(x),
                math_floor: (x: number): number => Math.floor(x),
                math_ceil: (x: number): number => Math.ceil(x),
                math_round: (x: number): number => Math.round(x),
                math_abs: (x: number): number => Math.abs(x),
                math_pow: (x: number, y: number): number => Math.pow(x, y),
                math_log: (x: number): number => Math.log(x),
                math_exp: (x: number): number => Math.exp(x),
                math_random: (): number => Math.random(),
                
                // ============ Type Functions ============
                
                type_of: (value: bigint): bigint => {
                    return memory.allocString(typeName(value));
                },
                
                to_int: (value: bigint): bigint => {
                    return valInt(Math.trunc(asNumber(value)));
                },
                
                to_float: (value: bigint): bigint => {
                    return valFloat(asNumber(value));
                },
                
                to_string: (value: bigint): bigint => {
                    return memory.allocString(valToString(value, memory));
                },
                
                // ============ Memory Functions ============
                
                gc_collect: (): void => {
                    memory.gc();
                },
                
                // ============ Error Handling ============
                
                runtime_error: (msgPtr: number): void => {
                    const msg = memory.getString(msgPtr);
                    throw new Error(`Runtime error: ${msg}`);
                },
            },
            
            // WASI-like interface for basic I/O
            wasi_snapshot_preview1: {
                fd_write: (fd: number, iovs: number, iovs_len: number, nwritten: number): number => {
                    // Simplified fd_write for stdout
                    if (fd !== 1) return 1; // Only stdout supported
                    
                    let totalWritten = 0;
                    const view = new DataView(memory.getBuffer());
                    
                    for (let i = 0; i < iovs_len; i++) {
                        const ptr = view.getUint32(iovs + i * 8, true);
                        const len = view.getUint32(iovs + i * 8 + 4, true);
                        const bytes = memory.getBytes().slice(ptr, ptr + len);
                        const str = new TextDecoder().decode(bytes);
                        vm.config.stdout!(str);
                        totalWritten += len;
                    }
                    
                    view.setUint32(nwritten, totalWritten, true);
                    return 0;
                },
                
                fd_close: (_fd: number): number => 0,
                fd_seek: (_fd: number, _offset: bigint, _whence: number, _newoffset: number): number => 0,
                proc_exit: (code: number): void => {
                    throw new Error(`Process exit with code ${code}`);
                },
            }
        };
    }
    
    /**
     * Get collected output.
     */
    getOutput(): string {
        return this.outputBuffer.join('');
    }
    
    /**
     * Clear output buffer.
     */
    clearOutput(): void {
        this.outputBuffer = [];
    }
    
    /**
     * Get memory statistics.
     */
    getStats(): { memory: ReturnType<Memory['getStats']> } {
        return {
            memory: this.memory.getStats()
        };
    }
}

/**
 * Create and configure a new VM instance.
 */
export function createVM(config?: VMConfig): VM {
    return new VM(config);
}
