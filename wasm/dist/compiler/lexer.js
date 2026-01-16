"use strict";
/*
 * Pseudocode WASM Compiler - Lexer
 *
 * Tokenizes Pseudocode source code into a stream of tokens.
 * Ported from the C implementation in cvm/lexer.c
 */
Object.defineProperty(exports, "__esModule", { value: true });
exports.Lexer = exports.TokenType = void 0;
exports.tokenize = tokenize;
/**
 * Token types for Pseudocode language.
 */
var TokenType;
(function (TokenType) {
    // Literals
    TokenType["INT"] = "INT";
    TokenType["FLOAT"] = "FLOAT";
    TokenType["STRING"] = "STRING";
    TokenType["TRUE"] = "TRUE";
    TokenType["FALSE"] = "FALSE";
    // Identifiers & Keywords
    TokenType["IDENT"] = "IDENT";
    TokenType["LET"] = "LET";
    TokenType["CONST"] = "CONST";
    TokenType["FN"] = "FN";
    TokenType["RETURN"] = "RETURN";
    TokenType["IF"] = "IF";
    TokenType["THEN"] = "THEN";
    TokenType["ELIF"] = "ELIF";
    TokenType["ELSE"] = "ELSE";
    TokenType["END"] = "END";
    TokenType["WHILE"] = "WHILE";
    TokenType["FOR"] = "FOR";
    TokenType["IN"] = "IN";
    TokenType["TO"] = "TO";
    TokenType["DO"] = "DO";
    TokenType["AND"] = "AND";
    TokenType["OR"] = "OR";
    TokenType["NOT"] = "NOT";
    TokenType["MATCH"] = "MATCH";
    TokenType["CASE"] = "CASE";
    TokenType["TRY"] = "TRY";
    TokenType["CATCH"] = "CATCH";
    TokenType["FINALLY"] = "FINALLY";
    TokenType["THROW"] = "THROW";
    TokenType["CLASS"] = "CLASS";
    TokenType["EXTENDS"] = "EXTENDS";
    TokenType["SELF"] = "SELF";
    TokenType["SUPER"] = "SUPER";
    TokenType["NIL"] = "NIL";
    TokenType["ENUM"] = "ENUM";
    // Advanced features
    TokenType["YIELD"] = "YIELD";
    TokenType["ASYNC"] = "ASYNC";
    TokenType["AWAIT"] = "AWAIT";
    TokenType["STATIC"] = "STATIC";
    TokenType["FROM"] = "FROM";
    TokenType["AS"] = "AS";
    TokenType["MODULE"] = "MODULE";
    TokenType["EXPORT"] = "EXPORT";
    TokenType["IMPORT"] = "IMPORT";
    // Operators
    TokenType["PLUS"] = "PLUS";
    TokenType["MINUS"] = "MINUS";
    TokenType["STAR"] = "STAR";
    TokenType["SLASH"] = "SLASH";
    TokenType["PERCENT"] = "PERCENT";
    TokenType["EQ"] = "EQ";
    TokenType["NEQ"] = "NEQ";
    TokenType["LT"] = "LT";
    TokenType["GT"] = "GT";
    TokenType["LTE"] = "LTE";
    TokenType["GTE"] = "GTE";
    TokenType["ASSIGN"] = "ASSIGN";
    TokenType["ARROW"] = "ARROW";
    TokenType["RANGE"] = "RANGE";
    // Delimiters
    TokenType["LPAREN"] = "LPAREN";
    TokenType["RPAREN"] = "RPAREN";
    TokenType["LBRACKET"] = "LBRACKET";
    TokenType["RBRACKET"] = "RBRACKET";
    TokenType["LBRACE"] = "LBRACE";
    TokenType["RBRACE"] = "RBRACE";
    TokenType["COMMA"] = "COMMA";
    TokenType["DOT"] = "DOT";
    TokenType["COLON"] = "COLON";
    TokenType["UNDERSCORE"] = "UNDERSCORE";
    TokenType["NEWLINE"] = "NEWLINE";
    // Special
    TokenType["EOF"] = "EOF";
    TokenType["ERROR"] = "ERROR";
})(TokenType || (exports.TokenType = TokenType = {}));
/**
 * Keywords map for quick lookup.
 */
const KEYWORDS = new Map([
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
class Lexer {
    source;
    pos = 0;
    line = 1;
    column = 1;
    tokens = [];
    constructor(source) {
        this.source = source;
    }
    /**
     * Tokenize the entire source and return all tokens.
     */
    tokenize() {
        this.tokens = [];
        while (!this.isAtEnd()) {
            this.skipWhitespaceAndComments();
            if (this.isAtEnd())
                break;
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
    nextToken() {
        this.skipWhitespaceAndComments();
        if (this.isAtEnd()) {
            return this.makeToken(TokenType.EOF, '');
        }
        return this.scanToken() || this.makeToken(TokenType.ERROR, this.advance());
    }
    isAtEnd() {
        return this.pos >= this.source.length;
    }
    peek() {
        if (this.isAtEnd())
            return '\0';
        return this.source[this.pos];
    }
    peekNext() {
        if (this.pos + 1 >= this.source.length)
            return '\0';
        return this.source[this.pos + 1];
    }
    advance() {
        const char = this.source[this.pos++];
        if (char === '\n') {
            this.line++;
            this.column = 1;
        }
        else {
            this.column++;
        }
        return char;
    }
    match(expected) {
        if (this.isAtEnd() || this.source[this.pos] !== expected) {
            return false;
        }
        this.advance();
        return true;
    }
    makeToken(type, value) {
        return {
            type,
            value,
            line: this.line,
            column: this.column - value.length,
        };
    }
    skipWhitespaceAndComments() {
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
                    }
                    else {
                        return; // It's a division operator
                    }
                    break;
                default:
                    return;
            }
        }
    }
    scanToken() {
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
                if (this.match('>'))
                    return this.makeToken(TokenType.ARROW, '->');
                return this.makeToken(TokenType.MINUS, '-');
            case '=':
                if (this.match('='))
                    return this.makeToken(TokenType.EQ, '==');
                return this.makeToken(TokenType.ASSIGN, '=');
            case '!':
                if (this.match('='))
                    return this.makeToken(TokenType.NEQ, '!=');
                return this.makeToken(TokenType.ERROR, '!');
            case '<':
                if (this.match('='))
                    return this.makeToken(TokenType.LTE, '<=');
                return this.makeToken(TokenType.LT, '<');
            case '>':
                if (this.match('='))
                    return this.makeToken(TokenType.GTE, '>=');
                return this.makeToken(TokenType.GT, '>');
            case '.':
                if (this.match('.'))
                    return this.makeToken(TokenType.RANGE, '..');
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
    scanString(quote) {
        const start = this.pos - 1;
        let value = '';
        while (!this.isAtEnd() && this.peek() !== quote) {
            if (this.peek() === '\\') {
                this.advance(); // Skip backslash
                if (!this.isAtEnd()) {
                    const escaped = this.advance();
                    switch (escaped) {
                        case 'n':
                            value += '\n';
                            break;
                        case 't':
                            value += '\t';
                            break;
                        case 'r':
                            value += '\r';
                            break;
                        case '\\':
                            value += '\\';
                            break;
                        case '"':
                            value += '"';
                            break;
                        case "'":
                            value += "'";
                            break;
                        default: value += escaped;
                    }
                }
            }
            else if (this.peek() === '\n') {
                return this.makeToken(TokenType.ERROR, 'Unterminated string');
            }
            else {
                value += this.advance();
            }
        }
        if (this.isAtEnd()) {
            return this.makeToken(TokenType.ERROR, 'Unterminated string');
        }
        this.advance(); // Closing quote
        return this.makeToken(TokenType.STRING, value);
    }
    scanNumber(first) {
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
    scanIdentifier(first) {
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
    isDigit(char) {
        return char >= '0' && char <= '9';
    }
    isAlpha(char) {
        return (char >= 'a' && char <= 'z') ||
            (char >= 'A' && char <= 'Z') ||
            char === '_';
    }
    isAlphaNumeric(char) {
        return this.isAlpha(char) || this.isDigit(char);
    }
}
exports.Lexer = Lexer;
/**
 * Tokenize source code and return tokens.
 */
function tokenize(source) {
    const lexer = new Lexer(source);
    return lexer.tokenize();
}
//# sourceMappingURL=lexer.js.map