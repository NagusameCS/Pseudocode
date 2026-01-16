"use strict";
/*
 * Pseudocode WASM - Main Entry Point
 *
 * This module provides a high-level API for compiling and running
 * Pseudocode programs using WebAssembly.
 */
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __exportStar = (this && this.__exportStar) || function(m, exports) {
    for (var p in m) if (p !== "default" && !Object.prototype.hasOwnProperty.call(exports, p)) __createBinding(exports, m, p);
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.VERSION = exports.PseudocodeRuntime = void 0;
exports.compileToWasm = compileToWasm;
exports.run = run;
exports.createRuntime = createRuntime;
__exportStar(require("./compiler"), exports);
__exportStar(require("./runtime"), exports);
const compiler_1 = require("./compiler");
const runtime_1 = require("./runtime");
/**
 * Compile Pseudocode source to WASM bytes.
 */
function compileToWasm(source) {
    const ast = (0, compiler_1.parse)(source);
    return (0, compiler_1.compile)(ast);
}
/**
 * Run Pseudocode source code.
 */
async function run(source, options = {}) {
    const output = [];
    const errors = [];
    try {
        // Parse source
        const ast = (0, compiler_1.parse)(source);
        // Check for parse errors
        // (Parser stores errors internally, would need to expose them)
        // Compile to WASM
        const result = (0, compiler_1.compile)(ast);
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
        const vm = (0, runtime_1.createVM)(vmConfig);
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
function createRuntime(config) {
    return new PseudocodeRuntime(config);
}
/**
 * Reusable Pseudocode runtime for running multiple programs.
 */
class PseudocodeRuntime {
    vm;
    output = [];
    constructor(config) {
        this.vm = (0, runtime_1.createVM)({
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
            const ast = (0, compiler_1.parse)(source);
            const result = (0, compiler_1.compile)(ast);
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
exports.PseudocodeRuntime = PseudocodeRuntime;
// Version info
exports.VERSION = '1.0.0';
//# sourceMappingURL=index.js.map