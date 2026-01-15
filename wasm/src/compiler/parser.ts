/*
 * Pseudocode WASM Compiler - Parser
 * 
 * Pratt parser that converts tokens into an AST.
 * Ported from the C implementation in cvm/compiler.c
 */

import { Token, TokenType, Lexer } from './lexer';
import * as AST from './ast';

/**
 * Precedence levels for Pratt parsing.
 */
enum Precedence {
    NONE = 0,
    ASSIGNMENT = 1,  // =
    OR = 2,          // or
    AND = 3,         // and
    EQUALITY = 4,    // == !=
    COMPARISON = 5,  // < > <= >=
    TERM = 6,        // + -
    FACTOR = 7,      // * / %
    UNARY = 8,       // not -
    CALL = 9,        // . () []
    PRIMARY = 10
}

/**
 * Parse rule for Pratt parsing.
 */
interface ParseRule {
    prefix: ((parser: Parser, canAssign: boolean) => AST.Expression) | null;
    infix: ((parser: Parser, left: AST.Expression, canAssign: boolean) => AST.Expression) | null;
    precedence: Precedence;
}

/**
 * Parser for Pseudocode.
 */
export class Parser {
    private tokens: Token[];
    private current: number = 0;
    private errors: string[] = [];
    
    // Parse rules table
    private rules: Map<TokenType, ParseRule>;
    
    constructor(source: string) {
        const lexer = new Lexer(source);
        this.tokens = lexer.tokenize();
        this.rules = this.initRules();
    }
    
    /**
     * Parse the source code and return the AST.
     */
    parse(): AST.Program {
        const statements: AST.Statement[] = [];
        
        while (!this.isAtEnd()) {
            try {
                const stmt = this.declaration();
                if (stmt) {
                    statements.push(stmt);
                }
            } catch (error) {
                this.synchronize();
            }
        }
        
        return AST.program(statements);
    }
    
    /**
     * Get parsing errors.
     */
    getErrors(): string[] {
        return this.errors;
    }
    
    /**
     * Check if parsing succeeded.
     */
    hasErrors(): boolean {
        return this.errors.length > 0;
    }
    
    // ============ Token Helpers ============
    
    private peek(): Token {
        return this.tokens[this.current];
    }
    
    private previous(): Token {
        return this.tokens[this.current - 1];
    }
    
    private isAtEnd(): boolean {
        return this.peek().type === TokenType.EOF;
    }
    
    private check(type: TokenType): boolean {
        if (this.isAtEnd()) return false;
        return this.peek().type === type;
    }
    
    private advance(): Token {
        if (!this.isAtEnd()) this.current++;
        return this.previous();
    }
    
    private match(...types: TokenType[]): boolean {
        for (const type of types) {
            if (this.check(type)) {
                this.advance();
                return true;
            }
        }
        return false;
    }
    
    private consume(type: TokenType, message: string): Token {
        if (this.check(type)) return this.advance();
        
        this.error(message);
        throw new Error(message);
    }
    
    private error(message: string): void {
        const token = this.peek();
        this.errors.push(`[Line ${token.line}:${token.column}] Error: ${message}`);
    }
    
    private synchronize(): void {
        this.advance();
        
        while (!this.isAtEnd()) {
            // Skip to next statement boundary
            switch (this.peek().type) {
                case TokenType.FN:
                case TokenType.LET:
                case TokenType.CONST:
                case TokenType.IF:
                case TokenType.WHILE:
                case TokenType.FOR:
                case TokenType.RETURN:
                case TokenType.CLASS:
                case TokenType.MATCH:
                case TokenType.TRY:
                case TokenType.IMPORT:
                case TokenType.EXPORT:
                    return;
            }
            this.advance();
        }
    }
    
    // ============ Declarations ============
    
    private declaration(): AST.Statement | null {
        if (this.match(TokenType.FN)) return this.functionDeclaration();
        if (this.match(TokenType.LET)) return this.letDeclaration(false);
        if (this.match(TokenType.CONST)) return this.letDeclaration(true);
        if (this.match(TokenType.CLASS)) return this.classDeclaration();
        if (this.match(TokenType.ENUM)) return this.enumDeclaration();
        if (this.match(TokenType.IMPORT)) return this.importDeclaration();
        if (this.match(TokenType.EXPORT)) return this.exportDeclaration();
        
        return this.statement();
    }
    
    private functionDeclaration(isAsync: boolean = false): AST.FunctionStmt {
        const token = this.consume(TokenType.IDENT, "Expected function name");
        const name = token.value;
        
        this.consume(TokenType.LPAREN, "Expected '(' after function name");
        
        const params: string[] = [];
        if (!this.check(TokenType.RPAREN)) {
            do {
                const param = this.consume(TokenType.IDENT, "Expected parameter name");
                params.push(param.value);
            } while (this.match(TokenType.COMMA));
        }
        
        this.consume(TokenType.RPAREN, "Expected ')' after parameters");
        
        const body = this.block();
        
        return AST.funcStmt(name, params, body, isAsync, token.line, token.column);
    }
    
    private letDeclaration(isConst: boolean): AST.LetStmt {
        const token = this.consume(TokenType.IDENT, "Expected variable name");
        const name = token.value;
        
        let initializer: AST.Expression | null = null;
        if (this.match(TokenType.ASSIGN)) {
            initializer = this.expression();
        } else if (isConst) {
            this.error("Const declarations must have an initializer");
        }
        
        return AST.letStmt(name, initializer, isConst, token.line, token.column);
    }
    
    private classDeclaration(): AST.ClassStmt {
        const token = this.consume(TokenType.IDENT, "Expected class name");
        const name = token.value;
        
        let superclass: string | null = null;
        if (this.match(TokenType.EXTENDS)) {
            const superToken = this.consume(TokenType.IDENT, "Expected superclass name");
            superclass = superToken.value;
        }
        
        const methods: AST.FunctionStmt[] = [];
        const fields: string[] = [];
        const staticMethods: AST.FunctionStmt[] = [];
        const staticFields: { name: string; initializer: AST.Expression }[] = [];
        
        while (!this.check(TokenType.END) && !this.isAtEnd()) {
            const isStatic = this.match(TokenType.STATIC);
            
            if (this.match(TokenType.FN)) {
                const method = this.functionDeclaration();
                if (isStatic) {
                    staticMethods.push(method);
                } else {
                    methods.push(method);
                }
            } else if (this.match(TokenType.LET)) {
                const fieldToken = this.consume(TokenType.IDENT, "Expected field name");
                if (isStatic && this.match(TokenType.ASSIGN)) {
                    const init = this.expression();
                    staticFields.push({ name: fieldToken.value, initializer: init });
                } else {
                    fields.push(fieldToken.value);
                }
            }
        }
        
        this.consume(TokenType.END, "Expected 'end' after class body");
        
        return {
            kind: 'Class',
            name,
            superclass,
            methods,
            fields,
            staticMethods,
            staticFields,
            line: token.line,
            column: token.column
        };
    }
    
    private enumDeclaration(): AST.EnumStmt {
        const token = this.consume(TokenType.IDENT, "Expected enum name");
        const name = token.value;
        
        this.consume(TokenType.ASSIGN, "Expected '=' after enum name");
        
        const values: string[] = [];
        do {
            const valueToken = this.consume(TokenType.IDENT, "Expected enum value");
            values.push(valueToken.value);
        } while (this.match(TokenType.COMMA));
        
        this.consume(TokenType.END, "Expected 'end' after enum values");
        
        return {
            kind: 'Enum',
            name,
            values,
            line: token.line,
            column: token.column
        };
    }
    
    private importDeclaration(): AST.ImportStmt {
        const token = this.previous();
        
        // import x from "module"
        // import x, y from "module"
        // import "module" as alias
        
        const symbols: { name: string; alias: string | null }[] = [];
        let moduleName: string;
        let alias: string | null = null;
        
        if (this.check(TokenType.STRING)) {
            // import "module" as alias
            moduleName = this.advance().value;
            if (this.match(TokenType.AS)) {
                const aliasToken = this.consume(TokenType.IDENT, "Expected alias name");
                alias = aliasToken.value;
            }
        } else {
            // import x, y from "module"
            do {
                const nameToken = this.consume(TokenType.IDENT, "Expected import name");
                let importAlias: string | null = null;
                if (this.match(TokenType.AS)) {
                    const aliasToken = this.consume(TokenType.IDENT, "Expected alias");
                    importAlias = aliasToken.value;
                }
                symbols.push({ name: nameToken.value, alias: importAlias });
            } while (this.match(TokenType.COMMA));
            
            this.consume(TokenType.FROM, "Expected 'from' after import names");
            moduleName = this.consume(TokenType.STRING, "Expected module path").value;
        }
        
        return {
            kind: 'Import',
            module: moduleName,
            symbols,
            alias,
            line: token.line,
            column: token.column
        };
    }
    
    private exportDeclaration(): AST.ExportStmt {
        const token = this.previous();
        const declaration = this.declaration();
        
        if (!declaration) {
            this.error("Expected declaration after 'export'");
            throw new Error("Expected declaration");
        }
        
        return {
            kind: 'Export',
            declaration,
            line: token.line,
            column: token.column
        };
    }
    
    // ============ Statements ============
    
    private statement(): AST.Statement {
        if (this.match(TokenType.IF)) return this.ifStatement();
        if (this.match(TokenType.WHILE)) return this.whileStatement();
        if (this.match(TokenType.FOR)) return this.forStatement();
        if (this.match(TokenType.RETURN)) return this.returnStatement();
        if (this.match(TokenType.MATCH)) return this.matchStatement();
        if (this.match(TokenType.TRY)) return this.tryStatement();
        if (this.match(TokenType.THROW)) return this.throwStatement();
        if (this.match(TokenType.YIELD)) return this.yieldStatement();
        
        return this.expressionStatement();
    }
    
    private block(): AST.Statement[] {
        const statements: AST.Statement[] = [];
        
        while (!this.check(TokenType.END) && 
               !this.check(TokenType.ELSE) && 
               !this.check(TokenType.ELIF) &&
               !this.check(TokenType.CATCH) &&
               !this.check(TokenType.FINALLY) &&
               !this.check(TokenType.CASE) &&
               !this.isAtEnd()) {
            const stmt = this.declaration();
            if (stmt) statements.push(stmt);
        }
        
        return statements;
    }
    
    private ifStatement(): AST.IfStmt {
        const token = this.previous();
        const condition = this.expression();
        
        this.consume(TokenType.THEN, "Expected 'then' after if condition");
        
        const thenBranch = this.block();
        const elifBranches: { condition: AST.Expression; body: AST.Statement[] }[] = [];
        let elseBranch: AST.Statement[] | null = null;
        
        while (this.match(TokenType.ELIF)) {
            const elifCond = this.expression();
            this.consume(TokenType.THEN, "Expected 'then' after elif condition");
            const elifBody = this.block();
            elifBranches.push({ condition: elifCond, body: elifBody });
        }
        
        if (this.match(TokenType.ELSE)) {
            elseBranch = this.block();
        }
        
        this.consume(TokenType.END, "Expected 'end' after if statement");
        
        return AST.ifStmt(condition, thenBranch, elifBranches, elseBranch, token.line, token.column);
    }
    
    private whileStatement(): AST.WhileStmt {
        const token = this.previous();
        const condition = this.expression();
        
        this.consume(TokenType.DO, "Expected 'do' after while condition");
        
        const body = this.block();
        
        this.consume(TokenType.END, "Expected 'end' after while body");
        
        return AST.whileStmt(condition, body, token.line, token.column);
    }
    
    private forStatement(): AST.Statement {
        const token = this.previous();
        const varToken = this.consume(TokenType.IDENT, "Expected variable name");
        const variable = varToken.value;
        
        this.consume(TokenType.IN, "Expected 'in' after for variable");
        
        const iterableOrStart = this.expression();
        
        // Check if it's a range: for i in 1 to 10
        if (this.match(TokenType.TO)) {
            const end = this.expression();
            this.consume(TokenType.DO, "Expected 'do' after range");
            const body = this.block();
            this.consume(TokenType.END, "Expected 'end' after for body");
            
            return {
                kind: 'ForRange',
                variable,
                start: iterableOrStart,
                end,
                body,
                line: token.line,
                column: token.column
            } as AST.ForRangeStmt;
        }
        
        // Check for .. range syntax: for i in 1..10
        // This is handled as a Range expression in the iterable
        
        this.consume(TokenType.DO, "Expected 'do' after for iterable");
        const body = this.block();
        this.consume(TokenType.END, "Expected 'end' after for body");
        
        return AST.forStmt(variable, iterableOrStart, body, token.line, token.column);
    }
    
    private returnStatement(): AST.ReturnStmt {
        const token = this.previous();
        let value: AST.Expression | null = null;
        
        // Check if there's a return value (not followed by end/else/etc.)
        if (!this.check(TokenType.END) && 
            !this.check(TokenType.ELSE) && 
            !this.check(TokenType.ELIF) &&
            !this.check(TokenType.EOF)) {
            value = this.expression();
        }
        
        return AST.returnStmt(value, token.line, token.column);
    }
    
    private matchStatement(): AST.MatchStmt {
        const token = this.previous();
        const subject = this.expression();
        
        const cases: AST.MatchCase[] = [];
        
        while (this.match(TokenType.CASE)) {
            const pattern = this.parsePattern();
            let guard: AST.Expression | null = null;
            
            if (this.match(TokenType.IF)) {
                guard = this.expression();
            }
            
            this.consume(TokenType.THEN, "Expected 'then' after case pattern");
            const body = this.block();
            
            cases.push({ pattern, guard, body });
        }
        
        this.consume(TokenType.END, "Expected 'end' after match statement");
        
        return {
            kind: 'Match',
            subject,
            cases,
            line: token.line,
            column: token.column
        };
    }
    
    private parsePattern(): AST.Pattern {
        if (this.match(TokenType.UNDERSCORE)) {
            return { kind: 'WildcardPattern' };
        }
        
        if (this.check(TokenType.INT) || this.check(TokenType.FLOAT) || 
            this.check(TokenType.STRING) || this.check(TokenType.TRUE) || 
            this.check(TokenType.FALSE) || this.check(TokenType.NIL)) {
            const lit = this.primary() as AST.LiteralExpr;
            return { kind: 'LiteralPattern', value: lit };
        }
        
        if (this.match(TokenType.LBRACKET)) {
            const elements: AST.Pattern[] = [];
            if (!this.check(TokenType.RBRACKET)) {
                do {
                    elements.push(this.parsePattern());
                } while (this.match(TokenType.COMMA));
            }
            this.consume(TokenType.RBRACKET, "Expected ']' after array pattern");
            return { kind: 'ArrayPattern', elements };
        }
        
        if (this.check(TokenType.IDENT)) {
            const name = this.advance().value;
            return { kind: 'IdentifierPattern', name };
        }
        
        this.error("Expected pattern");
        return { kind: 'WildcardPattern' };
    }
    
    private tryStatement(): AST.TryStmt {
        const token = this.previous();
        
        const tryBlock = this.block();
        
        let catchVariable: string | null = null;
        let catchBlock: AST.Statement[] | null = null;
        let finallyBlock: AST.Statement[] | null = null;
        
        if (this.match(TokenType.CATCH)) {
            if (this.check(TokenType.IDENT)) {
                catchVariable = this.advance().value;
            }
            catchBlock = this.block();
        }
        
        if (this.match(TokenType.FINALLY)) {
            finallyBlock = this.block();
        }
        
        this.consume(TokenType.END, "Expected 'end' after try statement");
        
        return {
            kind: 'Try',
            tryBlock,
            catchVariable,
            catchBlock,
            finallyBlock,
            line: token.line,
            column: token.column
        };
    }
    
    private throwStatement(): AST.ThrowStmt {
        const token = this.previous();
        const value = this.expression();
        
        return {
            kind: 'Throw',
            value,
            line: token.line,
            column: token.column
        };
    }
    
    private yieldStatement(): AST.YieldStmt {
        const token = this.previous();
        let value: AST.Expression | null = null;
        
        if (!this.check(TokenType.END) && !this.check(TokenType.EOF)) {
            value = this.expression();
        }
        
        return {
            kind: 'Yield',
            value,
            line: token.line,
            column: token.column
        };
    }
    
    private expressionStatement(): AST.ExpressionStmt | AST.AssignStmt {
        const token = this.peek();
        const expr = this.expression();
        
        // Check if this is an assignment
        if (expr.kind === 'Assign') {
            return {
                kind: 'AssignStmt',
                target: (expr as AST.AssignExpr).target,
                value: (expr as AST.AssignExpr).value,
                line: token.line,
                column: token.column
            };
        }
        
        return {
            kind: 'ExpressionStmt',
            expression: expr,
            line: token.line,
            column: token.column
        };
    }
    
    // ============ Expressions (Pratt Parser) ============
    
    private expression(): AST.Expression {
        return this.parsePrecedence(Precedence.ASSIGNMENT);
    }
    
    private parsePrecedence(precedence: Precedence): AST.Expression {
        const token = this.advance();
        const rule = this.rules.get(token.type);
        
        if (!rule || !rule.prefix) {
            this.error(`Unexpected token '${token.value}'`);
            throw new Error(`Unexpected token: ${token.type}`);
        }
        
        const canAssign = precedence <= Precedence.ASSIGNMENT;
        let left = rule.prefix(this, canAssign);
        
        while (precedence <= this.getRule(this.peek().type).precedence) {
            const infixToken = this.advance();
            const infixRule = this.rules.get(infixToken.type);
            
            if (infixRule && infixRule.infix) {
                left = infixRule.infix(this, left, canAssign);
            }
        }
        
        return left;
    }
    
    private getRule(type: TokenType): ParseRule {
        return this.rules.get(type) || { prefix: null, infix: null, precedence: Precedence.NONE };
    }
    
    // ============ Parse Rule Implementations ============
    
    private initRules(): Map<TokenType, ParseRule> {
        const rules = new Map<TokenType, ParseRule>();
        
        // Literals
        rules.set(TokenType.INT, { prefix: (p) => p.number(), infix: null, precedence: Precedence.NONE });
        rules.set(TokenType.FLOAT, { prefix: (p) => p.number(), infix: null, precedence: Precedence.NONE });
        rules.set(TokenType.STRING, { prefix: (p) => p.string(), infix: null, precedence: Precedence.NONE });
        rules.set(TokenType.TRUE, { prefix: (p) => p.literal(), infix: null, precedence: Precedence.NONE });
        rules.set(TokenType.FALSE, { prefix: (p) => p.literal(), infix: null, precedence: Precedence.NONE });
        rules.set(TokenType.NIL, { prefix: (p) => p.literal(), infix: null, precedence: Precedence.NONE });
        
        // Identifiers
        rules.set(TokenType.IDENT, { prefix: (p, c) => p.variable(c), infix: null, precedence: Precedence.NONE });
        rules.set(TokenType.SELF, { prefix: (p) => p.self(), infix: null, precedence: Precedence.NONE });
        rules.set(TokenType.SUPER, { prefix: (p) => p.superExpr(), infix: null, precedence: Precedence.NONE });
        
        // Grouping and arrays
        rules.set(TokenType.LPAREN, { prefix: (p) => p.grouping(), infix: (p, l) => p.callExpr(l), precedence: Precedence.CALL });
        rules.set(TokenType.LBRACKET, { prefix: (p) => p.array(), infix: (p, l) => p.indexExpr(l), precedence: Precedence.CALL });
        rules.set(TokenType.LBRACE, { prefix: (p) => p.dict(), infix: null, precedence: Precedence.NONE });
        
        // Unary operators
        rules.set(TokenType.MINUS, { prefix: (p) => p.unaryExpr(), infix: (p, l) => p.binaryExpr(l), precedence: Precedence.TERM });
        rules.set(TokenType.NOT, { prefix: (p) => p.unaryExpr(), infix: null, precedence: Precedence.NONE });
        
        // Binary operators
        rules.set(TokenType.PLUS, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.TERM });
        rules.set(TokenType.STAR, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.FACTOR });
        rules.set(TokenType.SLASH, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.FACTOR });
        rules.set(TokenType.PERCENT, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.FACTOR });
        
        // Comparison
        rules.set(TokenType.EQ, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.EQUALITY });
        rules.set(TokenType.NEQ, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.EQUALITY });
        rules.set(TokenType.LT, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.COMPARISON });
        rules.set(TokenType.GT, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.COMPARISON });
        rules.set(TokenType.LTE, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.COMPARISON });
        rules.set(TokenType.GTE, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.COMPARISON });
        
        // Logical
        rules.set(TokenType.AND, { prefix: null, infix: (p, l) => p.logicalExpr(l), precedence: Precedence.AND });
        rules.set(TokenType.OR, { prefix: null, infix: (p, l) => p.logicalExpr(l), precedence: Precedence.OR });
        
        // Member access and range
        rules.set(TokenType.DOT, { prefix: null, infix: (p, l) => p.memberExpr(l), precedence: Precedence.CALL });
        rules.set(TokenType.RANGE, { prefix: null, infix: (p, l) => p.rangeExpr(l), precedence: Precedence.COMPARISON });
        
        // Lambda
        rules.set(TokenType.FN, { prefix: (p) => p.lambdaExpr(), infix: null, precedence: Precedence.NONE });
        
        return rules;
    }
    
    // ============ Primary Expressions ============
    
    private primary(): AST.Expression {
        return this.parsePrecedence(Precedence.PRIMARY);
    }
    
    private number(): AST.LiteralExpr {
        const token = this.previous();
        const value = token.type === TokenType.INT 
            ? parseInt(token.value, 10) 
            : parseFloat(token.value);
        return AST.literal(value, token.line, token.column);
    }
    
    private string(): AST.LiteralExpr {
        const token = this.previous();
        return AST.literal(token.value, token.line, token.column);
    }
    
    private literal(): AST.LiteralExpr {
        const token = this.previous();
        let value: boolean | null;
        
        switch (token.type) {
            case TokenType.TRUE: value = true; break;
            case TokenType.FALSE: value = false; break;
            case TokenType.NIL: value = null; break;
            default: throw new Error(`Unexpected literal: ${token.type}`);
        }
        
        return AST.literal(value, token.line, token.column);
    }
    
    private variable(canAssign: boolean): AST.Expression {
        const token = this.previous();
        const name = token.value;
        
        if (canAssign && this.match(TokenType.ASSIGN)) {
            const value = this.expression();
            return {
                kind: 'Assign',
                target: AST.identifier(name, token.line, token.column),
                value,
                line: token.line,
                column: token.column
            };
        }
        
        return AST.identifier(name, token.line, token.column);
    }
    
    private self(): AST.SelfExpr {
        const token = this.previous();
        return {
            kind: 'Self',
            line: token.line,
            column: token.column
        };
    }
    
    private superExpr(): AST.SuperExpr {
        const token = this.previous();
        this.consume(TokenType.DOT, "Expected '.' after 'super'");
        const method = this.consume(TokenType.IDENT, "Expected superclass method name");
        
        return {
            kind: 'Super',
            method: method.value,
            line: token.line,
            column: token.column
        };
    }
    
    private grouping(): AST.Expression {
        const expr = this.expression();
        this.consume(TokenType.RPAREN, "Expected ')' after expression");
        return expr;
    }
    
    private array(): AST.ArrayExpr {
        const token = this.previous();
        const elements: AST.Expression[] = [];
        
        if (!this.check(TokenType.RBRACKET)) {
            do {
                elements.push(this.expression());
            } while (this.match(TokenType.COMMA));
        }
        
        this.consume(TokenType.RBRACKET, "Expected ']' after array elements");
        
        return {
            kind: 'Array',
            elements,
            line: token.line,
            column: token.column
        };
    }
    
    private dict(): AST.DictExpr {
        const token = this.previous();
        const entries: { key: AST.Expression; value: AST.Expression }[] = [];
        
        if (!this.check(TokenType.RBRACE)) {
            do {
                const key = this.expression();
                this.consume(TokenType.COLON, "Expected ':' after dictionary key");
                const value = this.expression();
                entries.push({ key, value });
            } while (this.match(TokenType.COMMA));
        }
        
        this.consume(TokenType.RBRACE, "Expected '}' after dictionary entries");
        
        return {
            kind: 'Dict',
            entries,
            line: token.line,
            column: token.column
        };
    }
    
    private lambdaExpr(): AST.LambdaExpr {
        const token = this.previous();
        
        this.consume(TokenType.LPAREN, "Expected '(' after 'fn'");
        
        const params: string[] = [];
        if (!this.check(TokenType.RPAREN)) {
            do {
                const param = this.consume(TokenType.IDENT, "Expected parameter name");
                params.push(param.value);
            } while (this.match(TokenType.COMMA));
        }
        
        this.consume(TokenType.RPAREN, "Expected ')' after parameters");
        
        // Lambda body can be a single expression with 'return' or a block
        let body: AST.Statement[];
        if (this.match(TokenType.RETURN)) {
            const expr = this.expression();
            body = [AST.returnStmt(expr)];
            this.consume(TokenType.END, "Expected 'end' after lambda");
        } else {
            body = this.block();
            this.consume(TokenType.END, "Expected 'end' after lambda body");
        }
        
        return {
            kind: 'Lambda',
            params,
            body,
            line: token.line,
            column: token.column
        };
    }
    
    // ============ Infix Expressions ============
    
    private unaryExpr(): AST.UnaryExpr {
        const token = this.previous();
        const operand = this.parsePrecedence(Precedence.UNARY);
        
        return AST.unary(token.value, operand, token.line, token.column);
    }
    
    private binaryExpr(left: AST.Expression): AST.BinaryExpr {
        const token = this.previous();
        const rule = this.getRule(token.type);
        const right = this.parsePrecedence(rule.precedence + 1);
        
        return AST.binary(token.value, left, right, token.line, token.column);
    }
    
    private logicalExpr(left: AST.Expression): AST.LogicalExpr {
        const token = this.previous();
        const operator = token.type === TokenType.AND ? 'and' : 'or';
        const rule = this.getRule(token.type);
        const right = this.parsePrecedence(rule.precedence + 1);
        
        return {
            kind: 'Logical',
            operator,
            left,
            right,
            line: token.line,
            column: token.column
        };
    }
    
    private callExpr(callee: AST.Expression): AST.CallExpr {
        const token = this.previous();
        const args: AST.Expression[] = [];
        
        if (!this.check(TokenType.RPAREN)) {
            do {
                args.push(this.expression());
            } while (this.match(TokenType.COMMA));
        }
        
        this.consume(TokenType.RPAREN, "Expected ')' after arguments");
        
        return AST.call(callee, args, token.line, token.column);
    }
    
    private indexExpr(object: AST.Expression): AST.IndexExpr {
        const token = this.previous();
        const index = this.expression();
        this.consume(TokenType.RBRACKET, "Expected ']' after index");
        
        return {
            kind: 'Index',
            object,
            index,
            line: token.line,
            column: token.column
        };
    }
    
    private memberExpr(object: AST.Expression): AST.Expression {
        const token = this.previous();
        const property = this.consume(TokenType.IDENT, "Expected property name after '.'");
        
        // Check for method call
        if (this.match(TokenType.LPAREN)) {
            const args: AST.Expression[] = [];
            if (!this.check(TokenType.RPAREN)) {
                do {
                    args.push(this.expression());
                } while (this.match(TokenType.COMMA));
            }
            this.consume(TokenType.RPAREN, "Expected ')' after arguments");
            
            // Return as a call with member access as callee
            const member: AST.MemberExpr = {
                kind: 'Member',
                object,
                property: property.value,
                line: token.line,
                column: token.column
            };
            return AST.call(member, args, token.line, token.column);
        }
        
        return {
            kind: 'Member',
            object,
            property: property.value,
            line: token.line,
            column: token.column
        };
    }
    
    private rangeExpr(start: AST.Expression): AST.RangeExpr {
        const token = this.previous();
        const end = this.parsePrecedence(Precedence.COMPARISON + 1);
        
        return {
            kind: 'Range',
            start,
            end,
            line: token.line,
            column: token.column
        };
    }
}

/**
 * Parse source code and return the AST.
 */
export function parse(source: string): AST.Program {
    const parser = new Parser(source);
    return parser.parse();
}
