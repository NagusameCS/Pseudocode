/*
 * Pseudocode WASM Standard Library - Math Functions
 */
import { valFloat, valInt, asNumber, isInt } from '../runtime/values';
/**
 * Math functions that can be used as pure WASM or JS imports.
 */
export const mathFunctions = {
    // Basic math
    abs: (x) => Math.abs(x),
    floor: (x) => Math.floor(x),
    ceil: (x) => Math.ceil(x),
    round: (x) => Math.round(x),
    trunc: (x) => Math.trunc(x),
    // Power and roots
    sqrt: (x) => Math.sqrt(x),
    cbrt: (x) => Math.cbrt(x),
    pow: (x, y) => Math.pow(x, y),
    exp: (x) => Math.exp(x),
    expm1: (x) => Math.expm1(x),
    // Logarithms
    log: (x) => Math.log(x),
    log10: (x) => Math.log10(x),
    log2: (x) => Math.log2(x),
    log1p: (x) => Math.log1p(x),
    // Trigonometry
    sin: (x) => Math.sin(x),
    cos: (x) => Math.cos(x),
    tan: (x) => Math.tan(x),
    asin: (x) => Math.asin(x),
    acos: (x) => Math.acos(x),
    atan: (x) => Math.atan(x),
    atan2: (y, x) => Math.atan2(y, x),
    // Hyperbolic
    sinh: (x) => Math.sinh(x),
    cosh: (x) => Math.cosh(x),
    tanh: (x) => Math.tanh(x),
    asinh: (x) => Math.asinh(x),
    acosh: (x) => Math.acosh(x),
    atanh: (x) => Math.atanh(x),
    // Comparison
    min: (...values) => Math.min(...values),
    max: (...values) => Math.max(...values),
    // Random
    random: () => Math.random(),
    // Sign
    sign: (x) => Math.sign(x),
    // Hypot
    hypot: (...values) => Math.hypot(...values),
    // Clamp (not native JS)
    clamp: (x, min, max) => Math.min(Math.max(x, min), max),
    // Lerp (linear interpolation)
    lerp: (a, b, t) => a + (b - a) * t,
    // Map range
    map: (x, inMin, inMax, outMin, outMax) => (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin,
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
        abs: (v) => {
            const n = asNumber(v);
            return isInt(v) ? valInt(Math.abs(n)) : valFloat(Math.abs(n));
        },
        sqrt: (v) => valFloat(Math.sqrt(asNumber(v))),
        sin: (v) => valFloat(Math.sin(asNumber(v))),
        cos: (v) => valFloat(Math.cos(asNumber(v))),
        tan: (v) => valFloat(Math.tan(asNumber(v))),
        floor: (v) => valInt(Math.floor(asNumber(v))),
        ceil: (v) => valInt(Math.ceil(asNumber(v))),
        round: (v) => valInt(Math.round(asNumber(v))),
        pow: (base, exp) => valFloat(Math.pow(asNumber(base), asNumber(exp))),
        log: (v) => valFloat(Math.log(asNumber(v))),
        exp: (v) => valFloat(Math.exp(asNumber(v))),
        min: (a, b) => {
            const na = asNumber(a);
            const nb = asNumber(b);
            const result = Math.min(na, nb);
            return (isInt(a) && isInt(b)) ? valInt(result) : valFloat(result);
        },
        max: (a, b) => {
            const na = asNumber(a);
            const nb = asNumber(b);
            const result = Math.max(na, nb);
            return (isInt(a) && isInt(b)) ? valInt(result) : valFloat(result);
        },
        random: () => valFloat(Math.random()),
    };
}
//# sourceMappingURL=math.js.map