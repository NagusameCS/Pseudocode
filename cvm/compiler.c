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

void scanner_init(const char *source);
Token scan_token(void);

/* Scanner state for string interpolation */
typedef struct
{
    const char *start;
    const char *current;
    int line;
} ScannerState;

void scanner_save_state(ScannerState *state);
void scanner_restore_state(const ScannerState *state);

/* ============ Parser State ============ */

typedef struct
{
    Token current;
    Token previous;
    bool had_error;
    bool panic_mode;
} Parser;

typedef enum
{
    PREC_NONE,
    PREC_ASSIGNMENT, /* = */
    PREC_OR,         /* or */
    PREC_AND,        /* and */
    PREC_BOR,        /* | */
    PREC_BXOR,       /* ^ */
    PREC_BAND,       /* & */
    PREC_EQUALITY,   /* == != */
    PREC_COMPARISON, /* < > <= >= */
    PREC_SHIFT,      /* << >> */
    PREC_TERM,       /* + - */
    PREC_FACTOR,     /* * / % */
    PREC_UNARY,      /* - not */
    PREC_CALL,       /* . () [] */
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool can_assign);

typedef struct
{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

/* Forward declaration of CompileTimeType */
typedef enum
{
    CTYPE_UNKNOWN, /* Type not known at compile time */
    CTYPE_INT,     /* Definitely an integer */
    CTYPE_NUM,     /* Definitely a float */
    CTYPE_BOOL,    /* Definitely a boolean */
    CTYPE_STRING,  /* Definitely a string */
    CTYPE_NIL,     /* Definitely nil */
    CTYPE_ARRAY,   /* Definitely an array */
} CompileTimeType;

/* Compile-time escape state for escape analysis */
typedef enum {
    ESCAPE_NONE = 0,          /* Does not escape */
    ESCAPE_VIA_RETURN,        /* Escapes via return */
    ESCAPE_VIA_UPVALUE,       /* Escapes via closure capture */
    ESCAPE_VIA_GLOBAL,        /* Escapes to global scope */
    ESCAPE_VIA_CALL,          /* Escapes via function argument */
} CompileEscapeState;

typedef struct
{
    Token name;
    int depth;
    CompileTimeType inferred_type;  /* Inferred type for specialization */
    bool is_captured;               /* True if captured by a closure */
    uint8_t escape_state;           /* CompileEscapeState - how this local escapes */
    bool is_object;                 /* True if this local holds an object reference */
} Local;

typedef enum
{
    TYPE_FUNCTION,
    TYPE_SCRIPT,
    TYPE_METHOD,
    TYPE_INITIALIZER,
    TYPE_GENERATOR, /* Generator function (uses yield) */
    TYPE_ASYNC,     /* Async function (returns Promise) */
} FunctionType;

/* Upvalue tracking for closures */
typedef struct
{
    uint8_t index;
    bool is_local; /* true if capturing enclosing local, false if upvalue */
} Upvalue;

typedef struct Compiler
{
    struct Compiler *enclosing;
    ObjFunction *function;
    FunctionType type;

    Local locals[256];
    int local_count;
    int scope_depth;

    Upvalue upvalues[256];
} Compiler;

static Parser parser;
static Compiler *current = NULL;
static Chunk *compiling_chunk;
static VM *compiling_vm;

static Chunk *current_chunk(void)
{
    return compiling_chunk;
}

/* ============ Compile-Time Optimization State ============ */

/* Track the last emitted value for constant folding and type inference */
typedef struct
{
    bool is_constant;     /* Was last emit a constant? */
    Value value;          /* The constant value */
    size_t bytecode_pos;  /* Position in bytecode (to remove if folded) */
    uint8_t const_idx;    /* Constant pool index */
    CompileTimeType type; /* Inferred type (for type specialization) */
} LastEmit;

static LastEmit last_emit = {false, 0, 0, 0, CTYPE_UNKNOWN};
static LastEmit second_last_emit = {false, 0, 0, 0, CTYPE_UNKNOWN};

/* Flag to prevent fusion after and/or expressions (they have internal jumps) */
static bool inhibit_jump_fusion = false;

/* Track if last emit was a function call (for tail call optimization) */
static bool last_was_call = false;
static uint8_t last_call_arg_count = 0;

static void reset_last_emit(void)
{
    second_last_emit = last_emit;
    last_emit.is_constant = false;
    last_emit.type = CTYPE_UNKNOWN;
    last_was_call = false;
}

static void track_constant(Value value, uint8_t idx)
{
    second_last_emit = last_emit;
    last_emit.is_constant = true;
    last_emit.value = value;
    last_emit.bytecode_pos = current_chunk()->count - 2; /* OP_CONST + idx */
    last_emit.const_idx = idx;
    /* Infer type from constant value */
    if (IS_INT(value))
    {
        last_emit.type = CTYPE_INT;
    }
    else if (IS_NUM(value))
    {
        last_emit.type = CTYPE_NUM;
    }
    else if (IS_BOOL(value))
    {
        last_emit.type = CTYPE_BOOL;
    }
    else if (IS_NIL(value))
    {
        last_emit.type = CTYPE_NIL;
    }
    else
    {
        last_emit.type = CTYPE_UNKNOWN;
    }
}

/* Track a non-constant integer value (e.g., result of integer operation) */
static void track_int_result(void)
{
    second_last_emit = last_emit;
    last_emit.is_constant = false;
    last_emit.type = CTYPE_INT;
}

/* Track a non-constant boolean value */
static void track_bool_result(void)
{
    second_last_emit = last_emit;
    last_emit.is_constant = false;
    last_emit.type = CTYPE_BOOL;
}

/* ============ Error Handling ============ */

static void error_at(Token *token, const char *message)
{
    if (parser.panic_mode)
        return;
    parser.panic_mode = true;

    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if (token->type != TOKEN_ERROR)
    {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.had_error = true;
}

static void error(const char *message)
{
    error_at(&parser.previous, message);
}

static void error_at_current(const char *message)
{
    error_at(&parser.current, message);
}

/* ============ Token Handling ============ */

static void advance(void)
{
    parser.previous = parser.current;

    for (;;)
    {
        parser.current = scan_token();
        if (parser.current.type != TOKEN_ERROR)
            break;
        error_at_current(parser.current.start);
    }
}

static void consume(TokenType type, const char *message)
{
    if (parser.current.type == type)
    {
        advance();
        return;
    }
    error_at_current(message);
}

static bool check(TokenType type)
{
    return parser.current.type == type;
}

static bool match(TokenType type)
{
    if (!check(type))
        return false;
    advance();
    return true;
}

static void skip_newlines(void)
{
    while (match(TOKEN_NEWLINE))
        ;
}

/* ============ Bytecode Emission ============ */

static void emit_byte(uint8_t byte)
{
    chunk_write(current_chunk(), byte, parser.previous.line);
}

static void emit_bytes(uint8_t byte1, uint8_t byte2)
{
    emit_byte(byte1);
    emit_byte(byte2);
}

static void emit_short(uint16_t value)
{
    emit_byte((value >> 8) & 0xff);
    emit_byte(value & 0xff);
}

/* Return values from emit_jump encode whether fusion happened */
/* If return value is negative, fusion happened (negate to get offset) */
/* If return value is positive, normal jump (offset directly) */
#define JUMP_WAS_FUSED(offset) ((offset) < 0)
#define JUMP_OFFSET(offset) ((offset) < 0 ? -(offset) : (offset))

/* Emit a raw jump without fusion - for cases where fusion would change semantics */
static int emit_jump_no_fuse(uint8_t instruction)
{
    emit_byte(instruction);
    emit_byte(0xff);
    emit_byte(0xff);
    return current_chunk()->count - 2;
}

/* Inline fusion: check if we can fuse comparison with jump */
static int emit_jump(uint8_t instruction)
{
    Chunk *chunk = current_chunk();

    /* INLINE FUSION: Fuse comparison + JMP_FALSE into single superinstruction */
    /* Skip fusion if inhibited (after and/or expressions with internal jumps) */
    if (instruction == OP_JMP_FALSE && chunk->count >= 1 && !inhibit_jump_fusion)
    {
        uint8_t last = chunk->code[chunk->count - 1];
        uint8_t fused = 0;

        switch (last)
        {
        case OP_LT:
            fused = OP_LT_JMP_FALSE;
            break;
        case OP_LTE:
            fused = OP_LTE_JMP_FALSE;
            break;
        case OP_GT:
            fused = OP_GT_JMP_FALSE;
            break;
        case OP_GTE:
            fused = OP_GTE_JMP_FALSE;
            break;
        case OP_EQ:
            fused = OP_EQ_JMP_FALSE;
            break;
        default:
            break;
        }

        if (fused != 0)
        {
            /* Replace comparison with fused instruction */
            chunk->code[chunk->count - 1] = fused;
            emit_byte(0xff);
            emit_byte(0xff);
            /* Return negative offset to signal fusion */
            return -(int)(chunk->count - 2);
        }
    }

    /* Clear fusion inhibit after use */
    inhibit_jump_fusion = false;

    emit_byte(instruction);
    emit_byte(0xff);
    emit_byte(0xff);
    return current_chunk()->count - 2;
}

/* Emit POP only if the jump was NOT fused */
static void emit_pop_for_jump(int jump_offset)
{
    if (!JUMP_WAS_FUSED(jump_offset))
    {
        emit_byte(OP_POP);
    }
}

static void patch_jump(int offset)
{
    /* Handle negative offsets from fused jumps */
    int actual_offset = JUMP_OFFSET(offset);
    int jump = current_chunk()->count - actual_offset - 2;

    if (jump > 65535)
    {
        error("Too much code to jump over.");
    }

    current_chunk()->code[actual_offset] = (jump >> 8) & 0xff;
    current_chunk()->code[actual_offset + 1] = jump & 0xff;
}

static void emit_loop(int loop_start)
{
    emit_byte(OP_LOOP);

    int offset = current_chunk()->count - loop_start + 2;
    if (offset > 65535)
        error("Loop body too large.");

    emit_byte((offset >> 8) & 0xff);
    emit_byte(offset & 0xff);
}

static uint8_t make_constant(Value value)
{
    int constant = chunk_add_const(current_chunk(), value);
    if (constant > 255)
    {
        error("Too many constants in one chunk.");
        return 0;
    }
    return (uint8_t)constant;
}

static void emit_constant(Value value)
{
    uint8_t idx = make_constant(value);
    emit_bytes(OP_CONST, idx);
    track_constant(value, idx);
}

/* Emit constant without tracking (used for folded results) */
static void emit_constant_untracked(Value value)
{
    emit_bytes(OP_CONST, make_constant(value));
    reset_last_emit();
}

static void emit_return(void)
{
    emit_byte(OP_NIL);
    emit_byte(OP_RETURN);
}

/* ============ Locals ============ */

static void init_compiler(Compiler *compiler, FunctionType type)
{
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->function = new_function(compiling_vm);
    current = compiler;

    /* Reserve slot 0 for the function itself in calls */
    Local *local = &current->locals[current->local_count++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
    local->is_captured = false;
    local->escape_state = ESCAPE_NONE;
    local->is_object = false;
}

static ObjFunction *end_compiler(void)
{
    emit_return();
    ObjFunction *function = current->function;
    
    /* Calculate function bytecode length for inlining analysis */
    uint32_t code_end = compiling_chunk->count;
    function->code_length = code_end - function->code_start;
    
    /* Determine if function is inlinable:
     * - Small bytecode size (< INLINE_THRESHOLD)
     * - No upvalues (closures complicate inlining)
     * - No recursive calls (would create infinite loop)
     */
    function->can_inline = (function->code_length < INLINE_THRESHOLD &&
                            function->upvalue_count == 0);
    
    current = current->enclosing;
    return function;
}

static void begin_scope(void)
{
    current->scope_depth++;
}

static void end_scope(void)
{
    current->scope_depth--;

    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth > current->scope_depth)
    {
        /* If variable was captured by a closure, close it instead of popping */
        if (current->locals[current->local_count - 1].is_captured)
        {
            emit_byte(OP_CLOSE_UPVALUE);
        }
        else
        {
            emit_byte(OP_POP);
        }
        current->local_count--;
    }
}

static bool identifiers_equal(Token *a, Token *b)
{
    if (a->length != b->length)
        return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolve_local(Compiler *compiler, Token *name)
{
    for (int i = compiler->local_count - 1; i >= 0; i--)
    {
        Local *local = &compiler->locals[i];
        if (identifiers_equal(name, &local->name))
        {
            if (local->depth == -1)
            {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }
    return -1;
}

/* Add an upvalue to the compiler's upvalue array */
static int add_upvalue(Compiler *compiler, uint8_t index, bool is_local)
{
    int upvalue_count = compiler->function->upvalue_count;

    /* Check if we already captured this variable */
    for (int i = 0; i < upvalue_count; i++)
    {
        Upvalue *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->is_local == is_local)
        {
            return i;
        }
    }

    if (upvalue_count == 256)
    {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalue_count].is_local = is_local;
    compiler->upvalues[upvalue_count].index = index;
    return compiler->function->upvalue_count++;
}

/* Resolve a variable in enclosing scopes, creating upvalues as needed */
static int resolve_upvalue(Compiler *compiler, Token *name)
{
    if (compiler->enclosing == NULL)
        return -1;

    /* First, look for a local variable in the immediately enclosing function */
    int local = resolve_local(compiler->enclosing, name);
    if (local != -1)
    {
        compiler->enclosing->locals[local].is_captured = true;
        /* ESCAPE ANALYSIS: Captured by closure = escapes via upvalue */
        compiler->enclosing->locals[local].escape_state = ESCAPE_VIA_UPVALUE;
        return add_upvalue(compiler, (uint8_t)local, true);
    }

    /* Otherwise, look for an upvalue in the enclosing function */
    int upvalue = resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1)
    {
        return add_upvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

static void add_local(Token name)
{
    if (current->local_count == 256)
    {
        error("Too many local variables in function.");
        return;
    }

    Local *local = &current->locals[current->local_count++];
    local->name = name;
    local->depth = -1;                    /* Mark uninitialized */
    local->inferred_type = CTYPE_UNKNOWN; /* Will be set on first assignment */
    local->is_captured = false;           /* Not captured by default */
    local->escape_state = ESCAPE_NONE;    /* Assume no escape initially */
    local->is_object = false;             /* Will be set based on assigned value */
}

static void declare_variable(void)
{
    if (current->scope_depth == 0)
        return;

    Token *name = &parser.previous;

    /* Check for redeclaration in same scope */
    for (int i = current->local_count - 1; i >= 0; i--)
    {
        Local *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scope_depth)
        {
            break;
        }

        if (identifiers_equal(name, &local->name))
        {
            error("Already a variable with this name in this scope.");
        }
    }

    add_local(*name);
}

/* ============ Escape Analysis ============ */

/* Mark a local variable as escaping with the given reason */
static void mark_local_escapes(int slot, CompileEscapeState reason)
{
    if (slot < 0 || slot >= current->local_count) return;
    Local *local = &current->locals[slot];
    /* Keep the "worst" escape state (higher enum = worse) */
    if (reason > local->escape_state)
    {
        local->escape_state = reason;
    }
}

/* Check if a local is a candidate for stack allocation (doesn't escape) */
__attribute__((unused))
static bool can_stack_allocate(int slot)
{
    if (slot < 0 || slot >= current->local_count) return false;
    Local *local = &current->locals[slot];
    return local->is_object && local->escape_state == ESCAPE_NONE;
}

/* Mark a local as holding an object reference */
__attribute__((unused))
static void mark_local_is_object(int slot)
{
    if (slot < 0 || slot >= current->local_count) return;
    current->locals[slot].is_object = true;
}

static uint8_t identifier_constant(Token *name)
{
    ObjString *str = copy_string(compiling_vm, name->start, name->length);
    return make_constant(val_obj(str));
}

static uint8_t parse_variable(const char *error_message)
{
    consume(TOKEN_IDENT, error_message);

    declare_variable();
    if (current->scope_depth > 0)
        return 0;

    return identifier_constant(&parser.previous);
}

static void mark_initialized(void)
{
    if (current->scope_depth == 0)
        return;
    current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(uint8_t global)
{
    if (current->scope_depth > 0)
    {
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
static ParseRule *get_rule(TokenType type);
static void parse_precedence(Precedence precedence);
static void lambda(bool can_assign);
static void super_(bool can_assign);
static void yield_expr(bool can_assign);
static void await_expr(bool can_assign);

static void grouping(bool can_assign)
{
    (void)can_assign;
    expression();
    consume(TOKEN_RPAREN, "Expect ')' after expression.");
}

static void number(bool can_assign)
{
    (void)can_assign;
    if (parser.previous.type == TOKEN_INT)
    {
        int64_t value = strtoll(parser.previous.start, NULL, 10);
        if (value >= INT32_MIN && value <= INT32_MAX)
        {
            int32_t v = (int32_t)value;
            /* SUPERINSTRUCTION: Use specialized opcodes for small constants */
            if (v == 0)
            {
                emit_byte(OP_CONST_0);
                /* Track for constant folding and type inference */
                second_last_emit = last_emit;
                last_emit.is_constant = true;
                last_emit.value = val_int(0);
                last_emit.bytecode_pos = current_chunk()->count - 1;
                last_emit.const_idx = 0;
                last_emit.type = CTYPE_INT;
            }
            else if (v == 1)
            {
                emit_byte(OP_CONST_1);
                second_last_emit = last_emit;
                last_emit.is_constant = true;
                last_emit.value = val_int(1);
                last_emit.bytecode_pos = current_chunk()->count - 1;
                last_emit.const_idx = 0;
                last_emit.type = CTYPE_INT;
            }
            else if (v == 2)
            {
                emit_byte(OP_CONST_2);
                second_last_emit = last_emit;
                last_emit.is_constant = true;
                last_emit.value = val_int(2);
                last_emit.bytecode_pos = current_chunk()->count - 1;
                last_emit.const_idx = 0;
                last_emit.type = CTYPE_INT;
            }
            else
            {
                emit_constant(val_int(v));
            }
        }
        else
        {
            emit_constant(val_num((double)value));
        }
    }
    else
    {
        double value = strtod(parser.previous.start, NULL);
        emit_constant(val_num(value));
    }
}

/* Process escape sequences in a string segment, returns malloc'd buffer */
static char *process_escape_sequences(const char *src, int src_len, int *out_len)
{
    char *buffer = malloc(src_len + 1);
    int dst_len = 0;

    for (int i = 0; i < src_len; i++)
    {
        if (src[i] == '\\' && i + 1 < src_len)
        {
            i++;
            switch (src[i])
            {
            case 'n':
                buffer[dst_len++] = '\n';
                break;
            case 't':
                buffer[dst_len++] = '\t';
                break;
            case 'r':
                buffer[dst_len++] = '\r';
                break;
            case '\\':
                buffer[dst_len++] = '\\';
                break;
            case '"':
                buffer[dst_len++] = '"';
                break;
            case '\'':
                buffer[dst_len++] = '\'';
                break;
            case '0':
                buffer[dst_len++] = '\0';
                break;
            case '$':
                buffer[dst_len++] = '$';
                break;
            default:
                buffer[dst_len++] = src[i];
                break;
            }
        }
        else
        {
            buffer[dst_len++] = src[i];
        }
    }
    buffer[dst_len] = '\0';
    *out_len = dst_len;
    return buffer;
}

/* Emit a string constant from processed buffer */
static void emit_string_segment(const char *buffer, int len)
{
    ObjString *str = copy_string(compiling_vm, buffer, len);
    emit_constant(val_obj(str));
}

/* Forward declarations for interpolation */
static void expression(void);

static void string(bool can_assign)
{
    (void)can_assign;
    /* Skip opening and closing quotes */
    const char *src = parser.previous.start + 1;
    int src_len = parser.previous.length - 2;

    /* Check if this string contains interpolation */
    bool has_interpolation = false;
    for (int i = 0; i < src_len - 1; i++)
    {
        if (src[i] == '$' && src[i + 1] == '{' && (i == 0 || src[i - 1] != '\\'))
        {
            has_interpolation = true;
            break;
        }
    }

    if (!has_interpolation)
    {
        /* Simple string - no interpolation */
        int dst_len;
        char *buffer = process_escape_sequences(src, src_len, &dst_len);
        ObjString *str = copy_string(compiling_vm, buffer, dst_len);
        free(buffer);
        emit_constant(val_obj(str));
        return;
    }

    /* String interpolation: "Hello ${name}!" becomes "Hello " + str(name) + "!" */
    int parts_emitted = 0;
    int segment_start = 0;

    for (int i = 0; i < src_len; i++)
    {
        /* Check for unescaped ${ */
        if (src[i] == '$' && i + 1 < src_len && src[i + 1] == '{' &&
            (i == 0 || src[i - 1] != '\\'))
        {
            /* Emit the string segment before ${ */
            if (i > segment_start)
            {
                int seg_len;
                char *seg = process_escape_sequences(src + segment_start, i - segment_start, &seg_len);
                emit_string_segment(seg, seg_len);
                free(seg);
                if (parts_emitted > 0)
                    emit_byte(OP_ADD);
                parts_emitted++;
            }

            /* Find matching } */
            int brace_depth = 1;
            int expr_start = i + 2;
            int expr_end = expr_start;
            while (expr_end < src_len && brace_depth > 0)
            {
                if (src[expr_end] == '{')
                    brace_depth++;
                else if (src[expr_end] == '}')
                    brace_depth--;
                if (brace_depth > 0)
                    expr_end++;
            }

            if (brace_depth != 0)
            {
                error("Unterminated interpolation in string.");
                return;
            }

            /* Parse the expression inside ${} */
            int expr_len = expr_end - expr_start;
            if (expr_len > 0)
            {
                /* Save scanner and parser state */
                ScannerState saved_scanner;
                scanner_save_state(&saved_scanner);
                Token old_current_tok = parser.current;
                Token old_previous_tok = parser.previous;

                /* Create a temporary null-terminated copy of the expression */
                char *expr_copy = malloc(expr_len + 1);
                memcpy(expr_copy, src + expr_start, expr_len);
                expr_copy[expr_len] = '\0';

                /* Initialize scanner on the expression */
                scanner_init(expr_copy);
                advance(); /* Prime the parser with first token */

                /* Parse the expression */
                expression();

                free(expr_copy);

                /* Restore scanner and parser state */
                scanner_restore_state(&saved_scanner);
                parser.current = old_current_tok;
                parser.previous = old_previous_tok;

                /* Convert to string */
                emit_byte(OP_STR);

                /* Concatenate with previous parts */
                if (parts_emitted > 0)
                    emit_byte(OP_ADD);
                parts_emitted++;
            }

            /* Move past the closing } */
            i = expr_end;
            segment_start = expr_end + 1;
        }
    }

    /* Emit remaining string segment after last interpolation */
    if (segment_start < src_len)
    {
        int seg_len;
        char *seg = process_escape_sequences(src + segment_start, src_len - segment_start, &seg_len);
        emit_string_segment(seg, seg_len);
        free(seg);
        if (parts_emitted > 0)
            emit_byte(OP_ADD);
        parts_emitted++;
    }

    /* If no parts were emitted (empty string with just ${}), emit empty string */
    if (parts_emitted == 0)
    {
        ObjString *str = copy_string(compiling_vm, "", 0);
        emit_constant(val_obj(str));
    }
}

static void named_variable(Token name, bool can_assign)
{
    uint8_t get_op, set_op;
    int arg = resolve_local(current, &name);

    if (arg != -1)
    {
        get_op = OP_GET_LOCAL;
        set_op = OP_SET_LOCAL;
    }
    else if ((arg = resolve_upvalue(current, &name)) != -1)
    {
        get_op = OP_GET_UPVALUE;
        set_op = OP_SET_UPVALUE;
    }
    else
    {
        arg = identifier_constant(&name);
        get_op = OP_GET_GLOBAL;
        set_op = OP_SET_GLOBAL;
    }

    if (can_assign && match(TOKEN_ASSIGN))
    {
        /* Parse RHS and emit assignment */
        expression();
        /* Update local's inferred type from the RHS */
        if (get_op == OP_SET_LOCAL)
        {
            current->locals[arg].inferred_type = last_emit.type;
        }
        emit_bytes(set_op, (uint8_t)arg);
        reset_last_emit(); /* Assignment result is not a constant */
    }
    else
    {
        /* SUPERINSTRUCTION: Use specialized opcodes for common local slots */
        if (get_op == OP_GET_LOCAL && arg <= 3)
        {
            switch (arg)
            {
            case 0:
                emit_byte(OP_GET_LOCAL_0);
                break;
            case 1:
                emit_byte(OP_GET_LOCAL_1);
                break;
            case 2:
                emit_byte(OP_GET_LOCAL_2);
                break;
            case 3:
                emit_byte(OP_GET_LOCAL_3);
                break;
            }
        }
        else
        {
            emit_bytes(get_op, (uint8_t)arg);
        }
        /* Propagate local's inferred type */
        if (arg != -1 && get_op == OP_GET_LOCAL)
        {
            second_last_emit = last_emit;
            last_emit.is_constant = false;
            last_emit.type = current->locals[arg].inferred_type;
        }
        else
        {
            reset_last_emit(); /* Global variable or unknown - type unknown */
        }
    }
}

static void variable(bool can_assign)
{
    Token name = parser.previous;

    /* Check for built-in function calls */
    if (check(TOKEN_LPAREN))
    {
        /* Check if it's a built-in */
        if (name.length == 5 && memcmp(name.start, "print", 5) == 0)
        {
            advance(); /* consume ( */
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after print argument.");
            emit_byte(OP_PRINT);
            emit_byte(OP_NIL); /* print returns nil */
            return;
        }
        if (name.length == 3 && memcmp(name.start, "len", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after len argument.");
            emit_byte(OP_LEN); /* len returns the length value */
            return;
        }
        if (name.length == 4 && memcmp(name.start, "push", 4) == 0)
        {
            advance();
            expression(); /* array */
            consume(TOKEN_COMMA, "Expect ',' after array.");
            expression(); /* value */
            consume(TOKEN_RPAREN, "Expect ')' after push arguments.");
            emit_byte(OP_PUSH); /* push returns the array */
            return;
        }
        if (name.length == 3 && memcmp(name.start, "pop", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after pop argument.");
            emit_byte(OP_POP_ARRAY); /* pop returns the popped value */
            return;
        }
        if (name.length == 4 && memcmp(name.start, "time", 4) == 0)
        {
            advance();
            consume(TOKEN_RPAREN, "Expect ')' after time.");
            emit_byte(OP_TIME); /* time returns the timestamp */
            return;
        }
        /* JIT-compiled intrinsics - native speed loops! */
        if (name.length == 14 && memcmp(name.start, "__jit_inc_loop", 14) == 0)
        {
            advance();
            expression(); /* x */
            consume(TOKEN_COMMA, "Expect ',' after x.");
            expression(); /* iterations */
            consume(TOKEN_RPAREN, "Expect ')' after __jit_inc_loop arguments.");
            emit_byte(OP_JIT_INC_LOOP);
            return;
        }
        if (name.length == 16 && memcmp(name.start, "__jit_arith_loop", 16) == 0)
        {
            advance();
            expression(); /* x */
            consume(TOKEN_COMMA, "Expect ',' after x.");
            expression(); /* iterations */
            consume(TOKEN_RPAREN, "Expect ')' after __jit_arith_loop arguments.");
            emit_byte(OP_JIT_ARITH_LOOP);
            return;
        }
        if (name.length == 17 && memcmp(name.start, "__jit_branch_loop", 17) == 0)
        {
            advance();
            expression(); /* x */
            consume(TOKEN_COMMA, "Expect ',' after x.");
            expression(); /* iterations */
            consume(TOKEN_RPAREN, "Expect ')' after __jit_branch_loop arguments.");
            emit_byte(OP_JIT_BRANCH_LOOP);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "input", 5) == 0)
        {
            advance();
            consume(TOKEN_RPAREN, "Expect ')' after input.");
            emit_byte(OP_INPUT);
            return;
        }
        /* Type conversion functions */
        if (name.length == 3 && memcmp(name.start, "int", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after int argument.");
            emit_byte(OP_INT);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "float", 5) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after float argument.");
            emit_byte(OP_FLOAT);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "str", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after str argument.");
            emit_byte(OP_STR);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "type", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after type argument.");
            emit_byte(OP_TYPE);
            return;
        }
        /* Math functions */
        if (name.length == 3 && memcmp(name.start, "abs", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after abs argument.");
            emit_byte(OP_ABS);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "min", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first min argument.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after min arguments.");
            emit_byte(OP_MIN);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "max", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first max argument.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after max arguments.");
            emit_byte(OP_MAX);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "sqrt", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after sqrt argument.");
            emit_byte(OP_SQRT);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "floor", 5) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after floor argument.");
            emit_byte(OP_FLOOR);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "ceil", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after ceil argument.");
            emit_byte(OP_CEIL);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "round", 5) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after round argument.");
            emit_byte(OP_ROUND);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "rand", 4) == 0)
        {
            advance();
            consume(TOKEN_RPAREN, "Expect ')' after rand.");
            emit_byte(OP_RAND);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "pow", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first pow argument.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after pow arguments.");
            emit_byte(OP_POW);
            return;
        }
        /* Bit manipulation intrinsics */
        if (name.length == 8 && memcmp(name.start, "popcount", 8) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after popcount argument.");
            emit_byte(OP_POPCOUNT);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "clz", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after clz argument.");
            emit_byte(OP_CLZ);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "ctz", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after ctz argument.");
            emit_byte(OP_CTZ);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "rotl", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first rotl argument.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after rotl arguments.");
            emit_byte(OP_ROTL);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "rotr", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first rotr argument.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after rotr arguments.");
            emit_byte(OP_ROTR);
            return;
        }
        /* String operations */
        if (name.length == 6 && memcmp(name.start, "substr", 6) == 0)
        {
            advance();
            expression(); /* string */
            consume(TOKEN_COMMA, "Expect ',' after string.");
            expression(); /* start */
            consume(TOKEN_COMMA, "Expect ',' after start.");
            expression(); /* length */
            consume(TOKEN_RPAREN, "Expect ')' after substr arguments.");
            emit_byte(OP_SUBSTR);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "upper", 5) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after upper argument.");
            emit_byte(OP_UPPER);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "lower", 5) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after lower argument.");
            emit_byte(OP_LOWER);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "split", 5) == 0)
        {
            advance();
            expression(); /* string */
            consume(TOKEN_COMMA, "Expect ',' after string.");
            expression(); /* delimiter */
            consume(TOKEN_RPAREN, "Expect ')' after split arguments.");
            emit_byte(OP_SPLIT);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "join", 4) == 0)
        {
            advance();
            expression(); /* array */
            consume(TOKEN_COMMA, "Expect ',' after array.");
            expression(); /* delimiter */
            consume(TOKEN_RPAREN, "Expect ')' after join arguments.");
            emit_byte(OP_JOIN);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "replace", 7) == 0)
        {
            advance();
            expression(); /* string */
            consume(TOKEN_COMMA, "Expect ',' after string.");
            expression(); /* from */
            consume(TOKEN_COMMA, "Expect ',' after from.");
            expression(); /* to */
            consume(TOKEN_RPAREN, "Expect ')' after replace arguments.");
            emit_byte(OP_REPLACE);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "find", 4) == 0)
        {
            advance();
            expression(); /* haystack */
            consume(TOKEN_COMMA, "Expect ',' after string.");
            expression(); /* needle */
            consume(TOKEN_RPAREN, "Expect ')' after find arguments.");
            emit_byte(OP_FIND);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "trim", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after trim argument.");
            emit_byte(OP_TRIM);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "char", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after char argument.");
            emit_byte(OP_CHAR);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "ord", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after ord argument.");
            emit_byte(OP_ORD);
            return;
        }

        /* ============ FILE I/O ============ */
        if (name.length == 9 && memcmp(name.start, "read_file", 9) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after read_file argument.");
            emit_byte(OP_READ_FILE);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "write_file", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after path.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after write_file arguments.");
            emit_byte(OP_WRITE_FILE);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "append_file", 11) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after path.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after append_file arguments.");
            emit_byte(OP_APPEND_FILE);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "file_exists", 11) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after file_exists argument.");
            emit_byte(OP_FILE_EXISTS);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "list_dir", 8) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after list_dir argument.");
            emit_byte(OP_LIST_DIR);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "delete_file", 11) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after delete_file argument.");
            emit_byte(OP_DELETE_FILE);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "mkdir", 5) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after mkdir argument.");
            emit_byte(OP_MKDIR);
            return;
        }

        /* ============ HTTP ============ */
        if (name.length == 8 && memcmp(name.start, "http_get", 8) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after http_get argument.");
            emit_byte(OP_HTTP_GET);
            return;
        }
        if (name.length == 9 && memcmp(name.start, "http_post", 9) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after URL.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after http_post arguments.");
            emit_byte(OP_HTTP_POST);
            return;
        }

        /* ============ JSON ============ */
        if (name.length == 10 && memcmp(name.start, "json_parse", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after json_parse argument.");
            emit_byte(OP_JSON_PARSE);
            return;
        }
        if (name.length == 14 && memcmp(name.start, "json_stringify", 14) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after json_stringify argument.");
            emit_byte(OP_JSON_STRINGIFY);
            return;
        }

        /* ============ PROCESS/SYSTEM ============ */
        if (name.length == 4 && memcmp(name.start, "exec", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after exec argument.");
            emit_byte(OP_EXEC);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "env", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after env argument.");
            emit_byte(OP_ENV);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "set_env", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after name.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after set_env arguments.");
            emit_byte(OP_SET_ENV);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "args", 4) == 0)
        {
            advance();
            consume(TOKEN_RPAREN, "Expect ')' after args.");
            emit_byte(OP_ARGS);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "exit", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after exit argument.");
            emit_byte(OP_EXIT);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "sleep", 5) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after sleep argument.");
            emit_byte(OP_SLEEP);
            return;
        }

        /* ============ DICTIONARY ============ */
        if (name.length == 4 && memcmp(name.start, "dict", 4) == 0)
        {
            advance();
            consume(TOKEN_RPAREN, "Expect ')' after dict.");
            emit_byte(OP_DICT);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "dict_get", 8) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after dict.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after dict_get arguments.");
            emit_byte(OP_DICT_GET);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "dict_set", 8) == 0)
        {
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
        if (name.length == 8 && memcmp(name.start, "dict_has", 8) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after dict.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after dict_has arguments.");
            emit_byte(OP_DICT_HAS);
            return;
        }
        if (name.length == 9 && memcmp(name.start, "dict_keys", 9) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after dict_keys argument.");
            emit_byte(OP_DICT_KEYS);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "dict_values", 11) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after dict_values argument.");
            emit_byte(OP_DICT_VALUES);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "dict_delete", 11) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after dict.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after dict_delete arguments.");
            emit_byte(OP_DICT_DELETE);
            return;
        }

        /* ============ ADVANCED MATH ============ */
        if (name.length == 3 && memcmp(name.start, "sin", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after sin argument.");
            emit_byte(OP_SIN);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "cos", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after cos argument.");
            emit_byte(OP_COS);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "tan", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tan argument.");
            emit_byte(OP_TAN);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "asin", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after asin argument.");
            emit_byte(OP_ASIN);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "acos", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after acos argument.");
            emit_byte(OP_ACOS);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "atan", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after atan argument.");
            emit_byte(OP_ATAN);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "atan2", 5) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after y.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after atan2 arguments.");
            emit_byte(OP_ATAN2);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "log", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after log argument.");
            emit_byte(OP_LOG);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "log10", 5) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after log10 argument.");
            emit_byte(OP_LOG10);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "log2", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after log2 argument.");
            emit_byte(OP_LOG2);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "exp", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after exp argument.");
            emit_byte(OP_EXP);
            return;
        }
        if (name.length == 5 && memcmp(name.start, "hypot", 5) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after x.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after hypot arguments.");
            emit_byte(OP_HYPOT);
            return;
        }

        /* ============ VECTOR OPERATIONS ============ */
        if (name.length == 7 && memcmp(name.start, "vec_add", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first array.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_add arguments.");
            emit_byte(OP_VEC_ADD);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_sub", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first array.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_sub arguments.");
            emit_byte(OP_VEC_SUB);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_mul", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first array.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_mul arguments.");
            emit_byte(OP_VEC_MUL);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_div", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first array.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_div arguments.");
            emit_byte(OP_VEC_DIV);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_dot", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first array.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_dot arguments.");
            emit_byte(OP_VEC_DOT);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_sum", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_sum argument.");
            emit_byte(OP_VEC_SUM);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "vec_prod", 8) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_prod argument.");
            emit_byte(OP_VEC_PROD);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_min", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_min argument.");
            emit_byte(OP_VEC_MIN);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_max", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_max argument.");
            emit_byte(OP_VEC_MAX);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "vec_mean", 8) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_mean argument.");
            emit_byte(OP_VEC_MEAN);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "vec_sort", 8) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_sort argument.");
            emit_byte(OP_VEC_SORT);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "vec_reverse", 11) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_reverse argument.");
            emit_byte(OP_VEC_REVERSE);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "vec_unique", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_unique argument.");
            emit_byte(OP_VEC_UNIQUE);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "vec_zip", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first array.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after vec_zip arguments.");
            emit_byte(OP_VEC_ZIP);
            return;
        }
        if (name.length == 9 && memcmp(name.start, "vec_range", 9) == 0)
        {
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
        if (name.length == 5 && memcmp(name.start, "bytes", 5) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after bytes argument.");
            emit_byte(OP_BYTES);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "encode_utf8", 11) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after encode_utf8 argument.");
            emit_byte(OP_ENCODE_UTF8);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "decode_utf8", 11) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after decode_utf8 argument.");
            emit_byte(OP_DECODE_UTF8);
            return;
        }
        if (name.length == 13 && memcmp(name.start, "encode_base64", 13) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after encode_base64 argument.");
            emit_byte(OP_ENCODE_BASE64);
            return;
        }
        if (name.length == 13 && memcmp(name.start, "decode_base64", 13) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after decode_base64 argument.");
            emit_byte(OP_DECODE_BASE64);
            return;
        }

        /* ============ HASHING ============ */
        if (name.length == 4 && memcmp(name.start, "hash", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after hash argument.");
            emit_byte(OP_HASH);
            return;
        }
        if (name.length == 6 && memcmp(name.start, "sha256", 6) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after sha256 argument.");
            emit_byte(OP_HASH_SHA256);
            return;
        }
        if (name.length == 3 && memcmp(name.start, "md5", 3) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after md5 argument.");
            emit_byte(OP_HASH_MD5);
            return;
        }

        /* ============ REGEX OPERATIONS ============ */
        if (name.length == 11 && memcmp(name.start, "regex_match", 11) == 0)
        {
            advance();
            expression(); /* text */
            consume(TOKEN_COMMA, "Expect ',' after text argument.");
            expression(); /* pattern */
            consume(TOKEN_RPAREN, "Expect ')' after regex_match arguments.");
            emit_byte(OP_REGEX_MATCH);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "regex_find", 10) == 0)
        {
            advance();
            expression(); /* text */
            consume(TOKEN_COMMA, "Expect ',' after text argument.");
            expression(); /* pattern */
            consume(TOKEN_RPAREN, "Expect ')' after regex_find arguments.");
            emit_byte(OP_REGEX_FIND);
            return;
        }
        if (name.length == 13 && memcmp(name.start, "regex_replace", 13) == 0)
        {
            advance();
            expression(); /* text */
            consume(TOKEN_COMMA, "Expect ',' after text argument.");
            expression(); /* pattern */
            consume(TOKEN_COMMA, "Expect ',' after pattern argument.");
            expression(); /* replacement */
            consume(TOKEN_RPAREN, "Expect ')' after regex_replace arguments.");
            emit_byte(OP_REGEX_REPLACE);
            return;
        }

        /* ============ TENSOR OPERATIONS ============ */
        if (name.length == 12 && memcmp(name.start, "tensor_zeros", 12) == 0)
        {
            advance();
            expression(); /* shape array */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_zeros argument.");
            emit_byte(OP_TENSOR_ZEROS);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "tensor_ones", 11) == 0)
        {
            advance();
            expression(); /* shape array */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_ones argument.");
            emit_byte(OP_TENSOR_ONES);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "tensor_rand", 11) == 0)
        {
            advance();
            expression(); /* shape array */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_rand argument.");
            emit_byte(OP_TENSOR_RAND);
            return;
        }
        if (name.length == 12 && memcmp(name.start, "tensor_randn", 12) == 0)
        {
            advance();
            expression(); /* shape array */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_randn argument.");
            emit_byte(OP_TENSOR_RANDN);
            return;
        }
        if (name.length == 13 && memcmp(name.start, "tensor_arange", 13) == 0)
        {
            advance();
            expression(); /* start */
            consume(TOKEN_COMMA, "Expect ',' after start.");
            expression(); /* stop */
            consume(TOKEN_COMMA, "Expect ',' after stop.");
            expression(); /* step */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_arange arguments.");
            emit_byte(OP_TENSOR_ARANGE);
            return;
        }
        if (name.length == 6 && memcmp(name.start, "tensor", 6) == 0)
        {
            advance();
            expression(); /* array data */
            consume(TOKEN_RPAREN, "Expect ')' after tensor argument.");
            emit_byte(OP_TENSOR);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_add", 10) == 0)
        {
            advance();
            expression(); /* tensor a */
            consume(TOKEN_COMMA, "Expect ',' after first tensor.");
            expression(); /* tensor b */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_add arguments.");
            emit_byte(OP_TENSOR_ADD);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_sub", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first tensor.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_sub arguments.");
            emit_byte(OP_TENSOR_SUB);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_mul", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first tensor.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_mul arguments.");
            emit_byte(OP_TENSOR_MUL);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_div", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first tensor.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_div arguments.");
            emit_byte(OP_TENSOR_DIV);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_sum", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_sum argument.");
            emit_byte(OP_TENSOR_SUM);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "tensor_mean", 11) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_mean argument.");
            emit_byte(OP_TENSOR_MEAN);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_min", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_min argument.");
            emit_byte(OP_TENSOR_MIN);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_max", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_max argument.");
            emit_byte(OP_TENSOR_MAX);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "tensor_sqrt", 11) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_sqrt argument.");
            emit_byte(OP_TENSOR_SQRT);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_exp", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_exp argument.");
            emit_byte(OP_TENSOR_EXP);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_log", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_log argument.");
            emit_byte(OP_TENSOR_LOG);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_abs", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_abs argument.");
            emit_byte(OP_TENSOR_ABS);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_neg", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_neg argument.");
            emit_byte(OP_TENSOR_NEG);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "tensor_dot", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first tensor.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_dot arguments.");
            emit_byte(OP_TENSOR_DOT);
            return;
        }
        if (name.length == 13 && memcmp(name.start, "tensor_matmul", 13) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first tensor.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tensor_matmul arguments.");
            emit_byte(OP_TENSOR_MATMUL);
            return;
        }
        if (name.length == 14 && memcmp(name.start, "tensor_reshape", 14) == 0)
        {
            advance();
            expression(); /* tensor */
            consume(TOKEN_COMMA, "Expect ',' after tensor.");
            expression(); /* new shape */
            consume(TOKEN_RPAREN, "Expect ')' after tensor_reshape arguments.");
            emit_byte(OP_TENSOR_RESHAPE);
            return;
        }

        /* ============ MATRIX OPERATIONS ============ */
        if (name.length == 6 && memcmp(name.start, "matrix", 6) == 0)
        {
            advance();
            expression(); /* 2D array */
            consume(TOKEN_RPAREN, "Expect ')' after matrix argument.");
            emit_byte(OP_MATRIX);
            return;
        }
        if (name.length == 12 && memcmp(name.start, "matrix_zeros", 12) == 0)
        {
            advance();
            expression(); /* rows */
            consume(TOKEN_COMMA, "Expect ',' after rows.");
            expression(); /* cols */
            consume(TOKEN_RPAREN, "Expect ')' after matrix_zeros arguments.");
            emit_byte(OP_MATRIX_ZEROS);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "matrix_ones", 11) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after rows.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_ones arguments.");
            emit_byte(OP_MATRIX_ONES);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "matrix_eye", 10) == 0)
        {
            advance();
            expression(); /* n */
            consume(TOKEN_RPAREN, "Expect ')' after matrix_eye argument.");
            emit_byte(OP_MATRIX_EYE);
            return;
        }
        if (name.length == 11 && memcmp(name.start, "matrix_rand", 11) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after rows.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_rand arguments.");
            emit_byte(OP_MATRIX_RAND);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "matrix_add", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first matrix.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_add arguments.");
            emit_byte(OP_MATRIX_ADD);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "matrix_sub", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first matrix.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_sub arguments.");
            emit_byte(OP_MATRIX_SUB);
            return;
        }
        if (name.length == 13 && memcmp(name.start, "matrix_matmul", 13) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after first matrix.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_matmul arguments.");
            emit_byte(OP_MATRIX_MATMUL);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "matrix_t", 8) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_t argument.");
            emit_byte(OP_MATRIX_T);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "matrix_inv", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_inv argument.");
            emit_byte(OP_MATRIX_INV);
            return;
        }
        if (name.length == 10 && memcmp(name.start, "matrix_det", 10) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_det argument.");
            emit_byte(OP_MATRIX_DET);
            return;
        }
        if (name.length == 12 && memcmp(name.start, "matrix_trace", 12) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after matrix_trace argument.");
            emit_byte(OP_MATRIX_TRACE);
            return;
        }
        if (name.length == 12 && memcmp(name.start, "matrix_solve", 12) == 0)
        {
            advance();
            expression(); /* A */
            consume(TOKEN_COMMA, "Expect ',' after A.");
            expression(); /* b */
            consume(TOKEN_RPAREN, "Expect ')' after matrix_solve arguments.");
            emit_byte(OP_MATRIX_SOLVE);
            return;
        }

        /* ============ NEURAL NETWORK ============ */
        if (name.length == 4 && memcmp(name.start, "relu", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after relu argument.");
            emit_byte(OP_NN_RELU);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "sigmoid", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after sigmoid argument.");
            emit_byte(OP_NN_SIGMOID);
            return;
        }
        if (name.length == 4 && memcmp(name.start, "tanh", 4) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after tanh argument.");
            emit_byte(OP_NN_TANH);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "softmax", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after softmax argument.");
            emit_byte(OP_NN_SOFTMAX);
            return;
        }
        if (name.length == 8 && memcmp(name.start, "mse_loss", 8) == 0)
        {
            advance();
            expression(); /* predictions */
            consume(TOKEN_COMMA, "Expect ',' after predictions.");
            expression(); /* targets */
            consume(TOKEN_RPAREN, "Expect ')' after mse_loss arguments.");
            emit_byte(OP_NN_MSE_LOSS);
            return;
        }
        if (name.length == 7 && memcmp(name.start, "ce_loss", 7) == 0)
        {
            advance();
            expression();
            consume(TOKEN_COMMA, "Expect ',' after predictions.");
            expression();
            consume(TOKEN_RPAREN, "Expect ')' after ce_loss arguments.");
            emit_byte(OP_NN_CE_LOSS);
            return;
        }

        /* ============ AUTOGRAD ============ */
        if (name.length == 9 && memcmp(name.start, "grad_tape", 9) == 0)
        {
            advance();
            consume(TOKEN_RPAREN, "Expect ')' after grad_tape.");
            emit_byte(OP_GRAD_TAPE);
            return;
        }
    }

    named_variable(name, can_assign);
}

static void literal(bool can_assign)
{
    (void)can_assign;
    switch (parser.previous.type)
    {
    case TOKEN_FALSE:
        emit_byte(OP_FALSE);
        /* Track for constant folding */
        second_last_emit = last_emit;
        last_emit.is_constant = true;
        last_emit.value = VAL_FALSE;
        last_emit.bytecode_pos = current_chunk()->count - 1;
        break;
    case TOKEN_TRUE:
        emit_byte(OP_TRUE);
        second_last_emit = last_emit;
        last_emit.is_constant = true;
        last_emit.value = VAL_TRUE;
        last_emit.bytecode_pos = current_chunk()->count - 1;
        break;
    case TOKEN_NIL:
        emit_byte(OP_NIL);
        second_last_emit = last_emit;
        last_emit.is_constant = true;
        last_emit.value = VAL_NIL;
        last_emit.bytecode_pos = current_chunk()->count - 1;
        break;
    default:
        return;
    }
}

static void unary(bool can_assign)
{
    (void)can_assign;
    TokenType op_type = parser.previous.type;

    parse_precedence(PREC_UNARY);

    /* ============ Constant Folding for Unary ============ */
    if (last_emit.is_constant)
    {
        Value v = last_emit.value;

        if (op_type == TOKEN_MINUS && (IS_NUM(v) || IS_INT(v)))
        {
            double n = IS_INT(v) ? (double)as_int(v) : as_num(v);
            double result = -n;

            /* Remove the constant instruction */
            current_chunk()->count = last_emit.bytecode_pos;

            /* Emit negated value - use specialized opcodes for common values */
            if (result == (double)(int32_t)result && result >= INT32_MIN && result <= INT32_MAX)
            {
                int32_t int_result = (int32_t)result;
                if (int_result == -1)
                {
                    emit_byte(OP_CONST_NEG1);
                    second_last_emit = last_emit;
                    last_emit.is_constant = true;
                    last_emit.value = val_int(-1);
                    last_emit.bytecode_pos = current_chunk()->count - 1;
                    last_emit.const_idx = 0;
                    last_emit.type = CTYPE_INT;
                }
                else if (int_result == 0)
                {
                    emit_byte(OP_CONST_0);
                    second_last_emit = last_emit;
                    last_emit.is_constant = true;
                    last_emit.value = val_int(0);
                    last_emit.bytecode_pos = current_chunk()->count - 1;
                    last_emit.const_idx = 0;
                    last_emit.type = CTYPE_INT;
                }
                else
                {
                    emit_constant(val_int(int_result));
                }
            }
            else
            {
                emit_constant(val_num(result));
            }
            return;
        }

        if (op_type == TOKEN_NOT)
        {
            bool result = !is_truthy(v);

            /* Remove the constant instruction */
            current_chunk()->count = last_emit.bytecode_pos;

            /* Emit boolean result */
            emit_byte(result ? OP_TRUE : OP_FALSE);
            reset_last_emit();
            return;
        }
    }

    /* Normal emission */
    reset_last_emit();

    switch (op_type)
    {
    case TOKEN_MINUS:
        emit_byte(OP_NEG);
        break;
    case TOKEN_NOT:
        emit_byte(OP_NOT);
        break;
    default:
        return;
    }
}

static void binary(bool can_assign)
{
    (void)can_assign;
    TokenType op_type = parser.previous.type;
    ParseRule *rule = get_rule(op_type);

    /* Save the state of the first operand before parsing the second */
    LastEmit first_operand = last_emit;
    size_t first_bytecode_pos = first_operand.bytecode_pos;

    parse_precedence((Precedence)(rule->precedence + 1));

    /* After parsing second operand, check if both operands are constants */
    LastEmit second_operand = last_emit;

    Chunk *chunk = current_chunk();

    /* ============ Constant Folding ============ */
    /* Fold CONST a CONST b OP -> CONST (a op b) at compile time */
    if (first_operand.is_constant && second_operand.is_constant)
    {
        Value v1 = first_operand.value;
        Value v2 = second_operand.value;

        /* Only fold numeric constants */
        bool is_num1 = IS_NUM(v1) || IS_INT(v1);
        bool is_num2 = IS_NUM(v2) || IS_INT(v2);

        if (is_num1 && is_num2)
        {
            double n1 = IS_INT(v1) ? (double)as_int(v1) : as_num(v1);
            double n2 = IS_INT(v2) ? (double)as_int(v2) : as_num(v2);
            double result = 0;
            bool can_fold = true;
            bool is_bool = false;
            bool bool_result = false;

            switch (op_type)
            {
            case TOKEN_PLUS:
                result = n1 + n2;
                break;
            case TOKEN_MINUS:
                result = n1 - n2;
                break;
            case TOKEN_STAR:
                result = n1 * n2;
                break;
            case TOKEN_SLASH:
                if (n2 != 0)
                    result = n1 / n2;
                else
                    can_fold = false;
                break;
            case TOKEN_PERCENT:
                if (n2 != 0)
                    result = (double)((int64_t)n1 % (int64_t)n2);
                else
                    can_fold = false;
                break;
            case TOKEN_LT:
                is_bool = true;
                bool_result = n1 < n2;
                break;
            case TOKEN_GT:
                is_bool = true;
                bool_result = n1 > n2;
                break;
            case TOKEN_LTE:
                is_bool = true;
                bool_result = n1 <= n2;
                break;
            case TOKEN_GTE:
                is_bool = true;
                bool_result = n1 >= n2;
                break;
            case TOKEN_EQ:
                is_bool = true;
                bool_result = n1 == n2;
                break;
            case TOKEN_NEQ:
                is_bool = true;
                bool_result = n1 != n2;
                break;
            case TOKEN_BAND:
            {
                int64_t i1 = (int64_t)n1, i2 = (int64_t)n2;
                result = (double)(i1 & i2);
                break;
            }
            case TOKEN_BOR:
            {
                int64_t i1 = (int64_t)n1, i2 = (int64_t)n2;
                result = (double)(i1 | i2);
                break;
            }
            case TOKEN_BXOR:
            {
                int64_t i1 = (int64_t)n1, i2 = (int64_t)n2;
                result = (double)(i1 ^ i2);
                break;
            }
            case TOKEN_SHL:
            {
                int64_t i1 = (int64_t)n1, i2 = (int64_t)n2;
                result = (double)(i1 << i2);
                break;
            }
            case TOKEN_SHR:
            {
                int64_t i1 = (int64_t)n1, i2 = (int64_t)n2;
                result = (double)(i1 >> i2);
                break;
            }
            default:
                can_fold = false;
                break;
            }

            if (can_fold && first_bytecode_pos <= chunk->count)
            {
                /* Rewind to before both constants were emitted */
                chunk->count = first_bytecode_pos;

                /* Emit the folded result */
                if (is_bool)
                {
                    emit_byte(bool_result ? OP_TRUE : OP_FALSE);
                    reset_last_emit();
                }
                else
                {
                    /* Try to keep as integer if possible */
                    if (result == (double)(int32_t)result && result >= INT32_MIN && result <= INT32_MAX)
                    {
                        emit_constant(val_int((int32_t)result));
                    }
                    else
                    {
                        emit_constant(val_num(result));
                    }
                }
                return;
            }
        }
    }

    /* ============ Identity/Zero Elimination ============ */
    /* Remove no-op operations at compile time:
     *   x + 0 -> x,  x - 0 -> x,  x * 1 -> x,  x / 1 -> x
     *   x * 0 -> 0,  0 / x -> 0
     */
    if (second_operand.is_constant)
    {
        Value v2 = second_operand.value;
        if (IS_INT(v2) || IS_NUM(v2))
        {
            double n2 = IS_INT(v2) ? (double)as_int(v2) : as_num(v2);

            /* x + 0 -> x: remove the constant and skip the add */
            if ((op_type == TOKEN_PLUS || op_type == TOKEN_MINUS) && n2 == 0.0)
            {
                chunk->count = second_operand.bytecode_pos;
                return; /* Result is already on stack */
            }

            /* x * 1 -> x, x / 1 -> x: remove the constant and skip the op */
            if ((op_type == TOKEN_STAR || op_type == TOKEN_SLASH) && n2 == 1.0)
            {
                chunk->count = second_operand.bytecode_pos;
                return;
            }

            /* x * 0 -> 0: remove both operands and push 0 */
            if (op_type == TOKEN_STAR && n2 == 0.0)
            {
                chunk->count = first_bytecode_pos;
                emit_constant(val_int(0));
                return;
            }
        }
    }

    /* Check first operand for 0 * x -> 0 */
    if (first_operand.is_constant && op_type == TOKEN_STAR)
    {
        Value v1 = first_operand.value;
        if (IS_INT(v1) || IS_NUM(v1))
        {
            double n1 = IS_INT(v1) ? (double)as_int(v1) : as_num(v1);
            if (n1 == 0.0)
            {
                chunk->count = first_bytecode_pos;
                emit_constant(val_int(0));
                return;
            }
        }
    }

    /* ============ Type Specialization ============ */
    /* Emit integer-specialized opcodes when both operands are known integers */
    bool both_int = (first_operand.type == CTYPE_INT && second_operand.type == CTYPE_INT);

    if (both_int)
    {
        switch (op_type)
        {
        case TOKEN_PLUS:
            emit_byte(OP_ADD_II);
            track_int_result();
            return;
        case TOKEN_MINUS:
            emit_byte(OP_SUB_II);
            track_int_result();
            return;
        case TOKEN_STAR:
            /* STRENGTH REDUCTION: x * 2^n -> x << n for power-of-2 constants */
            if (second_operand.is_constant && IS_INT(second_operand.value))
            {
                int32_t multiplier = as_int(second_operand.value);
                /* Check if multiplier is a positive power of 2 */
                if (multiplier > 0 && (multiplier & (multiplier - 1)) == 0)
                {
                    /* Find the shift amount */
                    int shift = 0;
                    while ((1 << shift) < multiplier)
                        shift++;
                    /* Rewind to remove the constant, emit shift */
                    chunk->count = second_operand.bytecode_pos;
                    emit_constant(val_int(shift));
                    emit_byte(OP_SHL);
                    track_int_result();
                    return;
                }
            }
            emit_byte(OP_MUL_II);
            track_int_result();
            return;
        case TOKEN_SLASH:
            /* STRENGTH REDUCTION: x / 2^n -> x >> n for power-of-2 constants */
            if (second_operand.is_constant && IS_INT(second_operand.value))
            {
                int32_t divisor = as_int(second_operand.value);
                /* Check if divisor is a positive power of 2 */
                if (divisor > 0 && (divisor & (divisor - 1)) == 0)
                {
                    /* Find the shift amount */
                    int shift = 0;
                    while ((1 << shift) < divisor)
                        shift++;
                    /* Rewind to remove the constant, emit shift */
                    chunk->count = second_operand.bytecode_pos;
                    emit_constant(val_int(shift));
                    emit_byte(OP_SHR);
                    track_int_result();
                    return;
                }
            }
            emit_byte(OP_DIV_II);
            track_int_result();
            return;
        case TOKEN_PERCENT:
            /* STRENGTH REDUCTION: x % 2^n -> x & (2^n - 1) for positive power-of-2 constants */
            if (second_operand.is_constant && IS_INT(second_operand.value))
            {
                int32_t divisor = as_int(second_operand.value);
                /* Check if divisor is a positive power of 2 */
                if (divisor > 0 && (divisor & (divisor - 1)) == 0)
                {
                    /* Rewind to remove the constant, re-emit (divisor - 1), then emit BAND */
                    chunk->count = second_operand.bytecode_pos;
                    emit_constant(val_int(divisor - 1));
                    emit_byte(OP_BAND);
                    track_int_result();
                    return;
                }
            }
            emit_byte(OP_MOD_II);
            track_int_result();
            return;
        case TOKEN_LT:
            emit_byte(OP_LT_II);
            track_bool_result();
            return;
        case TOKEN_GT:
            emit_byte(OP_GT_II);
            track_bool_result();
            return;
        case TOKEN_LTE:
            emit_byte(OP_LTE_II);
            track_bool_result();
            return;
        case TOKEN_GTE:
            emit_byte(OP_GTE_II);
            track_bool_result();
            return;
        case TOKEN_EQ:
            emit_byte(OP_EQ_II);
            track_bool_result();
            return;
        case TOKEN_NEQ:
            emit_byte(OP_NEQ_II);
            track_bool_result();
            return;
        default:
            break; /* Fall through to generic opcodes */
        }
    }

    /* ============ Normal code emission (no folding or specialization) ============ */
    reset_last_emit(); /* After binary op, result is not a constant */

    switch (op_type)
    {
    case TOKEN_PLUS:
        /* INLINE FUSION: CONST_1 ADD -> ADD_1 */
        if (chunk->count >= 1 && chunk->code[chunk->count - 1] == OP_CONST_1)
        {
            chunk->code[chunk->count - 1] = OP_ADD_1;
        }
        else
        {
            emit_byte(OP_ADD);
        }
        break;
    case TOKEN_MINUS:
        /* INLINE FUSION: CONST_1 SUB -> SUB_1 */
        if (chunk->count >= 1 && chunk->code[chunk->count - 1] == OP_CONST_1)
        {
            chunk->code[chunk->count - 1] = OP_SUB_1;
        }
        else
        {
            emit_byte(OP_SUB);
        }
        break;
    case TOKEN_STAR:
        emit_byte(OP_MUL);
        break;
    case TOKEN_SLASH:
        emit_byte(OP_DIV);
        break;
    case TOKEN_PERCENT:
        emit_byte(OP_MOD);
        break;
    case TOKEN_EQ:
        emit_byte(OP_EQ);
        break;
    case TOKEN_NEQ:
        emit_byte(OP_NEQ);
        break;
    case TOKEN_LT:
        emit_byte(OP_LT);
        break;
    case TOKEN_GT:
        emit_byte(OP_GT);
        break;
    case TOKEN_LTE:
        emit_byte(OP_LTE);
        break;
    case TOKEN_GTE:
        emit_byte(OP_GTE);
        break;
    case TOKEN_BAND:
        emit_byte(OP_BAND);
        break;
    case TOKEN_BOR:
        emit_byte(OP_BOR);
        break;
    case TOKEN_BXOR:
        emit_byte(OP_BXOR);
        break;
    case TOKEN_SHL:
        emit_byte(OP_SHL);
        break;
    case TOKEN_SHR:
        emit_byte(OP_SHR);
        break;
    default:
        return;
    }
}

static void and_(bool can_assign)
{
    (void)can_assign;
    /* IMPORTANT: Use emit_jump_no_fuse to prevent fusion with preceding comparison.
     * For short-circuit AND, we need the comparison result to stay on the stack
     * so it can become the result of the AND expression when left side is falsy.
     * Fusion would consume the operands and not push a result. */
    int end_jump = emit_jump_no_fuse(OP_JMP_FALSE);
    emit_byte(OP_POP);
    parse_precedence(PREC_AND);
    patch_jump(end_jump);
    /* Prevent outer if/while from fusing with internal comparison */
    inhibit_jump_fusion = true;
}

static void or_(bool can_assign)
{
    (void)can_assign;
    /* IMPORTANT: Use emit_jump_no_fuse to prevent fusion with preceding comparison.
     * For short-circuit OR, we need the comparison result to stay on the stack
     * so it can become the result of the OR expression when left side is truthy.
     * Fusion would consume the operands and not push a result. */
    int else_jump = emit_jump_no_fuse(OP_JMP_FALSE);
    int end_jump = emit_jump(OP_JMP);

    patch_jump(else_jump);
    emit_byte(OP_POP);

    parse_precedence(PREC_OR);
    patch_jump(end_jump);
    /* Prevent outer if/while from fusing with internal comparison */
    inhibit_jump_fusion = true;
}

static uint8_t argument_list(void)
{
    uint8_t arg_count = 0;
    if (!check(TOKEN_RPAREN))
    {
        do
        {
            expression();
            if (arg_count == 255)
            {
                error("Can't have more than 255 arguments.");
            }
            arg_count++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RPAREN, "Expect ')' after arguments.");
    return arg_count;
}

static void call(bool can_assign)
{
    (void)can_assign;
    uint8_t arg_count = argument_list();
    emit_bytes(OP_CALL, arg_count);
    /* Track for tail call optimization */
    last_was_call = true;
    last_call_arg_count = arg_count;
}

static void index_(bool can_assign)
{
    expression();
    consume(TOKEN_RBRACKET, "Expect ']' after index.");

    if (can_assign && match(TOKEN_ASSIGN))
    {
        expression();
        emit_byte(OP_INDEX_SET);
    }
    else
    {
        emit_byte(OP_INDEX);
    }
}

static void array_literal(bool can_assign)
{
    (void)can_assign;
    uint8_t count = 0;

    skip_newlines();
    if (!check(TOKEN_RBRACKET))
    {
        do
        {
            skip_newlines();
            if (check(TOKEN_RBRACKET))
                break; /* Trailing comma */
            expression();
            count++;
            skip_newlines();
        } while (match(TOKEN_COMMA));
    }
    skip_newlines();

    consume(TOKEN_RBRACKET, "Expect ']' after array elements.");
    emit_bytes(OP_ARRAY, count);
}

static void range_expr(bool can_assign)
{
    (void)can_assign;
    parse_precedence(PREC_TERM); /* Parse end value */
    emit_byte(OP_RANGE);
}

/* Parse property access: obj.property or obj.method() */
static void dot(bool can_assign)
{
    consume(TOKEN_IDENT, "Expect property name after '.'.");
    uint8_t name = identifier_constant(&parser.previous);

    if (can_assign && match(TOKEN_ASSIGN))
    {
        expression();
        emit_bytes(OP_SET_FIELD, name);
    }
    else if (match(TOKEN_LPAREN))
    {
        /* Method call: obj.method(args) */
        uint8_t arg_count = argument_list();
        emit_bytes(OP_INVOKE, name);
        emit_byte(arg_count);
    }
    else
    {
        emit_bytes(OP_GET_FIELD, name);
    }
}

/* Parse 'self' keyword in methods */
static void self_(bool can_assign)
{
    (void)can_assign;
    /* 'self' is always slot 0 in method calls */
    emit_bytes(OP_GET_LOCAL, 0);
}

/* Parse 'super' keyword for calling parent class methods */
static void super_(bool can_assign)
{
    (void)can_assign;

    /* Syntax: super.method() or super.method */
    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENT, "Expect superclass method name.");
    uint8_t method_name = identifier_constant(&parser.previous);

    /* Push 'self' (slot 0) */
    emit_bytes(OP_GET_LOCAL, 0);

    /* Check if this is a direct invoke: super.method(...) */
    if (match(TOKEN_LPAREN))
    {
        /* Parse arguments */
        uint8_t arg_count = 0;
        if (!check(TOKEN_RPAREN))
        {
            do
            {
                expression();
                if (arg_count == 255)
                {
                    error("Cannot have more than 255 arguments.");
                }
                arg_count++;
            } while (match(TOKEN_COMMA));
        }
        consume(TOKEN_RPAREN, "Expect ')' after arguments.");

        /* Emit fused super invoke - more efficient than GET_SUPER + CALL */
        emit_bytes(OP_SUPER_INVOKE, method_name);
        emit_byte(arg_count);
    }
    else
    {
        /* Just getting the bound method: super.method */
        emit_bytes(OP_GET_SUPER, method_name);
    }
}

/* Parse 'yield' expression for generators */
static void yield_expr(bool can_assign)
{
    (void)can_assign;

    /* Check we're in a generator function */
    if (current->type != TYPE_GENERATOR)
    {
        error("Cannot use 'yield' outside of a generator function.");
        return;
    }

    /* Optional value: yield expr or just yield (yields nil) */
    if (!check(TOKEN_NEWLINE) && !check(TOKEN_END) && !check(TOKEN_EOF))
    {
        expression();
    }
    else
    {
        emit_byte(OP_NIL);
    }

    emit_byte(OP_YIELD);
}

/* Parse 'await' expression for async functions */
static void await_expr(bool can_assign)
{
    (void)can_assign;

    /* Check we're in an async function */
    if (current->type != TYPE_ASYNC)
    {
        error("Cannot use 'await' outside of an async function.");
        return;
    }

    /* Await requires an expression (the promise) */
    parse_precedence(PREC_UNARY);
    emit_byte(OP_AWAIT);
}

ParseRule rules[] = {
    [TOKEN_LPAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RPAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LBRACKET] = {array_literal, index_, PREC_CALL},
    [TOKEN_RBRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_LBRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RBRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_COLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_PERCENT] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BAND] = {NULL, binary, PREC_BAND},
    [TOKEN_BOR] = {NULL, binary, PREC_BOR},
    [TOKEN_BXOR] = {NULL, binary, PREC_BXOR},
    [TOKEN_SHL] = {NULL, binary, PREC_SHIFT},
    [TOKEN_SHR] = {NULL, binary, PREC_SHIFT},
    [TOKEN_NEQ] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_ASSIGN] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQ] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GT] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GTE] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LT] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LTE] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENT] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_INT] = {number, NULL, PREC_NONE},
    [TOKEN_FLOAT] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_OR] = {NULL, or_, PREC_OR},
    [TOKEN_NOT] = {unary, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_RANGE] = {NULL, range_expr, PREC_COMPARISON},
    [TOKEN_LET] = {NULL, NULL, PREC_NONE},
    [TOKEN_CONST] = {NULL, NULL, PREC_NONE},
    [TOKEN_FN] = {lambda, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_THEN] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELIF] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_END] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_IN] = {NULL, NULL, PREC_NONE},
    [TOKEN_DO] = {NULL, NULL, PREC_NONE},
    [TOKEN_ARROW] = {NULL, NULL, PREC_NONE},
    [TOKEN_NEWLINE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ENUM] = {NULL, NULL, PREC_NONE},
    [TOKEN_SELF] = {self_, NULL, PREC_NONE},
    [TOKEN_SUPER] = {super_, NULL, PREC_NONE},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_EXTENDS] = {NULL, NULL, PREC_NONE},
    [TOKEN_TRY] = {NULL, NULL, PREC_NONE},
    [TOKEN_CATCH] = {NULL, NULL, PREC_NONE},
    [TOKEN_FINALLY] = {NULL, NULL, PREC_NONE},
    [TOKEN_THROW] = {NULL, NULL, PREC_NONE},
    [TOKEN_MATCH] = {NULL, NULL, PREC_NONE},
    [TOKEN_CASE] = {NULL, NULL, PREC_NONE},
    [TOKEN_YIELD] = {yield_expr, NULL, PREC_NONE},
    [TOKEN_ASYNC] = {NULL, NULL, PREC_NONE},
    [TOKEN_AWAIT] = {await_expr, NULL, PREC_NONE},
    [TOKEN_STATIC] = {NULL, NULL, PREC_NONE},
    [TOKEN_FROM] = {NULL, NULL, PREC_NONE},
    [TOKEN_AS] = {NULL, NULL, PREC_NONE},
    [TOKEN_MODULE] = {NULL, NULL, PREC_NONE},
    [TOKEN_EXPORT] = {NULL, NULL, PREC_NONE},
    [TOKEN_IMPORT] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static ParseRule *get_rule(TokenType type)
{
    return &rules[type];
}

static void parse_precedence(Precedence precedence)
{
    advance();
    ParseFn prefix_rule = get_rule(parser.previous.type)->prefix;

    if (prefix_rule == NULL)
    {
        error("Expect expression.");
        return;
    }

    bool can_assign = precedence <= PREC_ASSIGNMENT;
    prefix_rule(can_assign);

    while (precedence <= get_rule(parser.current.type)->precedence)
    {
        advance();
        ParseFn infix_rule = get_rule(parser.previous.type)->infix;
        infix_rule(can_assign);
    }

    if (can_assign && match(TOKEN_ASSIGN))
    {
        error("Invalid assignment target.");
    }
}

static void expression(void)
{
    parse_precedence(PREC_ASSIGNMENT);
}

/* ============ Statements ============ */

static void block(void)
{
    skip_newlines();
    while (!check(TOKEN_END) && !check(TOKEN_ELSE) && !check(TOKEN_ELIF) &&
           !check(TOKEN_CASE) && !check(TOKEN_CATCH) && !check(TOKEN_FINALLY) &&
           !check(TOKEN_EOF))
    {
        declaration();
        skip_newlines();
    }
}

static void if_statement(void)
{
    expression();
    consume(TOKEN_THEN, "Expect 'then' after condition.");
    skip_newlines();

    int then_jump = emit_jump(OP_JMP_FALSE);
    emit_pop_for_jump(then_jump); /* Skip POP if fused comparison+jump */

    block();

    /* Track all jumps that need to go to the end */
    int end_jumps[256];
    int end_jump_count = 0;

    end_jumps[end_jump_count++] = emit_jump(OP_JMP);
    patch_jump(then_jump);
    emit_pop_for_jump(then_jump); /* Skip POP if fused comparison+jump */

    while (match(TOKEN_ELIF))
    {
        expression();
        consume(TOKEN_THEN, "Expect 'then' after elif condition.");
        skip_newlines();

        int elif_jump = emit_jump(OP_JMP_FALSE);
        emit_pop_for_jump(elif_jump); /* Skip POP if fused comparison+jump */

        block();

        end_jumps[end_jump_count++] = emit_jump(OP_JMP);
        patch_jump(elif_jump);
        emit_pop_for_jump(elif_jump); /* Skip POP if fused comparison+jump */
    }

    if (match(TOKEN_ELSE))
    {
        skip_newlines();
        block();
    }

    /* Patch all end jumps to here */
    for (int i = 0; i < end_jump_count; i++)
    {
        patch_jump(end_jumps[i]);
    }

    consume(TOKEN_END, "Expect 'end' after if statement.");
}

static void while_statement(void)
{
    int loop_start = current_chunk()->count;

    expression();
    consume(TOKEN_DO, "Expect 'do' after condition.");
    skip_newlines();

    int exit_jump = emit_jump(OP_JMP_FALSE);
    emit_pop_for_jump(exit_jump); /* Skip POP if fused */

    block();

    emit_loop(loop_start);
    patch_jump(exit_jump);
    emit_pop_for_jump(exit_jump); /* Skip POP if fused */

    consume(TOKEN_END, "Expect 'end' after while loop.");
}

static void for_statement(void)
{
    begin_scope();

    consume(TOKEN_IDENT, "Expect variable name.");
    Token var_name = parser.previous;

    consume(TOKEN_IN, "Expect 'in' after variable.");

    /* Parse the start expression (before ..) */
    parse_precedence(PREC_SHIFT); /* Parse up to but not including .. */

    /* Check if this is a range expression for the fast path */
    if (match(TOKEN_RANGE))
    {
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
        emit_byte(OP_NIL); /* Placeholder, will be set by FOR_COUNT */

        int loop_start = current_chunk()->count;

        /* Ultra-fast FOR_COUNT instruction */
        /* Format: OP_FOR_COUNT, start_slot, end_slot, var_slot, offset[2] */
        emit_byte(OP_FOR_COUNT);
        emit_byte(start_slot); /* Counter (starts at start, incremented each iter) */
        emit_byte(end_slot);   /* End value (constant) */
        emit_byte(var_slot);   /* Loop variable (set to counter each iter) */
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
    }
    else
    {
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
        emit_byte(OP_CONST_0); /* Start at index 0 */
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

static void return_statement(void)
{
    if (current->type == TYPE_SCRIPT)
    {
        error("Can't return from top-level code.");
    }

    if (check(TOKEN_NEWLINE) || check(TOKEN_END) || check(TOKEN_EOF))
    {
        emit_return();
    }
    else
    {
        /* ESCAPE ANALYSIS: Check if we're returning a local variable */
        /* If the expression starts with an identifier, check if it's a local */
        if (check(TOKEN_IDENT))
        {
            /* Peek at the identifier to check for escape */
            Token ident = parser.current;
            for (int i = current->local_count - 1; i >= 0; i--)
            {
                Local *local = &current->locals[i];
                if (local->name.length == ident.length &&
                    memcmp(local->name.start, ident.start, ident.length) == 0)
                {
                    /* Mark this local as escaping via return */
                    mark_local_escapes(i, ESCAPE_VIA_RETURN);
                    break;
                }
            }
        }

        expression();

        /* TAIL CALL OPTIMIZATION: If the expression was just a function call,
         * convert OP_CALL to OP_TAIL_CALL and skip the OP_RETURN.
         * This allows recursive functions to reuse stack frames. */
        if (last_was_call && current->type == TYPE_FUNCTION)
        {
            Chunk *chunk = current_chunk();
            /* The last two bytes should be OP_CALL + arg_count */
            if (chunk->count >= 2 && chunk->code[chunk->count - 2] == OP_CALL)
            {
                chunk->code[chunk->count - 2] = OP_TAIL_CALL;
                /* No need for OP_RETURN - tail call handles it */
                last_was_call = false;
                return;
            }
        }

        emit_byte(OP_RETURN);
    }
    last_was_call = false;
}

static void expression_statement(void)
{
    expression();
    emit_byte(OP_POP);
}

static void print_statement(void)
{
    /* Handle print(expr) as built-in */
    consume(TOKEN_LPAREN, "Expect '(' after 'print'.");
    expression();
    consume(TOKEN_RPAREN, "Expect ')' after value.");
    emit_byte(OP_PRINT);
}

/* Exception handling: try ... catch e then ... end */
static void try_statement(void)
{
    skip_newlines();

    /* Emit OP_TRY with placeholder catch offset */
    emit_byte(OP_TRY);
    int try_start = current_chunk()->count;
    emit_byte(0xff); /* Placeholder high byte */
    emit_byte(0xff); /* Placeholder low byte */

    /* Parse try block */
    block();

    /* End of try - pop handler and jump over catch */
    emit_byte(OP_TRY_END);
    int end_jump = emit_jump(OP_JMP);

    /* Patch the catch offset */
    int catch_offset = current_chunk()->count - try_start - 2;
    current_chunk()->code[try_start] = (catch_offset >> 8) & 0xff;
    current_chunk()->code[try_start + 1] = catch_offset & 0xff;

    /* Parse catch clause */
    consume(TOKEN_CATCH, "Expect 'catch' after try block.");

    /* Emit OP_CATCH to push exception onto stack */
    emit_byte(OP_CATCH);

    /* Optional: bind exception to variable */
    if (check(TOKEN_IDENT))
    {
        begin_scope();
        uint8_t var = parse_variable("Expect exception variable name.");
        define_variable(var);

        if (match(TOKEN_THEN))
        {
            skip_newlines();
        }

        block();
        end_scope();
    }
    else
    {
        /* No variable - just pop the exception */
        emit_byte(OP_POP);

        if (match(TOKEN_THEN))
        {
            skip_newlines();
        }

        block();
    }

    /* Patch end jump */
    patch_jump(end_jump);

    consume(TOKEN_END, "Expect 'end' after catch block.");
}

/* Throw statement: throw expr */
static void throw_statement(void)
{
    expression();
    emit_byte(OP_THROW);
}

/* Pattern matching: match expr case val1 then ... case val2 then ... else ... end */
static void match_statement(void)
{
    expression(); /* The value to match against */
    skip_newlines();

    int end_jumps[256];
    int end_jump_count = 0;

    /* Parse case clauses */
    while (match(TOKEN_CASE))
    {
        /* Duplicate the match value for comparison */
        emit_byte(OP_DUP);

        expression(); /* The case pattern/value */
        consume(TOKEN_THEN, "Expect 'then' after case value.");
        skip_newlines();

        /* Compare - use no_fuse because we have OP_DUP above which changes stack semantics */
        emit_byte(OP_EQ);
        int next_case = emit_jump_no_fuse(OP_JMP_FALSE);
        emit_byte(OP_POP); /* Pop comparison result */

        /* Execute case body */
        block();

        /* Jump to end after case body */
        end_jumps[end_jump_count++] = emit_jump(OP_JMP);

        patch_jump(next_case);
        emit_byte(OP_POP); /* Pop comparison result */
    }

    /* Optional else clause (default case) */
    if (match(TOKEN_ELSE))
    {
        skip_newlines();
        block();
    }

    /* Pop the match value */
    emit_byte(OP_POP);

    /* Patch all end jumps to here */
    for (int i = 0; i < end_jump_count; i++)
    {
        patch_jump(end_jumps[i]);
    }

    consume(TOKEN_END, "Expect 'end' after match statement.");
}

static void statement(void)
{
    if (match(TOKEN_IF))
    {
        if_statement();
    }
    else if (match(TOKEN_WHILE))
    {
        while_statement();
    }
    else if (match(TOKEN_FOR))
    {
        for_statement();
    }
    else if (match(TOKEN_MATCH))
    {
        match_statement();
    }
    else if (match(TOKEN_TRY))
    {
        try_statement();
    }
    else if (match(TOKEN_THROW))
    {
        throw_statement();
    }
    else if (match(TOKEN_RETURN))
    {
        return_statement();
    }
    else
    {
        expression_statement();
    }
}

static void var_declaration(void)
{
    uint8_t global = parse_variable("Expect variable name.");

    /* Track which local we're defining (before expression parsing) */
    int local_slot = current->scope_depth > 0 ? current->local_count - 1 : -1;

    /* Optional type annotation */
    if (match(TOKEN_COLON))
    {
        consume(TOKEN_IDENT, "Expect type name.");
    }

    consume(TOKEN_ASSIGN, "Expect '=' after variable name.");
    expression();

    /* Infer local type from initializer expression */
    if (local_slot >= 0)
    {
        current->locals[local_slot].inferred_type = last_emit.type;
    }

    define_variable(global);
}

/* Lambda/anonymous function expression: fn(x) return x * 2 end */
static void lambda(bool can_assign)
{
    (void)can_assign;

    Compiler compiler;
    init_compiler(&compiler, TYPE_FUNCTION);

    /* Anonymous functions get a synthetic name for debugging */
    current->function->name = copy_string(compiling_vm, "<lambda>", 8);

    begin_scope();

    consume(TOKEN_LPAREN, "Expect '(' after 'fn' in lambda.");

    if (!check(TOKEN_RPAREN))
    {
        do
        {
            current->function->arity++;
            if (current->function->arity > 255)
            {
                error_at_current("Can't have more than 255 parameters.");
            }
            uint8_t constant = parse_variable("Expect parameter name.");
            (void)constant;
            if (match(TOKEN_COLON))
            {
                consume(TOKEN_IDENT, "Expect type name.");
            }
            mark_initialized();
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RPAREN, "Expect ')' after lambda parameters.");

    /* Optional return type */
    if (match(TOKEN_ARROW))
    {
        consume(TOKEN_IDENT, "Expect return type.");
    }

    /* Jump over the lambda body - we'll patch this later */
    int jump = emit_jump(OP_JMP);

    /* Record where the function code starts */
    current->function->code_start = current_chunk()->count;

    skip_newlines();
    block();
    consume(TOKEN_END, "Expect 'end' after lambda body.");

    ObjFunction *func = end_compiler();

    /* Patch the jump to skip the function body */
    patch_jump(jump);

    /* Emit closure instruction with upvalue info */
    emit_bytes(OP_CLOSURE, make_constant(val_obj(func)));

    /* Emit upvalue descriptors */
    for (int i = 0; i < func->upvalue_count; i++)
    {
        emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler.upvalues[i].index);
    }
}

static void function(FunctionType type)
{
    Compiler compiler;
    init_compiler(&compiler, type);
    begin_scope();

    consume(TOKEN_LPAREN, "Expect '(' after function name.");

    if (!check(TOKEN_RPAREN))
    {
        do
        {
            current->function->arity++;
            if (current->function->arity > 255)
            {
                error_at_current("Can't have more than 255 parameters.");
            }
            uint8_t constant = parse_variable("Expect parameter name.");
            (void)constant;
            if (match(TOKEN_COLON))
            {
                consume(TOKEN_IDENT, "Expect type name.");
            }
            mark_initialized();
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RPAREN, "Expect ')' after parameters.");

    /* Optional return type */
    if (match(TOKEN_ARROW))
    {
        consume(TOKEN_IDENT, "Expect return type.");
    }

    /* Jump over the function body - we'll patch this later */
    int jump = emit_jump(OP_JMP);

    /* Record where the function code starts */
    current->function->code_start = current_chunk()->count;

    skip_newlines();
    block();
    consume(TOKEN_END, "Expect 'end' after function body.");

    ObjFunction *func = end_compiler();

    /* Patch the jump to skip the function body */
    patch_jump(jump);

    /* Emit closure instruction with upvalue info */
    emit_bytes(OP_CLOSURE, make_constant(val_obj(func)));

    /* Emit upvalue descriptors */
    for (int i = 0; i < func->upvalue_count; i++)
    {
        emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler.upvalues[i].index);
    }
}

/* Enum declaration parser
 * Syntax:
 *   enum Color
 *       Red
 *       Green
 *       Blue
 *   end
 *
 * or with explicit values:
 *   enum Status
 *       Pending = 0
 *       Active = 1
 *       Done = 2
 *   end
 *
 * Creates global constants: Color_Red = 0, Color_Green = 1, etc.
 * Also creates Color_name(value) function and Color_values array.
 */
static void enum_declaration(void)
{
    consume(TOKEN_IDENT, "Expect enum name.");
    Token enum_name = parser.previous;

    /* Create the enum name string for prefixing members */
    char prefix[256];
    int prefix_len = snprintf(prefix, sizeof(prefix), "%.*s_",
                              enum_name.length, enum_name.start);

    skip_newlines();

    /* Track enum members for creating helper array */
    char member_names[64][256];
    int member_values[64];
    int member_count = 0;
    int next_value = 0;

    /* Parse enum members until 'end' */
    while (!check(TOKEN_END) && !check(TOKEN_EOF))
    {
        skip_newlines();
        if (check(TOKEN_END))
            break;

        consume(TOKEN_IDENT, "Expect enum member name.");
        Token member = parser.previous;

        /* Check for explicit value assignment */
        int value = next_value;
        if (match(TOKEN_ASSIGN))
        {
            consume(TOKEN_INT, "Expect integer value for enum member.");
            value = (int)strtol(parser.previous.start, NULL, 10);
        }

        /* Store member info */
        if (member_count < 64)
        {
            snprintf(member_names[member_count], sizeof(member_names[0]),
                     "%.*s", member.length, member.start);
            member_values[member_count] = value;
            member_count++;
        }

        /* Create global constant: EnumName_MemberName */
        char const_name[512];
        snprintf(const_name, sizeof(const_name), "%s%.*s",
                 prefix, member.length, member.start);

        /* Emit the value and define as global */
        ObjString *name_str = copy_string(compiling_vm, const_name, strlen(const_name));
        emit_constant(val_num(value));
        uint8_t global = make_constant(val_obj(name_str));
        emit_bytes(OP_SET_GLOBAL, global);
        emit_byte(OP_POP);

        next_value = value + 1;
        skip_newlines();
    }

    consume(TOKEN_END, "Expect 'end' after enum declaration.");

    /* Create EnumName_values array containing all values */
    char values_name[512];
    snprintf(values_name, sizeof(values_name), "%.*s_values",
             enum_name.length, enum_name.start);

    /* Emit array literal with all values */
    for (int i = 0; i < member_count; i++)
    {
        emit_constant(val_num(member_values[i]));
    }
    emit_bytes(OP_ARRAY, member_count);

    ObjString *values_str = copy_string(compiling_vm, values_name, strlen(values_name));
    uint8_t values_global = make_constant(val_obj(values_str));
    emit_bytes(OP_SET_GLOBAL, values_global);
    emit_byte(OP_POP);

    /* Create EnumName_names array containing all member names */
    char names_name[512];
    snprintf(names_name, sizeof(names_name), "%.*s_names",
             enum_name.length, enum_name.start);

    for (int i = 0; i < member_count; i++)
    {
        ObjString *name = copy_string(compiling_vm, member_names[i], strlen(member_names[i]));
        emit_constant(val_obj(name));
    }
    emit_bytes(OP_ARRAY, member_count);

    ObjString *names_str = copy_string(compiling_vm, names_name, strlen(names_name));
    uint8_t names_global = make_constant(val_obj(names_str));
    emit_bytes(OP_SET_GLOBAL, names_global);
    emit_byte(OP_POP);
}

static void fn_declaration(void)
{
    uint8_t global = parse_variable("Expect function name.");
    mark_initialized();
    function(TYPE_FUNCTION);
    define_variable(global);
}

/* Async function declaration: async fn name() */
static void async_fn_declaration(void)
{
    uint8_t global = parse_variable("Expect async function name.");
    mark_initialized();
    function(TYPE_ASYNC);
    emit_byte(OP_ASYNC); /* Wrap result in Promise */
    define_variable(global);
}

/* Generator function declaration: fn* name() or fn name() with yield */
static void generator_fn_declaration(void)
{
    uint8_t global = parse_variable("Expect generator function name.");
    mark_initialized();
    function(TYPE_GENERATOR);
    emit_byte(OP_GENERATOR); /* Wrap in generator object */
    define_variable(global);
}

/* Module declaration: module ModuleName */
static void module_declaration(void)
{
    consume(TOKEN_IDENT, "Expect module name.");
    uint8_t name = identifier_constant(&parser.previous);

    emit_bytes(OP_MODULE, name);

    skip_newlines();

    /* Parse module body */
    while (!check(TOKEN_END) && !check(TOKEN_EOF))
    {
        skip_newlines();
        if (check(TOKEN_END))
            break;
        declaration();
        skip_newlines();
    }

    consume(TOKEN_END, "Expect 'end' after module body.");

    /* Define module as global */
    emit_bytes(OP_SET_GLOBAL, name);
}

/* Export declaration: export fn name or export let name */
static void export_declaration(void)
{
    if (match(TOKEN_FN))
    {
        /* export fn name() */
        consume(TOKEN_IDENT, "Expect function name after 'export fn'.");
        uint8_t name = identifier_constant(&parser.previous);

        function(TYPE_FUNCTION);
        emit_bytes(OP_EXPORT, name);
    }
    else if (match(TOKEN_LET) || match(TOKEN_CONST))
    {
        /* export let name = value */
        consume(TOKEN_IDENT, "Expect variable name after 'export let'.");
        uint8_t name = identifier_constant(&parser.previous);

        if (match(TOKEN_ASSIGN))
        {
            expression();
        }
        else
        {
            emit_byte(OP_NIL);
        }
        emit_bytes(OP_EXPORT, name);
    }
    else if (match(TOKEN_CLASS))
    {
        /* export class ClassName */
        consume(TOKEN_IDENT, "Expect class name after 'export class'.");
        uint8_t name = identifier_constant(&parser.previous);

        /* Class body parsing - simplified version */
        emit_bytes(OP_CLASS, name);

        if (match(TOKEN_EXTENDS))
        {
            consume(TOKEN_IDENT, "Expect superclass name.");
            variable(false);
            emit_byte(OP_INHERIT);
        }

        skip_newlines();
        while (!check(TOKEN_END) && !check(TOKEN_EOF))
        {
            skip_newlines();
            if (match(TOKEN_FN))
            {
                consume(TOKEN_IDENT, "Expect method name.");
                uint8_t method_name = identifier_constant(&parser.previous);
                function(TYPE_METHOD);
                emit_bytes(OP_METHOD, method_name);
            }
            else if (check(TOKEN_END))
            {
                break;
            }
            else
            {
                advance();
            }
            skip_newlines();
        }
        consume(TOKEN_END, "Expect 'end' after class body.");

        emit_bytes(OP_EXPORT, name);
    }
    else
    {
        error("Expect declaration after 'export'.");
    }
}

/* Import declaration: import name from "module" or from "module" import a, b */
static void import_declaration(void)
{
    if (match(TOKEN_IDENT))
    {
        /* import ModuleName - import entire module */
        uint8_t name = identifier_constant(&parser.previous);

        if (match(TOKEN_AS))
        {
            /* import ModuleName as alias */
            consume(TOKEN_IDENT, "Expect alias name after 'as'.");
            uint8_t alias = identifier_constant(&parser.previous);
            emit_bytes(OP_IMPORT_AS, name);
            emit_byte(alias);
        }
        else if (match(TOKEN_FROM))
        {
            /* import name from "module" */
            consume(TOKEN_STRING, "Expect module path string.");
            /* Module loading would happen at runtime */
            emit_bytes(OP_IMPORT_FROM, name);
        }
        else
        {
            /* Just import by name */
            emit_bytes(OP_IMPORT_AS, name);
            emit_byte(name);
        }

        /* Define as global */
        emit_bytes(OP_SET_GLOBAL, name);
    }
    else if (match(TOKEN_LBRACE))
    {
        /* import { a, b, c } from "module" - selective import */
        do
        {
            consume(TOKEN_IDENT, "Expect symbol name.");
            uint8_t sym = identifier_constant(&parser.previous);

            uint8_t alias = sym;
            if (match(TOKEN_AS))
            {
                consume(TOKEN_IDENT, "Expect alias after 'as'.");
                alias = identifier_constant(&parser.previous);
            }

            emit_bytes(OP_IMPORT_FROM, sym);
            emit_bytes(OP_SET_GLOBAL, alias);
        } while (match(TOKEN_COMMA));

        consume(TOKEN_RBRACE, "Expect '}' after import list.");
        consume(TOKEN_FROM, "Expect 'from' after import list.");
        consume(TOKEN_STRING, "Expect module path string.");
    }
    else
    {
        error("Expect module name or '{' after 'import'.");
    }
}

/* Class declaration parser */
static void class_declaration(void)
{
    consume(TOKEN_IDENT, "Expect class name.");
    uint8_t name_constant = identifier_constant(&parser.previous);

    /* Emit OP_CLASS to create the class object */
    emit_bytes(OP_CLASS, name_constant);

    /* Store class in global variable with same name */
    if (current->scope_depth > 0)
    {
        add_local(parser.previous);
        mark_initialized();
    }
    else
    {
        emit_bytes(OP_SET_GLOBAL, name_constant);
    }

    /* Optional inheritance: class Foo extends Bar */
    if (match(TOKEN_EXTENDS))
    {
        consume(TOKEN_IDENT, "Expect superclass name.");
        variable(false); /* Load the superclass */
        emit_byte(OP_INHERIT);
    }

    skip_newlines();

    /* Parse class body until 'end' */
    while (!check(TOKEN_END) && !check(TOKEN_EOF))
    {
        skip_newlines();

        if (match(TOKEN_STATIC))
        {
            /* Static method or property */
            if (match(TOKEN_FN))
            {
                /* Static method: static fn name() */
                consume(TOKEN_IDENT, "Expect static method name.");
                uint8_t method_name = identifier_constant(&parser.previous);
                function(TYPE_FUNCTION);
                emit_bytes(OP_STATIC, method_name);
            }
            else if (match(TOKEN_LET))
            {
                /* Static property: static let name = value */
                consume(TOKEN_IDENT, "Expect static property name.");
                uint8_t prop_name = identifier_constant(&parser.previous);

                if (match(TOKEN_ASSIGN))
                {
                    expression();
                }
                else
                {
                    emit_byte(OP_NIL);
                }
                emit_bytes(OP_STATIC, prop_name);
            }
            else
            {
                error("Expect 'fn' or 'let' after 'static'.");
            }
        }
        else if (match(TOKEN_FN))
        {
            /* Method definition */
            consume(TOKEN_IDENT, "Expect method name.");
            uint8_t method_name = identifier_constant(&parser.previous);

            /* Check if this is an initializer */
            Token method_token = parser.previous;
            bool is_init = (method_token.length == 4 &&
                            memcmp(method_token.start, "init", 4) == 0);

            function(is_init ? TYPE_INITIALIZER : TYPE_METHOD);
            emit_bytes(OP_METHOD, method_name);
        }
        else if (match(TOKEN_LET))
        {
            /* Field declaration */
            consume(TOKEN_IDENT, "Expect field name.");
            uint8_t field_name = identifier_constant(&parser.previous);
            emit_bytes(OP_FIELD, field_name);

            /* Optional initializer - skip for now, handled in constructor */
            if (match(TOKEN_ASSIGN))
            {
                /* Just skip the initializer expression for now */
                expression();
                emit_byte(OP_POP);
            }
        }
        else if (check(TOKEN_END))
        {
            break;
        }
        else
        {
            skip_newlines();
            if (!check(TOKEN_END) && !check(TOKEN_EOF))
            {
                advance(); /* Skip unknown tokens in class body */
            }
        }

        skip_newlines();
    }

    consume(TOKEN_END, "Expect 'end' after class body.");
}

static void declaration(void)
{
    skip_newlines();

    if (match(TOKEN_FN))
    {
        fn_declaration();
    }
    else if (match(TOKEN_ASYNC))
    {
        /* async fn name() - async function declaration */
        consume(TOKEN_FN, "Expect 'fn' after 'async'.");
        async_fn_declaration();
    }
    else if (match(TOKEN_CLASS))
    {
        class_declaration();
    }
    else if (match(TOKEN_ENUM))
    {
        enum_declaration();
    }
    else if (match(TOKEN_MODULE))
    {
        module_declaration();
    }
    else if (match(TOKEN_EXPORT))
    {
        export_declaration();
    }
    else if (match(TOKEN_IMPORT))
    {
        import_declaration();
    }
    else if (match(TOKEN_LET) || match(TOKEN_CONST))
    {
        var_declaration();
    }
    else
    {
        statement();
    }

    skip_newlines();
}

/* ============ Peephole Optimizer ============ */
/* Optimizes bytecode patterns after initial emission */
/* Note: Disabled for now as it corrupts jump offsets */
/* TODO: Track and update jump offsets when shifting bytecode */

#if 0 /* Disabled - needs jump offset tracking */
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
        
        /* Pattern: NEQ JMP_FALSE -> NEQ_JMP_FALSE */
        if (code[i] == OP_NEQ && code[i+1] == OP_JMP_FALSE) {
            code[i] = OP_NEQ_JMP_FALSE;
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

#if 0 /* Disabled - needs jump offset tracking */
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

bool compile(const char *source, Chunk *chunk, VM *vm)
{
    scanner_init(source);

    Compiler compiler;
    compiling_chunk = chunk;
    compiling_vm = vm;
    init_compiler(&compiler, TYPE_SCRIPT);

    parser.had_error = false;
    parser.panic_mode = false;

    /* Reset constant folding state */
    last_emit.is_constant = false;
    last_emit.value = 0;
    last_emit.bytecode_pos = 0;
    last_emit.const_idx = 0;
    second_last_emit.is_constant = false;

    advance();

    while (!match(TOKEN_EOF))
    {
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
