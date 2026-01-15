import * as AST from './ast';
/**
 * Parser for Pseudocode.
 */
export declare class Parser {
    private tokens;
    private current;
    private errors;
    private rules;
    constructor(source: string);
    /**
     * Parse the source code and return the AST.
     */
    parse(): AST.Program;
    /**
     * Get parsing errors.
     */
    getErrors(): string[];
    /**
     * Check if parsing succeeded.
     */
    hasErrors(): boolean;
    private peek;
    private previous;
    private isAtEnd;
    private check;
    private advance;
    private match;
    private consume;
    private error;
    private synchronize;
    private declaration;
    private functionDeclaration;
    private letDeclaration;
    private classDeclaration;
    private enumDeclaration;
    private importDeclaration;
    private exportDeclaration;
    private statement;
    private block;
    private ifStatement;
    private whileStatement;
    private forStatement;
    private returnStatement;
    private matchStatement;
    private parsePattern;
    private tryStatement;
    private throwStatement;
    private yieldStatement;
    private expressionStatement;
    private expression;
    private parsePrecedence;
    private getRule;
    private initRules;
    private primary;
    private number;
    private string;
    private literal;
    private variable;
    private self;
    private superExpr;
    private grouping;
    private array;
    private dict;
    private lambdaExpr;
    private unaryExpr;
    private binaryExpr;
    private logicalExpr;
    private callExpr;
    private indexExpr;
    private memberExpr;
    private rangeExpr;
}
/**
 * Parse source code and return the AST.
 */
export declare function parse(source: string): AST.Program;
//# sourceMappingURL=parser.d.ts.map