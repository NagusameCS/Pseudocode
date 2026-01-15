/*
 * Pseudocode WASM Standard Library - Exports
 */

export * from './io';
export * from './math';
export * from './string';
export * from './array';
export * from './dict';
export * from './file';
export * from './http';
export * from './json';
export * from './crypto';

import { createIOFunctions } from './io';
import { createMathFunctions, mathFunctions, PI, E, TAU } from './math';
import { createStringFunctions } from './string';
import { createArrayFunctions } from './array';
import { createDictFunctions } from './dict';
import { createFileFunctions } from './file';
import { createHttpFunctions } from './http';
import { createJsonFunctions } from './json';
import { createCryptoFunctions } from './crypto';
import { Memory } from '../runtime/memory';
import { Value } from '../runtime/values';

/**
 * Configuration for creating the standard library.
 */
export interface StdlibConfig {
    memory: Memory;
    stdout?: (msg: string) => void;
    stdin?: () => Promise<string>;
    callFunction?: (funcIdx: number, ...args: Value[]) => Value;
}

/**
 * Create the complete standard library for the runtime.
 */
export function createStdlib(config: StdlibConfig) {
    const { memory, stdout = console.log, stdin = async () => '' } = config;
    const callFunction = config.callFunction ?? (() => { throw new Error('Function calls not implemented'); });
    
    return {
        // I/O
        ...createIOFunctions(memory, stdout, stdin),
        
        // Math
        ...createMathFunctions(),
        PI, E, TAU,
        
        // Strings
        ...createStringFunctions(memory),
        
        // Arrays
        ...createArrayFunctions(memory, callFunction),
        
        // Dictionaries
        ...createDictFunctions(memory, callFunction),
        
        // Files
        ...createFileFunctions(memory),
        
        // HTTP
        ...createHttpFunctions(memory),
        
        // JSON
        ...createJsonFunctions(memory),
        
        // Crypto
        ...createCryptoFunctions(memory),
        
        // Raw math functions (for direct WASM calls)
        mathRaw: mathFunctions,
    };
}

/**
 * Type for the complete standard library.
 */
export type Stdlib = ReturnType<typeof createStdlib>;
