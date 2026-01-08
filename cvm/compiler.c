/*
 * Pseudocode Language - Compiler
 * Single-pass Pratt parser with bytecode emission
 */

#include "pseudo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations from lexer.c */
typedef enum {
    TOKEN_INT, TOKEN_FLOAT, TOKEN_STRING, TOKEN_TRUE, TOKEN_FALSE,
    TOKEN_IDENT, TOKEN_LET, TOKEN_CONST, TOKEN_FN, TOKEN_RETURN,
    TOKEN_IF, TOKEN_THEN, TOKEN_ELIF, TOKEN_ELSE, TOKEN_END,
    TOKEN_WHILE, TOKEN_FOR, TOKEN_IN, TOKEN_DO, TOKEN_AND, TOKEN_OR, TOKEN_NOT,
    TOKEN_MATCH, TOKEN_CASE,
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT,
    TOKEN_EQ, TOKEN_NEQ, TOKEN_LT, TOKEN_GT, TOKEN_LTE, TOKEN_GTE,
    TOKEN_ASSIGN, TOKEN_ARROW, TOKEN_RANGE,
    TOKEN_BAND, TOKEN_BOR, TOKEN_BXOR, TOKEN_SHL, TOKEN_SHR,
    TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACKET, TOKEN_RBRACKET,
    TOKEN_COMMA, TOKEN_COLON, TOKEN_NEWLINE,
    TOKEN_EOF, TOKEN_ERROR,
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
} Token;

void scanner_init(const char* source);
Token scan_token(void);

/* ============ Parser State ============ */

typedef struct {
    Token current;
    Token previous;
    bool had_error;
    bool panic_mode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  /* = */
    PREC_OR,          /* or */
    PREC_AND,         /* and */
    PREC_BOR,         /* | */
    PREC_BXOR,        /* ^ */
    PREC_BAND,        /* & */
    PREC_EQUALITY,    /* == != */
    PREC_COMPARISON,  /* < > <= >= */
    PREC_SHIFT,       /* << >> */
    PREC_TERM,        /* + - */
    PREC_FACTOR,      /* * / % */
    PREC_UNARY,       /* - not */
    PREC_CALL,        /* . () [] */
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool can_assign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth;
} Local;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;
    
    Local locals[256];
    int local_count;
    int scope_depth;
} Compiler;

static Parser parser;
static Compiler* current = NULL;
static Chunk* compiling_chunk;
static VM* compiling_vm;

static Chunk* current_chunk(void) {
    return compiling_chunk;
}

/* ============ Error Handling ============ */

static void error_at(Token* token, const char* message) {
    if (parser.panic_mode) return;
    parser.panic_mode = true;
    
    fprintf(stderr, "[line %d] Error", token->line);
    
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type != TOKEN_ERROR) {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    
    fprintf(stderr, ": %s\n", message);
    parser.had_error = true;
}

static void error(const char* message) {
    error_at(&parser.previous, message);
}

static void error_at_current(const char* message) {
    error_at(&parser.current, message);
}

/* ============ Token Handling ============ */

static void advance(void) {
    parser.previous = parser.current;
    
    for (;;) {
        parser.current = scan_token();
        if (parser.current.type != TOKEN_ERROR) break;
        error_at_current(parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    error_at_current(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

static void skip_newlines(void) {
    while (match(TOKEN_NEWLINE));
}

/* ============ Bytecode Emission ============ */

static void emit_byte(uint8_t byte) {
    chunk_write(current_chunk(), byte, parser.previous.line);
}

static void emit_bytes(uint8_t byte1, uint8_t byte2) {
    emit_byte(byte1);
    emit_byte(byte2);
}

static void emit_short(uint16_t value) {
    emit_byte((value >> 8) & 0xff);
    emit_byte(value & 0xff);
}

static int emit_jump(uint8_t instruction) {
    emit_byte(instruction);
    emit_byte(0xff);
    emit_byte(0xff);
    return current_chunk()->count - 2;
}

static void patch_jump(int offset) {
    int jump = current_chunk()->count - offset - 2;
    
    if (jump > 65535) {
        error("Too much code to jump over.");
    }
    
    current_chunk()->code[offset] = (jump >> 8) & 0xff;
    current_chunk()->code[offset + 1] = jump & 0xff;
}

static void emit_loop(int loop_start) {
    emit_byte(OP_LOOP);
    
    int offset = current_chunk()->count - loop_start + 2;
    if (offset > 65535) error("Loop body too large.");
    
    emit_byte((offset >> 8) & 0xff);
    emit_byte(offset & 0xff);
}

static uint8_t make_constant(Value value) {
    int constant = chunk_add_const(current_chunk(), value);
    if (constant > 255) {
        error("Too many constants in one chunk.");
        return 0;
    }
    return (uint8_t)constant;
}

static void emit_constant(Value value) {
    emit_bytes(OP_CONST, make_constant(value));
}

static void emit_return(void) {
    emit_byte(OP_NIL);
    emit_byte(OP_RETURN);
}

/* ============ Locals ============ */

static void init_compiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->function = new_function(compiling_vm);
    current = compiler;
    
    /* Reserve slot 0 for the function itself in calls */
    Local* local = &current->locals[current->local_count++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
}

static ObjFunction* end_compiler(void) {
    emit_return();
    ObjFunction* function = current->function;
    current = current->enclosing;
    return function;
}

static void begin_scope(void) {
    current->scope_depth++;
}

static void end_scope(void) {
    current->scope_depth--;
    
    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth > current->scope_depth) {
        emit_byte(OP_POP);
        current->local_count--;
    }
}

static bool identifiers_equal(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolve_local(Compiler* compiler, Token* name) {
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiers_equal(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }
    return -1;
}

static void add_local(Token name) {
    if (current->local_count == 256) {
        error("Too many local variables in function.");
        return;
    }
    
    Local* local = &current->locals[current->local_count++];
    local->name = name;
    local->depth = -1; /* Mark uninitialized */
}

static void declare_variable(void) {
    if (current->scope_depth == 0) return;
    
    Token* name = &parser.previous;
    
    /* Check for redeclaration in same scope */
    for (int i = current->local_count - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scope_depth) {
            break;
        }
        
        if (identifiers_equal(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }
    
    add_local(*name);
}

static uint8_t identifier_constant(Token* name) {
    ObjString* str = copy_string(compiling_vm, name->start, name->length);
    return make_constant(val_obj(str));
}

static uint8_t parse_variable(const char* error_message) {
    consume(TOKEN_IDENT, error_message);
    
    declare_variable();
    if (current->scope_depth > 0) return 0;
    
    return identifier_constant(&parser.previous);
}

static void mark_initialized(void) {
    if (current->scope_depth == 0) return;
    current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(uint8_t global) {
    if (current->scope_depth > 0) {
        mark_initialized();
        return;
    }
    emit_bytes(OP_SET_GLOBAL, global);
    emit_byte(OP_POP);
}

/* ============ Expression Parsing ============ */

static void expression(void);
static void statement(void);
static void declaration(void);
static ParseRule* get_rule(TokenType type);
static void parse_precedence(Precedence precedence);

static void grouping(bool can_assign) {
    (void)can_assign;
    expression();
    consume(TOKEN_RPAREN, "Expect ')' after expression.");
}

static void number(bool can_assign) {
    (void)can_assign;
    if (parser.previous.type == TOKEN_INT) {
        int64_t value = strtoll(parser.previous.start, NULL, 10);
        if (value >= INT32_MIN && value <= INT32_MAX) {
            int32_t v = (int32_t)value;
            /* SUPERINSTRUCTION: Use specialized opcodes for small constants */
            if (v == 0) {
                emit_byte(OP_CONST_0);
            } else if (v == 1) {
                emit_byte(OP_CONST_1);
            } else if (v == 2) {
                emit_byte(OP_CONST_2);
            } else {
                emit_constant(val_int(v));
            }
        } else {
            emit_constant(val_num((double)value));
        }
    } else {
        double value = strtod(parser.previous.start, NULL);
        emit_constant(val_num(value));
    }
}

static void string(bool can_assign) {
    (void)can_assign;
    /* Skip opening and closing quotes */
    ObjString* str = copy_string(compiling_vm, 
        parser.previous.start + 1, 
        parser.previous.length - 2);
    emit_constant(val_obj(str));
}

static void named_variable(Token name, bool can_assign) {
    uint8_t get_op, set_op;
    int arg = resolve_local(current, &name);
    
    if (arg != -1) {
        get_op = OP_GET_LOCAL;
        set_op = OP_SET_LOCAL;
    } else {
        arg = identifier_constant(&name);
        get_op = OP_GET_GLOBAL;
        set_op = OP_SET_GLOBAL;
    }
    
    if (can_assign && match(TOKEN_ASSIGN)) {
        expression();
        emit_bytes(set_op, (uint8_t)arg);
    } else {
        /* SUPERINSTRUCTION: Use specialized opcodes for common local slots */
        if (get_op == OP_GET_LOCAL && arg <= 3) {
            switch (arg) {
                case 0: emit_byte(OP_GET_LOCAL_0); break;
                case 1: emit_byte(OP_GET_LOCAL_1); break;
                case 2: emit_byte(OP_GET_LOCAL_2); break;
                case 3: emit_byte(OP_GET_LOCAL_3); break;
            }
        } else {
            emit_bytes(get_op, (uint8_t)arg);
        }
    }
}

static void variable(bool can_assign) {
    Token name = parser.previous;
    
    /* Check for built-in function calls */
    if (check(TOKEN_LPAREN)) {
        /* Check if it's a built-in */
        if (name.length == 5 && memcmp(name.start, "print", 5) == 0) {
            advance();  /* consume ( */
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after print argument.");
            emit_byte(OP_PRINT);
            emit_byte(OP_NIL);  /* print returns nil */
            return;
        }
        if (name.length == 3 && memcmp(name.start, "len", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after len argument.");
            emit_byte(OP_LEN);  /* len returns the length value */
            return;
        }
        if (name.length == 4 && memcmp(name.start, "push", 4) == 0) {
            advance();
            expression();  /* array */
            consume(TOKEN_COMMA, "Expect ',' after array.");
            expression();  /* value */
            consume(TOKEN_RPAREN, "Expect ')' after push arguments.");
            emit_byte(OP_PUSH);  /* push returns the array */
            return;
        }
        if (name.length == 3 && memcmp(name.start, "pop", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after pop argument.");
            emit_byte(OP_POP_ARRAY);  /* pop returns the popped value */
            return;
        }
        if (name.length == 4 && memcmp(name.start, "time", 4) == 0) {
            advance();
            consume(TOKEN_RPAREN, "Expect ')' after time.");
            emit_byte(OP_TIME);  /* time returns the timestamp */
            return;
        }
        if (name.length == 5 && memcmp(name.start, "input", 5) == 0) {
            advance();
            consume(TOKEN_RPAREN, "Expect ')' after input.");
            emit_byte(OP_INPUT);
            return;
        }
        /* Type conversion functions */
        if (name.length == 3 && memcmp(name.start, "int", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after int argument.");
            emit_byte(OP_INT);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "float", 5) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after float argument.");
            emit_byte(OP_FLOAT);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "str", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after str argument.");
            emit_byte(OP_STR);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "type", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after type argument.");
            emit_byte(OP_TYPE);
            return;
        }
        /* Math functions */
        if (name.length == 3 && memcmp(name.start, "abs", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after abs argument.");
            emit_byte(OP_ABS);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "min", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first min argument.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after min arguments.");
            emit_byte(OP_MIN);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "max", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first max argument.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after max arguments.");
            emit_byte(OP_MAX);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "sqrt", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after sqrt argument.");
            emit_byte(OP_SQRT);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "floor", 5) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after floor argument.");
            emit_byte(OP_FLOOR);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "ceil", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after ceil argument.");
            emit_byte(OP_CEIL);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "round", 5) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after round argument.");
            emit_byte(OP_ROUND);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "rand", 4) == 0) {
            advance();
            consume(TOKEN_RPAREN, "Expect ')' after rand.");
            emit_byte(OP_RAND);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "pow", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first pow argument.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after pow arguments.");
            emit_byte(OP_POW);
            return;
        }
        /* Bit manipulation intrinsics */
        if (name.length == 8 && memcmp(name.start, "popcount", 8) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after popcount argument.");
            emit_byte(OP_POPCOUNT);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "clz", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after clz argument.");
            emit_byte(OP_CLZ);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "ctz", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after ctz argument.");
            emit_byte(OP_CTZ);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "rotl", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first rotl argument.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after rotl arguments.");
            emit_byte(OP_ROTL);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "rotr", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first rotr argument.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after rotr arguments.");
            emit_byte(OP_ROTR);
            return;
        }
        /* String operations */
        if (name.length == 6 && memcmp(name.start, "substr", 6) == 0) {
            advance();
            expression();  /* string */
            consume(TOKEN_COMMA, "Expect ',' after string.");
            expression();  /* start */
            consume(TOKEN_COMMA, "Expect ',' after start.");
            expression();  /* length */
            consume(TOKEN_RPAREN, "Expect ')' after substr arguments.");
            emit_byte(OP_SUBSTR);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "upper", 5) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after upper argument.");
            emit_byte(OP_UPPER);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "lower", 5) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after lower argument.");
            emit_byte(OP_LOWER);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "split", 5) == 0) {
            advance();
            expression();  /* string */
            consume(TOKEN_COMMA, "Expect ',' after string.");
            expression();  /* delimiter */
            consume(TOKEN_RPAREN, "Expect ')' after split arguments.");
            emit_byte(OP_SPLIT);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "join", 4) == 0) {
            advance();
            expression();  /* array */
            consume(TOKEN_COMMA, "Expect ',' after array.");
            expression();  /* delimiter */
            consume(TOKEN_RPAREN, "Expect ')' after join arguments.");
            emit_byte(OP_JOIN);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "replace", 7) == 0) {
            advance();
            expression();  /* string */
            consume(TOKEN_COMMA, "Expect ',' after string.");
            expression();  /* from */
            consume(TOKEN_COMMA, "Expect ',' after from.");
            expression();  /* to */
            consume(TOKEN_RPAREN, "Expect ')' after replace arguments.");
            emit_byte(OP_REPLACE);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "find", 4) == 0) {
            advance();
            expression();  /* haystack */
            consume(TOKEN_COMMA, "Expect ',' after string.");
            expression();  /* needle */
            consume(TOKEN_RPAREN, "Expect ')' after find arguments.");
            emit_byte(OP_FIND);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "trim", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after trim argument.");
            emit_byte(OP_TRIM);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "char", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after char argument.");
            emit_byte(OP_CHAR);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "ord", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after ord argument.");
            emit_byte(OP_ORD);
            return;
        }
    }
    
    named_variable(name, can_assign);
}

static void literal(bool can_assign) {
    (void)can_assign;
    switch (parser.previous.type) {
        case TOKEN_FALSE: emit_byte(OP_FALSE); break;
        case TOKEN_TRUE: emit_byte(OP_TRUE); break;
        default: return;
    }
}

static void unary(bool can_assign) {
    (void)can_assign;
    TokenType op_type = parser.previous.type;
    
    parse_precedence(PREC_UNARY);
    
    switch (op_type) {
        case TOKEN_MINUS: emit_byte(OP_NEG); break;
        case TOKEN_NOT: emit_byte(OP_NOT); break;
        default: return;
    }
}

static void binary(bool can_assign) {
    (void)can_assign;
    TokenType op_type = parser.previous.type;
    ParseRule* rule = get_rule(op_type);
    parse_precedence((Precedence)(rule->precedence + 1));
    
    switch (op_type) {
        case TOKEN_PLUS:    emit_byte(OP_ADD); break;
        case TOKEN_MINUS:   emit_byte(OP_SUB); break;
        case TOKEN_STAR:    emit_byte(OP_MUL); break;
        case TOKEN_SLASH:   emit_byte(OP_DIV); break;
        case TOKEN_PERCENT: emit_byte(OP_MOD); break;
        case TOKEN_EQ:      emit_byte(OP_EQ); break;
        case TOKEN_NEQ:     emit_byte(OP_NEQ); break;
        case TOKEN_LT:      emit_byte(OP_LT); break;
        case TOKEN_GT:      emit_byte(OP_GT); break;
        case TOKEN_LTE:     emit_byte(OP_LTE); break;
        case TOKEN_GTE:     emit_byte(OP_GTE); break;
        case TOKEN_BAND:    emit_byte(OP_BAND); break;
        case TOKEN_BOR:     emit_byte(OP_BOR); break;
        case TOKEN_BXOR:    emit_byte(OP_BXOR); break;
        case TOKEN_SHL:     emit_byte(OP_SHL); break;
        case TOKEN_SHR:     emit_byte(OP_SHR); break;
        default: return;
    }
}

static void and_(bool can_assign) {
    (void)can_assign;
    int end_jump = emit_jump(OP_JMP_FALSE);
    emit_byte(OP_POP);
    parse_precedence(PREC_AND);
    patch_jump(end_jump);
}

static void or_(bool can_assign) {
    (void)can_assign;
    int else_jump = emit_jump(OP_JMP_FALSE);
    int end_jump = emit_jump(OP_JMP);
    
    patch_jump(else_jump);
    emit_byte(OP_POP);
    
    parse_precedence(PREC_OR);
    patch_jump(end_jump);
}

static uint8_t argument_list(void) {
    uint8_t arg_count = 0;
    if (!check(TOKEN_RPAREN)) {
        do {
            expression();
            if (arg_count == 255) {
                error("Can't have more than 255 arguments.");
            }
            arg_count++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RPAREN, "Expect ')' after arguments.");
    return arg_count;
}

static void call(bool can_assign) {
    (void)can_assign;
    uint8_t arg_count = argument_list();
    emit_bytes(OP_CALL, arg_count);
}

static void index_(bool can_assign) {
    expression();
    consume(TOKEN_RBRACKET, "Expect ']' after index.");
    
    if (can_assign && match(TOKEN_ASSIGN)) {
        expression();
        emit_byte(OP_INDEX_SET);
    } else {
        emit_byte(OP_INDEX);
    }
}

static void array_literal(bool can_assign) {
    (void)can_assign;
    uint8_t count = 0;
    
    if (!check(TOKEN_RBRACKET)) {
        do {
            if (check(TOKEN_RBRACKET)) break;  /* Trailing comma */
            expression();
            count++;
        } while (match(TOKEN_COMMA));
    }
    
    consume(TOKEN_RBRACKET, "Expect ']' after array elements.");
    emit_bytes(OP_ARRAY, count);
}

static void range_expr(bool can_assign) {
    (void)can_assign;
    parse_precedence(PREC_TERM);  /* Parse end value */
    emit_byte(OP_RANGE);
}

ParseRule rules[] = {
    [TOKEN_LPAREN]    = {grouping,    call,    PREC_CALL},
    [TOKEN_RPAREN]    = {NULL,        NULL,    PREC_NONE},
    [TOKEN_LBRACKET]  = {array_literal, index_, PREC_CALL},
    [TOKEN_RBRACKET]  = {NULL,        NULL,    PREC_NONE},
    [TOKEN_COMMA]     = {NULL,        NULL,    PREC_NONE},
    [TOKEN_COLON]     = {NULL,        NULL,    PREC_NONE},
    [TOKEN_MINUS]     = {unary,       binary,  PREC_TERM},
    [TOKEN_PLUS]      = {NULL,        binary,  PREC_TERM},
    [TOKEN_SLASH]     = {NULL,        binary,  PREC_FACTOR},
    [TOKEN_STAR]      = {NULL,        binary,  PREC_FACTOR},
    [TOKEN_PERCENT]   = {NULL,        binary,  PREC_FACTOR},
    [TOKEN_BAND]      = {NULL,        binary,  PREC_BAND},
    [TOKEN_BOR]       = {NULL,        binary,  PREC_BOR},
    [TOKEN_BXOR]      = {NULL,        binary,  PREC_BXOR},
    [TOKEN_SHL]       = {NULL,        binary,  PREC_SHIFT},
    [TOKEN_SHR]       = {NULL,        binary,  PREC_SHIFT},
    [TOKEN_NEQ]       = {NULL,        binary,  PREC_EQUALITY},
    [TOKEN_ASSIGN]    = {NULL,        NULL,    PREC_NONE},
    [TOKEN_EQ]        = {NULL,        binary,  PREC_EQUALITY},
    [TOKEN_GT]        = {NULL,        binary,  PREC_COMPARISON},
    [TOKEN_GTE]       = {NULL,        binary,  PREC_COMPARISON},
    [TOKEN_LT]        = {NULL,        binary,  PREC_COMPARISON},
    [TOKEN_LTE]       = {NULL,        binary,  PREC_COMPARISON},
    [TOKEN_IDENT]     = {variable,    NULL,    PREC_NONE},
    [TOKEN_STRING]    = {string,      NULL,    PREC_NONE},
    [TOKEN_INT]       = {number,      NULL,    PREC_NONE},
    [TOKEN_FLOAT]     = {number,      NULL,    PREC_NONE},
    [TOKEN_AND]       = {NULL,        and_,    PREC_AND},
    [TOKEN_OR]        = {NULL,        or_,     PREC_OR},
    [TOKEN_NOT]       = {unary,       NULL,    PREC_NONE},
    [TOKEN_TRUE]      = {literal,     NULL,    PREC_NONE},
    [TOKEN_FALSE]     = {literal,     NULL,    PREC_NONE},
    [TOKEN_RANGE]     = {NULL,        range_expr, PREC_TERM},
    [TOKEN_LET]       = {NULL,        NULL,    PREC_NONE},
    [TOKEN_CONST]     = {NULL,        NULL,    PREC_NONE},
    [TOKEN_FN]        = {NULL,        NULL,    PREC_NONE},
    [TOKEN_RETURN]    = {NULL,        NULL,    PREC_NONE},
    [TOKEN_IF]        = {NULL,        NULL,    PREC_NONE},
    [TOKEN_THEN]      = {NULL,        NULL,    PREC_NONE},
    [TOKEN_ELIF]      = {NULL,        NULL,    PREC_NONE},
    [TOKEN_ELSE]      = {NULL,        NULL,    PREC_NONE},
    [TOKEN_END]       = {NULL,        NULL,    PREC_NONE},
    [TOKEN_WHILE]     = {NULL,        NULL,    PREC_NONE},
    [TOKEN_FOR]       = {NULL,        NULL,    PREC_NONE},
    [TOKEN_IN]        = {NULL,        NULL,    PREC_NONE},
    [TOKEN_DO]        = {NULL,        NULL,    PREC_NONE},
    [TOKEN_ARROW]     = {NULL,        NULL,    PREC_NONE},
    [TOKEN_NEWLINE]   = {NULL,        NULL,    PREC_NONE},
    [TOKEN_ERROR]     = {NULL,        NULL,    PREC_NONE},
    [TOKEN_EOF]       = {NULL,        NULL,    PREC_NONE},
};

static ParseRule* get_rule(TokenType type) {
    return &rules[type];
}

static void parse_precedence(Precedence precedence) {
    advance();
    ParseFn prefix_rule = get_rule(parser.previous.type)->prefix;
    
    if (prefix_rule == NULL) {
        error("Expect expression.");
        return;
    }
    
    bool can_assign = precedence <= PREC_ASSIGNMENT;
    prefix_rule(can_assign);
    
    while (precedence <= get_rule(parser.current.type)->precedence) {
        advance();
        ParseFn infix_rule = get_rule(parser.previous.type)->infix;
        infix_rule(can_assign);
    }
    
    if (can_assign && match(TOKEN_ASSIGN)) {
        error("Invalid assignment target.");
    }
}

static void expression(void) {
    parse_precedence(PREC_ASSIGNMENT);
}

/* ============ Statements ============ */

static void block(void) {
    skip_newlines();
    while (!check(TOKEN_END) && !check(TOKEN_ELSE) && !check(TOKEN_ELIF) && 
           !check(TOKEN_CASE) && !check(TOKEN_EOF)) {
        declaration();
        skip_newlines();
    }
}

static void if_statement(void) {
    expression();
    consume(TOKEN_THEN, "Expect 'then' after condition.");
    skip_newlines();
    
    int then_jump = emit_jump(OP_JMP_FALSE);
    emit_byte(OP_POP);
    
    block();
    
    /* Track all jumps that need to go to the end */
    int end_jumps[256];
    int end_jump_count = 0;
    
    end_jumps[end_jump_count++] = emit_jump(OP_JMP);
    patch_jump(then_jump);
    emit_byte(OP_POP);
    
    while (match(TOKEN_ELIF)) {
        expression();
        consume(TOKEN_THEN, "Expect 'then' after elif condition.");
        skip_newlines();
        
        int elif_jump = emit_jump(OP_JMP_FALSE);
        emit_byte(OP_POP);
        
        block();
        
        end_jumps[end_jump_count++] = emit_jump(OP_JMP);
        patch_jump(elif_jump);
        emit_byte(OP_POP);
    }
    
    if (match(TOKEN_ELSE)) {
        skip_newlines();
        block();
    }
    
    /* Patch all end jumps to here */
    for (int i = 0; i < end_jump_count; i++) {
        patch_jump(end_jumps[i]);
    }
    
    consume(TOKEN_END, "Expect 'end' after if statement.");
}

static void while_statement(void) {
    int loop_start = current_chunk()->count;
    
    expression();
    consume(TOKEN_DO, "Expect 'do' after condition.");
    skip_newlines();
    
    int exit_jump = emit_jump(OP_JMP_FALSE);
    emit_byte(OP_POP);
    
    block();
    
    emit_loop(loop_start);
    patch_jump(exit_jump);
    emit_byte(OP_POP);
    
    consume(TOKEN_END, "Expect 'end' after while loop.");
}

static void for_statement(void) {
    begin_scope();
    
    consume(TOKEN_IDENT, "Expect variable name.");
    Token var_name = parser.previous;
    
    consume(TOKEN_IN, "Expect 'in' after variable.");
    
    /* Parse the iterable expression (could be a range like 1..10 or an array) */
    expression();
    /* Stack now has the iterable (Range or Array) */
    
    consume(TOKEN_DO, "Expect 'do' after iterable.");
    skip_newlines();
    
    /* Create iterator local - the iterable is already on stack, just declare the local */
    add_local((Token){.start = "__iter", .length = 6, .line = 0});
    mark_initialized();
    int iter_slot = current->local_count - 1;
    /* The iterable on stack is now bound to iter_slot */
    
    /* Create loop variable local */
    add_local(var_name);
    mark_initialized();
    int var_slot = current->local_count - 1;
    emit_byte(OP_NIL);  /* Push placeholder for loop var */
    
    int loop_start = current_chunk()->count;
    
    /* Load iterator and get next */
    emit_bytes(OP_GET_LOCAL, iter_slot);
    int exit_jump = emit_jump(OP_ITER_NEXT);
    /* Stack now has: [..., iterator_copy, next_value] */
    emit_bytes(OP_SET_LOCAL, var_slot);
    emit_byte(OP_POP);  /* Pop the next_value */
    emit_byte(OP_POP);  /* Pop the iterator_copy */
    
    /* Begin inner scope for loop body variables */
    begin_scope();
    block();
    end_scope();  /* Clean up loop body variables before next iteration */
    
    emit_loop(loop_start);
    patch_jump(exit_jump);
    
    consume(TOKEN_END, "Expect 'end' after for loop.");
    end_scope();
}

static void return_statement(void) {
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }
    
    if (check(TOKEN_NEWLINE) || check(TOKEN_END) || check(TOKEN_EOF)) {
        emit_return();
    } else {
        expression();
        emit_byte(OP_RETURN);
    }
}

static void expression_statement(void) {
    expression();
    emit_byte(OP_POP);
}

static void print_statement(void) {
    /* Handle print(expr) as built-in */
    consume(TOKEN_LPAREN, "Expect '(' after 'print'.");
    expression();
    consume(TOKEN_RPAREN, "Expect ')' after value.");
    emit_byte(OP_PRINT);
}

/* Pattern matching: match expr case val1 then ... case val2 then ... else ... end */
static void match_statement(void) {
    expression();  /* The value to match against */
    skip_newlines();
    
    int end_jumps[256];
    int end_jump_count = 0;
    
    /* Parse case clauses */
    while (match(TOKEN_CASE)) {
        /* Duplicate the match value for comparison */
        emit_byte(OP_DUP);
        
        expression();  /* The case pattern/value */
        consume(TOKEN_THEN, "Expect 'then' after case value.");
        skip_newlines();
        
        /* Compare */
        emit_byte(OP_EQ);
        int next_case = emit_jump(OP_JMP_FALSE);
        emit_byte(OP_POP);  /* Pop comparison result */
        
        /* Execute case body */
        block();
        
        /* Jump to end after case body */
        end_jumps[end_jump_count++] = emit_jump(OP_JMP);
        
        patch_jump(next_case);
        emit_byte(OP_POP);  /* Pop comparison result */
    }
    
    /* Optional else clause (default case) */
    if (match(TOKEN_ELSE)) {
        skip_newlines();
        block();
    }
    
    /* Pop the match value */
    emit_byte(OP_POP);
    
    /* Patch all end jumps to here */
    for (int i = 0; i < end_jump_count; i++) {
        patch_jump(end_jumps[i]);
    }
    
    consume(TOKEN_END, "Expect 'end' after match statement.");
}

static void statement(void) {
    if (match(TOKEN_IF)) {
        if_statement();
    } else if (match(TOKEN_WHILE)) {
        while_statement();
    } else if (match(TOKEN_FOR)) {
        for_statement();
    } else if (match(TOKEN_MATCH)) {
        match_statement();
    } else if (match(TOKEN_RETURN)) {
        return_statement();
    } else {
        expression_statement();
    }
}

static void var_declaration(void) {
    uint8_t global = parse_variable("Expect variable name.");
    
    /* Optional type annotation */
    if (match(TOKEN_COLON)) {
        consume(TOKEN_IDENT, "Expect type name.");
    }
    
    consume(TOKEN_ASSIGN, "Expect '=' after variable name.");
    expression();
    
    define_variable(global);
}

static void function(FunctionType type) {
    Compiler compiler;
    init_compiler(&compiler, type);
    begin_scope();
    
    consume(TOKEN_LPAREN, "Expect '(' after function name.");
    
    if (!check(TOKEN_RPAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                error_at_current("Can't have more than 255 parameters.");
            }
            uint8_t constant = parse_variable("Expect parameter name.");
            (void)constant;
            if (match(TOKEN_COLON)) {
                consume(TOKEN_IDENT, "Expect type name.");
            }
            mark_initialized();
        } while (match(TOKEN_COMMA));
    }
    
    consume(TOKEN_RPAREN, "Expect ')' after parameters.");
    
    /* Optional return type */
    if (match(TOKEN_ARROW)) {
        consume(TOKEN_IDENT, "Expect return type.");
    }
    
    /* Jump over the function body - we'll patch this later */
    int jump = emit_jump(OP_JMP);
    
    /* Record where the function code starts */
    current->function->code_start = current_chunk()->count;
    
    skip_newlines();
    block();
    consume(TOKEN_END, "Expect 'end' after function body.");
    
    ObjFunction* func = end_compiler();
    
    /* Patch the jump to skip the function body */
    patch_jump(jump);
    
    emit_bytes(OP_CONST, make_constant(val_obj(func)));
}

static void fn_declaration(void) {
    uint8_t global = parse_variable("Expect function name.");
    mark_initialized();
    function(TYPE_FUNCTION);
    define_variable(global);
}

static void declaration(void) {
    skip_newlines();
    
    if (match(TOKEN_FN)) {
        fn_declaration();
    } else if (match(TOKEN_LET) || match(TOKEN_CONST)) {
        var_declaration();
    } else {
        statement();
    }
    
    skip_newlines();
}

/* ============ Compile Entry Point ============ */

bool compile(const char* source, Chunk* chunk, VM* vm) {
    scanner_init(source);
    
    Compiler compiler;
    compiling_chunk = chunk;
    compiling_vm = vm;
    init_compiler(&compiler, TYPE_SCRIPT);
    
    parser.had_error = false;
    parser.panic_mode = false;
    
    advance();
    
    while (!match(TOKEN_EOF)) {
        declaration();
    }
    
    emit_byte(OP_HALT);
    end_compiler();
    
    return !parser.had_error;
}
