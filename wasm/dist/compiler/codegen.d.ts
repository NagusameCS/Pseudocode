import * as AST from './ast';
/**
 * Compile Pseudocode AST to WASM.
 */
export declare function compile(program: AST.Program): {
    wasm: Uint8Array;
    errors: string[];
};
//# sourceMappingURL=codegen.d.ts.map