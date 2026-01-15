/*
 * Pseudocode WASM Compiler - WASM Binary Builder
 *
 * Low-level utilities for constructing WebAssembly binary modules.
 * Implements the WASM binary format specification.
 */
// WASM Section IDs
export const SECTION_CUSTOM = 0;
export const SECTION_TYPE = 1;
export const SECTION_IMPORT = 2;
export const SECTION_FUNCTION = 3;
export const SECTION_TABLE = 4;
export const SECTION_MEMORY = 5;
export const SECTION_GLOBAL = 6;
export const SECTION_EXPORT = 7;
export const SECTION_START = 8;
export const SECTION_ELEMENT = 9;
export const SECTION_CODE = 10;
export const SECTION_DATA = 11;
// WASM Value Types
export const TYPE_I32 = 0x7F;
export const TYPE_I64 = 0x7E;
export const TYPE_F32 = 0x7D;
export const TYPE_F64 = 0x7C;
export const TYPE_FUNCREF = 0x70;
export const TYPE_EXTERNREF = 0x6F;
// WASM Opcodes
export const OP = {
    // Control flow
    UNREACHABLE: 0x00,
    NOP: 0x01,
    BLOCK: 0x02,
    LOOP: 0x03,
    IF: 0x04,
    ELSE: 0x05,
    END: 0x0B,
    BR: 0x0C,
    BR_IF: 0x0D,
    BR_TABLE: 0x0E,
    RETURN: 0x0F,
    // Call
    CALL: 0x10,
    CALL_INDIRECT: 0x11,
    // Parametric
    DROP: 0x1A,
    SELECT: 0x1B,
    // Variable
    LOCAL_GET: 0x20,
    LOCAL_SET: 0x21,
    LOCAL_TEE: 0x22,
    GLOBAL_GET: 0x23,
    GLOBAL_SET: 0x24,
    // Memory
    I32_LOAD: 0x28,
    I64_LOAD: 0x29,
    F32_LOAD: 0x2A,
    F64_LOAD: 0x2B,
    I32_STORE: 0x36,
    I64_STORE: 0x37,
    F32_STORE: 0x38,
    F64_STORE: 0x39,
    MEMORY_SIZE: 0x3F,
    MEMORY_GROW: 0x40,
    // Constants
    I32_CONST: 0x41,
    I64_CONST: 0x42,
    F32_CONST: 0x43,
    F64_CONST: 0x44,
    // Comparison (i32)
    I32_EQZ: 0x45,
    I32_EQ: 0x46,
    I32_NE: 0x47,
    I32_LT_S: 0x48,
    I32_LT_U: 0x49,
    I32_GT_S: 0x4A,
    I32_GT_U: 0x4B,
    I32_LE_S: 0x4C,
    I32_LE_U: 0x4D,
    I32_GE_S: 0x4E,
    I32_GE_U: 0x4F,
    // Comparison (i64)
    I64_EQZ: 0x50,
    I64_EQ: 0x51,
    I64_NE: 0x52,
    I64_LT_S: 0x53,
    I64_LT_U: 0x54,
    I64_GT_S: 0x55,
    I64_GT_U: 0x56,
    I64_LE_S: 0x57,
    I64_LE_U: 0x58,
    I64_GE_S: 0x59,
    I64_GE_U: 0x5A,
    // Comparison (f64)
    F64_EQ: 0x61,
    F64_NE: 0x62,
    F64_LT: 0x63,
    F64_GT: 0x64,
    F64_LE: 0x65,
    F64_GE: 0x66,
    // Numeric (i32)
    I32_CLZ: 0x67,
    I32_CTZ: 0x68,
    I32_POPCNT: 0x69,
    I32_ADD: 0x6A,
    I32_SUB: 0x6B,
    I32_MUL: 0x6C,
    I32_DIV_S: 0x6D,
    I32_DIV_U: 0x6E,
    I32_REM_S: 0x6F,
    I32_REM_U: 0x70,
    I32_AND: 0x71,
    I32_OR: 0x72,
    I32_XOR: 0x73,
    I32_SHL: 0x74,
    I32_SHR_S: 0x75,
    I32_SHR_U: 0x76,
    I32_ROTL: 0x77,
    I32_ROTR: 0x78,
    // Numeric (i64)
    I64_CLZ: 0x79,
    I64_CTZ: 0x7A,
    I64_POPCNT: 0x7B,
    I64_ADD: 0x7C,
    I64_SUB: 0x7D,
    I64_MUL: 0x7E,
    I64_DIV_S: 0x7F,
    I64_DIV_U: 0x80,
    I64_REM_S: 0x81,
    I64_REM_U: 0x82,
    I64_AND: 0x83,
    I64_OR: 0x84,
    I64_XOR: 0x85,
    I64_SHL: 0x86,
    I64_SHR_S: 0x87,
    I64_SHR_U: 0x88,
    I64_ROTL: 0x89,
    I64_ROTR: 0x8A,
    // Numeric (f64)
    F64_ABS: 0x99,
    F64_NEG: 0x9A,
    F64_CEIL: 0x9B,
    F64_FLOOR: 0x9C,
    F64_TRUNC: 0x9D,
    F64_NEAREST: 0x9E,
    F64_SQRT: 0x9F,
    F64_ADD: 0xA0,
    F64_SUB: 0xA1,
    F64_MUL: 0xA2,
    F64_DIV: 0xA3,
    F64_MIN: 0xA4,
    F64_MAX: 0xA5,
    F64_COPYSIGN: 0xA6,
    // Conversions
    I32_WRAP_I64: 0xA7,
    I32_TRUNC_F64_S: 0xAA,
    I32_TRUNC_F64_U: 0xAB,
    I64_EXTEND_I32_S: 0xAC,
    I64_EXTEND_I32_U: 0xAD,
    I64_TRUNC_F64_S: 0xB0,
    I64_TRUNC_F64_U: 0xB1,
    F64_CONVERT_I32_S: 0xB7,
    F64_CONVERT_I32_U: 0xB8,
    F64_CONVERT_I64_S: 0xB9,
    F64_CONVERT_I64_U: 0xBA,
    F64_PROMOTE_F32: 0xBB,
    I32_REINTERPRET_F32: 0xBC,
    I64_REINTERPRET_F64: 0xBD,
    F32_REINTERPRET_I32: 0xBE,
    F64_REINTERPRET_I64: 0xBF,
};
/**
 * Encodes an unsigned LEB128 integer.
 */
export function encodeULEB128(value) {
    const bytes = [];
    do {
        let byte = value & 0x7F;
        value >>>= 7;
        if (value !== 0) {
            byte |= 0x80;
        }
        bytes.push(byte);
    } while (value !== 0);
    return bytes;
}
/**
 * Encodes a signed LEB128 integer.
 */
export function encodeSLEB128(value) {
    const bytes = [];
    let more = true;
    while (more) {
        let byte = value & 0x7F;
        value >>= 7;
        // Check if more bytes needed
        if ((value === 0 && (byte & 0x40) === 0) ||
            (value === -1 && (byte & 0x40) !== 0)) {
            more = false;
        }
        else {
            byte |= 0x80;
        }
        bytes.push(byte);
    }
    return bytes;
}
/**
 * Encodes a signed LEB128 BigInt (for full 64-bit values).
 */
export function encodeSLEB128BigInt(value) {
    const bytes = [];
    let more = true;
    while (more) {
        let byte = Number(value & 0x7fn);
        value >>= 7n;
        // Check if more bytes needed
        if ((value === 0n && (byte & 0x40) === 0) ||
            (value === -1n && (byte & 0x40) !== 0)) {
            more = false;
        }
        else {
            byte |= 0x80;
        }
        bytes.push(byte);
    }
    return bytes;
}
/**
 * Encodes a string with its length prefix.
 */
export function encodeString(str) {
    const encoder = new TextEncoder();
    const bytes = encoder.encode(str);
    return [...encodeULEB128(bytes.length), ...bytes];
}
/**
 * Encodes a vector (array with length prefix).
 */
export function encodeVector(items) {
    const result = encodeULEB128(items.length);
    for (const item of items) {
        result.push(...item);
    }
    return result;
}
/**
 * Builder for WASM binary modules.
 */
export class WasmBuilder {
    types = [];
    imports = [];
    functions = []; // Type indices for functions
    tables = [];
    memories = [];
    globals = [];
    exports = [];
    startFunc = null;
    elements = [];
    codes = []; // Function bodies
    data = [];
    typeMap = new Map();
    importFuncCount = 0;
    /**
     * Add a function type signature.
     * Returns the type index.
     */
    addType(params, results) {
        const key = `${params.join(',')}->${results.join(',')}`;
        // Check if type already exists
        const existing = this.typeMap.get(key);
        if (existing !== undefined) {
            return existing;
        }
        const typeIdx = this.types.length;
        const encoded = [
            0x60, // func type
            ...encodeULEB128(params.length),
            ...params,
            ...encodeULEB128(results.length),
            ...results
        ];
        this.types.push(encoded);
        this.typeMap.set(key, typeIdx);
        return typeIdx;
    }
    /**
     * Add an import.
     * Returns the import index (for functions, this is the function index).
     */
    addImport(module, name, kind, typeIdx) {
        const index = kind === 0 ? this.importFuncCount++ : this.imports.length;
        this.imports.push({ module, name, kind, typeIdx });
        return index;
    }
    /**
     * Add a function.
     * Returns the function index.
     */
    addFunction(typeIdx, locals, body) {
        const funcIdx = this.importFuncCount + this.functions.length;
        this.functions.push(typeIdx);
        // Encode locals and body
        const localGroups = [];
        let i = 0;
        while (i < locals.length) {
            const type = locals[i];
            let count = 1;
            while (i + count < locals.length && locals[i + count] === type) {
                count++;
            }
            localGroups.push([...encodeULEB128(count), type]);
            i += count;
        }
        const codeBody = [
            ...encodeULEB128(localGroups.length),
            ...localGroups.flat(),
            ...body,
            OP.END
        ];
        const code = [...encodeULEB128(codeBody.length), ...codeBody];
        this.codes.push(code);
        return funcIdx;
    }
    /**
     * Add a memory.
     */
    addMemory(initial, maximum) {
        const memIdx = this.memories.length;
        if (maximum !== undefined) {
            this.memories.push([0x01, ...encodeULEB128(initial), ...encodeULEB128(maximum)]);
        }
        else {
            this.memories.push([0x00, ...encodeULEB128(initial)]);
        }
        return memIdx;
    }
    /**
     * Add a global.
     */
    addGlobal(type, mutable, initExpr) {
        const globalIdx = this.globals.length;
        this.globals.push([type, mutable ? 0x01 : 0x00, ...initExpr, OP.END]);
        return globalIdx;
    }
    /**
     * Add an export.
     */
    addExport(name, kind, index) {
        this.exports.push({ name, kind, index });
    }
    /**
     * Set the start function.
     */
    setStart(funcIdx) {
        this.startFunc = funcIdx;
    }
    /**
     * Add a data segment.
     */
    addData(offset, bytes) {
        this.data.push({ offset, bytes });
    }
    /**
     * Build the WASM binary.
     */
    build() {
        const sections = [];
        // WASM magic number and version
        sections.push(0x00, 0x61, 0x73, 0x6D); // \0asm
        sections.push(0x01, 0x00, 0x00, 0x00); // version 1
        // Type section
        if (this.types.length > 0) {
            sections.push(...this.encodeSection(SECTION_TYPE, encodeVector(this.types)));
        }
        // Import section
        if (this.imports.length > 0) {
            const importBytes = [];
            for (const imp of this.imports) {
                importBytes.push([
                    ...encodeString(imp.module),
                    ...encodeString(imp.name),
                    imp.kind,
                    ...encodeULEB128(imp.typeIdx)
                ]);
            }
            sections.push(...this.encodeSection(SECTION_IMPORT, encodeVector(importBytes)));
        }
        // Function section
        if (this.functions.length > 0) {
            const funcBytes = [
                ...encodeULEB128(this.functions.length),
                ...this.functions.flatMap(idx => encodeULEB128(idx))
            ];
            sections.push(...this.encodeSection(SECTION_FUNCTION, funcBytes));
        }
        // Memory section
        if (this.memories.length > 0) {
            sections.push(...this.encodeSection(SECTION_MEMORY, encodeVector(this.memories)));
        }
        // Global section
        if (this.globals.length > 0) {
            sections.push(...this.encodeSection(SECTION_GLOBAL, encodeVector(this.globals)));
        }
        // Export section
        if (this.exports.length > 0) {
            const exportBytes = [];
            for (const exp of this.exports) {
                exportBytes.push([
                    ...encodeString(exp.name),
                    exp.kind,
                    ...encodeULEB128(exp.index)
                ]);
            }
            sections.push(...this.encodeSection(SECTION_EXPORT, encodeVector(exportBytes)));
        }
        // Start section
        if (this.startFunc !== null) {
            sections.push(...this.encodeSection(SECTION_START, encodeULEB128(this.startFunc)));
        }
        // Code section
        if (this.codes.length > 0) {
            const codeBytes = [
                ...encodeULEB128(this.codes.length),
                ...this.codes.flat()
            ];
            sections.push(...this.encodeSection(SECTION_CODE, codeBytes));
        }
        // Data section
        if (this.data.length > 0) {
            const dataBytes = [];
            for (const seg of this.data) {
                dataBytes.push([
                    0x00, // memory index (always 0)
                    OP.I32_CONST, ...encodeSLEB128(seg.offset), OP.END,
                    ...encodeULEB128(seg.bytes.length),
                    ...seg.bytes
                ]);
            }
            sections.push(...this.encodeSection(SECTION_DATA, encodeVector(dataBytes)));
        }
        return new Uint8Array(sections);
    }
    encodeSection(id, content) {
        return [id, ...encodeULEB128(content.length), ...content];
    }
}
/**
 * Create a new WASM builder.
 */
export function createWasmBuilder() {
    return new WasmBuilder();
}
//# sourceMappingURL=wasm-builder.js.map