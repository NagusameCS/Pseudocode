import { Memory } from './memory';
import { Value } from './values';
/**
 * Configuration for the VM.
 */
export interface VMConfig {
    memorySize?: number;
    stackSize?: number;
    debug?: boolean;
    stdout?: (msg: string) => void;
    stdin?: () => Promise<string>;
}
/**
 * The Pseudocode WASM Virtual Machine.
 */
export declare class VM {
    private memory;
    private config;
    private wasmInstance;
    private wasmModule;
    private frames;
    private globals;
    private outputBuffer;
    constructor(config?: VMConfig);
    /**
     * Load and instantiate a WASM module.
     */
    loadModule(wasmBytes: Uint8Array): Promise<void>;
    /**
     * Load a WASM module from a URL (for browser use).
     */
    loadModuleFromUrl(url: string): Promise<void>;
    /**
     * Run the main function of the loaded module.
     */
    run(): Promise<Value>;
    /**
     * Call an exported function by name.
     */
    call(funcName: string, ...args: Value[]): Promise<Value>;
    /**
     * Get the memory manager.
     */
    getMemory(): Memory;
    /**
     * Get global variable.
     */
    getGlobal(name: string): Value;
    /**
     * Set global variable.
     */
    setGlobal(name: string, value: Value): void;
    /**
     * Create the import object for WASM instantiation.
     * This provides all the runtime functions the compiled code needs.
     */
    private createImports;
    /**
     * Get collected output.
     */
    getOutput(): string;
    /**
     * Clear output buffer.
     */
    clearOutput(): void;
    /**
     * Get memory statistics.
     */
    getStats(): {
        memory: ReturnType<Memory['getStats']>;
    };
}
/**
 * Create and configure a new VM instance.
 */
export declare function createVM(config?: VMConfig): VM;
//# sourceMappingURL=vm.d.ts.map