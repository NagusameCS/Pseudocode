export * from './compiler';
export * from './runtime';
import { VM, VMConfig } from './runtime';
/**
 * Result of running Pseudocode code.
 */
export interface RunResult {
    success: boolean;
    output: string;
    errors: string[];
    value?: unknown;
}
/**
 * Options for running Pseudocode.
 */
export interface RunOptions {
    debug?: boolean;
    timeout?: number;
}
/**
 * Compile Pseudocode source to WASM bytes.
 */
export declare function compileToWasm(source: string): {
    wasm: Uint8Array;
    errors: string[];
};
/**
 * Run Pseudocode source code.
 */
export declare function run(source: string, options?: RunOptions): Promise<RunResult>;
/**
 * Create a reusable Pseudocode runtime.
 */
export declare function createRuntime(config?: VMConfig): PseudocodeRuntime;
/**
 * Reusable Pseudocode runtime for running multiple programs.
 */
export declare class PseudocodeRuntime {
    private vm;
    private output;
    constructor(config?: VMConfig);
    /**
     * Run Pseudocode source code.
     */
    run(source: string): Promise<RunResult>;
    /**
     * Get the underlying VM instance.
     */
    getVM(): VM;
    /**
     * Get memory statistics.
     */
    getStats(): {
        memory: ReturnType<import("./runtime").Memory["getStats"]>;
    };
}
export declare const VERSION = "1.0.0";
//# sourceMappingURL=index.d.ts.map