/*
 * Pseudocode WASM Compiler - Code Generator
 * 
 * Translates Pseudocode AST into WebAssembly bytecode.
 */

import * as AST from './ast';
import {
    WasmBuilder, createWasmBuilder,
    TYPE_I32, TYPE_I64, TYPE_F64,
    OP, encodeULEB128, encodeSLEB128, encodeSLEB128BigInt, encodeString
} from './wasm-builder';

// We use i64 for tagged values
const VALUE_TYPE = TYPE_I64;

/**
 * Local variable info.
 */
interface Local {
    name: string;
    index: number;
    type: number;
}

/**
 * Function info during compilation.
 */
interface FunctionContext {
    name: string;
    params: string[];
    locals: Local[];
    localIndex: number;
    body: number[];
    loopStack: number[]; // For break/continue
}

/**
 * Compiler state.
 */
interface CompilerState {
    builder: WasmBuilder;
    functions: Map<string, number>;     // Function name -> index
    globals: Map<string, number>;       // Global name -> index
    imports: Map<string, number>;       // Import name -> index
    strings: Map<string, number>;       // String literal -> data offset
    currentFunc: FunctionContext | null;
    stringDataOffset: number;
    errors: string[];
}

/**
 * Compile Pseudocode AST to WASM.
 */
export function compile(program: AST.Program): { wasm: Uint8Array; errors: string[] } {
    const state: CompilerState = {
        builder: createWasmBuilder(),
        functions: new Map(),
        globals: new Map(),
        imports: new Map(),
        strings: new Map(),
        currentFunc: null,
        stringDataOffset: 0x10000, // Start strings at 64KB
        errors: []
    };
    
    // Setup imports from runtime
    setupImports(state);
    
    // First pass: collect all function declarations
    for (const stmt of program.statements) {
        if (stmt.kind === 'Function') {
            registerFunction(state, stmt as AST.FunctionStmt);
        }
    }
    
    // Second pass: compile all statements
    compileTopLevel(state, program.statements);
    
    // Add data segment for strings
    if (state.strings.size > 0) {
        const stringData: number[] = [];
        for (const [str, offset] of state.strings) {
            const encoder = new TextEncoder();
            const bytes = encoder.encode(str);
            // Store length + string bytes
            stringData.push(...encodeULEB128(bytes.length));
            stringData.push(...bytes);
            stringData.push(0); // null terminator
        }
        state.builder.addData(0x10000, stringData);
    }
    
    return {
        wasm: state.builder.build(),
        errors: state.errors
    };
}

/**
 * Setup runtime imports.
 */
function setupImports(state: CompilerState): void {
    const { builder } = state;
    
    // Type signatures
    const voidType = builder.addType([], []);
    const i64RetType = builder.addType([], [VALUE_TYPE]);
    const i64ParamType = builder.addType([VALUE_TYPE], []);
    const i64i64RetType = builder.addType([VALUE_TYPE, VALUE_TYPE], [VALUE_TYPE]);
    const i64i64ParamType = builder.addType([VALUE_TYPE, VALUE_TYPE], []);
    const i64ParamRetType = builder.addType([VALUE_TYPE], [VALUE_TYPE]);
    const i32RetType = builder.addType([VALUE_TYPE], [TYPE_I32]);
    
    // Value operations
    state.imports.set('value_add', builder.addImport('env', 'value_add', 0, i64i64RetType));
    state.imports.set('value_sub', builder.addImport('env', 'value_sub', 0, i64i64RetType));
    state.imports.set('value_mul', builder.addImport('env', 'value_mul', 0, i64i64RetType));
    state.imports.set('value_div', builder.addImport('env', 'value_div', 0, i64i64RetType));
    state.imports.set('value_mod', builder.addImport('env', 'value_mod', 0, i64i64RetType));
    state.imports.set('value_neg', builder.addImport('env', 'value_neg', 0, i64ParamRetType));
    state.imports.set('value_eq', builder.addImport('env', 'value_eq', 0, i64i64RetType));
    state.imports.set('value_lt', builder.addImport('env', 'value_lt', 0, i64i64RetType));
    state.imports.set('value_gt', builder.addImport('env', 'value_gt', 0, i64i64RetType));
    state.imports.set('value_lte', builder.addImport('env', 'value_lte', 0, i64i64RetType));
    state.imports.set('value_gte', builder.addImport('env', 'value_gte', 0, i64i64RetType));
    state.imports.set('value_not', builder.addImport('env', 'value_not', 0, i64ParamRetType));
    state.imports.set('value_truthy', builder.addImport('env', 'value_truthy', 0, i32RetType));
    
    // I/O
    state.imports.set('print', builder.addImport('env', 'print', 0, i64ParamType));
    state.imports.set('println', builder.addImport('env', 'println', 0, i64ParamType));
    
    // String operations
    const stringNewType = builder.addType([TYPE_I32, TYPE_I32], [VALUE_TYPE]);
    state.imports.set('string_new', builder.addImport('env', 'string_new', 0, stringNewType));
    
    // Array operations
    const arrayNewType = builder.addType([TYPE_I32], [TYPE_I32]);
    state.imports.set('array_new', builder.addImport('env', 'array_new', 0, arrayNewType));
    
    const arrayGetType = builder.addType([TYPE_I32, TYPE_I32], [VALUE_TYPE]);
    state.imports.set('array_get', builder.addImport('env', 'array_get', 0, arrayGetType));
    
    const arraySetType = builder.addType([TYPE_I32, TYPE_I32, VALUE_TYPE], []);
    state.imports.set('array_set', builder.addImport('env', 'array_set', 0, arraySetType));
    
    const arrayPushType = builder.addType([TYPE_I32, VALUE_TYPE], []);
    state.imports.set('array_push', builder.addImport('env', 'array_push', 0, arrayPushType));
    
    const arrayLenType = builder.addType([TYPE_I32], [TYPE_I32]);
    state.imports.set('array_len', builder.addImport('env', 'array_len', 0, arrayLenType));
    
    // Type operations
    state.imports.set('type_of', builder.addImport('env', 'type_of', 0, i64ParamRetType));
    state.imports.set('to_int', builder.addImport('env', 'to_int', 0, i64ParamRetType));
    state.imports.set('to_float', builder.addImport('env', 'to_float', 0, i64ParamRetType));
    state.imports.set('to_string', builder.addImport('env', 'to_string', 0, i64ParamRetType));
    
    // Math operations (take f64, return f64)
    const f64f64Type = builder.addType([TYPE_F64], [TYPE_F64]);
    const f64f64f64Type = builder.addType([TYPE_F64, TYPE_F64], [TYPE_F64]);
    const f64RetType = builder.addType([], [TYPE_F64]);
    
    state.imports.set('math_sqrt', builder.addImport('env', 'math_sqrt', 0, f64f64Type));
    state.imports.set('math_sin', builder.addImport('env', 'math_sin', 0, f64f64Type));
    state.imports.set('math_cos', builder.addImport('env', 'math_cos', 0, f64f64Type));
    state.imports.set('math_floor', builder.addImport('env', 'math_floor', 0, f64f64Type));
    state.imports.set('math_ceil', builder.addImport('env', 'math_ceil', 0, f64f64Type));
    state.imports.set('math_abs', builder.addImport('env', 'math_abs', 0, f64f64Type));
    state.imports.set('math_pow', builder.addImport('env', 'math_pow', 0, f64f64f64Type));
    state.imports.set('math_random', builder.addImport('env', 'math_random', 0, f64RetType));
    
    // Add memory (256 pages = 16MB initial)
    builder.addMemory(256, 2048);
}

/**
 * Register a function (first pass).
 */
function registerFunction(state: CompilerState, func: AST.FunctionStmt): void {
    const paramTypes = func.params.map(() => VALUE_TYPE);
    const resultTypes = [VALUE_TYPE]; // All functions return a Value
    
    const typeIdx = state.builder.addType(paramTypes, resultTypes);
    const funcIndex = state.imports.size + state.functions.size;
    
    state.functions.set(func.name, funcIndex);
}

/**
 * Compile top-level statements.
 */
function compileTopLevel(state: CompilerState, statements: AST.Statement[]): void {
    // Separate functions from other statements
    const functions: AST.FunctionStmt[] = [];
    const mainStatements: AST.Statement[] = [];
    
    for (const stmt of statements) {
        if (stmt.kind === 'Function') {
            functions.push(stmt as AST.FunctionStmt);
        } else {
            mainStatements.push(stmt);
        }
    }
    
    // Compile all functions
    for (const func of functions) {
        compileFunction(state, func);
    }
    
    // Create main function for top-level code
    if (mainStatements.length > 0) {
        const mainFunc: AST.FunctionStmt = {
            kind: 'Function',
            name: 'main',
            params: [],
            body: mainStatements,
            isAsync: false,
            line: 1,
            column: 1
        };
        
        // Register and compile main
        if (!state.functions.has('main')) {
            registerFunction(state, mainFunc);
        }
        compileFunction(state, mainFunc);
        
        // Export main
        const mainIdx = state.functions.get('main')!;
        state.builder.addExport('main', 0, mainIdx);
    }
}

/**
 * Compile a function.
 */
function compileFunction(state: CompilerState, func: AST.FunctionStmt): void {
    const ctx: FunctionContext = {
        name: func.name,
        params: func.params,
        locals: [],
        localIndex: 0,
        body: [],
        loopStack: []
    };
    
    // Add parameters as locals
    for (const param of func.params) {
        ctx.locals.push({
            name: param,
            index: ctx.localIndex++,
            type: VALUE_TYPE
        });
    }
    
    state.currentFunc = ctx;
    
    // Compile function body
    for (const stmt of func.body) {
        compileStatement(state, stmt);
    }
    
    // If no explicit return, return nil
    if (ctx.body.length === 0 || ctx.body[ctx.body.length - 1] !== OP.RETURN) {
        // Push nil value and return
        emitNil(ctx);
        ctx.body.push(OP.RETURN);
    }
    
    // Get local types (excluding params)
    const localTypes: number[] = [];
    for (let i = func.params.length; i < ctx.locals.length; i++) {
        localTypes.push(ctx.locals[i].type);
    }
    
    // Get function type
    const paramTypes = func.params.map(() => VALUE_TYPE);
    const typeIdx = state.builder.addType(paramTypes, [VALUE_TYPE]);
    
    // Add function to builder
    state.builder.addFunction(typeIdx, localTypes, ctx.body);
    
    state.currentFunc = null;
}

/**
 * Compile a statement.
 */
function compileStatement(state: CompilerState, stmt: AST.Statement): void {
    const ctx = state.currentFunc!;
    
    switch (stmt.kind) {
        case 'ExpressionStmt':
            compileExpression(state, (stmt as AST.ExpressionStmt).expression);
            ctx.body.push(OP.DROP); // Discard result
            break;
            
        case 'Let': {
            const letStmt = stmt as AST.LetStmt;
            const local: Local = {
                name: letStmt.name,
                index: ctx.localIndex++,
                type: VALUE_TYPE
            };
            ctx.locals.push(local);
            
            if (letStmt.initializer) {
                compileExpression(state, letStmt.initializer);
            } else {
                emitNil(ctx);
            }
            ctx.body.push(OP.LOCAL_SET, ...encodeULEB128(local.index));
            break;
        }
        
        case 'AssignStmt': {
            const assignStmt = stmt as AST.AssignStmt;
            compileAssignment(state, assignStmt.target, assignStmt.value);
            break;
        }
        
        case 'If':
            compileIf(state, stmt as AST.IfStmt);
            break;
            
        case 'While':
            compileWhile(state, stmt as AST.WhileStmt);
            break;
            
        case 'For':
            compileFor(state, stmt as AST.ForStmt);
            break;
            
        case 'ForRange':
            compileForRange(state, stmt as AST.ForRangeStmt);
            break;
            
        case 'Return': {
            const retStmt = stmt as AST.ReturnStmt;
            if (retStmt.value) {
                compileExpression(state, retStmt.value);
            } else {
                emitNil(ctx);
            }
            ctx.body.push(OP.RETURN);
            break;
        }
        
        default:
            state.errors.push(`Unsupported statement: ${stmt.kind}`);
    }
}

/**
 * Compile an expression.
 */
function compileExpression(state: CompilerState, expr: AST.Expression): void {
    const ctx = state.currentFunc!;
    
    switch (expr.kind) {
        case 'Literal':
            compileLiteral(state, expr as AST.LiteralExpr);
            break;
            
        case 'Identifier': {
            const name = (expr as AST.IdentifierExpr).name;
            const local = ctx.locals.find(l => l.name === name);
            
            if (local) {
                ctx.body.push(OP.LOCAL_GET, ...encodeULEB128(local.index));
            } else if (state.globals.has(name)) {
                ctx.body.push(OP.GLOBAL_GET, ...encodeULEB128(state.globals.get(name)!));
            } else {
                state.errors.push(`Undefined variable: ${name}`);
                emitNil(ctx);
            }
            break;
        }
        
        case 'Binary':
            compileBinary(state, expr as AST.BinaryExpr);
            break;
            
        case 'Unary':
            compileUnary(state, expr as AST.UnaryExpr);
            break;
            
        case 'Logical':
            compileLogical(state, expr as AST.LogicalExpr);
            break;
            
        case 'Call':
            compileCall(state, expr as AST.CallExpr);
            break;
            
        case 'Array':
            compileArray(state, expr as AST.ArrayExpr);
            break;
            
        case 'Index':
            compileIndex(state, expr as AST.IndexExpr);
            break;
            
        case 'Assign':
            compileAssignment(state, (expr as AST.AssignExpr).target, (expr as AST.AssignExpr).value);
            // Leave value on stack
            const assignExpr = expr as AST.AssignExpr;
            compileExpression(state, assignExpr.target);
            break;
            
        default:
            state.errors.push(`Unsupported expression: ${expr.kind}`);
            emitNil(ctx);
    }
}

/**
 * Compile a literal value.
 */
function compileLiteral(state: CompilerState, lit: AST.LiteralExpr): void {
    const ctx = state.currentFunc!;
    
    switch (lit.literalType) {
        case 'nil':
            emitNil(ctx);
            break;
            
        case 'bool':
            emitBool(ctx, lit.value as boolean);
            break;
            
        case 'int':
            emitInt(ctx, lit.value as number);
            break;
            
        case 'float':
            emitFloat(ctx, lit.value as number);
            break;
            
        case 'string':
            emitString(state, lit.value as string);
            break;
    }
}

/**
 * Compile a binary expression.
 */
function compileBinary(state: CompilerState, expr: AST.BinaryExpr): void {
    const ctx = state.currentFunc!;
    
    // Compile operands
    compileExpression(state, expr.left);
    compileExpression(state, expr.right);
    
    // Call runtime function for the operation
    const opMap: Record<string, string> = {
        '+': 'value_add',
        '-': 'value_sub',
        '*': 'value_mul',
        '/': 'value_div',
        '%': 'value_mod',
        '==': 'value_eq',
        '!=': 'value_eq', // Will negate
        '<': 'value_lt',
        '>': 'value_gt',
        '<=': 'value_lte',
        '>=': 'value_gte'
    };
    
    const importName = opMap[expr.operator];
    if (importName) {
        const importIdx = state.imports.get(importName)!;
        ctx.body.push(OP.CALL, ...encodeULEB128(importIdx));
        
        // Negate for !=
        if (expr.operator === '!=') {
            const notIdx = state.imports.get('value_not')!;
            ctx.body.push(OP.CALL, ...encodeULEB128(notIdx));
        }
    } else {
        state.errors.push(`Unknown binary operator: ${expr.operator}`);
    }
}

/**
 * Compile a unary expression.
 */
function compileUnary(state: CompilerState, expr: AST.UnaryExpr): void {
    const ctx = state.currentFunc!;
    
    compileExpression(state, expr.operand);
    
    if (expr.operator === '-') {
        const negIdx = state.imports.get('value_neg')!;
        ctx.body.push(OP.CALL, ...encodeULEB128(negIdx));
    } else if (expr.operator === 'not') {
        const notIdx = state.imports.get('value_not')!;
        ctx.body.push(OP.CALL, ...encodeULEB128(notIdx));
    } else {
        state.errors.push(`Unknown unary operator: ${expr.operator}`);
    }
}

/**
 * Compile a logical expression (short-circuit).
 */
function compileLogical(state: CompilerState, expr: AST.LogicalExpr): void {
    const ctx = state.currentFunc!;
    
    compileExpression(state, expr.left);
    
    // Check truthiness
    const truthyIdx = state.imports.get('value_truthy')!;
    ctx.body.push(OP.CALL, ...encodeULEB128(truthyIdx));
    
    if (expr.operator === 'and') {
        // If falsy, return left value; otherwise evaluate right
        ctx.body.push(OP.IF, VALUE_TYPE);
        compileExpression(state, expr.right);
        ctx.body.push(OP.ELSE);
        emitBool(ctx, false);
        ctx.body.push(OP.END);
    } else {
        // If truthy, return left value; otherwise evaluate right
        ctx.body.push(OP.IF, VALUE_TYPE);
        emitBool(ctx, true);
        ctx.body.push(OP.ELSE);
        compileExpression(state, expr.right);
        ctx.body.push(OP.END);
    }
}

/**
 * Compile a function call.
 */
function compileCall(state: CompilerState, expr: AST.CallExpr): void {
    const ctx = state.currentFunc!;
    
    // Check if it's a builtin function
    if (expr.callee.kind === 'Identifier') {
        const name = (expr.callee as AST.IdentifierExpr).name;
        
        // Handle builtins
        if (name === 'print' || name === 'println') {
            if (expr.args.length > 0) {
                compileExpression(state, expr.args[0]);
            } else {
                emitString(state, '');
            }
            const printIdx = state.imports.get(name)!;
            ctx.body.push(OP.CALL, ...encodeULEB128(printIdx));
            emitNil(ctx); // print returns nil
            return;
        }
        
        if (name === 'len') {
            if (expr.args.length !== 1) {
                state.errors.push('len() requires exactly 1 argument');
                emitNil(ctx);
                return;
            }
            compileExpression(state, expr.args[0]);
            // Extract pointer and call array_len
            // For now, assume it's an array (tagged value)
            ctx.body.push(OP.I32_WRAP_I64); // Get lower 32 bits (pointer)
            ctx.body.push(OP.I32_CONST, ...encodeSLEB128(3));
            ctx.body.push(OP.I32_SHR_U); // Remove tag
            const lenIdx = state.imports.get('array_len')!;
            ctx.body.push(OP.CALL, ...encodeULEB128(lenIdx));
            // Convert to Value
            ctx.body.push(OP.I64_EXTEND_I32_U);
            ctx.body.push(OP.I64_CONST, ...encodeSLEB128(3));
            ctx.body.push(OP.I64_SHL);
            ctx.body.push(OP.I64_CONST, ...encodeSLEB128(2)); // TAG_INT
            ctx.body.push(OP.I64_OR);
            return;
        }
        
        if (name === 'type') {
            if (expr.args.length !== 1) {
                state.errors.push('type() requires exactly 1 argument');
                emitNil(ctx);
                return;
            }
            compileExpression(state, expr.args[0]);
            const typeIdx = state.imports.get('type_of')!;
            ctx.body.push(OP.CALL, ...encodeULEB128(typeIdx));
            return;
        }
        
        if (name === 'int') {
            if (expr.args.length !== 1) {
                state.errors.push('int() requires exactly 1 argument');
                emitNil(ctx);
                return;
            }
            compileExpression(state, expr.args[0]);
            const intIdx = state.imports.get('to_int')!;
            ctx.body.push(OP.CALL, ...encodeULEB128(intIdx));
            return;
        }
        
        if (name === 'str') {
            if (expr.args.length !== 1) {
                state.errors.push('str() requires exactly 1 argument');
                emitNil(ctx);
                return;
            }
            compileExpression(state, expr.args[0]);
            const strIdx = state.imports.get('to_string')!;
            ctx.body.push(OP.CALL, ...encodeULEB128(strIdx));
            return;
        }
        
        // User-defined function
        if (state.functions.has(name)) {
            // Compile arguments
            for (const arg of expr.args) {
                compileExpression(state, arg);
            }
            const funcIdx = state.functions.get(name)!;
            ctx.body.push(OP.CALL, ...encodeULEB128(funcIdx));
            return;
        }
        
        state.errors.push(`Unknown function: ${name}`);
        emitNil(ctx);
        return;
    }
    
    state.errors.push('Complex callees not yet supported');
    emitNil(ctx);
}

/**
 * Compile an array literal.
 */
function compileArray(state: CompilerState, expr: AST.ArrayExpr): void {
    const ctx = state.currentFunc!;
    
    // Create array with capacity
    ctx.body.push(OP.I32_CONST, ...encodeSLEB128(expr.elements.length));
    const newIdx = state.imports.get('array_new')!;
    ctx.body.push(OP.CALL, ...encodeULEB128(newIdx));
    
    // Store array pointer in a temp local
    const tempLocal = ctx.localIndex++;
    ctx.locals.push({ name: '$arr_temp', index: tempLocal, type: TYPE_I32 });
    ctx.body.push(OP.LOCAL_TEE, ...encodeULEB128(tempLocal));
    
    // Push each element
    for (const elem of expr.elements) {
        ctx.body.push(OP.LOCAL_GET, ...encodeULEB128(tempLocal));
        compileExpression(state, elem);
        const pushIdx = state.imports.get('array_push')!;
        ctx.body.push(OP.CALL, ...encodeULEB128(pushIdx));
    }
    
    // Convert array pointer to Value (tagged)
    ctx.body.push(OP.LOCAL_GET, ...encodeULEB128(tempLocal));
    ctx.body.push(OP.I64_EXTEND_I32_U);
    ctx.body.push(OP.I64_CONST, ...encodeSLEB128(3));
    ctx.body.push(OP.I64_SHL);
    ctx.body.push(OP.I64_CONST, ...encodeSLEB128(5)); // TAG_ARRAY
    ctx.body.push(OP.I64_OR);
}

/**
 * Compile an index expression.
 */
function compileIndex(state: CompilerState, expr: AST.IndexExpr): void {
    const ctx = state.currentFunc!;
    
    // Get array pointer
    compileExpression(state, expr.object);
    ctx.body.push(OP.I32_WRAP_I64);
    ctx.body.push(OP.I32_CONST, ...encodeSLEB128(3));
    ctx.body.push(OP.I32_SHR_U); // Remove tag
    
    // Get index
    compileExpression(state, expr.index);
    ctx.body.push(OP.I32_WRAP_I64);
    ctx.body.push(OP.I32_CONST, ...encodeSLEB128(3));
    ctx.body.push(OP.I32_SHR_U); // Remove tag from index
    
    // Call array_get
    const getIdx = state.imports.get('array_get')!;
    ctx.body.push(OP.CALL, ...encodeULEB128(getIdx));
}

/**
 * Compile an assignment.
 */
function compileAssignment(state: CompilerState, target: AST.Expression, value: AST.Expression): void {
    const ctx = state.currentFunc!;
    
    if (target.kind === 'Identifier') {
        const name = (target as AST.IdentifierExpr).name;
        const local = ctx.locals.find(l => l.name === name);
        
        compileExpression(state, value);
        
        if (local) {
            ctx.body.push(OP.LOCAL_SET, ...encodeULEB128(local.index));
        } else if (state.globals.has(name)) {
            ctx.body.push(OP.GLOBAL_SET, ...encodeULEB128(state.globals.get(name)!));
        } else {
            state.errors.push(`Undefined variable: ${name}`);
        }
    } else if (target.kind === 'Index') {
        const indexExpr = target as AST.IndexExpr;
        
        // Get array pointer
        compileExpression(state, indexExpr.object);
        ctx.body.push(OP.I32_WRAP_I64);
        ctx.body.push(OP.I32_CONST, ...encodeSLEB128(3));
        ctx.body.push(OP.I32_SHR_U);
        
        // Get index
        compileExpression(state, indexExpr.index);
        ctx.body.push(OP.I32_WRAP_I64);
        ctx.body.push(OP.I32_CONST, ...encodeSLEB128(3));
        ctx.body.push(OP.I32_SHR_U);
        
        // Get value
        compileExpression(state, value);
        
        // Call array_set
        const setIdx = state.imports.get('array_set')!;
        ctx.body.push(OP.CALL, ...encodeULEB128(setIdx));
    } else {
        state.errors.push('Unsupported assignment target');
    }
}

/**
 * Compile an if statement.
 */
function compileIf(state: CompilerState, stmt: AST.IfStmt): void {
    const ctx = state.currentFunc!;
    
    // Compile condition
    compileExpression(state, stmt.condition);
    const truthyIdx = state.imports.get('value_truthy')!;
    ctx.body.push(OP.CALL, ...encodeULEB128(truthyIdx));
    
    // If block
    ctx.body.push(OP.IF, 0x40); // void block type
    
    for (const s of stmt.thenBranch) {
        compileStatement(state, s);
    }
    
    // Elif branches
    for (const elif of stmt.elifBranches) {
        ctx.body.push(OP.ELSE);
        compileExpression(state, elif.condition);
        ctx.body.push(OP.CALL, ...encodeULEB128(truthyIdx));
        ctx.body.push(OP.IF, 0x40);
        
        for (const s of elif.body) {
            compileStatement(state, s);
        }
    }
    
    // Else branch
    if (stmt.elseBranch) {
        ctx.body.push(OP.ELSE);
        for (const s of stmt.elseBranch) {
            compileStatement(state, s);
        }
    }
    
    // Close all if/elif blocks
    for (let i = 0; i < stmt.elifBranches.length; i++) {
        ctx.body.push(OP.END);
    }
    ctx.body.push(OP.END);
}

/**
 * Compile a while loop.
 */
function compileWhile(state: CompilerState, stmt: AST.WhileStmt): void {
    const ctx = state.currentFunc!;
    
    // Block for break
    ctx.body.push(OP.BLOCK, 0x40);
    // Loop for continue
    ctx.body.push(OP.LOOP, 0x40);
    
    // Condition
    compileExpression(state, stmt.condition);
    const truthyIdx = state.imports.get('value_truthy')!;
    ctx.body.push(OP.CALL, ...encodeULEB128(truthyIdx));
    ctx.body.push(OP.I32_EQZ);
    ctx.body.push(OP.BR_IF, ...encodeULEB128(1)); // Break if false
    
    // Body
    for (const s of stmt.body) {
        compileStatement(state, s);
    }
    
    // Loop back
    ctx.body.push(OP.BR, ...encodeULEB128(0));
    
    ctx.body.push(OP.END); // End loop
    ctx.body.push(OP.END); // End block
}

/**
 * Compile a for-in loop.
 */
function compileFor(state: CompilerState, stmt: AST.ForStmt): void {
    // TODO: Implement for-in iteration
    state.errors.push('for-in loops not yet implemented');
}

/**
 * Compile a for-range loop.
 */
function compileForRange(state: CompilerState, stmt: AST.ForRangeStmt): void {
    const ctx = state.currentFunc!;
    
    // Create loop variable
    const loopVar: Local = {
        name: stmt.variable,
        index: ctx.localIndex++,
        type: VALUE_TYPE
    };
    ctx.locals.push(loopVar);
    
    // Create end variable
    const endVar: Local = {
        name: '$end_temp',
        index: ctx.localIndex++,
        type: VALUE_TYPE
    };
    ctx.locals.push(endVar);
    
    // Initialize loop variable to start
    compileExpression(state, stmt.start);
    ctx.body.push(OP.LOCAL_SET, ...encodeULEB128(loopVar.index));
    
    // Store end value
    compileExpression(state, stmt.end);
    ctx.body.push(OP.LOCAL_SET, ...encodeULEB128(endVar.index));
    
    // Block for break
    ctx.body.push(OP.BLOCK, 0x40);
    // Loop for continue
    ctx.body.push(OP.LOOP, 0x40);
    
    // Check condition: loopVar < end
    ctx.body.push(OP.LOCAL_GET, ...encodeULEB128(loopVar.index));
    ctx.body.push(OP.LOCAL_GET, ...encodeULEB128(endVar.index));
    const ltIdx = state.imports.get('value_lt')!;
    ctx.body.push(OP.CALL, ...encodeULEB128(ltIdx));
    const truthyIdx = state.imports.get('value_truthy')!;
    ctx.body.push(OP.CALL, ...encodeULEB128(truthyIdx));
    ctx.body.push(OP.I32_EQZ);
    ctx.body.push(OP.BR_IF, ...encodeULEB128(1)); // Break if false
    
    // Body
    for (const s of stmt.body) {
        compileStatement(state, s);
    }
    
    // Increment loop variable
    ctx.body.push(OP.LOCAL_GET, ...encodeULEB128(loopVar.index));
    emitInt(ctx, 1);
    const addIdx = state.imports.get('value_add')!;
    ctx.body.push(OP.CALL, ...encodeULEB128(addIdx));
    ctx.body.push(OP.LOCAL_SET, ...encodeULEB128(loopVar.index));
    
    // Loop back
    ctx.body.push(OP.BR, ...encodeULEB128(0));
    
    ctx.body.push(OP.END); // End loop
    ctx.body.push(OP.END); // End block
}

// ============ Value Emission Helpers ============

/**
 * Emit nil value.
 */
function emitNil(ctx: FunctionContext): void {
    // TAG_NIL = 0
    ctx.body.push(OP.I64_CONST, ...encodeSLEB128(0));
}

/**
 * Emit boolean value.
 */
function emitBool(ctx: FunctionContext, value: boolean): void {
    // TAG_BOOL = 1, payload in bits 3+
    const encoded = 1n | (BigInt(value ? 1 : 0) << 3n);
    ctx.body.push(OP.I64_CONST, ...encodeSLEB128(Number(encoded)));
}

/**
 * Emit integer value.
 */
function emitInt(ctx: FunctionContext, value: number): void {
    // TAG_INT = 2, value in bits 3+
    const encoded = 2n | (BigInt(Math.trunc(value)) << 3n);
    ctx.body.push(OP.I64_CONST, ...encodeSLEB128(Number(encoded)));
}

/**
 * Emit float value.
 */
function emitFloat(ctx: FunctionContext, value: number): void {
    // TAG_FLOAT = 3, f64 bits in payload
    const buffer = new ArrayBuffer(8);
    const view = new DataView(buffer);
    view.setFloat64(0, value, true);
    const bits = view.getBigUint64(0, true);
    const encoded = 3n | (bits << 3n);
    
    // Emit full 64-bit value using SLEB128 encoding
    // encodeSLEB128 handles BigInt properly for full i64 range
    ctx.body.push(OP.I64_CONST, ...encodeSLEB128BigInt(encoded));
}

/**
 * Emit string value.
 */
function emitString(state: CompilerState, value: string): void {
    const ctx = state.currentFunc!;
    
    // Get or create string offset
    let offset = state.strings.get(value);
    if (offset === undefined) {
        offset = state.stringDataOffset;
        state.strings.set(value, offset);
        
        // Calculate next offset
        const encoder = new TextEncoder();
        const encoded = encoder.encode(value);
        state.stringDataOffset += 4 + encoded.length + 1; // length + data + null
    }
    
    // Call string_new with offset and length
    ctx.body.push(OP.I32_CONST, ...encodeSLEB128(offset));
    ctx.body.push(OP.I32_CONST, ...encodeSLEB128(value.length));
    const strNewIdx = state.imports.get('string_new')!;
    ctx.body.push(OP.CALL, ...encodeULEB128(strNewIdx));
}
