/*
 * Pseudocode WASM Standard Library - Math Functions
 */

import { Value, valFloat, valInt, asNumber, isInt } from '../runtime/values';

/**
 * Math functions that can be used as pure WASM or JS imports.
 */
export const mathFunctions = {
    // Basic math
    abs: (x: number): number => Math.abs(x),
    floor: (x: number): number => Math.floor(x),
    ceil: (x: number): number => Math.ceil(x),
    round: (x: number): number => Math.round(x),
    trunc: (x: number): number => Math.trunc(x),
    
    // Power and roots
    sqrt: (x: number): number => Math.sqrt(x),
    cbrt: (x: number): number => Math.cbrt(x),
    pow: (x: number, y: number): number => Math.pow(x, y),
    exp: (x: number): number => Math.exp(x),
    expm1: (x: number): number => Math.expm1(x),
    
    // Logarithms
    log: (x: number): number => Math.log(x),
    log10: (x: number): number => Math.log10(x),
    log2: (x: number): number => Math.log2(x),
    log1p: (x: number): number => Math.log1p(x),
    
    // Trigonometry
    sin: (x: number): number => Math.sin(x),
    cos: (x: number): number => Math.cos(x),
    tan: (x: number): number => Math.tan(x),
    asin: (x: number): number => Math.asin(x),
    acos: (x: number): number => Math.acos(x),
    atan: (x: number): number => Math.atan(x),
    atan2: (y: number, x: number): number => Math.atan2(y, x),
    
    // Hyperbolic
    sinh: (x: number): number => Math.sinh(x),
    cosh: (x: number): number => Math.cosh(x),
    tanh: (x: number): number => Math.tanh(x),
    asinh: (x: number): number => Math.asinh(x),
    acosh: (x: number): number => Math.acosh(x),
    atanh: (x: number): number => Math.atanh(x),
    
    // Comparison
    min: (...values: number[]): number => Math.min(...values),
    max: (...values: number[]): number => Math.max(...values),
    
    // Random
    random: (): number => Math.random(),
    
    // Sign
    sign: (x: number): number => Math.sign(x),
    
    // Hypot
    hypot: (...values: number[]): number => Math.hypot(...values),
    
    // Clamp (not native JS)
    clamp: (x: number, min: number, max: number): number => 
        Math.min(Math.max(x, min), max),
    
    // Lerp (linear interpolation)
    lerp: (a: number, b: number, t: number): number => 
        a + (b - a) * t,
    
    // Map range
    map: (x: number, inMin: number, inMax: number, outMin: number, outMax: number): number =>
        (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin,
};

// Constants
export const PI = Math.PI;
export const E = Math.E;
export const TAU = Math.PI * 2;
export const SQRT2 = Math.SQRT2;
export const SQRT1_2 = Math.SQRT1_2;
export const LN2 = Math.LN2;
export const LN10 = Math.LN10;
export const LOG2E = Math.LOG2E;
export const LOG10E = Math.LOG10E;

/**
 * Create Value-based math functions for runtime.
 */
export function createMathFunctions() {
    return {
        abs: (v: Value): Value => {
            const n = asNumber(v);
            return isInt(v) ? valInt(Math.abs(n)) : valFloat(Math.abs(n));
        },
        
        sqrt: (v: Value): Value => valFloat(Math.sqrt(asNumber(v))),
        sin: (v: Value): Value => valFloat(Math.sin(asNumber(v))),
        cos: (v: Value): Value => valFloat(Math.cos(asNumber(v))),
        tan: (v: Value): Value => valFloat(Math.tan(asNumber(v))),
        
        floor: (v: Value): Value => valInt(Math.floor(asNumber(v))),
        ceil: (v: Value): Value => valInt(Math.ceil(asNumber(v))),
        round: (v: Value): Value => valInt(Math.round(asNumber(v))),
        
        pow: (base: Value, exp: Value): Value => 
            valFloat(Math.pow(asNumber(base), asNumber(exp))),
        
        log: (v: Value): Value => valFloat(Math.log(asNumber(v))),
        exp: (v: Value): Value => valFloat(Math.exp(asNumber(v))),
        
        min: (a: Value, b: Value): Value => {
            const na = asNumber(a);
            const nb = asNumber(b);
            const result = Math.min(na, nb);
            return (isInt(a) && isInt(b)) ? valInt(result) : valFloat(result);
        },
        
        max: (a: Value, b: Value): Value => {
            const na = asNumber(a);
            const nb = asNumber(b);
            const result = Math.max(na, nb);
            return (isInt(a) && isInt(b)) ? valInt(result) : valFloat(result);
        },
        
        random: (): Value => valFloat(Math.random()),
    };
}
