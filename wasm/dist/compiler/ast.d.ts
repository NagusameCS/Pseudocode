/**
 * Base interface for all AST nodes.
 */
export interface ASTNode {
    kind: string;
    line: number;
    column: number;
}
export interface LiteralExpr extends ASTNode {
    kind: 'Literal';
    value: number | string | boolean | null;
    literalType: 'int' | 'float' | 'string' | 'bool' | 'nil';
}
export interface IdentifierExpr extends ASTNode {
    kind: 'Identifier';
    name: string;
}
export interface UnaryExpr extends ASTNode {
    kind: 'Unary';
    operator: string;
    operand: Expression;
}
export interface BinaryExpr extends ASTNode {
    kind: 'Binary';
    operator: string;
    left: Expression;
    right: Expression;
}
export interface LogicalExpr extends ASTNode {
    kind: 'Logical';
    operator: 'and' | 'or';
    left: Expression;
    right: Expression;
}
export interface CallExpr extends ASTNode {
    kind: 'Call';
    callee: Expression;
    args: Expression[];
}
export interface IndexExpr extends ASTNode {
    kind: 'Index';
    object: Expression;
    index: Expression;
}
export interface MemberExpr extends ASTNode {
    kind: 'Member';
    object: Expression;
    property: string;
}
export interface ArrayExpr extends ASTNode {
    kind: 'Array';
    elements: Expression[];
}
export interface DictExpr extends ASTNode {
    kind: 'Dict';
    entries: {
        key: Expression;
        value: Expression;
    }[];
}
export interface RangeExpr extends ASTNode {
    kind: 'Range';
    start: Expression;
    end: Expression;
}
export interface LambdaExpr extends ASTNode {
    kind: 'Lambda';
    params: string[];
    body: Statement[];
}
export interface TernaryExpr extends ASTNode {
    kind: 'Ternary';
    condition: Expression;
    thenBranch: Expression;
    elseBranch: Expression;
}
export interface AssignExpr extends ASTNode {
    kind: 'Assign';
    target: Expression;
    value: Expression;
}
export interface SelfExpr extends ASTNode {
    kind: 'Self';
}
export interface SuperExpr extends ASTNode {
    kind: 'Super';
    method: string;
}
export type Expression = LiteralExpr | IdentifierExpr | UnaryExpr | BinaryExpr | LogicalExpr | CallExpr | IndexExpr | MemberExpr | ArrayExpr | DictExpr | RangeExpr | LambdaExpr | TernaryExpr | AssignExpr | SelfExpr | SuperExpr;
export interface ExpressionStmt extends ASTNode {
    kind: 'ExpressionStmt';
    expression: Expression;
}
export interface LetStmt extends ASTNode {
    kind: 'Let';
    name: string;
    initializer: Expression | null;
    isConst: boolean;
}
export interface AssignStmt extends ASTNode {
    kind: 'AssignStmt';
    target: Expression;
    value: Expression;
}
export interface IfStmt extends ASTNode {
    kind: 'If';
    condition: Expression;
    thenBranch: Statement[];
    elifBranches: {
        condition: Expression;
        body: Statement[];
    }[];
    elseBranch: Statement[] | null;
}
export interface WhileStmt extends ASTNode {
    kind: 'While';
    condition: Expression;
    body: Statement[];
}
export interface ForStmt extends ASTNode {
    kind: 'For';
    variable: string;
    iterable: Expression;
    body: Statement[];
}
export interface ForRangeStmt extends ASTNode {
    kind: 'ForRange';
    variable: string;
    start: Expression;
    end: Expression;
    body: Statement[];
}
export interface FunctionStmt extends ASTNode {
    kind: 'Function';
    name: string;
    params: string[];
    body: Statement[];
    isAsync: boolean;
}
export interface ReturnStmt extends ASTNode {
    kind: 'Return';
    value: Expression | null;
}
export interface MatchStmt extends ASTNode {
    kind: 'Match';
    subject: Expression;
    cases: MatchCase[];
}
export interface MatchCase {
    pattern: Pattern;
    guard: Expression | null;
    body: Statement[];
}
export type Pattern = {
    kind: 'LiteralPattern';
    value: LiteralExpr;
} | {
    kind: 'IdentifierPattern';
    name: string;
} | {
    kind: 'WildcardPattern';
} | {
    kind: 'ArrayPattern';
    elements: Pattern[];
};
export interface ClassStmt extends ASTNode {
    kind: 'Class';
    name: string;
    superclass: string | null;
    methods: FunctionStmt[];
    fields: string[];
    staticMethods: FunctionStmt[];
    staticFields: {
        name: string;
        initializer: Expression;
    }[];
}
export interface TryStmt extends ASTNode {
    kind: 'Try';
    tryBlock: Statement[];
    catchVariable: string | null;
    catchBlock: Statement[] | null;
    finallyBlock: Statement[] | null;
}
export interface ThrowStmt extends ASTNode {
    kind: 'Throw';
    value: Expression;
}
export interface YieldStmt extends ASTNode {
    kind: 'Yield';
    value: Expression | null;
}
export interface ImportStmt extends ASTNode {
    kind: 'Import';
    module: string;
    symbols: {
        name: string;
        alias: string | null;
    }[];
    alias: string | null;
}
export interface ExportStmt extends ASTNode {
    kind: 'Export';
    declaration: Statement;
}
export interface EnumStmt extends ASTNode {
    kind: 'Enum';
    name: string;
    values: string[];
}
export type Statement = ExpressionStmt | LetStmt | AssignStmt | IfStmt | WhileStmt | ForStmt | ForRangeStmt | FunctionStmt | ReturnStmt | MatchStmt | ClassStmt | TryStmt | ThrowStmt | YieldStmt | ImportStmt | ExportStmt | EnumStmt;
export interface Program extends ASTNode {
    kind: 'Program';
    statements: Statement[];
}
export declare function literal(value: number | string | boolean | null, line?: number, column?: number): LiteralExpr;
export declare function identifier(name: string, line?: number, column?: number): IdentifierExpr;
export declare function binary(op: string, left: Expression, right: Expression, line?: number, column?: number): BinaryExpr;
export declare function unary(op: string, operand: Expression, line?: number, column?: number): UnaryExpr;
export declare function call(callee: Expression, args: Expression[], line?: number, column?: number): CallExpr;
export declare function letStmt(name: string, init: Expression | null, isConst?: boolean, line?: number, column?: number): LetStmt;
export declare function ifStmt(cond: Expression, thenB: Statement[], elifB?: {
    condition: Expression;
    body: Statement[];
}[], elseB?: Statement[] | null, line?: number, column?: number): IfStmt;
export declare function whileStmt(cond: Expression, body: Statement[], line?: number, column?: number): WhileStmt;
export declare function forStmt(variable: string, iterable: Expression, body: Statement[], line?: number, column?: number): ForStmt;
export declare function funcStmt(name: string, params: string[], body: Statement[], isAsync?: boolean, line?: number, column?: number): FunctionStmt;
export declare function returnStmt(value?: Expression | null, line?: number, column?: number): ReturnStmt;
export declare function program(statements: Statement[]): Program;
//# sourceMappingURL=ast.d.ts.map