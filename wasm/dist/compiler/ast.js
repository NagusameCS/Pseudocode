/*
 * Pseudocode WASM Compiler - AST Types
 *
 * Abstract Syntax Tree node definitions for Pseudocode.
 */
// ============ AST Helpers ============
export function literal(value, line = 0, column = 0) {
    let literalType;
    if (value === null)
        literalType = 'nil';
    else if (typeof value === 'boolean')
        literalType = 'bool';
    else if (typeof value === 'string')
        literalType = 'string';
    else if (Number.isInteger(value))
        literalType = 'int';
    else
        literalType = 'float';
    return { kind: 'Literal', value, literalType, line, column };
}
export function identifier(name, line = 0, column = 0) {
    return { kind: 'Identifier', name, line, column };
}
export function binary(op, left, right, line = 0, column = 0) {
    return { kind: 'Binary', operator: op, left, right, line, column };
}
export function unary(op, operand, line = 0, column = 0) {
    return { kind: 'Unary', operator: op, operand, line, column };
}
export function call(callee, args, line = 0, column = 0) {
    return { kind: 'Call', callee, args, line, column };
}
export function letStmt(name, init, isConst = false, line = 0, column = 0) {
    return { kind: 'Let', name, initializer: init, isConst, line, column };
}
export function ifStmt(cond, thenB, elifB = [], elseB = null, line = 0, column = 0) {
    return { kind: 'If', condition: cond, thenBranch: thenB, elifBranches: elifB, elseBranch: elseB, line, column };
}
export function whileStmt(cond, body, line = 0, column = 0) {
    return { kind: 'While', condition: cond, body, line, column };
}
export function forStmt(variable, iterable, body, line = 0, column = 0) {
    return { kind: 'For', variable, iterable, body, line, column };
}
export function funcStmt(name, params, body, isAsync = false, line = 0, column = 0) {
    return { kind: 'Function', name, params, body, isAsync, line, column };
}
export function returnStmt(value = null, line = 0, column = 0) {
    return { kind: 'Return', value, line, column };
}
export function program(statements) {
    return { kind: 'Program', statements, line: 1, column: 1 };
}
//# sourceMappingURL=ast.js.map