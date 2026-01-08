/*
 * Pseudocode Language - Lexer (Scanner)
 * High-performance single-pass tokenization
 */

#include "pseudo.h"
#include <string.h>
#include <stdlib.h>

typedef enum {
    /* Literals */
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_STRING,
    TOKEN_TRUE,
    TOKEN_FALSE,
    
    /* Identifiers & Keywords */
    TOKEN_IDENT,
    TOKEN_LET,
    TOKEN_CONST,
    TOKEN_FN,
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_THEN,
    TOKEN_ELIF,
    TOKEN_ELSE,
    TOKEN_END,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_IN,
    TOKEN_DO,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    
    /* Operators */
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_EQ,
    TOKEN_NEQ,
    TOKEN_LT,
    TOKEN_GT,
    TOKEN_LTE,
    TOKEN_GTE,
    TOKEN_ASSIGN,
    TOKEN_ARROW,
    TOKEN_RANGE,
    
    /* Bitwise */
    TOKEN_BAND,
    TOKEN_BOR,
    TOKEN_BXOR,
    TOKEN_SHL,
    TOKEN_SHR,
    
    /* Delimiters */
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_NEWLINE,
    
    /* Special */
    TOKEN_EOF,
    TOKEN_ERROR,
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
} Token;

typedef struct {
    const char* start;
    const char* current;
    int line;
} Scanner;

static Scanner scanner;

void scanner_init(const char* source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

static bool is_at_end(void) {
    return *scanner.current == '\0';
}

static char advance(void) {
    scanner.current++;
    return scanner.current[-1];
}

static char peek(void) {
    return *scanner.current;
}

static char peek_next(void) {
    if (is_at_end()) return '\0';
    return scanner.current[1];
}

static bool match(char expected) {
    if (is_at_end()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

static Token make_token(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

static Token error_token(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;
    return token;
}

static void skip_whitespace(void) {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '/':
                if (peek_next() == '/') {
                    /* Single-line comment */
                    while (peek() != '\n' && !is_at_end()) advance();
                } else if (peek_next() == '*') {
                    /* Multi-line comment */
                    advance(); advance();
                    while (!is_at_end()) {
                        if (peek() == '*' && peek_next() == '/') {
                            advance(); advance();
                            break;
                        }
                        if (peek() == '\n') scanner.line++;
                        advance();
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static TokenType check_keyword(int start, int length, const char* rest, TokenType type) {
    if (scanner.current - scanner.start == start + length &&
        memcmp(scanner.start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENT;
}

static TokenType identifier_type(void) {
    switch (scanner.start[0]) {
        case 'a': return check_keyword(1, 2, "nd", TOKEN_AND);
        case 'c': return check_keyword(1, 4, "onst", TOKEN_CONST);
        case 'd': return check_keyword(1, 1, "o", TOKEN_DO);
        case 'e':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'l':
                        if (scanner.current - scanner.start > 2) {
                            switch (scanner.start[2]) {
                                case 'i': return check_keyword(3, 1, "f", TOKEN_ELIF);
                                case 's': return check_keyword(3, 1, "e", TOKEN_ELSE);
                            }
                        }
                        break;
                    case 'n': return check_keyword(2, 1, "d", TOKEN_END);
                }
            }
            break;
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a': return check_keyword(2, 3, "lse", TOKEN_FALSE);
                    case 'n': return check_keyword(2, 0, "", TOKEN_FN);
                    case 'o': return check_keyword(2, 1, "r", TOKEN_FOR);
                }
            }
            break;
        case 'i':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'f': return check_keyword(2, 0, "", TOKEN_IF);
                    case 'n': return check_keyword(2, 0, "", TOKEN_IN);
                }
            }
            break;
        case 'l': return check_keyword(1, 2, "et", TOKEN_LET);
        case 'n': return check_keyword(1, 2, "ot", TOKEN_NOT);
        case 'o': return check_keyword(1, 1, "r", TOKEN_OR);
        case 'r': return check_keyword(1, 5, "eturn", TOKEN_RETURN);
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h': return check_keyword(2, 2, "en", TOKEN_THEN);
                    case 'r': return check_keyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
        case 'w': return check_keyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENT;
}

static Token identifier(void) {
    while ((peek() >= 'a' && peek() <= 'z') ||
           (peek() >= 'A' && peek() <= 'Z') ||
           (peek() >= '0' && peek() <= '9') ||
           peek() == '_') {
        advance();
    }
    return make_token(identifier_type());
}

static Token number(void) {
    bool is_float = false;
    
    while (peek() >= '0' && peek() <= '9') advance();
    
    /* Decimal part */
    if (peek() == '.' && peek_next() >= '0' && peek_next() <= '9') {
        is_float = true;
        advance(); /* Consume '.' */
        while (peek() >= '0' && peek() <= '9') advance();
    }
    
    /* Exponent */
    if (peek() == 'e' || peek() == 'E') {
        is_float = true;
        advance();
        if (peek() == '+' || peek() == '-') advance();
        while (peek() >= '0' && peek() <= '9') advance();
    }
    
    return make_token(is_float ? TOKEN_FLOAT : TOKEN_INT);
}

static Token string(char quote) {
    while (peek() != quote && !is_at_end()) {
        if (peek() == '\n') scanner.line++;
        if (peek() == '\\' && peek_next() != '\0') advance();
        advance();
    }
    
    if (is_at_end()) return error_token("Unterminated string.");
    
    advance(); /* Closing quote */
    return make_token(TOKEN_STRING);
}

Token scan_token(void) {
    skip_whitespace();
    scanner.start = scanner.current;
    
    if (is_at_end()) return make_token(TOKEN_EOF);
    
    char c = advance();
    
    /* Identifiers */
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        return identifier();
    }
    
    /* Numbers */
    if (c >= '0' && c <= '9') {
        return number();
    }
    
    switch (c) {
        case '\n': scanner.line++; return make_token(TOKEN_NEWLINE);
        case '(': return make_token(TOKEN_LPAREN);
        case ')': return make_token(TOKEN_RPAREN);
        case '[': return make_token(TOKEN_LBRACKET);
        case ']': return make_token(TOKEN_RBRACKET);
        case ',': return make_token(TOKEN_COMMA);
        case ':': return make_token(TOKEN_COLON);
        case '+': return make_token(TOKEN_PLUS);
        case '*': return make_token(TOKEN_STAR);
        case '/': return make_token(TOKEN_SLASH);
        case '%': return make_token(TOKEN_PERCENT);
        case '&': return make_token(TOKEN_BAND);
        case '|': return make_token(TOKEN_BOR);
        case '^': return make_token(TOKEN_BXOR);
        case '"':
        case '\'': return string(c);
        
        case '-':
            return make_token(match('>') ? TOKEN_ARROW : TOKEN_MINUS);
        case '=':
            return make_token(match('=') ? TOKEN_EQ : TOKEN_ASSIGN);
        case '!':
            return make_token(match('=') ? TOKEN_NEQ : TOKEN_ERROR);
        case '<':
            if (match('<')) return make_token(TOKEN_SHL);
            if (match('=')) return make_token(TOKEN_LTE);
            return make_token(TOKEN_LT);
        case '>':
            if (match('>')) return make_token(TOKEN_SHR);
            if (match('=')) return make_token(TOKEN_GTE);
            return make_token(TOKEN_GT);
        case '.':
            if (match('.')) return make_token(TOKEN_RANGE);
            return error_token("Unexpected character.");
    }
    
    return error_token("Unexpected character.");
}
