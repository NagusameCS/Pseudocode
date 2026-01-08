/*
 * Pseudocode Language - High Performance C Implementation
 * Uses NaN-boxing, computed gotos, and direct threading
 */

#ifndef PSEUDO_H
#define PSEUDO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============ NaN Boxing ============
 * Pack all values into 64 bits using NaN boxing:
 * - Doubles: normal IEEE 754 doubles
 * - Other types: use quiet NaN with payload
 * 
 * IEEE 754 double NaN: sign(1) | exp(11 all 1s) | quiet(1) | payload(51)
 * We use the quiet NaN space to encode other types.
 */

typedef uint64_t Value;

/*
 * NaN Boxing Scheme:
 * - Floating point: standard IEEE 754 double
 * - Quiet NaN: 0x7FF8000000000000 (all NaN values indicate non-doubles)
 * - We use: 0x7FFC000000000000 as our "quiet NaN" base
 * 
 * Value layout for non-doubles (stored in lower 48 bits of NaN):
 *   Bits 0-2:   type tag (0-7)
 *   Bits 3-47:  payload (45 bits for pointers/ints)
 */

#define QNAN     ((uint64_t)0x7ffc000000000000)  /* Quiet NaN base */
#define SIGN_BIT ((uint64_t)0x8000000000000000)

/* Tags use lower 3 bits */
#define TAG_NIL   1
#define TAG_FALSE 2
#define TAG_TRUE  3
#define TAG_INT   4
#define TAG_OBJ   (SIGN_BIT | QNAN)  /* Object pointer (uses sign bit) */

#define VAL_NIL   ((Value)(QNAN | TAG_NIL))
#define VAL_FALSE ((Value)(QNAN | TAG_FALSE))
#define VAL_TRUE  ((Value)(QNAN | TAG_TRUE))

/* Value creation */
static inline Value val_num(double n) {
    union { double d; Value v; } u;
    u.d = n;
    return u.v;
}

static inline Value val_int(int32_t i) {
    /* Tag in bits 0-2, integer in bits 3-34 (32 bits) */
    return QNAN | TAG_INT | ((uint64_t)(uint32_t)i << 3);
}

static inline Value val_bool(bool b) {
    return b ? VAL_TRUE : VAL_FALSE;
}

static inline Value val_obj(void* ptr) {
    return TAG_OBJ | (uint64_t)(uintptr_t)ptr;
}

/* Value extraction */
static inline double as_num(Value v) {
    union { Value v; double d; } u;
    u.v = v;
    return u.d;
}

static inline int32_t as_int(Value v) {
    return (int32_t)((v >> 3) & 0xFFFFFFFF);
}

static inline void* as_obj(Value v) {
    return (void*)(uintptr_t)(v & ~TAG_OBJ);
}

/* Type checks */
#define IS_NUM(v)   (((v) & QNAN) != QNAN)
#define IS_NIL(v)   ((v) == VAL_NIL)
#define IS_BOOL(v)  (((v) & (QNAN | TAG_FALSE)) == (QNAN | TAG_FALSE))
#define IS_TRUE(v)  ((v) == VAL_TRUE)
#define IS_FALSE(v) ((v) == VAL_FALSE)
#define IS_INT(v)   (((v) & (QNAN | 0x7)) == (QNAN | TAG_INT))
#define IS_OBJ(v)   (((v) & TAG_OBJ) == TAG_OBJ)

/* Truthiness */
static inline bool is_truthy(Value v) {
    if (IS_NIL(v) || IS_FALSE(v)) return false;
    if (IS_NUM(v)) return as_num(v) != 0.0;
    if (IS_INT(v)) return as_int(v) != 0;
    return true;
}

/* ============ Object Types ============ */

typedef enum {
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_RANGE,
    OBJ_DICT,       /* Dictionary/HashMap */
    OBJ_BYTES,      /* Byte array for binary data */
} ObjType;

typedef struct Obj {
    ObjType type;
    struct Obj* next;  /* GC linked list */
    bool marked;
} Obj;

typedef struct {
    Obj obj;
    uint32_t length;
    uint32_t hash;
    char chars[];  /* Flexible array member */
} ObjString;

typedef struct {
    Obj obj;
    uint32_t count;
    uint32_t capacity;
    Value* values;
} ObjArray;

typedef struct {
    Obj obj;
    int32_t start;
    int32_t current;
    int32_t end;
} ObjRange;

typedef struct {
    Obj obj;
    uint8_t arity;
    uint16_t locals_count;
    uint32_t code_start;
    ObjString* name;
} ObjFunction;

/* Dictionary - high-performance hash table */
typedef struct {
    Obj obj;
    uint32_t count;
    uint32_t capacity;
    ObjString** keys;
    Value* values;
} ObjDict;

/* Byte array for binary data */
typedef struct {
    Obj obj;
    uint32_t length;
    uint32_t capacity;
    uint8_t* data;
} ObjBytes;

/* Object type checks */
#define OBJ_TYPE(v)    (((Obj*)as_obj(v))->type)
#define IS_STRING(v)   (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_STRING)
#define IS_ARRAY(v)    (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_ARRAY)
#define IS_FUNCTION(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_FUNCTION)
#define IS_RANGE(v)    (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_RANGE)
#define IS_DICT(v)     (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_DICT)
#define IS_BYTES(v)    (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_BYTES)

#define AS_STRING(v)   ((ObjString*)as_obj(v))
#define AS_ARRAY(v)    ((ObjArray*)as_obj(v))
#define AS_FUNCTION(v) ((ObjFunction*)as_obj(v))
#define AS_RANGE(v)    ((ObjRange*)as_obj(v))
#define AS_DICT(v)     ((ObjDict*)as_obj(v))
#define AS_BYTES(v)    ((ObjBytes*)as_obj(v))

/* ============ Bytecode ============ */

typedef enum {
    /* Stack ops */
    OP_CONST,       /* Push constant */
    OP_CONST_LONG,  /* Push constant (16-bit index) */
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_POPN,        /* Pop N values */
    OP_DUP,
    
    /* Variables */
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    
    /* Arithmetic */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEG,
    OP_INC,         /* Increment */
    OP_DEC,         /* Decrement */
    OP_POW,         /* Power/exponent */
    
    /* Comparison */
    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_GT,
    OP_LTE,
    OP_GTE,
    
    /* Logical */
    OP_NOT,
    OP_AND,         /* Short-circuit AND */
    OP_OR,          /* Short-circuit OR */
    
    /* Bitwise */
    OP_BAND,
    OP_BOR,
    OP_BXOR,
    OP_BNOT,        /* Bitwise NOT */
    OP_SHL,
    OP_SHR,
    
    /* Control flow */
    OP_JMP,
    OP_JMP_FALSE,
    OP_JMP_TRUE,
    OP_LOOP,        /* Backward jump */
    
    /* Functions */
    OP_CALL,
    OP_RETURN,
    
    /* Arrays */
    OP_ARRAY,
    OP_INDEX,
    OP_INDEX_SET,
    OP_LEN,
    OP_PUSH,
    OP_POP_ARRAY,
    OP_SLICE,       /* Array slicing */
    OP_CONCAT,      /* Array/string concat */
    
    /* Iterators */
    OP_RANGE,
    OP_ITER_NEXT,
    OP_ITER_ARRAY,  /* Array iterator */
    
    /* Built-ins */
    OP_PRINT,
    OP_PRINTLN,     /* Print with newline */
    OP_TIME,
    OP_INPUT,
    OP_INT,         /* Convert to int */
    OP_FLOAT,       /* Convert to float */
    OP_STR,         /* Convert to string */
    OP_TYPE,        /* Get type name */
    OP_ABS,         /* Absolute value */
    OP_MIN,         /* Min of two values */
    OP_MAX,         /* Max of two values */
    OP_SQRT,        /* Square root */
    OP_FLOOR,       /* Floor */
    OP_CEIL,        /* Ceiling */
    OP_ROUND,       /* Round */
    OP_RAND,        /* Random 0-1 */
    
    /* Bit manipulation intrinsics - map to CPU instructions */
    OP_POPCOUNT,    /* Population count (count 1 bits) */
    OP_CLZ,         /* Count leading zeros */
    OP_CTZ,         /* Count trailing zeros */
    OP_ROTL,        /* Rotate left */
    OP_ROTR,        /* Rotate right */
    
    /* String operations */
    OP_SUBSTR,      /* Substring extraction */
    OP_UPPER,       /* To uppercase */
    OP_LOWER,       /* To lowercase */
    OP_SPLIT,       /* Split string into array */
    OP_JOIN,        /* Join array into string */
    OP_REPLACE,     /* Replace in string */
    OP_FIND,        /* Find substring */
    OP_TRIM,        /* Trim whitespace */
    OP_CHAR,        /* Get char code */
    OP_ORD,         /* Char code to string */
    
    OP_HALT,
    
    /* ============ SUPERINSTRUCTIONS ============ */
    /* Fused instructions for 2-3x speedup on common patterns */
    
    /* Fused local + arithmetic */
    OP_GET_LOCAL_0,     /* Get local slot 0 (most common) */
    OP_GET_LOCAL_1,     /* Get local slot 1 */
    OP_GET_LOCAL_2,     /* Get local slot 2 */
    OP_GET_LOCAL_3,     /* Get local slot 3 */
    
    /* Fused arithmetic for small integers */
    OP_ADD_1,           /* Add 1 to top of stack */
    OP_SUB_1,           /* Subtract 1 from top of stack */
    
    /* Fused comparison + jump (critical for loops) */
    OP_LT_JMP_FALSE,    /* Compare < and jump if false */
    OP_LTE_JMP_FALSE,   /* Compare <= and jump if false */
    OP_GT_JMP_FALSE,    /* Compare > and jump if false */
    OP_GTE_JMP_FALSE,   /* Compare >= and jump if false */
    OP_EQ_JMP_FALSE,    /* Compare == and jump if false */
    
    /* Fused local operations */
    OP_GET_LOCAL_ADD,   /* Get local and add to TOS */
    OP_GET_LOCAL_SUB,   /* Get local and sub from TOS */
    
    /* Tail call optimization */
    OP_TAIL_CALL,       /* Tail recursive call - reuse stack frame */
    
    /* Small integer constants */
    OP_CONST_0,
    OP_CONST_1,
    OP_CONST_2,
    
    /* ============ INFRASTRUCTURE ============ */
    /* File I/O */
    OP_READ_FILE,       /* Read entire file to string */
    OP_WRITE_FILE,      /* Write string to file */
    OP_APPEND_FILE,     /* Append string to file */
    OP_FILE_EXISTS,     /* Check if file exists */
    OP_LIST_DIR,        /* List directory contents */
    OP_DELETE_FILE,     /* Delete a file */
    OP_MKDIR,           /* Create directory */
    
    /* HTTP/Network */
    OP_HTTP_GET,        /* HTTP GET request */
    OP_HTTP_POST,       /* HTTP POST request */
    
    /* JSON */
    OP_JSON_PARSE,      /* Parse JSON string to value */
    OP_JSON_STRINGIFY,  /* Convert value to JSON string */
    
    /* Process/System */
    OP_EXEC,            /* Execute shell command */
    OP_ENV,             /* Get environment variable */
    OP_SET_ENV,         /* Set environment variable */
    OP_ARGS,            /* Get command line arguments */
    OP_EXIT,            /* Exit with code */
    OP_SLEEP,           /* Sleep milliseconds */
    
    /* Dictionary/Map operations */
    OP_DICT,            /* Create dictionary */
    OP_DICT_GET,        /* Get value from dict */
    OP_DICT_SET,        /* Set value in dict */
    OP_DICT_HAS,        /* Check if key exists */
    OP_DICT_KEYS,       /* Get all keys */
    OP_DICT_VALUES,     /* Get all values */
    OP_DICT_DELETE,     /* Delete key from dict */
    
    /* Advanced math / SIMD */
    OP_SIN,             /* Sine */
    OP_COS,             /* Cosine */
    OP_TAN,             /* Tangent */
    OP_ASIN,            /* Arc sine */
    OP_ACOS,            /* Arc cosine */
    OP_ATAN,            /* Arc tangent */
    OP_ATAN2,           /* Arc tangent of y/x */
    OP_LOG,             /* Natural logarithm */
    OP_LOG10,           /* Base-10 logarithm */
    OP_LOG2,            /* Base-2 logarithm */
    OP_EXP,             /* e^x */
    OP_HYPOT,           /* sqrt(x² + y²) */
    
    /* Vector operations (SIMD accelerated) */
    OP_VEC_ADD,         /* Element-wise add */
    OP_VEC_SUB,         /* Element-wise subtract */
    OP_VEC_MUL,         /* Element-wise multiply */
    OP_VEC_DIV,         /* Element-wise divide */
    OP_VEC_DOT,         /* Dot product */
    OP_VEC_SUM,         /* Sum of all elements */
    OP_VEC_PROD,        /* Product of all elements */
    OP_VEC_MIN,         /* Minimum element */
    OP_VEC_MAX,         /* Maximum element */
    OP_VEC_MEAN,        /* Mean of elements */
    OP_VEC_MAP,         /* Map function over array */
    OP_VEC_FILTER,      /* Filter array by predicate */
    OP_VEC_REDUCE,      /* Reduce array with function */
    OP_VEC_SORT,        /* Sort array */
    OP_VEC_REVERSE,     /* Reverse array */
    OP_VEC_UNIQUE,      /* Remove duplicates */
    OP_VEC_ZIP,         /* Zip two arrays */
    OP_VEC_RANGE,       /* Generate numeric range array */
    
    /* Bytes/Binary */
    OP_BYTES,           /* Create byte array */
    OP_BYTES_GET,       /* Get byte at index */
    OP_BYTES_SET,       /* Set byte at index */
    OP_ENCODE_UTF8,     /* String to UTF-8 bytes */
    OP_DECODE_UTF8,     /* UTF-8 bytes to string */
    OP_ENCODE_BASE64,   /* Encode to base64 */
    OP_DECODE_BASE64,   /* Decode from base64 */
    
    /* Regex */
    OP_REGEX_MATCH,     /* Check if string matches regex */
    OP_REGEX_FIND,      /* Find all regex matches */
    OP_REGEX_REPLACE,   /* Replace with regex */
    
    /* Hashing */
    OP_HASH,            /* Hash a value (fast) */
    OP_HASH_SHA256,     /* SHA-256 hash */
    OP_HASH_MD5,        /* MD5 hash */
    
    OP_OPCODE_COUNT,    /* Total number of opcodes */
} OpCode;

/* ============ Chunk (bytecode container) ============ */

typedef struct {
    uint32_t count;
    uint32_t capacity;
    uint8_t* code;
    uint16_t* lines;
    
    uint32_t const_count;
    uint32_t const_capacity;
    Value* constants;
} Chunk;

void chunk_init(Chunk* chunk);
void chunk_free(Chunk* chunk);
void chunk_write(Chunk* chunk, uint8_t byte, uint16_t line);
uint32_t chunk_add_const(Chunk* chunk, Value value);

/* ============ VM ============ */

#define STACK_MAX 65536
#define FRAMES_MAX 1024

typedef struct {
    ObjFunction* function;
    uint8_t* ip;
    Value* slots;
} CallFrame;

typedef struct {
    Chunk chunk;
    uint8_t* ip;
    
    Value stack[STACK_MAX];
    Value* sp;
    
    CallFrame frames[FRAMES_MAX];
    int frame_count;
    
    /* Globals hash table */
    struct {
        ObjString** keys;
        Value* values;
        uint32_t count;
        uint32_t capacity;
    } globals;
    
    /* GC */
    Obj* objects;
    size_t bytes_allocated;
    size_t next_gc;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void vm_init(VM* vm);
void vm_free(VM* vm);
InterpretResult vm_interpret(VM* vm, const char* source);
InterpretResult vm_run(VM* vm);

/* ============ Compiler ============ */

bool compile(const char* source, Chunk* chunk, VM* vm);

/* ============ Memory ============ */

void* pseudo_realloc(VM* vm, void* ptr, size_t old_size, size_t new_size);
ObjString* copy_string(VM* vm, const char* chars, int length);
ObjString* take_string(VM* vm, char* chars, int length);
ObjArray* new_array(VM* vm, uint32_t capacity);
ObjRange* new_range(VM* vm, int32_t start, int32_t end);
ObjFunction* new_function(VM* vm);
ObjDict* new_dict(VM* vm, uint32_t capacity);
ObjBytes* new_bytes(VM* vm, uint32_t capacity);

/* Arena allocator for temporary allocations */
typedef struct Arena {
    uint8_t* data;
    size_t size;
    size_t used;
    struct Arena* next;
} Arena;

Arena* arena_create(size_t size);
void* arena_alloc(Arena* arena, size_t size);
void arena_reset(Arena* arena);
void arena_destroy(Arena* arena);

#define ALLOCATE(vm, type, count) \
    (type*)pseudo_realloc(vm, NULL, 0, sizeof(type) * (count))

#define FREE(vm, type, ptr) \
    pseudo_realloc(vm, ptr, sizeof(type), 0)

#define GROW_CAPACITY(cap) ((cap) < 8 ? 8 : (cap) * 2)

#define GROW_ARRAY(vm, type, ptr, old_count, new_count) \
    (type*)pseudo_realloc(vm, ptr, sizeof(type) * (old_count), sizeof(type) * (new_count))

#define FREE_ARRAY(vm, type, ptr, count) \
    pseudo_realloc(vm, ptr, sizeof(type) * (count), 0)

/* ============ Branch Hints ============ */
#if defined(__GNUC__) || defined(__clang__)
    #define LIKELY(x)   __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
    #define PREFETCH(addr) __builtin_prefetch(addr)
#else
    #define LIKELY(x)   (x)
    #define UNLIKELY(x) (x)
    #define PREFETCH(addr)
#endif

#endif /* PSEUDO_H */
