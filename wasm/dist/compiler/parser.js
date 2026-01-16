"use strict";
/*
 * Pseudocode WASM Compiler - Parser
 *
 * Pratt parser that converts tokens into an AST.
 * Ported from the C implementation in cvm/compiler.c
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
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.Parser = void 0;
exports.parse = parse;
const lexer_1 = require("./lexer");
const AST = __importStar(require("./ast"));
/**
 * Precedence levels for Pratt parsing.
 */
var Precedence;
(function (Precedence) {
    Precedence[Precedence["NONE"] = 0] = "NONE";
    Precedence[Precedence["ASSIGNMENT"] = 1] = "ASSIGNMENT";
    Precedence[Precedence["OR"] = 2] = "OR";
    Precedence[Precedence["AND"] = 3] = "AND";
    Precedence[Precedence["EQUALITY"] = 4] = "EQUALITY";
    Precedence[Precedence["COMPARISON"] = 5] = "COMPARISON";
    Precedence[Precedence["TERM"] = 6] = "TERM";
    Precedence[Precedence["FACTOR"] = 7] = "FACTOR";
    Precedence[Precedence["UNARY"] = 8] = "UNARY";
    Precedence[Precedence["CALL"] = 9] = "CALL";
    Precedence[Precedence["PRIMARY"] = 10] = "PRIMARY";
})(Precedence || (Precedence = {}));
/**
 * Parser for Pseudocode.
 */
class Parser {
    tokens;
    current = 0;
    errors = [];
    // Parse rules table
    rules;
    constructor(source) {
        const lexer = new lexer_1.Lexer(source);
        this.tokens = lexer.tokenize();
        this.rules = this.initRules();
    }
    /**
     * Parse the source code and return the AST.
     */
    parse() {
        const statements = [];
        while (!this.isAtEnd()) {
            try {
                const stmt = this.declaration();
                if (stmt) {
                    statements.push(stmt);
                }
            }
            catch (error) {
                this.synchronize();
            }
        }
        return AST.program(statements);
    }
    /**
     * Get parsing errors.
     */
    getErrors() {
        return this.errors;
    }
    /**
     * Check if parsing succeeded.
     */
    hasErrors() {
        return this.errors.length > 0;
    }
    // ============ Token Helpers ============
    peek() {
        return this.tokens[this.current];
    }
    previous() {
        return this.tokens[this.current - 1];
    }
    isAtEnd() {
        return this.peek().type === lexer_1.TokenType.EOF;
    }
    check(type) {
        if (this.isAtEnd())
            return false;
        return this.peek().type === type;
    }
    advance() {
        if (!this.isAtEnd())
            this.current++;
        return this.previous();
    }
    match(...types) {
        for (const type of types) {
            if (this.check(type)) {
                this.advance();
                return true;
            }
        }
        return false;
    }
    consume(type, message) {
        if (this.check(type))
            return this.advance();
        this.error(message);
        throw new Error(message);
    }
    error(message) {
        const token = this.peek();
        this.errors.push(`[Line ${token.line}:${token.column}] Error: ${message}`);
    }
    synchronize() {
        this.advance();
        while (!this.isAtEnd()) {
            // Skip to next statement boundary
            switch (this.peek().type) {
                case lexer_1.TokenType.FN:
                case lexer_1.TokenType.LET:
                case lexer_1.TokenType.CONST:
                case lexer_1.TokenType.IF:
                case lexer_1.TokenType.WHILE:
                case lexer_1.TokenType.FOR:
                case lexer_1.TokenType.RETURN:
                case lexer_1.TokenType.CLASS:
                case lexer_1.TokenType.MATCH:
                case lexer_1.TokenType.TRY:
                case lexer_1.TokenType.IMPORT:
                case lexer_1.TokenType.EXPORT:
                    return;
            }
            this.advance();
        }
    }
    // ============ Declarations ============
    declaration() {
        if (this.match(lexer_1.TokenType.FN))
            return this.functionDeclaration();
        if (this.match(lexer_1.TokenType.LET))
            return this.letDeclaration(false);
        if (this.match(lexer_1.TokenType.CONST))
            return this.letDeclaration(true);
        if (this.match(lexer_1.TokenType.CLASS))
            return this.classDeclaration();
        if (this.match(lexer_1.TokenType.ENUM))
            return this.enumDeclaration();
        if (this.match(lexer_1.TokenType.IMPORT))
            return this.importDeclaration();
        if (this.match(lexer_1.TokenType.EXPORT))
            return this.exportDeclaration();
        return this.statement();
    }
    functionDeclaration(isAsync = false) {
        const token = this.consume(lexer_1.TokenType.IDENT, "Expected function name");
        const name = token.value;
        this.consume(lexer_1.TokenType.LPAREN, "Expected '(' after function name");
        const params = [];
        if (!this.check(lexer_1.TokenType.RPAREN)) {
            do {
                const param = this.consume(lexer_1.TokenType.IDENT, "Expected parameter name");
                params.push(param.value);
            } while (this.match(lexer_1.TokenType.COMMA));
        }
        this.consume(lexer_1.TokenType.RPAREN, "Expected ')' after parameters");
        const body = this.block();
        return AST.funcStmt(name, params, body, isAsync, token.line, token.column);
    }
    letDeclaration(isConst) {
        const token = this.consume(lexer_1.TokenType.IDENT, "Expected variable name");
        const name = token.value;
        let initializer = null;
        if (this.match(lexer_1.TokenType.ASSIGN)) {
            initializer = this.expression();
        }
        else if (isConst) {
            this.error("Const declarations must have an initializer");
        }
        return AST.letStmt(name, initializer, isConst, token.line, token.column);
    }
    classDeclaration() {
        const token = this.consume(lexer_1.TokenType.IDENT, "Expected class name");
        const name = token.value;
        let superclass = null;
        if (this.match(lexer_1.TokenType.EXTENDS)) {
            const superToken = this.consume(lexer_1.TokenType.IDENT, "Expected superclass name");
            superclass = superToken.value;
        }
        const methods = [];
        const fields = [];
        const staticMethods = [];
        const staticFields = [];
        while (!this.check(lexer_1.TokenType.END) && !this.isAtEnd()) {
            const isStatic = this.match(lexer_1.TokenType.STATIC);
            if (this.match(lexer_1.TokenType.FN)) {
                const method = this.functionDeclaration();
                if (isStatic) {
                    staticMethods.push(method);
                }
                else {
                    methods.push(method);
                }
            }
            else if (this.match(lexer_1.TokenType.LET)) {
                const fieldToken = this.consume(lexer_1.TokenType.IDENT, "Expected field name");
                if (isStatic && this.match(lexer_1.TokenType.ASSIGN)) {
                    const init = this.expression();
                    staticFields.push({ name: fieldToken.value, initializer: init });
                }
                else {
                    fields.push(fieldToken.value);
                }
            }
        }
        this.consume(lexer_1.TokenType.END, "Expected 'end' after class body");
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
    enumDeclaration() {
        const token = this.consume(lexer_1.TokenType.IDENT, "Expected enum name");
        const name = token.value;
        this.consume(lexer_1.TokenType.ASSIGN, "Expected '=' after enum name");
        const values = [];
        do {
            const valueToken = this.consume(lexer_1.TokenType.IDENT, "Expected enum value");
            values.push(valueToken.value);
        } while (this.match(lexer_1.TokenType.COMMA));
        this.consume(lexer_1.TokenType.END, "Expected 'end' after enum values");
        return {
            kind: 'Enum',
            name,
            values,
            line: token.line,
            column: token.column
        };
    }
    importDeclaration() {
        const token = this.previous();
        // import x from "module"
        // import x, y from "module"
        // import "module" as alias
        const symbols = [];
        let moduleName;
        let alias = null;
        if (this.check(lexer_1.TokenType.STRING)) {
            // import "module" as alias
            moduleName = this.advance().value;
            if (this.match(lexer_1.TokenType.AS)) {
                const aliasToken = this.consume(lexer_1.TokenType.IDENT, "Expected alias name");
                alias = aliasToken.value;
            }
        }
        else {
            // import x, y from "module"
            do {
                const nameToken = this.consume(lexer_1.TokenType.IDENT, "Expected import name");
                let importAlias = null;
                if (this.match(lexer_1.TokenType.AS)) {
                    const aliasToken = this.consume(lexer_1.TokenType.IDENT, "Expected alias");
                    importAlias = aliasToken.value;
                }
                symbols.push({ name: nameToken.value, alias: importAlias });
            } while (this.match(lexer_1.TokenType.COMMA));
            this.consume(lexer_1.TokenType.FROM, "Expected 'from' after import names");
            moduleName = this.consume(lexer_1.TokenType.STRING, "Expected module path").value;
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
    exportDeclaration() {
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
    statement() {
        if (this.match(lexer_1.TokenType.IF))
            return this.ifStatement();
        if (this.match(lexer_1.TokenType.WHILE))
            return this.whileStatement();
        if (this.match(lexer_1.TokenType.FOR))
            return this.forStatement();
        if (this.match(lexer_1.TokenType.RETURN))
            return this.returnStatement();
        if (this.match(lexer_1.TokenType.MATCH))
            return this.matchStatement();
        if (this.match(lexer_1.TokenType.TRY))
            return this.tryStatement();
        if (this.match(lexer_1.TokenType.THROW))
            return this.throwStatement();
        if (this.match(lexer_1.TokenType.YIELD))
            return this.yieldStatement();
        return this.expressionStatement();
    }
    block() {
        const statements = [];
        while (!this.check(lexer_1.TokenType.END) &&
            !this.check(lexer_1.TokenType.ELSE) &&
            !this.check(lexer_1.TokenType.ELIF) &&
            !this.check(lexer_1.TokenType.CATCH) &&
            !this.check(lexer_1.TokenType.FINALLY) &&
            !this.check(lexer_1.TokenType.CASE) &&
            !this.isAtEnd()) {
            const stmt = this.declaration();
            if (stmt)
                statements.push(stmt);
        }
        return statements;
    }
    ifStatement() {
        const token = this.previous();
        const condition = this.expression();
        this.consume(lexer_1.TokenType.THEN, "Expected 'then' after if condition");
        const thenBranch = this.block();
        const elifBranches = [];
        let elseBranch = null;
        while (this.match(lexer_1.TokenType.ELIF)) {
            const elifCond = this.expression();
            this.consume(lexer_1.TokenType.THEN, "Expected 'then' after elif condition");
            const elifBody = this.block();
            elifBranches.push({ condition: elifCond, body: elifBody });
        }
        if (this.match(lexer_1.TokenType.ELSE)) {
            elseBranch = this.block();
        }
        this.consume(lexer_1.TokenType.END, "Expected 'end' after if statement");
        return AST.ifStmt(condition, thenBranch, elifBranches, elseBranch, token.line, token.column);
    }
    whileStatement() {
        const token = this.previous();
        const condition = this.expression();
        this.consume(lexer_1.TokenType.DO, "Expected 'do' after while condition");
        const body = this.block();
        this.consume(lexer_1.TokenType.END, "Expected 'end' after while body");
        return AST.whileStmt(condition, body, token.line, token.column);
    }
    forStatement() {
        const token = this.previous();
        const varToken = this.consume(lexer_1.TokenType.IDENT, "Expected variable name");
        const variable = varToken.value;
        this.consume(lexer_1.TokenType.IN, "Expected 'in' after for variable");
        const iterableOrStart = this.expression();
        // Check if it's a range: for i in 1 to 10
        if (this.match(lexer_1.TokenType.TO)) {
            const end = this.expression();
            this.consume(lexer_1.TokenType.DO, "Expected 'do' after range");
            const body = this.block();
            this.consume(lexer_1.TokenType.END, "Expected 'end' after for body");
            return {
                kind: 'ForRange',
                variable,
                start: iterableOrStart,
                end,
                body,
                line: token.line,
                column: token.column
            };
        }
        // Check for .. range syntax: for i in 1..10
        // This is handled as a Range expression in the iterable
        this.consume(lexer_1.TokenType.DO, "Expected 'do' after for iterable");
        const body = this.block();
        this.consume(lexer_1.TokenType.END, "Expected 'end' after for body");
        return AST.forStmt(variable, iterableOrStart, body, token.line, token.column);
    }
    returnStatement() {
        const token = this.previous();
        let value = null;
        // Check if there's a return value (not followed by end/else/etc.)
        if (!this.check(lexer_1.TokenType.END) &&
            !this.check(lexer_1.TokenType.ELSE) &&
            !this.check(lexer_1.TokenType.ELIF) &&
            !this.check(lexer_1.TokenType.EOF)) {
            value = this.expression();
        }
        return AST.returnStmt(value, token.line, token.column);
    }
    matchStatement() {
        const token = this.previous();
        const subject = this.expression();
        const cases = [];
        while (this.match(lexer_1.TokenType.CASE)) {
            const pattern = this.parsePattern();
            let guard = null;
            if (this.match(lexer_1.TokenType.IF)) {
                guard = this.expression();
            }
            this.consume(lexer_1.TokenType.THEN, "Expected 'then' after case pattern");
            const body = this.block();
            cases.push({ pattern, guard, body });
        }
        this.consume(lexer_1.TokenType.END, "Expected 'end' after match statement");
        return {
            kind: 'Match',
            subject,
            cases,
            line: token.line,
            column: token.column
        };
    }
    parsePattern() {
        if (this.match(lexer_1.TokenType.UNDERSCORE)) {
            return { kind: 'WildcardPattern' };
        }
        if (this.check(lexer_1.TokenType.INT) || this.check(lexer_1.TokenType.FLOAT) ||
            this.check(lexer_1.TokenType.STRING) || this.check(lexer_1.TokenType.TRUE) ||
            this.check(lexer_1.TokenType.FALSE) || this.check(lexer_1.TokenType.NIL)) {
            const lit = this.primary();
            return { kind: 'LiteralPattern', value: lit };
        }
        if (this.match(lexer_1.TokenType.LBRACKET)) {
            const elements = [];
            if (!this.check(lexer_1.TokenType.RBRACKET)) {
                do {
                    elements.push(this.parsePattern());
                } while (this.match(lexer_1.TokenType.COMMA));
            }
            this.consume(lexer_1.TokenType.RBRACKET, "Expected ']' after array pattern");
            return { kind: 'ArrayPattern', elements };
        }
        if (this.check(lexer_1.TokenType.IDENT)) {
            const name = this.advance().value;
            return { kind: 'IdentifierPattern', name };
        }
        this.error("Expected pattern");
        return { kind: 'WildcardPattern' };
    }
    tryStatement() {
        const token = this.previous();
        const tryBlock = this.block();
        let catchVariable = null;
        let catchBlock = null;
        let finallyBlock = null;
        if (this.match(lexer_1.TokenType.CATCH)) {
            if (this.check(lexer_1.TokenType.IDENT)) {
                catchVariable = this.advance().value;
            }
            catchBlock = this.block();
        }
        if (this.match(lexer_1.TokenType.FINALLY)) {
            finallyBlock = this.block();
        }
        this.consume(lexer_1.TokenType.END, "Expected 'end' after try statement");
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
    throwStatement() {
        const token = this.previous();
        const value = this.expression();
        return {
            kind: 'Throw',
            value,
            line: token.line,
            column: token.column
        };
    }
    yieldStatement() {
        const token = this.previous();
        let value = null;
        if (!this.check(lexer_1.TokenType.END) && !this.check(lexer_1.TokenType.EOF)) {
            value = this.expression();
        }
        return {
            kind: 'Yield',
            value,
            line: token.line,
            column: token.column
        };
    }
    expressionStatement() {
        const token = this.peek();
        const expr = this.expression();
        // Check if this is an assignment
        if (expr.kind === 'Assign') {
            return {
                kind: 'AssignStmt',
                target: expr.target,
                value: expr.value,
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
    expression() {
        return this.parsePrecedence(Precedence.ASSIGNMENT);
    }
    parsePrecedence(precedence) {
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
    getRule(type) {
        return this.rules.get(type) || { prefix: null, infix: null, precedence: Precedence.NONE };
    }
    // ============ Parse Rule Implementations ============
    initRules() {
        const rules = new Map();
        // Literals
        rules.set(lexer_1.TokenType.INT, { prefix: (p) => p.number(), infix: null, precedence: Precedence.NONE });
        rules.set(lexer_1.TokenType.FLOAT, { prefix: (p) => p.number(), infix: null, precedence: Precedence.NONE });
        rules.set(lexer_1.TokenType.STRING, { prefix: (p) => p.string(), infix: null, precedence: Precedence.NONE });
        rules.set(lexer_1.TokenType.TRUE, { prefix: (p) => p.literal(), infix: null, precedence: Precedence.NONE });
        rules.set(lexer_1.TokenType.FALSE, { prefix: (p) => p.literal(), infix: null, precedence: Precedence.NONE });
        rules.set(lexer_1.TokenType.NIL, { prefix: (p) => p.literal(), infix: null, precedence: Precedence.NONE });
        // Identifiers
        rules.set(lexer_1.TokenType.IDENT, { prefix: (p, c) => p.variable(c), infix: null, precedence: Precedence.NONE });
        rules.set(lexer_1.TokenType.SELF, { prefix: (p) => p.self(), infix: null, precedence: Precedence.NONE });
        rules.set(lexer_1.TokenType.SUPER, { prefix: (p) => p.superExpr(), infix: null, precedence: Precedence.NONE });
        // Grouping and arrays
        rules.set(lexer_1.TokenType.LPAREN, { prefix: (p) => p.grouping(), infix: (p, l) => p.callExpr(l), precedence: Precedence.CALL });
        rules.set(lexer_1.TokenType.LBRACKET, { prefix: (p) => p.array(), infix: (p, l) => p.indexExpr(l), precedence: Precedence.CALL });
        rules.set(lexer_1.TokenType.LBRACE, { prefix: (p) => p.dict(), infix: null, precedence: Precedence.NONE });
        // Unary operators
        rules.set(lexer_1.TokenType.MINUS, { prefix: (p) => p.unaryExpr(), infix: (p, l) => p.binaryExpr(l), precedence: Precedence.TERM });
        rules.set(lexer_1.TokenType.NOT, { prefix: (p) => p.unaryExpr(), infix: null, precedence: Precedence.NONE });
        // Binary operators
        rules.set(lexer_1.TokenType.PLUS, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.TERM });
        rules.set(lexer_1.TokenType.STAR, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.FACTOR });
        rules.set(lexer_1.TokenType.SLASH, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.FACTOR });
        rules.set(lexer_1.TokenType.PERCENT, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.FACTOR });
        // Comparison
        rules.set(lexer_1.TokenType.EQ, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.EQUALITY });
        rules.set(lexer_1.TokenType.NEQ, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.EQUALITY });
        rules.set(lexer_1.TokenType.LT, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.COMPARISON });
        rules.set(lexer_1.TokenType.GT, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.COMPARISON });
        rules.set(lexer_1.TokenType.LTE, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.COMPARISON });
        rules.set(lexer_1.TokenType.GTE, { prefix: null, infix: (p, l) => p.binaryExpr(l), precedence: Precedence.COMPARISON });
        // Logical
        rules.set(lexer_1.TokenType.AND, { prefix: null, infix: (p, l) => p.logicalExpr(l), precedence: Precedence.AND });
        rules.set(lexer_1.TokenType.OR, { prefix: null, infix: (p, l) => p.logicalExpr(l), precedence: Precedence.OR });
        // Member access and range
        rules.set(lexer_1.TokenType.DOT, { prefix: null, infix: (p, l) => p.memberExpr(l), precedence: Precedence.CALL });
        rules.set(lexer_1.TokenType.RANGE, { prefix: null, infix: (p, l) => p.rangeExpr(l), precedence: Precedence.COMPARISON });
        // Lambda
        rules.set(lexer_1.TokenType.FN, { prefix: (p) => p.lambdaExpr(), infix: null, precedence: Precedence.NONE });
        return rules;
    }
    // ============ Primary Expressions ============
    primary() {
        return this.parsePrecedence(Precedence.PRIMARY);
    }
    number() {
        const token = this.previous();
        const value = token.type === lexer_1.TokenType.INT
            ? parseInt(token.value, 10)
            : parseFloat(token.value);
        return AST.literal(value, token.line, token.column);
    }
    string() {
        const token = this.previous();
        return AST.literal(token.value, token.line, token.column);
    }
    literal() {
        const token = this.previous();
        let value;
        switch (token.type) {
            case lexer_1.TokenType.TRUE:
                value = true;
                break;
            case lexer_1.TokenType.FALSE:
                value = false;
                break;
            case lexer_1.TokenType.NIL:
                value = null;
                break;
            default: throw new Error(`Unexpected literal: ${token.type}`);
        }
        return AST.literal(value, token.line, token.column);
    }
    variable(canAssign) {
        const token = this.previous();
        const name = token.value;
        if (canAssign && this.match(lexer_1.TokenType.ASSIGN)) {
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
    self() {
        const token = this.previous();
        return {
            kind: 'Self',
            line: token.line,
            column: token.column
        };
    }
    superExpr() {
        const token = this.previous();
        this.consume(lexer_1.TokenType.DOT, "Expected '.' after 'super'");
        const method = this.consume(lexer_1.TokenType.IDENT, "Expected superclass method name");
        return {
            kind: 'Super',
            method: method.value,
            line: token.line,
            column: token.column
        };
    }
    grouping() {
        const expr = this.expression();
        this.consume(lexer_1.TokenType.RPAREN, "Expected ')' after expression");
        return expr;
    }
    array() {
        const token = this.previous();
        const elements = [];
        if (!this.check(lexer_1.TokenType.RBRACKET)) {
            do {
                elements.push(this.expression());
            } while (this.match(lexer_1.TokenType.COMMA));
        }
        this.consume(lexer_1.TokenType.RBRACKET, "Expected ']' after array elements");
        return {
            kind: 'Array',
            elements,
            line: token.line,
            column: token.column
        };
    }
    dict() {
        const token = this.previous();
        const entries = [];
        if (!this.check(lexer_1.TokenType.RBRACE)) {
            do {
                const key = this.expression();
                this.consume(lexer_1.TokenType.COLON, "Expected ':' after dictionary key");
                const value = this.expression();
                entries.push({ key, value });
            } while (this.match(lexer_1.TokenType.COMMA));
        }
        this.consume(lexer_1.TokenType.RBRACE, "Expected '}' after dictionary entries");
        return {
            kind: 'Dict',
            entries,
            line: token.line,
            column: token.column
        };
    }
    lambdaExpr() {
        const token = this.previous();
        this.consume(lexer_1.TokenType.LPAREN, "Expected '(' after 'fn'");
        const params = [];
        if (!this.check(lexer_1.TokenType.RPAREN)) {
            do {
                const param = this.consume(lexer_1.TokenType.IDENT, "Expected parameter name");
                params.push(param.value);
            } while (this.match(lexer_1.TokenType.COMMA));
        }
        this.consume(lexer_1.TokenType.RPAREN, "Expected ')' after parameters");
        // Lambda body can be a single expression with 'return' or a block
        let body;
        if (this.match(lexer_1.TokenType.RETURN)) {
            const expr = this.expression();
            body = [AST.returnStmt(expr)];
            this.consume(lexer_1.TokenType.END, "Expected 'end' after lambda");
        }
        else {
            body = this.block();
            this.consume(lexer_1.TokenType.END, "Expected 'end' after lambda body");
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
    unaryExpr() {
        const token = this.previous();
        const operand = this.parsePrecedence(Precedence.UNARY);
        return AST.unary(token.value, operand, token.line, token.column);
    }
    binaryExpr(left) {
        const token = this.previous();
        const rule = this.getRule(token.type);
        const right = this.parsePrecedence(rule.precedence + 1);
        return AST.binary(token.value, left, right, token.line, token.column);
    }
    logicalExpr(left) {
        const token = this.previous();
        const operator = token.type === lexer_1.TokenType.AND ? 'and' : 'or';
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
    callExpr(callee) {
        const token = this.previous();
        const args = [];
        if (!this.check(lexer_1.TokenType.RPAREN)) {
            do {
                args.push(this.expression());
            } while (this.match(lexer_1.TokenType.COMMA));
        }
        this.consume(lexer_1.TokenType.RPAREN, "Expected ')' after arguments");
        return AST.call(callee, args, token.line, token.column);
    }
    indexExpr(object) {
        const token = this.previous();
        const index = this.expression();
        this.consume(lexer_1.TokenType.RBRACKET, "Expected ']' after index");
        return {
            kind: 'Index',
            object,
            index,
            line: token.line,
            column: token.column
        };
    }
    memberExpr(object) {
        const token = this.previous();
        const property = this.consume(lexer_1.TokenType.IDENT, "Expected property name after '.'");
        // Check for method call
        if (this.match(lexer_1.TokenType.LPAREN)) {
            const args = [];
            if (!this.check(lexer_1.TokenType.RPAREN)) {
                do {
                    args.push(this.expression());
                } while (this.match(lexer_1.TokenType.COMMA));
            }
            this.consume(lexer_1.TokenType.RPAREN, "Expected ')' after arguments");
            // Return as a call with member access as callee
            const member = {
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
    rangeExpr(start) {
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
exports.Parser = Parser;
/**
 * Parse source code and return the AST.
 */
function parse(source) {
    const parser = new Parser(source);
    return parser.parse();
}
//# sourceMappingURL=parser.js.map