/*
 * Pseudocode Language - Lexer (Scanner)
 * High-performance single-pass tokenization
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#include "pseudo.h"
#include <string.h>
#include <stdlib.h>

typedef enum
{
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
    TOKEN_MATCH,
    TOKEN_CASE,
    TOKEN_TRY,
    TOKEN_CATCH,
    TOKEN_FINALLY,
    TOKEN_THROW,
    TOKEN_CLASS,
    TOKEN_EXTENDS,
    TOKEN_SELF,
    TOKEN_SUPER,
    TOKEN_NIL,
    TOKEN_ENUM,

    /* Advanced language features */
    TOKEN_YIELD,
    TOKEN_ASYNC,
    TOKEN_AWAIT,
    TOKEN_STATIC,
    TOKEN_FROM,
    TOKEN_AS,
    TOKEN_MODULE,
    TOKEN_EXPORT,
    TOKEN_IMPORT,
    
    /* Control flow extensions */
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_REPEAT,
    TOKEN_UNTIL,
    TOKEN_STEP,
    TOKEN_TO,
    
    /* IB/Educational compatibility keywords */
    TOKEN_MOD,       /* alias for % */
    TOKEN_DIV,       /* integer division */
    TOKEN_OUTPUT,    /* alias for print */
    TOKEN_FUNCTION,  /* alias for fn */
    TOKEN_PROCEDURE, /* alias for fn (no return) */

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
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_DOT,
    TOKEN_NEWLINE,

    /* Type annotation tokens (parsed but ignored at runtime) */
    TOKEN_TYPE_NUMBER,
    TOKEN_TYPE_STRING,
    TOKEN_TYPE_BOOL,
    TOKEN_TYPE_ARRAY,
    TOKEN_TYPE_DICT,
    TOKEN_TYPE_NIL,
    TOKEN_TYPE_ANY,
    TOKEN_TYPE_VOID,

    /* Special */
    TOKEN_EOF,
    TOKEN_ERROR,
} TokenType;

typedef struct
{
    TokenType type;
    const char *start;
    int length;
    int line;
} Token;

typedef struct
{
    const char *start;
    const char *current;
    int line;
} Scanner;

static Scanner scanner;

void scanner_init(const char *source)
{
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

static bool is_at_end(void)
{
    return *scanner.current == '\0';
}

static char advance(void)
{
    scanner.current++;
    return scanner.current[-1];
}

static char peek(void)
{
    return *scanner.current;
}

static char peek_next(void)
{
    if (is_at_end())
        return '\0';
    return scanner.current[1];
}

static bool match(char expected)
{
    if (is_at_end())
        return false;
    if (*scanner.current != expected)
        return false;
    scanner.current++;
    return true;
}

static Token make_token(TokenType type)
{
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

static Token error_token(const char *message)
{
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;
    return token;
}

static void skip_whitespace(void)
{
    for (;;)
    {
        char c = peek();
        switch (c)
        {
        case ' ':
        case '\r':
        case '\t':
            advance();
            break;
        case '/':
            if (peek_next() == '/')
            {
                /* Single-line comment */
                while (peek() != '\n' && !is_at_end())
                    advance();
            }
            else if (peek_next() == '*')
            {
                /* Multi-line comment */
                advance();
                advance();
                while (!is_at_end())
                {
                    if (peek() == '*' && peek_next() == '/')
                    {
                        advance();
                        advance();
                        break;
                    }
                    if (peek() == '\n')
                        scanner.line++;
                    advance();
                }
            }
            else
            {
                return;
            }
            break;
        default:
            return;
        }
    }
}

static TokenType check_keyword(int start, int length, const char *rest, TokenType type)
{
    if (scanner.current - scanner.start == start + length &&
        memcmp(scanner.start + start, rest, length) == 0)
    {
        return type;
    }
    return TOKEN_IDENT;
}

static TokenType identifier_type(void)
{
    switch (scanner.start[0])
    {
    case 'a':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'n':
                return check_keyword(2, 1, "d", TOKEN_AND);
            case 's':
                if (scanner.current - scanner.start == 2)
                    return TOKEN_AS;
                if (scanner.current - scanner.start > 2 && scanner.start[2] == 'y')
                    return check_keyword(3, 2, "nc", TOKEN_ASYNC);
                break;
            case 'w':
                return check_keyword(2, 3, "ait", TOKEN_AWAIT);
            }
        }
        break;
    case 'b':
        return check_keyword(1, 4, "reak", TOKEN_BREAK);
    case 'c':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'o':
                if (scanner.current - scanner.start > 2 && scanner.start[2] == 'n')
                {
                    if (scanner.current - scanner.start == 5)
                        return check_keyword(3, 2, "st", TOKEN_CONST);
                    if (scanner.current - scanner.start == 8)
                        return check_keyword(3, 5, "tinue", TOKEN_CONTINUE);
                }
                break;
            case 'a':
                if (scanner.current - scanner.start > 2)
                {
                    if (scanner.start[2] == 's')
                        return check_keyword(2, 2, "se", TOKEN_CASE);
                    if (scanner.start[2] == 't')
                        return check_keyword(2, 3, "tch", TOKEN_CATCH);
                }
                break;
            case 'l':
                return check_keyword(2, 3, "ass", TOKEN_CLASS);
            }
        }
        break;
    case 'd':
        if (scanner.current - scanner.start == 2)
            return check_keyword(1, 1, "o", TOKEN_DO);
        if (scanner.current - scanner.start == 3)
            return check_keyword(1, 2, "iv", TOKEN_DIV);
        break;
    case 'e':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'l':
                if (scanner.current - scanner.start > 2)
                {
                    switch (scanner.start[2])
                    {
                    case 'i':
                        return check_keyword(3, 1, "f", TOKEN_ELIF);
                    case 's':
                        return check_keyword(3, 1, "e", TOKEN_ELSE);
                    }
                }
                break;
            case 'n':
                if (scanner.current - scanner.start > 2)
                {
                    switch (scanner.start[2])
                    {
                    case 'd':
                        return check_keyword(3, 0, "", TOKEN_END);
                    case 'u':
                        return check_keyword(3, 1, "m", TOKEN_ENUM);
                    }
                }
                break;
            case 'x':
                return check_keyword(2, 5, "tends", TOKEN_EXTENDS);
            case 'p':
                return check_keyword(2, 4, "port", TOKEN_EXPORT);
            }
        }
        break;
    case 'f':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'a':
                return check_keyword(2, 3, "lse", TOKEN_FALSE);
            case 'n':
                return check_keyword(2, 0, "", TOKEN_FN);
            case 'o':
                return check_keyword(2, 1, "r", TOKEN_FOR);
            case 'i':
                return check_keyword(2, 5, "nally", TOKEN_FINALLY);
            case 'r':
                return check_keyword(2, 2, "om", TOKEN_FROM);
            case 'u':
                return check_keyword(2, 6, "nction", TOKEN_FUNCTION);
            }
        }
        break;
    case 'i':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'f':
                return check_keyword(2, 0, "", TOKEN_IF);
            case 'n':
                return check_keyword(2, 0, "", TOKEN_IN);
            case 'm':
                return check_keyword(2, 4, "port", TOKEN_IMPORT);
            }
        }
        break;
    case 'l':
        return check_keyword(1, 2, "et", TOKEN_LET);
    case 'm':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'a':
                return check_keyword(2, 3, "tch", TOKEN_MATCH);
            case 'o':
                if (scanner.current - scanner.start == 3)
                    return check_keyword(2, 1, "d", TOKEN_MOD);
                return check_keyword(2, 4, "dule", TOKEN_MODULE);
            }
        }
        break;
    case 'n':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'o':
                return check_keyword(2, 1, "t", TOKEN_NOT);
            case 'i':
                return check_keyword(2, 1, "l", TOKEN_NIL);
            }
        }
        break;
    case 'o':
        if (scanner.current - scanner.start == 2)
            return check_keyword(1, 1, "r", TOKEN_OR);
        if (scanner.current - scanner.start == 6)
            return check_keyword(1, 5, "utput", TOKEN_OUTPUT);
        break;
    case 'p':
        return check_keyword(1, 8, "rocedure", TOKEN_PROCEDURE);
    case 'r':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'e':
                if (scanner.current - scanner.start > 2)
                {
                    if (scanner.start[2] == 't')
                        return check_keyword(3, 3, "urn", TOKEN_RETURN);
                    if (scanner.start[2] == 'p')
                        return check_keyword(3, 3, "eat", TOKEN_REPEAT);
                }
                break;
            }
        }
        break;
    case 's':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'e':
                return check_keyword(2, 2, "lf", TOKEN_SELF);
            case 'u':
                return check_keyword(2, 3, "per", TOKEN_SUPER);
            case 't':
                if (scanner.current - scanner.start == 4)
                    return check_keyword(2, 2, "ep", TOKEN_STEP);
                return check_keyword(2, 4, "atic", TOKEN_STATIC);
            }
        }
        break;
    case 't':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'h':
                if (scanner.current - scanner.start > 2 && scanner.start[2] == 'e')
                {
                    return check_keyword(3, 1, "n", TOKEN_THEN);
                }
                if (scanner.current - scanner.start > 2 && scanner.start[2] == 'r')
                {
                    return check_keyword(3, 2, "ow", TOKEN_THROW);
                }
                break;
            case 'r':
                if (scanner.current - scanner.start > 2)
                {
                    if (scanner.start[2] == 'u')
                        return check_keyword(3, 1, "e", TOKEN_TRUE);
                    if (scanner.start[2] == 'y')
                        return check_keyword(3, 0, "", TOKEN_TRY);
                }
                break;
            case 'o':
                return check_keyword(2, 0, "", TOKEN_TO);
            }
        }
        break;
    case 'u':
        return check_keyword(1, 4, "ntil", TOKEN_UNTIL);
    case 'w':
        return check_keyword(1, 4, "hile", TOKEN_WHILE);
    case 'y':
        return check_keyword(1, 4, "ield", TOKEN_YIELD);
    }
    return TOKEN_IDENT;
}

static Token identifier(void)
{
    while ((peek() >= 'a' && peek() <= 'z') ||
           (peek() >= 'A' && peek() <= 'Z') ||
           (peek() >= '0' && peek() <= '9') ||
           peek() == '_')
    {
        advance();
    }
    return make_token(identifier_type());
}

static Token number(void)
{
    bool is_float = false;

    while (peek() >= '0' && peek() <= '9')
        advance();

    /* Decimal part */
    if (peek() == '.' && peek_next() >= '0' && peek_next() <= '9')
    {
        is_float = true;
        advance(); /* Consume '.' */
        while (peek() >= '0' && peek() <= '9')
            advance();
    }

    /* Exponent */
    if (peek() == 'e' || peek() == 'E')
    {
        is_float = true;
        advance();
        if (peek() == '+' || peek() == '-')
            advance();
        while (peek() >= '0' && peek() <= '9')
            advance();
    }

    return make_token(is_float ? TOKEN_FLOAT : TOKEN_INT);
}

static Token string(char quote)
{
    while (peek() != quote && !is_at_end())
    {
        if (peek() == '\n')
            scanner.line++;
        if (peek() == '\\' && peek_next() != '\0')
            advance();
        advance();
    }

    if (is_at_end())
        return error_token("Unterminated string.");

    advance(); /* Closing quote */
    return make_token(TOKEN_STRING);
}

Token scan_token(void)
{
    skip_whitespace();
    scanner.start = scanner.current;

    if (is_at_end())
        return make_token(TOKEN_EOF);

    char c = advance();

    /* Identifiers */
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
    {
        return identifier();
    }

    /* Numbers */
    if (c >= '0' && c <= '9')
    {
        return number();
    }

    switch (c)
    {
    case '\n':
        scanner.line++;
        return make_token(TOKEN_NEWLINE);
    case '(':
        return make_token(TOKEN_LPAREN);
    case ')':
        return make_token(TOKEN_RPAREN);
    case '[':
        return make_token(TOKEN_LBRACKET);
    case ']':
        return make_token(TOKEN_RBRACKET);
    case ',':
        return make_token(TOKEN_COMMA);
    case ':':
        return make_token(TOKEN_COLON);
    case '+':
        return make_token(TOKEN_PLUS);
    case '*':
        return make_token(TOKEN_STAR);
    case '/':
        return make_token(TOKEN_SLASH);
    case '%':
        return make_token(TOKEN_PERCENT);
    case '&':
        return make_token(TOKEN_BAND);
    case '|':
        return make_token(TOKEN_BOR);
    case '^':
        return make_token(TOKEN_BXOR);
    case '"':
    case '\'':
        return string(c);

    case '-':
        return make_token(match('>') ? TOKEN_ARROW : TOKEN_MINUS);
    case '=':
        return make_token(match('=') ? TOKEN_EQ : TOKEN_ASSIGN);
    case '!':
        return make_token(match('=') ? TOKEN_NEQ : TOKEN_ERROR);
    case '<':
        if (match('<'))
            return make_token(TOKEN_SHL);
        if (match('='))
            return make_token(TOKEN_LTE);
        return make_token(TOKEN_LT);
    case '>':
        if (match('>'))
            return make_token(TOKEN_SHR);
        if (match('='))
            return make_token(TOKEN_GTE);
        return make_token(TOKEN_GT);
    case '.':
        if (match('.'))
            return make_token(TOKEN_RANGE);
        return make_token(TOKEN_DOT);
    case '{':
        return make_token(TOKEN_LBRACE);
    case '}':
        return make_token(TOKEN_RBRACE);
    }

    return error_token("Unexpected character.");
}

/* Scanner state save/restore for string interpolation */
typedef struct
{
    const char *start;
    const char *current;
    int line;
} ScannerState;

void scanner_save_state(ScannerState *state)
{
    state->start = scanner.start;
    state->current = scanner.current;
    state->line = scanner.line;
}

void scanner_restore_state(const ScannerState *state)
{
    scanner.start = state->start;
    scanner.current = state->current;
    scanner.line = state->line;
}
