export declare const SECTION_CUSTOM = 0;
export declare const SECTION_TYPE = 1;
export declare const SECTION_IMPORT = 2;
export declare const SECTION_FUNCTION = 3;
export declare const SECTION_TABLE = 4;
export declare const SECTION_MEMORY = 5;
export declare const SECTION_GLOBAL = 6;
export declare const SECTION_EXPORT = 7;
export declare const SECTION_START = 8;
export declare const SECTION_ELEMENT = 9;
export declare const SECTION_CODE = 10;
export declare const SECTION_DATA = 11;
export declare const TYPE_I32 = 127;
export declare const TYPE_I64 = 126;
export declare const TYPE_F32 = 125;
export declare const TYPE_F64 = 124;
export declare const TYPE_FUNCREF = 112;
export declare const TYPE_EXTERNREF = 111;
export declare const OP: {
    UNREACHABLE: number;
    NOP: number;
    BLOCK: number;
    LOOP: number;
    IF: number;
    ELSE: number;
    END: number;
    BR: number;
    BR_IF: number;
    BR_TABLE: number;
    RETURN: number;
    CALL: number;
    CALL_INDIRECT: number;
    DROP: number;
    SELECT: number;
    LOCAL_GET: number;
    LOCAL_SET: number;
    LOCAL_TEE: number;
    GLOBAL_GET: number;
    GLOBAL_SET: number;
    I32_LOAD: number;
    I64_LOAD: number;
    F32_LOAD: number;
    F64_LOAD: number;
    I32_STORE: number;
    I64_STORE: number;
    F32_STORE: number;
    F64_STORE: number;
    MEMORY_SIZE: number;
    MEMORY_GROW: number;
    I32_CONST: number;
    I64_CONST: number;
    F32_CONST: number;
    F64_CONST: number;
    I32_EQZ: number;
    I32_EQ: number;
    I32_NE: number;
    I32_LT_S: number;
    I32_LT_U: number;
    I32_GT_S: number;
    I32_GT_U: number;
    I32_LE_S: number;
    I32_LE_U: number;
    I32_GE_S: number;
    I32_GE_U: number;
    I64_EQZ: number;
    I64_EQ: number;
    I64_NE: number;
    I64_LT_S: number;
    I64_LT_U: number;
    I64_GT_S: number;
    I64_GT_U: number;
    I64_LE_S: number;
    I64_LE_U: number;
    I64_GE_S: number;
    I64_GE_U: number;
    F64_EQ: number;
    F64_NE: number;
    F64_LT: number;
    F64_GT: number;
    F64_LE: number;
    F64_GE: number;
    I32_CLZ: number;
    I32_CTZ: number;
    I32_POPCNT: number;
    I32_ADD: number;
    I32_SUB: number;
    I32_MUL: number;
    I32_DIV_S: number;
    I32_DIV_U: number;
    I32_REM_S: number;
    I32_REM_U: number;
    I32_AND: number;
    I32_OR: number;
    I32_XOR: number;
    I32_SHL: number;
    I32_SHR_S: number;
    I32_SHR_U: number;
    I32_ROTL: number;
    I32_ROTR: number;
    I64_CLZ: number;
    I64_CTZ: number;
    I64_POPCNT: number;
    I64_ADD: number;
    I64_SUB: number;
    I64_MUL: number;
    I64_DIV_S: number;
    I64_DIV_U: number;
    I64_REM_S: number;
    I64_REM_U: number;
    I64_AND: number;
    I64_OR: number;
    I64_XOR: number;
    I64_SHL: number;
    I64_SHR_S: number;
    I64_SHR_U: number;
    I64_ROTL: number;
    I64_ROTR: number;
    F64_ABS: number;
    F64_NEG: number;
    F64_CEIL: number;
    F64_FLOOR: number;
    F64_TRUNC: number;
    F64_NEAREST: number;
    F64_SQRT: number;
    F64_ADD: number;
    F64_SUB: number;
    F64_MUL: number;
    F64_DIV: number;
    F64_MIN: number;
    F64_MAX: number;
    F64_COPYSIGN: number;
    I32_WRAP_I64: number;
    I32_TRUNC_F64_S: number;
    I32_TRUNC_F64_U: number;
    I64_EXTEND_I32_S: number;
    I64_EXTEND_I32_U: number;
    I64_TRUNC_F64_S: number;
    I64_TRUNC_F64_U: number;
    F64_CONVERT_I32_S: number;
    F64_CONVERT_I32_U: number;
    F64_CONVERT_I64_S: number;
    F64_CONVERT_I64_U: number;
    F64_PROMOTE_F32: number;
    I32_REINTERPRET_F32: number;
    I64_REINTERPRET_F64: number;
    F32_REINTERPRET_I32: number;
    F64_REINTERPRET_I64: number;
};
/**
 * Encodes an unsigned LEB128 integer.
 */
export declare function encodeULEB128(value: number): number[];
/**
 * Encodes a signed LEB128 integer.
 */
export declare function encodeSLEB128(value: number): number[];
/**
 * Encodes a signed LEB128 BigInt (for full 64-bit values).
 */
export declare function encodeSLEB128BigInt(value: bigint): number[];
/**
 * Encodes a string with its length prefix.
 */
export declare function encodeString(str: string): number[];
/**
 * Encodes a vector (array with length prefix).
 */
export declare function encodeVector(items: number[][]): number[];
/**
 * Builder for WASM binary modules.
 */
export declare class WasmBuilder {
    private types;
    private imports;
    private functions;
    private tables;
    private memories;
    private globals;
    private exports;
    private startFunc;
    private elements;
    private codes;
    private data;
    private typeMap;
    private importFuncCount;
    /**
     * Add a function type signature.
     * Returns the type index.
     */
    addType(params: number[], results: number[]): number;
    /**
     * Add an import.
     * Returns the import index (for functions, this is the function index).
     */
    addImport(module: string, name: string, kind: number, typeIdx: number): number;
    /**
     * Add a function.
     * Returns the function index.
     */
    addFunction(typeIdx: number, locals: number[], body: number[]): number;
    /**
     * Add a memory.
     */
    addMemory(initial: number, maximum?: number): number;
    /**
     * Add a global.
     */
    addGlobal(type: number, mutable: boolean, initExpr: number[]): number;
    /**
     * Add an export.
     */
    addExport(name: string, kind: number, index: number): void;
    /**
     * Set the start function.
     */
    setStart(funcIdx: number): void;
    /**
     * Add a data segment.
     */
    addData(offset: number, bytes: number[]): void;
    /**
     * Build the WASM binary.
     */
    build(): Uint8Array;
    private encodeSection;
}
/**
 * Create a new WASM builder.
 */
export declare function createWasmBuilder(): WasmBuilder;
//# sourceMappingURL=wasm-builder.d.ts.map