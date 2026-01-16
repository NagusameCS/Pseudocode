"use strict";
/*
 * Pseudocode WASM Standard Library - Exports
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
exports.createStdlib = createStdlib;
__exportStar(require("./io"), exports);
__exportStar(require("./math"), exports);
__exportStar(require("./string"), exports);
__exportStar(require("./array"), exports);
__exportStar(require("./dict"), exports);
__exportStar(require("./file"), exports);
__exportStar(require("./http"), exports);
__exportStar(require("./json"), exports);
__exportStar(require("./crypto"), exports);
const io_1 = require("./io");
const math_1 = require("./math");
const string_1 = require("./string");
const array_1 = require("./array");
const dict_1 = require("./dict");
const file_1 = require("./file");
const http_1 = require("./http");
const json_1 = require("./json");
const crypto_1 = require("./crypto");
/**
 * Create the complete standard library for the runtime.
 */
function createStdlib(config) {
    const { memory, stdout = console.log, stdin = async () => '' } = config;
    const callFunction = config.callFunction ?? (() => { throw new Error('Function calls not implemented'); });
    return {
        // I/O
        ...(0, io_1.createIOFunctions)(memory, stdout, stdin),
        // Math
        ...(0, math_1.createMathFunctions)(),
        PI: math_1.PI, E: math_1.E, TAU: math_1.TAU,
        // Strings
        ...(0, string_1.createStringFunctions)(memory),
        // Arrays
        ...(0, array_1.createArrayFunctions)(memory, callFunction),
        // Dictionaries
        ...(0, dict_1.createDictFunctions)(memory, callFunction),
        // Files
        ...(0, file_1.createFileFunctions)(memory),
        // HTTP
        ...(0, http_1.createHttpFunctions)(memory),
        // JSON
        ...(0, json_1.createJsonFunctions)(memory),
        // Crypto
        ...(0, crypto_1.createCryptoFunctions)(memory),
        // Raw math functions (for direct WASM calls)
        mathRaw: math_1.mathFunctions,
    };
}
//# sourceMappingURL=index.js.map