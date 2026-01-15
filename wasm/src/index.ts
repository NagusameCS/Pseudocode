/*
 * Pseudocode WASM - Main Entry Point
 * 
 * This module provides a high-level API for compiling and running
 * Pseudocode programs using WebAssembly.
 */

export * from './compiler';
export * from './runtime';

import { parse, compile } from './compiler';
import { VM, createVM, VMConfig } from './runtime';

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
    timeout?: number;  // Max execution time in ms
}

/**
 * Compile Pseudocode source to WASM bytes.
 */
export function compileToWasm(source: string): { wasm: Uint8Array; errors: string[] } {
    const ast = parse(source);
    return compile(ast);
}

/**
 * Run Pseudocode source code.
 */
export async function run(source: string, options: RunOptions = {}): Promise<RunResult> {
    const output: string[] = [];
    const errors: string[] = [];
    
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
        const vmConfig: VMConfig = {
            debug: options.debug,
            stdout: (msg: string) => output.push(msg)
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
        
    } catch (error) {
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
export function createRuntime(config?: VMConfig): PseudocodeRuntime {
    return new PseudocodeRuntime(config);
}

/**
 * Reusable Pseudocode runtime for running multiple programs.
 */
export class PseudocodeRuntime {
    private vm: VM;
    private output: string[] = [];
    
    constructor(config?: VMConfig) {
        this.vm = createVM({
            ...config,
            stdout: (msg: string) => {
                this.output.push(msg);
                config?.stdout?.(msg);
            }
        });
    }
    
    /**
     * Run Pseudocode source code.
     */
    async run(source: string): Promise<RunResult> {
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
            
        } catch (error) {
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
    getVM(): VM {
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
