/*
 * Pseudocode WASM - Main Entry Point
 *
 * This module provides a high-level API for compiling and running
 * Pseudocode programs using WebAssembly.
 */
export * from './compiler';
export * from './runtime';
import { parse, compile } from './compiler';
import { createVM } from './runtime';
/**
 * Compile Pseudocode source to WASM bytes.
 */
export function compileToWasm(source) {
    const ast = parse(source);
    return compile(ast);
}
/**
 * Run Pseudocode source code.
 */
export async function run(source, options = {}) {
    const output = [];
    const errors = [];
    try {
        // Parse source
        const ast = parse(source);
        // Check for parse errors
        // (Parser stores errors internally, would need to expose them)
        // Compile to WASM
        const result = compile(ast);
        if (result.errors.length > 0) {
            return {
                success: false,
                output: '',
                errors: result.errors
            };
        }
        // Create VM with output capture
        const vmConfig = {
            debug: options.debug,
            stdout: (msg) => output.push(msg)
        };
        const vm = createVM(vmConfig);
        // Load and run
        await vm.loadModule(result.wasm);
        const value = await vm.run();
        return {
            success: true,
            output: output.join(''),
            errors: [],
            value
        };
    }
    catch (error) {
        errors.push(error instanceof Error ? error.message : String(error));
        return {
            success: false,
            output: output.join(''),
            errors
        };
    }
}
/**
 * Create a reusable Pseudocode runtime.
 */
export function createRuntime(config) {
    return new PseudocodeRuntime(config);
}
/**
 * Reusable Pseudocode runtime for running multiple programs.
 */
export class PseudocodeRuntime {
    vm;
    output = [];
    constructor(config) {
        this.vm = createVM({
            ...config,
            stdout: (msg) => {
                this.output.push(msg);
                config?.stdout?.(msg);
            }
        });
    }
    /**
     * Run Pseudocode source code.
     */
    async run(source) {
        this.output = [];
        try {
            const ast = parse(source);
            const result = compile(ast);
            if (result.errors.length > 0) {
                return {
                    success: false,
                    output: '',
                    errors: result.errors
                };
            }
            await this.vm.loadModule(result.wasm);
            const value = await this.vm.run();
            return {
                success: true,
                output: this.output.join(''),
                errors: [],
                value
            };
        }
        catch (error) {
            return {
                success: false,
                output: this.output.join(''),
                errors: [error instanceof Error ? error.message : String(error)]
            };
        }
    }
    /**
     * Get the underlying VM instance.
     */
    getVM() {
        return this.vm;
    }
    /**
     * Get memory statistics.
     */
    getStats() {
        return this.vm.getStats();
    }
}
// Version info
export const VERSION = '1.0.0';
//# sourceMappingURL=index.js.map