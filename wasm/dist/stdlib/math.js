"use strict";
/*
 * Pseudocode WASM Standard Library - Math Functions
 */
Object.defineProperty(exports, "__esModule", { value: true });
exports.LOG10E = exports.LOG2E = exports.LN10 = exports.LN2 = exports.SQRT1_2 = exports.SQRT2 = exports.TAU = exports.E = exports.PI = exports.mathFunctions = void 0;
exports.createMathFunctions = createMathFunctions;
const values_1 = require("../runtime/values");
/**
 * Math functions that can be used as pure WASM or JS imports.
 */
exports.mathFunctions = {
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
exports.PI = Math.PI;
exports.E = Math.E;
exports.TAU = Math.PI * 2;
exports.SQRT2 = Math.SQRT2;
exports.SQRT1_2 = Math.SQRT1_2;
exports.LN2 = Math.LN2;
exports.LN10 = Math.LN10;
exports.LOG2E = Math.LOG2E;
exports.LOG10E = Math.LOG10E;
/**
 * Create Value-based math functions for runtime.
 */
function createMathFunctions() {
    return {
        abs: (v) => {
            const n = (0, values_1.asNumber)(v);
            return (0, values_1.isInt)(v) ? (0, values_1.valInt)(Math.abs(n)) : (0, values_1.valFloat)(Math.abs(n));
        },
        sqrt: (v) => (0, values_1.valFloat)(Math.sqrt((0, values_1.asNumber)(v))),
        sin: (v) => (0, values_1.valFloat)(Math.sin((0, values_1.asNumber)(v))),
        cos: (v) => (0, values_1.valFloat)(Math.cos((0, values_1.asNumber)(v))),
        tan: (v) => (0, values_1.valFloat)(Math.tan((0, values_1.asNumber)(v))),
        floor: (v) => (0, values_1.valInt)(Math.floor((0, values_1.asNumber)(v))),
        ceil: (v) => (0, values_1.valInt)(Math.ceil((0, values_1.asNumber)(v))),
        round: (v) => (0, values_1.valInt)(Math.round((0, values_1.asNumber)(v))),
        pow: (base, exp) => (0, values_1.valFloat)(Math.pow((0, values_1.asNumber)(base), (0, values_1.asNumber)(exp))),
        log: (v) => (0, values_1.valFloat)(Math.log((0, values_1.asNumber)(v))),
        exp: (v) => (0, values_1.valFloat)(Math.exp((0, values_1.asNumber)(v))),
        min: (a, b) => {
            const na = (0, values_1.asNumber)(a);
            const nb = (0, values_1.asNumber)(b);
            const result = Math.min(na, nb);
            return ((0, values_1.isInt)(a) && (0, values_1.isInt)(b)) ? (0, values_1.valInt)(result) : (0, values_1.valFloat)(result);
        },
        max: (a, b) => {
            const na = (0, values_1.asNumber)(a);
            const nb = (0, values_1.asNumber)(b);
            const result = Math.max(na, nb);
            return ((0, values_1.isInt)(a) && (0, values_1.isInt)(b)) ? (0, values_1.valInt)(result) : (0, values_1.valFloat)(result);
        },
        random: () => (0, values_1.valFloat)(Math.random()),
    };
}
//# sourceMappingURL=math.js.map