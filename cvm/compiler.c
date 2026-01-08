/*
 * Pseudocode Language - Compiler
 * Single-pass Pratt parser with bytecode emission
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
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

/* Return values from emit_jump encode whether fusion happened */
/* If return value is negative, fusion happened (negate to get offset) */
/* If return value is positive, normal jump (offset directly) */
#define JUMP_WAS_FUSED(offset) ((offset) < 0)
#define JUMP_OFFSET(offset) ((offset) < 0 ? -(offset) : (offset))

/* Emit a raw jump without fusion - for cases where fusion would change semantics */
static int emit_jump_no_fuse(uint8_t instruction) {
    emit_byte(instruction);
    emit_byte(0xff);
    emit_byte(0xff);
    return current_chunk()->count - 2;
}

/* Inline fusion: check if we can fuse comparison with jump */
static int emit_jump(uint8_t instruction) {
    Chunk* chunk = current_chunk();
    
    /* INLINE FUSION: Fuse comparison + JMP_FALSE into single superinstruction */
    if (instruction == OP_JMP_FALSE && chunk->count >= 1) {
        uint8_t last = chunk->code[chunk->count - 1];
        uint8_t fused = 0;
        
        switch (last) {
            case OP_LT:  fused = OP_LT_JMP_FALSE; break;
            case OP_LTE: fused = OP_LTE_JMP_FALSE; break;
            case OP_GT:  fused = OP_GT_JMP_FALSE; break;
            case OP_GTE: fused = OP_GTE_JMP_FALSE; break;
            case OP_EQ:  fused = OP_EQ_JMP_FALSE; break;
            default: break;
        }
        
        if (fused != 0) {
            /* Replace comparison with fused instruction */
            chunk->code[chunk->count - 1] = fused;
            emit_byte(0xff);
            emit_byte(0xff);
            /* Return negative offset to signal fusion */
            return -(int)(chunk->count - 2);
        }
    }
    
    emit_byte(instruction);
    emit_byte(0xff);
    emit_byte(0xff);
    return current_chunk()->count - 2;
}

/* Emit POP only if the jump was NOT fused */
static void emit_pop_for_jump(int jump_offset) {
    if (!JUMP_WAS_FUSED(jump_offset)) {
        emit_byte(OP_POP);
    }
}

static void patch_jump(int offset) {
    /* Handle negative offsets from fused jumps */
    int actual_offset = JUMP_OFFSET(offset);
    int jump = current_chunk()->count - actual_offset - 2;
    
    if (jump > 65535) {
        error("Too much code to jump over.");
    }
    
    current_chunk()->code[actual_offset] = (jump >> 8) & 0xff;
    current_chunk()->code[actual_offset + 1] = jump & 0xff;
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
    const char* src = parser.previous.start + 1;
    int src_len = parser.previous.length - 2;
    
    /* Process escape sequences */
    char* buffer = malloc(src_len + 1);
    int dst_len = 0;
    
    for (int i = 0; i < src_len; i++) {
        if (src[i] == '\\' && i + 1 < src_len) {
            i++;
            switch (src[i]) {
                case 'n': buffer[dst_len++] = '\n'; break;
                case 't': buffer[dst_len++] = '\t'; break;
                case 'r': buffer[dst_len++] = '\r'; break;
                case '\\': buffer[dst_len++] = '\\'; break;
                case '"': buffer[dst_len++] = '"'; break;
                case '\'': buffer[dst_len++] = '\''; break;
                case '0': buffer[dst_len++] = '\0'; break;
                default: buffer[dst_len++] = src[i]; break;
            }
        } else {
            buffer[dst_len++] = src[i];
        }
    }
    buffer[dst_len] = '\0';
    
    ObjString* str = copy_string(compiling_vm, buffer, dst_len);
    free(buffer);
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
        /* Parse RHS and emit assignment */
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
        /* JIT-compiled intrinsics - native speed loops! */
        if (name.length == 14 && memcmp(name.start, "__jit_inc_loop", 14) == 0) {
            advance();
            expression();  /* x */
            consume(TOKEN_COMMA, "Expect ',' after x.");
            expression();  /* iterations */
            consume(TOKEN_RPAREN, "Expect ')' after __jit_inc_loop arguments.");
            emit_byte(OP_JIT_INC_LOOP);
            return;
        }
        if (name.length == 16 && memcmp(name.start, "__jit_arith_loop", 16) == 0) {
            advance();
            expression();  /* x */
            consume(TOKEN_COMMA, "Expect ',' after x.");
            expression();  /* iterations */
            consume(TOKEN_RPAREN, "Expect ')' after __jit_arith_loop arguments.");
            emit_byte(OP_JIT_ARITH_LOOP);
            return;
        }
        if (name.length == 17 && memcmp(name.start, "__jit_branch_loop", 17) == 0) {
            advance();
            expression();  /* x */
            consume(TOKEN_COMMA, "Expect ',' after x.");
            expression();  /* iterations */
            consume(TOKEN_RPAREN, "Expect ')' after __jit_branch_loop arguments.");
            emit_byte(OP_JIT_BRANCH_LOOP);
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
        
        /* ============ FILE I/O ============ */
        if (name.length == 9 && memcmp(name.start, "read_file", 9) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after read_file argument.");
            emit_byte(OP_READ_FILE);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "write_file", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after path.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after write_file arguments.");
            emit_byte(OP_WRITE_FILE);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "append_file", 11) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after path.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after append_file arguments.");
            emit_byte(OP_APPEND_FILE);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "file_exists", 11) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after file_exists argument.");
            emit_byte(OP_FILE_EXISTS);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "list_dir", 8) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after list_dir argument.");
            emit_byte(OP_LIST_DIR);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "delete_file", 11) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after delete_file argument.");
            emit_byte(OP_DELETE_FILE);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "mkdir", 5) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after mkdir argument.");
            emit_byte(OP_MKDIR);
            return;
        }
        
        /* ============ HTTP ============ */
        if (name.length == 8 && memcmp(name.start, "http_get", 8) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after http_get argument.");
            emit_byte(OP_HTTP_GET);
            return;
        }
        if (name.length == 9 && memcmp(name.start, "http_post", 9) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after URL.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after http_post arguments.");
            emit_byte(OP_HTTP_POST);
            return;
        }
        
        /* ============ JSON ============ */
        if (name.length == 10 && memcmp(name.start, "json_parse", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after json_parse argument.");
            emit_byte(OP_JSON_PARSE);
            return;
        }
        if (name.length == 14 && memcmp(name.start, "json_stringify", 14) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after json_stringify argument.");
            emit_byte(OP_JSON_STRINGIFY);
            return;
        }
        
        /* ============ PROCESS/SYSTEM ============ */
        if (name.length == 4 && memcmp(name.start, "exec", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after exec argument.");
            emit_byte(OP_EXEC);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "env", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after env argument.");
            emit_byte(OP_ENV);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "set_env", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after name.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after set_env arguments.");
            emit_byte(OP_SET_ENV);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "args", 4) == 0) {
            advance();
            consume(TOKEN_RPAREN, "Expect ')' after args.");
            emit_byte(OP_ARGS);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "exit", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after exit argument.");
            emit_byte(OP_EXIT);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "sleep", 5) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after sleep argument.");
            emit_byte(OP_SLEEP);
            return;
        }
        
        /* ============ DICTIONARY ============ */
        if (name.length == 4 && memcmp(name.start, "dict", 4) == 0) {
            advance();
            consume(TOKEN_RPAREN, "Expect ')' after dict.");
            emit_byte(OP_DICT);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "dict_get", 8) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after dict.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after dict_get arguments.");
            emit_byte(OP_DICT_GET);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "dict_set", 8) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after dict.");
            expression();
            consume(TOKEN_COMMA, "Expect ',' after key.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after dict_set arguments.");
            emit_byte(OP_DICT_SET);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "dict_has", 8) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after dict.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after dict_has arguments.");
            emit_byte(OP_DICT_HAS);
            return;
        }
        if (name.length == 9 && memcmp(name.start, "dict_keys", 9) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after dict_keys argument.");
            emit_byte(OP_DICT_KEYS);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "dict_values", 11) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after dict_values argument.");
            emit_byte(OP_DICT_VALUES);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "dict_delete", 11) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after dict.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after dict_delete arguments.");
            emit_byte(OP_DICT_DELETE);
            return;
        }
        
        /* ============ ADVANCED MATH ============ */
        if (name.length == 3 && memcmp(name.start, "sin", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after sin argument.");
            emit_byte(OP_SIN);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "cos", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after cos argument.");
            emit_byte(OP_COS);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "tan", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tan argument.");
            emit_byte(OP_TAN);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "asin", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after asin argument.");
            emit_byte(OP_ASIN);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "acos", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after acos argument.");
            emit_byte(OP_ACOS);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "atan", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after atan argument.");
            emit_byte(OP_ATAN);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "atan2", 5) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after y.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after atan2 arguments.");
            emit_byte(OP_ATAN2);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "log", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after log argument.");
            emit_byte(OP_LOG);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "log10", 5) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after log10 argument.");
            emit_byte(OP_LOG10);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "log2", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after log2 argument.");
            emit_byte(OP_LOG2);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "exp", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after exp argument.");
            emit_byte(OP_EXP);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "hypot", 5) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after x.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after hypot arguments.");
            emit_byte(OP_HYPOT);
            return;
        }
        
        /* ============ VECTOR OPERATIONS ============ */
        if (name.length == 7 && memcmp(name.start, "vec_add", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first array.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_add arguments.");
            emit_byte(OP_VEC_ADD);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_sub", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first array.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_sub arguments.");
            emit_byte(OP_VEC_SUB);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_mul", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first array.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_mul arguments.");
            emit_byte(OP_VEC_MUL);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_div", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first array.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_div arguments.");
            emit_byte(OP_VEC_DIV);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_dot", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first array.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_dot arguments.");
            emit_byte(OP_VEC_DOT);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_sum", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_sum argument.");
            emit_byte(OP_VEC_SUM);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "vec_prod", 8) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_prod argument.");
            emit_byte(OP_VEC_PROD);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_min", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_min argument.");
            emit_byte(OP_VEC_MIN);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_max", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_max argument.");
            emit_byte(OP_VEC_MAX);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "vec_mean", 8) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_mean argument.");
            emit_byte(OP_VEC_MEAN);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "vec_sort", 8) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_sort argument.");
            emit_byte(OP_VEC_SORT);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "vec_reverse", 11) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_reverse argument.");
            emit_byte(OP_VEC_REVERSE);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "vec_unique", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_unique argument.");
            emit_byte(OP_VEC_UNIQUE);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_zip", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first array.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_zip arguments.");
            emit_byte(OP_VEC_ZIP);
            return;
        }
        if (name.length == 9 && memcmp(name.start, "vec_range", 9) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after start.");
            expression();
            consume(TOKEN_COMMA, "Expect ',' after end.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_range arguments.");
            emit_byte(OP_VEC_RANGE);
            return;
        }
        
        /* ============ BINARY ============ */
        if (name.length == 5 && memcmp(name.start, "bytes", 5) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after bytes argument.");
            emit_byte(OP_BYTES);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "encode_utf8", 11) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after encode_utf8 argument.");
            emit_byte(OP_ENCODE_UTF8);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "decode_utf8", 11) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after decode_utf8 argument.");
            emit_byte(OP_DECODE_UTF8);
            return;
        }
        if (name.length == 13 && memcmp(name.start, "encode_base64", 13) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after encode_base64 argument.");
            emit_byte(OP_ENCODE_BASE64);
            return;
        }
        if (name.length == 13 && memcmp(name.start, "decode_base64", 13) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after decode_base64 argument.");
            emit_byte(OP_DECODE_BASE64);
            return;
        }
        
        /* ============ HASHING ============ */
        if (name.length == 4 && memcmp(name.start, "hash", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after hash argument.");
            emit_byte(OP_HASH);
            return;
        }
        if (name.length == 6 && memcmp(name.start, "sha256", 6) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after sha256 argument.");
            emit_byte(OP_HASH_SHA256);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "md5", 3) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after md5 argument.");
            emit_byte(OP_HASH_MD5);
            return;
        }
        
        /* ============ TENSOR OPERATIONS ============ */
        if (name.length == 12 && memcmp(name.start, "tensor_zeros", 12) == 0) {
            advance();
            expression();  /* shape array */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_zeros argument.");
            emit_byte(OP_TENSOR_ZEROS);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "tensor_ones", 11) == 0) {
            advance();
            expression();  /* shape array */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_ones argument.");
            emit_byte(OP_TENSOR_ONES);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "tensor_rand", 11) == 0) {
            advance();
            expression();  /* shape array */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_rand argument.");
            emit_byte(OP_TENSOR_RAND);
            return;
        }
        if (name.length == 12 && memcmp(name.start, "tensor_randn", 12) == 0) {
            advance();
            expression();  /* shape array */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_randn argument.");
            emit_byte(OP_TENSOR_RANDN);
            return;
        }
        if (name.length == 13 && memcmp(name.start, "tensor_arange", 13) == 0) {
            advance();
            expression();  /* start */
            consume(TOKEN_COMMA, "Expect ',' after start.");
            expression();  /* stop */
            consume(TOKEN_COMMA, "Expect ',' after stop.");
            expression();  /* step */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_arange arguments.");
            emit_byte(OP_TENSOR_ARANGE);
            return;
        }
        if (name.length == 6 && memcmp(name.start, "tensor", 6) == 0) {
            advance();
            expression();  /* array data */
            consume(TOKEN_RPAREN, "Expect ')' after tensor argument.");
            emit_byte(OP_TENSOR);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_add", 10) == 0) {
            advance();
            expression();  /* tensor a */
            consume(TOKEN_COMMA, "Expect ',' after first tensor.");
            expression();  /* tensor b */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_add arguments.");
            emit_byte(OP_TENSOR_ADD);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_sub", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first tensor.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_sub arguments.");
            emit_byte(OP_TENSOR_SUB);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_mul", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first tensor.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_mul arguments.");
            emit_byte(OP_TENSOR_MUL);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_div", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first tensor.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_div arguments.");
            emit_byte(OP_TENSOR_DIV);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_sum", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_sum argument.");
            emit_byte(OP_TENSOR_SUM);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "tensor_mean", 11) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_mean argument.");
            emit_byte(OP_TENSOR_MEAN);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_min", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_min argument.");
            emit_byte(OP_TENSOR_MIN);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_max", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_max argument.");
            emit_byte(OP_TENSOR_MAX);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "tensor_sqrt", 11) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_sqrt argument.");
            emit_byte(OP_TENSOR_SQRT);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_exp", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_exp argument.");
            emit_byte(OP_TENSOR_EXP);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_log", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_log argument.");
            emit_byte(OP_TENSOR_LOG);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_abs", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_abs argument.");
            emit_byte(OP_TENSOR_ABS);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_neg", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_neg argument.");
            emit_byte(OP_TENSOR_NEG);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_dot", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first tensor.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_dot arguments.");
            emit_byte(OP_TENSOR_DOT);
            return;
        }
        if (name.length == 13 && memcmp(name.start, "tensor_matmul", 13) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first tensor.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_matmul arguments.");
            emit_byte(OP_TENSOR_MATMUL);
            return;
        }
        if (name.length == 14 && memcmp(name.start, "tensor_reshape", 14) == 0) {
            advance();
            expression();  /* tensor */
            consume(TOKEN_COMMA, "Expect ',' after tensor.");
            expression();  /* new shape */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_reshape arguments.");
            emit_byte(OP_TENSOR_RESHAPE);
            return;
        }
        
        /* ============ MATRIX OPERATIONS ============ */
        if (name.length == 6 && memcmp(name.start, "matrix", 6) == 0) {
            advance();
            expression();  /* 2D array */
            consume(TOKEN_RPAREN, "Expect ')' after matrix argument.");
            emit_byte(OP_MATRIX);
            return;
        }
        if (name.length == 12 && memcmp(name.start, "matrix_zeros", 12) == 0) {
            advance();
            expression();  /* rows */
            consume(TOKEN_COMMA, "Expect ',' after rows.");
            expression();  /* cols */
            consume(TOKEN_RPAREN, "Expect ')' after matrix_zeros arguments.");
            emit_byte(OP_MATRIX_ZEROS);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "matrix_ones", 11) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after rows.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_ones arguments.");
            emit_byte(OP_MATRIX_ONES);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "matrix_eye", 10) == 0) {
            advance();
            expression();  /* n */
            consume(TOKEN_RPAREN, "Expect ')' after matrix_eye argument.");
            emit_byte(OP_MATRIX_EYE);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "matrix_rand", 11) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after rows.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_rand arguments.");
            emit_byte(OP_MATRIX_RAND);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "matrix_add", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first matrix.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_add arguments.");
            emit_byte(OP_MATRIX_ADD);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "matrix_sub", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first matrix.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_sub arguments.");
            emit_byte(OP_MATRIX_SUB);
            return;
        }
        if (name.length == 13 && memcmp(name.start, "matrix_matmul", 13) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first matrix.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_matmul arguments.");
            emit_byte(OP_MATRIX_MATMUL);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "matrix_t", 8) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_t argument.");
            emit_byte(OP_MATRIX_T);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "matrix_inv", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_inv argument.");
            emit_byte(OP_MATRIX_INV);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "matrix_det", 10) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_det argument.");
            emit_byte(OP_MATRIX_DET);
            return;
        }
        if (name.length == 12 && memcmp(name.start, "matrix_trace", 12) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_trace argument.");
            emit_byte(OP_MATRIX_TRACE);
            return;
        }
        if (name.length == 12 && memcmp(name.start, "matrix_solve", 12) == 0) {
            advance();
            expression();  /* A */
            consume(TOKEN_COMMA, "Expect ',' after A.");
            expression();  /* b */
            consume(TOKEN_RPAREN, "Expect ')' after matrix_solve arguments.");
            emit_byte(OP_MATRIX_SOLVE);
            return;
        }
        
        /* ============ NEURAL NETWORK ============ */
        if (name.length == 4 && memcmp(name.start, "relu", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after relu argument.");
            emit_byte(OP_NN_RELU);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "sigmoid", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after sigmoid argument.");
            emit_byte(OP_NN_SIGMOID);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "tanh", 4) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tanh argument.");
            emit_byte(OP_NN_TANH);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "softmax", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after softmax argument.");
            emit_byte(OP_NN_SOFTMAX);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "mse_loss", 8) == 0) {
            advance();
            expression();  /* predictions */
            consume(TOKEN_COMMA, "Expect ',' after predictions.");
            expression();  /* targets */
            consume(TOKEN_RPAREN, "Expect ')' after mse_loss arguments.");
            emit_byte(OP_NN_MSE_LOSS);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "ce_loss", 7) == 0) {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after predictions.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after ce_loss arguments.");
            emit_byte(OP_NN_CE_LOSS);
            return;
        }
        
        /* ============ AUTOGRAD ============ */
        if (name.length == 9 && memcmp(name.start, "grad_tape", 9) == 0) {
            advance();
            consume(TOKEN_RPAREN, "Expect ')' after grad_tape.");
            emit_byte(OP_GRAD_TAPE);
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
    
    Chunk* chunk = current_chunk();
    
    switch (op_type) {
        case TOKEN_PLUS:
            /* INLINE FUSION: CONST_1 ADD -> ADD_1 */
            if (chunk->count >= 1 && chunk->code[chunk->count - 1] == OP_CONST_1) {
                chunk->code[chunk->count - 1] = OP_ADD_1;
            } else {
                emit_byte(OP_ADD);
            }
            break;
        case TOKEN_MINUS:
            /* INLINE FUSION: CONST_1 SUB -> SUB_1 */
            if (chunk->count >= 1 && chunk->code[chunk->count - 1] == OP_CONST_1) {
                chunk->code[chunk->count - 1] = OP_SUB_1;
            } else {
                emit_byte(OP_SUB);
            }
            break;
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
    emit_pop_for_jump(then_jump);  /* Skip POP if fused comparison+jump */
    
    block();
    
    /* Track all jumps that need to go to the end */
    int end_jumps[256];
    int end_jump_count = 0;
    
    end_jumps[end_jump_count++] = emit_jump(OP_JMP);
    patch_jump(then_jump);
    emit_pop_for_jump(then_jump);  /* Skip POP if fused comparison+jump */
    
    while (match(TOKEN_ELIF)) {
        expression();
        consume(TOKEN_THEN, "Expect 'then' after elif condition.");
        skip_newlines();
        
        int elif_jump = emit_jump(OP_JMP_FALSE);
        emit_pop_for_jump(elif_jump);  /* Skip POP if fused comparison+jump */
        
        block();
        
        end_jumps[end_jump_count++] = emit_jump(OP_JMP);
        patch_jump(elif_jump);
        emit_pop_for_jump(elif_jump);  /* Skip POP if fused comparison+jump */
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
    emit_pop_for_jump(exit_jump);  /* Skip POP if fused */
    
    block();
    
    emit_loop(loop_start);
    patch_jump(exit_jump);
    emit_pop_for_jump(exit_jump);  /* Skip POP if fused */
    
    consume(TOKEN_END, "Expect 'end' after while loop.");
}

static void for_statement(void) {
    begin_scope();
    
    consume(TOKEN_IDENT, "Expect variable name.");
    Token var_name = parser.previous;
    
    consume(TOKEN_IN, "Expect 'in' after variable.");
    
    /* Parse the start expression (before ..) */
    parse_precedence(PREC_TERM);  /* Parse up to but not including .. */
    
    /* Check if this is a range expression for the fast path */
    if (match(TOKEN_RANGE)) {
        /* FAST PATH: for i in start..end - no Range object! */
        /* Stack has: start_value */
        
        /* Create __start local */
        add_local((Token){.start = "__start", .length = 7, .line = 0});
        mark_initialized();
        int start_slot = current->local_count - 1;
        /* start_value is now in start_slot */
        
        /* Parse end expression */
        parse_precedence(PREC_TERM);
        /* Stack has: end_value */
        
        /* Create __end local */
        add_local((Token){.start = "__end", .length = 5, .line = 0});
        mark_initialized();
        int end_slot = current->local_count - 1;
        /* end_value is now in end_slot */
        
        consume(TOKEN_DO, "Expect 'do' after range.");
        skip_newlines();
        
        /* Create loop variable local - initialized with start value */
        add_local(var_name);
        mark_initialized();
        int var_slot = current->local_count - 1;
        emit_byte(OP_NIL);  /* Placeholder, will be set by FOR_COUNT */
        
        int loop_start = current_chunk()->count;
        
        /* Ultra-fast FOR_COUNT instruction */
        /* Format: OP_FOR_COUNT, start_slot, end_slot, var_slot, offset[2] */
        emit_byte(OP_FOR_COUNT);
        emit_byte(start_slot);  /* Counter (starts at start, incremented each iter) */
        emit_byte(end_slot);    /* End value (constant) */
        emit_byte(var_slot);    /* Loop variable (set to counter each iter) */
        emit_byte(0xff);
        emit_byte(0xff);
        int exit_jump = current_chunk()->count - 2;
        
        /* Loop body */
        begin_scope();
        block();
        end_scope();
        
        emit_loop(loop_start);
        
        /* Patch exit jump */
        int jump = current_chunk()->count - exit_jump - 2;
        current_chunk()->code[exit_jump] = (jump >> 8) & 0xff;
        current_chunk()->code[exit_jump + 1] = jump & 0xff;
        
    } else {
        /* SLOW PATH: generic iterable (array, etc.) */
        /* The expression we just parsed is the full iterable */
        /* But we only parsed PREC_TERM, so we might have missed higher precedence */
        /* Actually for arrays this is fine, array literals use PREC_CALL */
        
        consume(TOKEN_DO, "Expect 'do' after iterable.");
        skip_newlines();
        
        /* Create iterator local (holds the array/range) */
        add_local((Token){.start = "__iter", .length = 6, .line = 0});
        mark_initialized();
        int iter_slot = current->local_count - 1;
        
        /* Create index local (for array iteration) */
        emit_byte(OP_CONST_0);  /* Start at index 0 */
        add_local((Token){.start = "__idx", .length = 5, .line = 0});
        mark_initialized();
        int idx_slot = current->local_count - 1;
        
        /* Create loop variable local */
        add_local(var_name);
        mark_initialized();
        int var_slot = current->local_count - 1;
        emit_byte(OP_NIL);
        
        int loop_start = current_chunk()->count;
        
        /* Use OP_FOR_LOOP with 3 slots: iter, idx, var */
        emit_byte(OP_FOR_LOOP);
        emit_byte(iter_slot);
        emit_byte(idx_slot);
        emit_byte(var_slot);
        emit_byte(0xff);
        emit_byte(0xff);
        int exit_jump = current_chunk()->count - 2;
        
        begin_scope();
        block();
        end_scope();
        
        emit_loop(loop_start);
        
        int jump = current_chunk()->count - exit_jump - 2;
        current_chunk()->code[exit_jump] = (jump >> 8) & 0xff;
        current_chunk()->code[exit_jump + 1] = jump & 0xff;
    }
    
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
        
        /* Compare - use no_fuse because we have OP_DUP above which changes stack semantics */
        emit_byte(OP_EQ);
        int next_case = emit_jump_no_fuse(OP_JMP_FALSE);
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

/* ============ Peephole Optimizer ============ */
/* Optimizes bytecode patterns after initial emission */
/* Note: Disabled for now as it corrupts jump offsets */
/* TODO: Track and update jump offsets when shifting bytecode */

#if 0  /* Disabled - needs jump offset tracking */
static void peephole_optimize(Chunk* chunk) {
    uint8_t* code = chunk->code;
    uint32_t count = chunk->count;
    
    for (uint32_t i = 0; i + 2 < count; ) {
        /* Pattern: CONST_1 ADD -> ADD_1 */
        if (code[i] == OP_CONST_1 && code[i+1] == OP_ADD) {
            code[i] = OP_ADD_1;
            /* Shift remaining code back by 1 */
            memmove(&code[i+1], &code[i+2], count - i - 2);
            chunk->count--;
            count--;
            continue;
        }
        
        /* Pattern: CONST_1 SUB -> SUB_1 */
        if (code[i] == OP_CONST_1 && code[i+1] == OP_SUB) {
            code[i] = OP_SUB_1;
            memmove(&code[i+1], &code[i+2], count - i - 2);
            chunk->count--;
            count--;
            continue;
        }
        
        /* Pattern: LT JMP_FALSE -> LT_JMP_FALSE */
        if (code[i] == OP_LT && code[i+1] == OP_JMP_FALSE) {
            code[i] = OP_LT_JMP_FALSE;
            memmove(&code[i+1], &code[i+2], count - i - 2);
            chunk->count--;
            count--;
            continue;
        }
        
        /* Pattern: LTE JMP_FALSE -> LTE_JMP_FALSE */
        if (code[i] == OP_LTE && code[i+1] == OP_JMP_FALSE) {
            code[i] = OP_LTE_JMP_FALSE;
            memmove(&code[i+1], &code[i+2], count - i - 2);
            chunk->count--;
            count--;
            continue;
        }
        
        /* Pattern: GT JMP_FALSE -> GT_JMP_FALSE */
        if (code[i] == OP_GT && code[i+1] == OP_JMP_FALSE) {
            code[i] = OP_GT_JMP_FALSE;
            memmove(&code[i+1], &code[i+2], count - i - 2);
            chunk->count--;
            count--;
            continue;
        }
        
        /* Pattern: GTE JMP_FALSE -> GTE_JMP_FALSE */
        if (code[i] == OP_GTE && code[i+1] == OP_JMP_FALSE) {
            code[i] = OP_GTE_JMP_FALSE;
            memmove(&code[i+1], &code[i+2], count - i - 2);
            chunk->count--;
            count--;
            continue;
        }
        
        /* Pattern: EQ JMP_FALSE -> EQ_JMP_FALSE */
        if (code[i] == OP_EQ && code[i+1] == OP_JMP_FALSE) {
            code[i] = OP_EQ_JMP_FALSE;
            memmove(&code[i+1], &code[i+2], count - i - 2);
            chunk->count--;
            count--;
            continue;
        }
        
        /* Pattern: POP POP -> POPN 2 (but we don't have POPN yet) */
        /* Pattern: NIL POP -> nothing */
        if (code[i] == OP_NIL && code[i+1] == OP_POP) {
            memmove(&code[i], &code[i+2], count - i - 2);
            chunk->count -= 2;
            count -= 2;
            continue;
        }
        
        /* Pattern: CONST CONST op -> fold to single CONST if both are numbers */
        /* This is more complex, handled separately */
        
        i++;
    }
}
#endif

/* ============ Constant Folding ============ */
/* Folds constant expressions at compile time */
/* Note: Disabled for now as it corrupts jump offsets */

#if 0  /* Disabled - needs jump offset tracking */
static bool try_fold_constants(Chunk* chunk) {
    uint8_t* code = chunk->code;
    uint32_t count = chunk->count;
    Value* constants = chunk->constants;
    bool folded = false;
    
    for (uint32_t i = 0; i + 4 < count; ) {
        /* Pattern: CONST a CONST b OP -> CONST result */
        if (code[i] == OP_CONST && code[i+2] == OP_CONST) {
            uint8_t idx1 = code[i+1];
            uint8_t idx2 = code[i+3];
            uint8_t op = code[i+4];
            
            Value v1 = constants[idx1];
            Value v2 = constants[idx2];
            
            /* Only fold numeric constants */
            bool is_num1 = IS_NUM(v1) || IS_INT(v1);
            bool is_num2 = IS_NUM(v2) || IS_INT(v2);
            
            if (is_num1 && is_num2) {
                double n1 = IS_INT(v1) ? (double)as_int(v1) : as_num(v1);
                double n2 = IS_INT(v2) ? (double)as_int(v2) : as_num(v2);
                double result = 0;
                bool can_fold = true;
                bool is_bool = false;
                bool bool_result = false;
                
                switch (op) {
                    case OP_ADD: result = n1 + n2; break;
                    case OP_SUB: result = n1 - n2; break;
                    case OP_MUL: result = n1 * n2; break;
                    case OP_DIV: 
                        if (n2 != 0) result = n1 / n2;
                        else can_fold = false;
                        break;
                    case OP_MOD:
                        if (n2 != 0) result = (double)((int64_t)n1 % (int64_t)n2);
                        else can_fold = false;
                        break;
                    case OP_LT: is_bool = true; bool_result = n1 < n2; break;
                    case OP_GT: is_bool = true; bool_result = n1 > n2; break;
                    case OP_LTE: is_bool = true; bool_result = n1 <= n2; break;
                    case OP_GTE: is_bool = true; bool_result = n1 >= n2; break;
                    case OP_EQ: is_bool = true; bool_result = n1 == n2; break;
                    case OP_NEQ: is_bool = true; bool_result = n1 != n2; break;
                    default: can_fold = false; break;
                }
                
                if (can_fold) {
                    /* Replace with single constant or boolean */
                    if (is_bool) {
                        code[i] = bool_result ? OP_TRUE : OP_FALSE;
                        /* Remove the next 4 bytes */
                        memmove(&code[i+1], &code[i+5], count - i - 5);
                        chunk->count -= 4;
                        count -= 4;
                    } else {
                        /* Add new constant */
                        Value folded;
                        if (IS_INT(v1) && IS_INT(v2) && result == (int32_t)result) {
                            folded = val_int((int32_t)result);
                        } else {
                            folded = val_num(result);
                        }
                        uint8_t new_idx = (uint8_t)chunk_add_const(chunk, folded);
                        code[i] = OP_CONST;
                        code[i+1] = new_idx;
                        /* Remove the next 3 bytes */
                        memmove(&code[i+2], &code[i+5], count - i - 5);
                        chunk->count -= 3;
                        count -= 3;
                    }
                    folded = true;
                    continue;
                }
            }
        }
        i++;
    }
    
    return folded;
}
#endif

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
    
    /* Note: Peephole and constant folding optimizations are disabled
     * because they corrupt jump offsets when shifting bytecode.
     * The existing superinstructions (OP_CONST_0/1/2, OP_GET_LOCAL_0-3)
     * are emitted directly during compilation which is safer. */
    
    return !parser.had_error;
}
