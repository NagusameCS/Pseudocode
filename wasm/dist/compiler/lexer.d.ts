/**
 * Token types for Pseudocode language.
 */
export declare enum TokenType {
    INT = "INT",
    FLOAT = "FLOAT",
    STRING = "STRING",
    TRUE = "TRUE",
    FALSE = "FALSE",
    IDENT = "IDENT",
    LET = "LET",
    CONST = "CONST",
    FN = "FN",
    RETURN = "RETURN",
    IF = "IF",
    THEN = "THEN",
    ELIF = "ELIF",
    ELSE = "ELSE",
    END = "END",
    WHILE = "WHILE",
    FOR = "FOR",
    IN = "IN",
    TO = "TO",
    DO = "DO",
    AND = "AND",
    OR = "OR",
    NOT = "NOT",
    MATCH = "MATCH",
    CASE = "CASE",
    TRY = "TRY",
    CATCH = "CATCH",
    FINALLY = "FINALLY",
    THROW = "THROW",
    CLASS = "CLASS",
    EXTENDS = "EXTENDS",
    SELF = "SELF",
    SUPER = "SUPER",
    NIL = "NIL",
    ENUM = "ENUM",
    YIELD = "YIELD",
    ASYNC = "ASYNC",
    AWAIT = "AWAIT",
    STATIC = "STATIC",
    FROM = "FROM",
    AS = "AS",
    MODULE = "MODULE",
    EXPORT = "EXPORT",
    IMPORT = "IMPORT",
    PLUS = "PLUS",
    MINUS = "MINUS",
    STAR = "STAR",
    SLASH = "SLASH",
    PERCENT = "PERCENT",
    EQ = "EQ",
    NEQ = "NEQ",
    LT = "LT",
    GT = "GT",
    LTE = "LTE",
    GTE = "GTE",
    ASSIGN = "ASSIGN",
    ARROW = "ARROW",
    RANGE = "RANGE",
    LPAREN = "LPAREN",
    RPAREN = "RPAREN",
    LBRACKET = "LBRACKET",
    RBRACKET = "RBRACKET",
    LBRACE = "LBRACE",
    RBRACE = "RBRACE",
    COMMA = "COMMA",
    DOT = "DOT",
    COLON = "COLON",
    UNDERSCORE = "UNDERSCORE",
    NEWLINE = "NEWLINE",
    EOF = "EOF",
    ERROR = "ERROR"
}
/**
 * A token with its type, value, and location information.
 */
export interface Token {
    type: TokenType;
    value: string;
    line: number;
    column: number;
}
/**
 * Lexer for Pseudocode source code.
 */
export declare class Lexer {
    private source;
    private pos;
    private line;
    private column;
    private tokens;
    constructor(source: string);
    /**
     * Tokenize the entire source and return all tokens.
     */
    tokenize(): Token[];
    /**
     * Get the next token (for streaming use).
     */
    nextToken(): Token;
    private isAtEnd;
    private peek;
    private peekNext;
    private advance;
    private match;
    private makeToken;
    private skipWhitespaceAndComments;
    private scanToken;
    private scanString;
    private scanNumber;
    private scanIdentifier;
    private isDigit;
    private isAlpha;
    private isAlphaNumeric;
}
/**
 * Tokenize source code and return tokens.
 */
export declare function tokenize(source: string): Token[];
//# sourceMappingURL=lexer.d.ts.map