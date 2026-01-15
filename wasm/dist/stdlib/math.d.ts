import { Value } from '../runtime/values';
/**
 * Math functions that can be used as pure WASM or JS imports.
 */
export declare const mathFunctions: {
    abs: (x: number) => number;
    floor: (x: number) => number;
    ceil: (x: number) => number;
    round: (x: number) => number;
    trunc: (x: number) => number;
    sqrt: (x: number) => number;
    cbrt: (x: number) => number;
    pow: (x: number, y: number) => number;
    exp: (x: number) => number;
    expm1: (x: number) => number;
    log: (x: number) => number;
    log10: (x: number) => number;
    log2: (x: number) => number;
    log1p: (x: number) => number;
    sin: (x: number) => number;
    cos: (x: number) => number;
    tan: (x: number) => number;
    asin: (x: number) => number;
    acos: (x: number) => number;
    atan: (x: number) => number;
    atan2: (y: number, x: number) => number;
    sinh: (x: number) => number;
    cosh: (x: number) => number;
    tanh: (x: number) => number;
    asinh: (x: number) => number;
    acosh: (x: number) => number;
    atanh: (x: number) => number;
    min: (...values: number[]) => number;
    max: (...values: number[]) => number;
    random: () => number;
    sign: (x: number) => number;
    hypot: (...values: number[]) => number;
    clamp: (x: number, min: number, max: number) => number;
    lerp: (a: number, b: number, t: number) => number;
    map: (x: number, inMin: number, inMax: number, outMin: number, outMax: number) => number;
};
export declare const PI: number;
export declare const E: number;
export declare const TAU: number;
export declare const SQRT2: number;
export declare const SQRT1_2: number;
export declare const LN2: number;
export declare const LN10: number;
export declare const LOG2E: number;
export declare const LOG10E: number;
/**
 * Create Value-based math functions for runtime.
 */
export declare function createMathFunctions(): {
    abs: (v: Value) => Value;
    sqrt: (v: Value) => Value;
    sin: (v: Value) => Value;
    cos: (v: Value) => Value;
    tan: (v: Value) => Value;
    floor: (v: Value) => Value;
    ceil: (v: Value) => Value;
    round: (v: Value) => Value;
    pow: (base: Value, exp: Value) => Value;
    log: (v: Value) => Value;
    exp: (v: Value) => Value;
    min: (a: Value, b: Value) => Value;
    max: (a: Value, b: Value) => Value;
    random: () => Value;
};
//# sourceMappingURL=math.d.ts.map