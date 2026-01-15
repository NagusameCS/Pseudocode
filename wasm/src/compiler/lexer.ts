/*
 * Pseudocode WASM Compiler - Lexer
 * 
 * Tokenizes Pseudocode source code into a stream of tokens.
 * Ported from the C implementation in cvm/lexer.c
 */

/**
 * Token types for Pseudocode language.
 */
export enum TokenType {
    // Literals
    INT = 'INT',
    FLOAT = 'FLOAT',
    STRING = 'STRING',
    TRUE = 'TRUE',
    FALSE = 'FALSE',
    
    // Identifiers & Keywords
    IDENT = 'IDENT',
    LET = 'LET',
    CONST = 'CONST',
    FN = 'FN',
    RETURN = 'RETURN',
    IF = 'IF',
    THEN = 'THEN',
    ELIF = 'ELIF',
    ELSE = 'ELSE',
    END = 'END',
    WHILE = 'WHILE',
    FOR = 'FOR',
    IN = 'IN',
    TO = 'TO',
    DO = 'DO',
    AND = 'AND',
    OR = 'OR',
    NOT = 'NOT',
    MATCH = 'MATCH',
    CASE = 'CASE',
    TRY = 'TRY',
    CATCH = 'CATCH',
    FINALLY = 'FINALLY',
    THROW = 'THROW',
    CLASS = 'CLASS',
    EXTENDS = 'EXTENDS',
    SELF = 'SELF',
    SUPER = 'SUPER',
    NIL = 'NIL',
    ENUM = 'ENUM',
    
    // Advanced features
    YIELD = 'YIELD',
    ASYNC = 'ASYNC',
    AWAIT = 'AWAIT',
    STATIC = 'STATIC',
    FROM = 'FROM',
    AS = 'AS',
    MODULE = 'MODULE',
    EXPORT = 'EXPORT',
    IMPORT = 'IMPORT',
    
    // Operators
    PLUS = 'PLUS',
    MINUS = 'MINUS',
    STAR = 'STAR',
    SLASH = 'SLASH',
    PERCENT = 'PERCENT',
    EQ = 'EQ',
    NEQ = 'NEQ',
    LT = 'LT',
    GT = 'GT',
    LTE = 'LTE',
    GTE = 'GTE',
    ASSIGN = 'ASSIGN',
    ARROW = 'ARROW',
    RANGE = 'RANGE',
    
    // Delimiters
    LPAREN = 'LPAREN',
    RPAREN = 'RPAREN',
    LBRACKET = 'LBRACKET',
    RBRACKET = 'RBRACKET',
    LBRACE = 'LBRACE',
    RBRACE = 'RBRACE',
    COMMA = 'COMMA',
    DOT = 'DOT',
    COLON = 'COLON',
    UNDERSCORE = 'UNDERSCORE',
    NEWLINE = 'NEWLINE',
    
    // Special
    EOF = 'EOF',
    ERROR = 'ERROR',
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
 * Keywords map for quick lookup.
 */
const KEYWORDS: Map<string, TokenType> = new Map([
    ['let', TokenType.LET],
    ['const', TokenType.CONST],
    ['fn', TokenType.FN],
    ['return', TokenType.RETURN],
    ['if', TokenType.IF],
    ['then', TokenType.THEN],
    ['elif', TokenType.ELIF],
    ['else', TokenType.ELSE],
    ['end', TokenType.END],
    ['while', TokenType.WHILE],
    ['for', TokenType.FOR],
    ['in', TokenType.IN],
    ['to', TokenType.TO],
    ['do', TokenType.DO],
    ['and', TokenType.AND],
    ['or', TokenType.OR],
    ['not', TokenType.NOT],
    ['match', TokenType.MATCH],
    ['case', TokenType.CASE],
    ['try', TokenType.TRY],
    ['catch', TokenType.CATCH],
    ['finally', TokenType.FINALLY],
    ['throw', TokenType.THROW],
    ['class', TokenType.CLASS],
    ['extends', TokenType.EXTENDS],
    ['self', TokenType.SELF],
    ['super', TokenType.SUPER],
    ['nil', TokenType.NIL],
    ['true', TokenType.TRUE],
    ['false', TokenType.FALSE],
    ['enum', TokenType.ENUM],
    ['yield', TokenType.YIELD],
    ['async', TokenType.ASYNC],
    ['await', TokenType.AWAIT],
    ['static', TokenType.STATIC],
    ['from', TokenType.FROM],
    ['as', TokenType.AS],
    ['module', TokenType.MODULE],
    ['export', TokenType.EXPORT],
    ['import', TokenType.IMPORT],
]);

/**
 * Lexer for Pseudocode source code.
 */
export class Lexer {
    private source: string;
    private pos: number = 0;
    private line: number = 1;
    private column: number = 1;
    private tokens: Token[] = [];
    
    constructor(source: string) {
        this.source = source;
    }
    
    /**
     * Tokenize the entire source and return all tokens.
     */
    tokenize(): Token[] {
        this.tokens = [];
        
        while (!this.isAtEnd()) {
            this.skipWhitespaceAndComments();
            if (this.isAtEnd()) break;
            
            const token = this.scanToken();
            if (token) {
                this.tokens.push(token);
            }
        }
        
        this.tokens.push(this.makeToken(TokenType.EOF, ''));
        return this.tokens;
    }
    
    /**
     * Get the next token (for streaming use).
     */
    nextToken(): Token {
        this.skipWhitespaceAndComments();
        
        if (this.isAtEnd()) {
            return this.makeToken(TokenType.EOF, '');
        }
        
        return this.scanToken() || this.makeToken(TokenType.ERROR, this.advance());
    }
    
    private isAtEnd(): boolean {
        return this.pos >= this.source.length;
    }
    
    private peek(): string {
        if (this.isAtEnd()) return '\0';
        return this.source[this.pos];
    }
    
    private peekNext(): string {
        if (this.pos + 1 >= this.source.length) return '\0';
        return this.source[this.pos + 1];
    }
    
    private advance(): string {
        const char = this.source[this.pos++];
        if (char === '\n') {
            this.line++;
            this.column = 1;
        } else {
            this.column++;
        }
        return char;
    }
    
    private match(expected: string): boolean {
        if (this.isAtEnd() || this.source[this.pos] !== expected) {
            return false;
        }
        this.advance();
        return true;
    }
    
    private makeToken(type: TokenType, value: string): Token {
        return {
            type,
            value,
            line: this.line,
            column: this.column - value.length,
        };
    }
    
    private skipWhitespaceAndComments(): void {
        while (!this.isAtEnd()) {
            const char = this.peek();
            
            switch (char) {
                case ' ':
                case '\t':
                case '\r':
                    this.advance();
                    break;
                    
                case '\n':
                    // Newlines can be significant, emit NEWLINE token
                    // For now, just skip (the parser handles statement separation)
                    this.advance();
                    break;
                    
                case '/':
                    if (this.peekNext() === '/') {
                        // Single-line comment
                        while (!this.isAtEnd() && this.peek() !== '\n') {
                            this.advance();
                        }
                    } else {
                        return; // It's a division operator
                    }
                    break;
                    
                default:
                    return;
            }
        }
    }
    
    private scanToken(): Token | null {
        const startLine = this.line;
        const startColumn = this.column;
        const char = this.advance();
        
        // Single-character tokens
        switch (char) {
            case '(': return this.makeToken(TokenType.LPAREN, '(');
            case ')': return this.makeToken(TokenType.RPAREN, ')');
            case '[': return this.makeToken(TokenType.LBRACKET, '[');
            case ']': return this.makeToken(TokenType.RBRACKET, ']');
            case '{': return this.makeToken(TokenType.LBRACE, '{');
            case '}': return this.makeToken(TokenType.RBRACE, '}');
            case ',': return this.makeToken(TokenType.COMMA, ',');
            case ':': return this.makeToken(TokenType.COLON, ':');
            case '+': return this.makeToken(TokenType.PLUS, '+');
            case '*': return this.makeToken(TokenType.STAR, '*');
            case '%': return this.makeToken(TokenType.PERCENT, '%');
            case '_': 
                if (!this.isAlphaNumeric(this.peek())) {
                    return this.makeToken(TokenType.UNDERSCORE, '_');
                }
                // Fall through to identifier
                break;
        }
        
        // Two-character tokens
        switch (char) {
            case '-':
                if (this.match('>')) return this.makeToken(TokenType.ARROW, '->');
                return this.makeToken(TokenType.MINUS, '-');
                
            case '=':
                if (this.match('=')) return this.makeToken(TokenType.EQ, '==');
                return this.makeToken(TokenType.ASSIGN, '=');
                
            case '!':
                if (this.match('=')) return this.makeToken(TokenType.NEQ, '!=');
                return this.makeToken(TokenType.ERROR, '!');
                
            case '<':
                if (this.match('=')) return this.makeToken(TokenType.LTE, '<=');
                return this.makeToken(TokenType.LT, '<');
                
            case '>':
                if (this.match('=')) return this.makeToken(TokenType.GTE, '>=');
                return this.makeToken(TokenType.GT, '>');
                
            case '.':
                if (this.match('.')) return this.makeToken(TokenType.RANGE, '..');
                return this.makeToken(TokenType.DOT, '.');
                
            case '/':
                return this.makeToken(TokenType.SLASH, '/');
        }
        
        // String literals
        if (char === '"' || char === "'") {
            return this.scanString(char);
        }
        
        // Number literals
        if (this.isDigit(char)) {
            return this.scanNumber(char);
        }
        
        // Identifiers and keywords
        if (this.isAlpha(char) || char === '_') {
            return this.scanIdentifier(char);
        }
        
        return this.makeToken(TokenType.ERROR, char);
    }
    
    private scanString(quote: string): Token {
        const start = this.pos - 1;
        let value = '';
        
        while (!this.isAtEnd() && this.peek() !== quote) {
            if (this.peek() === '\\') {
                this.advance(); // Skip backslash
                if (!this.isAtEnd()) {
                    const escaped = this.advance();
                    switch (escaped) {
                        case 'n': value += '\n'; break;
                        case 't': value += '\t'; break;
                        case 'r': value += '\r'; break;
                        case '\\': value += '\\'; break;
                        case '"': value += '"'; break;
                        case "'": value += "'"; break;
                        default: value += escaped;
                    }
                }
            } else if (this.peek() === '\n') {
                return this.makeToken(TokenType.ERROR, 'Unterminated string');
            } else {
                value += this.advance();
            }
        }
        
        if (this.isAtEnd()) {
            return this.makeToken(TokenType.ERROR, 'Unterminated string');
        }
        
        this.advance(); // Closing quote
        return this.makeToken(TokenType.STRING, value);
    }
    
    private scanNumber(first: string): Token {
        let value = first;
        let isFloat = false;
        
        while (this.isDigit(this.peek())) {
            value += this.advance();
        }
        
        // Look for decimal part
        if (this.peek() === '.' && this.isDigit(this.peekNext())) {
            isFloat = true;
            value += this.advance(); // '.'
            while (this.isDigit(this.peek())) {
                value += this.advance();
            }
        }
        
        // Look for exponent
        if (this.peek() === 'e' || this.peek() === 'E') {
            isFloat = true;
            value += this.advance();
            if (this.peek() === '+' || this.peek() === '-') {
                value += this.advance();
            }
            while (this.isDigit(this.peek())) {
                value += this.advance();
            }
        }
        
        return this.makeToken(isFloat ? TokenType.FLOAT : TokenType.INT, value);
    }
    
    private scanIdentifier(first: string): Token {
        let value = first;
        
        while (this.isAlphaNumeric(this.peek())) {
            value += this.advance();
        }
        
        // Check for keyword
        const keyword = KEYWORDS.get(value.toLowerCase());
        if (keyword !== undefined) {
            return this.makeToken(keyword, value);
        }
        
        return this.makeToken(TokenType.IDENT, value);
    }
    
    private isDigit(char: string): boolean {
        return char >= '0' && char <= '9';
    }
    
    private isAlpha(char: string): boolean {
        return (char >= 'a' && char <= 'z') ||
               (char >= 'A' && char <= 'Z') ||
               char === '_';
    }
    
    private isAlphaNumeric(char: string): boolean {
        return this.isAlpha(char) || this.isDigit(char);
    }
}

/**
 * Tokenize source code and return tokens.
 */
export function tokenize(source: string): Token[] {
    const lexer = new Lexer(source);
    return lexer.tokenize();
}
