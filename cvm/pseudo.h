/*
 * Pseudocode Language - High Performance C Implementation
 * Uses NaN-boxing, computed gotos, and direct threading
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
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

#define QNAN ((uint64_t)0x7ffc000000000000) /* Quiet NaN base */
#define SIGN_BIT ((uint64_t)0x8000000000000000)

/* Tags use lower 3 bits */
#define TAG_NIL 1
#define TAG_FALSE 2
#define TAG_TRUE 3
#define TAG_INT 4
#define TAG_OBJ (SIGN_BIT | QNAN) /* Object pointer (uses sign bit) */

#define VAL_NIL ((Value)(QNAN | TAG_NIL))
#define VAL_FALSE ((Value)(QNAN | TAG_FALSE))
#define VAL_TRUE ((Value)(QNAN | TAG_TRUE))

/* Value creation */
static inline Value val_num(double n)
{
    union
    {
        double d;
        Value v;
    } u;
    u.d = n;
    return u.v;
}

static inline Value val_int(int32_t i)
{
    /* Tag in bits 0-2, integer in bits 3-34 (32 bits) */
    return QNAN | TAG_INT | ((uint64_t)(uint32_t)i << 3);
}

static inline Value val_bool(bool b)
{
    return b ? VAL_TRUE : VAL_FALSE;
}

static inline Value val_obj(void *ptr)
{
    return TAG_OBJ | (uint64_t)(uintptr_t)ptr;
}

/* Value extraction */
static inline double as_num(Value v)
{
    union
    {
        Value v;
        double d;
    } u;
    u.v = v;
    return u.d;
}

static inline int32_t as_int(Value v)
{
    return (int32_t)((v >> 3) & 0xFFFFFFFF);
}

static inline void *as_obj(Value v)
{
    return (void *)(uintptr_t)(v & ~TAG_OBJ);
}

/* Type checks */
#define IS_NUM(v) (((v) & QNAN) != QNAN)
#define IS_NIL(v) ((v) == VAL_NIL)
#define IS_BOOL(v) (((v) & (QNAN | TAG_FALSE)) == (QNAN | TAG_FALSE))
#define IS_TRUE(v) ((v) == VAL_TRUE)
#define IS_FALSE(v) ((v) == VAL_FALSE)
#define IS_INT(v) (((v) & (QNAN | 0x7)) == (QNAN | TAG_INT))
#define IS_OBJ(v) (((v) & TAG_OBJ) == TAG_OBJ)

/* Truthiness */
static inline bool is_truthy(Value v)
{
    if (IS_NIL(v) || IS_FALSE(v))
        return false;
    if (IS_NUM(v))
        return as_num(v) != 0.0;
    if (IS_INT(v))
        return as_int(v) != 0;
    return true;
}

/* ============ Object Types ============ */

typedef enum
{
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_RANGE,
    OBJ_DICT,         /* Dictionary/HashMap */
    OBJ_BYTES,        /* Byte array for binary data */
    OBJ_TENSOR,       /* N-dimensional tensor for ML/scientific computing */
    OBJ_MATRIX,       /* 2D matrix with BLAS-style operations */
    OBJ_DATAFRAME,    /* Tabular data with named columns */
    OBJ_GRAD_TAPE,    /* Autograd tape for automatic differentiation */
    OBJ_CLASS,        /* Class definition (compile-time) */
    OBJ_INSTANCE,     /* Class instance */
    OBJ_GENERATOR,    /* Generator (coroutine) for yield */
    OBJ_PROMISE,      /* Promise/Future for async/await */
    OBJ_MODULE,       /* Module namespace */
    OBJ_BOUND_METHOD, /* Method bound to instance */
} ObjType;

typedef struct Obj
{
    ObjType type;
    struct Obj *next; /* GC linked list */
    bool marked;
} Obj;

typedef struct
{
    Obj obj;
    uint32_t length;
    uint32_t hash;
    char chars[]; /* Flexible array member */
} ObjString;

typedef struct
{
    Obj obj;
    uint32_t count;
    uint32_t capacity;
    Value *values;
} ObjArray;

typedef struct
{
    Obj obj;
    int32_t start;
    int32_t current;
    int32_t end;
} ObjRange;

typedef struct
{
    Obj obj;
    uint8_t arity;
    uint16_t locals_count;
    uint16_t upvalue_count;
    uint32_t code_start;
    ObjString *name;
} ObjFunction;

/* Upvalue - captures a variable from enclosing scope */
typedef struct ObjUpvalue
{
    Obj obj;
    Value *location;         /* Points to stack slot (open) or 'closed' field */
    Value closed;            /* Holds value after variable goes out of scope */
    struct ObjUpvalue *next; /* Linked list of open upvalues */
} ObjUpvalue;

/* Closure - function + captured environment */
#define MAX_UPVALUES 255
typedef struct
{
    Obj obj;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    uint16_t upvalue_count;
} ObjClosure;

/* Forward declarations for types used in structs below */
typedef struct ObjDict ObjDict;

/* Generator - coroutine for yield-based iteration */
typedef enum
{
    GEN_CREATED,   /* Just created, not started */
    GEN_RUNNING,   /* Currently executing */
    GEN_SUSPENDED, /* Paused at yield */
    GEN_CLOSED     /* Finished or threw */
} GeneratorState;

typedef struct ObjGenerator
{
    Obj obj;
    ObjClosure *closure;     /* The generator function */
    Value *stack;            /* Private stack for generator */
    uint16_t stack_size;     /* Current stack size */
    uint16_t stack_capacity; /* Stack capacity */
    uint8_t *ip;             /* Saved instruction pointer */
    GeneratorState state;    /* Current state */
    Value sent_value;        /* Value sent via next(gen, val) */
} ObjGenerator;

/* Promise - future value for async/await */
typedef enum
{
    PROMISE_PENDING,
    PROMISE_RESOLVED,
    PROMISE_REJECTED
} PromiseState;

typedef struct ObjPromise
{
    Obj obj;
    PromiseState state;
    Value result;            /* Resolved value or rejection reason */
    struct ObjPromise *next; /* For continuation chain */
    Value on_resolve;        /* Callback when resolved */
    Value on_reject;         /* Callback when rejected */
} ObjPromise;

/* Module - namespace for imports */
typedef struct ObjModule
{
    Obj obj;
    ObjString *name;  /* Module name/path */
    ObjDict *exports; /* Exported symbols */
    bool loaded;      /* True if fully loaded */
} ObjModule;

/* BoundMethod - method bound to a receiver instance */
typedef struct ObjBoundMethod
{
    Obj obj;
    Value receiver;     /* The instance (self) */
    ObjClosure *method; /* The method closure */
} ObjBoundMethod;

/* Class - compile-time class definition with fixed slots */
#define CLASS_MAX_FIELDS 64
#define CLASS_MAX_METHODS 64
#define CLASS_MAX_STATIC 32

typedef struct ObjClass
{
    Obj obj;
    ObjString *name;
    struct ObjClass *superclass;
    uint16_t field_count;
    uint16_t method_count;
    uint16_t static_count;
    ObjString *field_names[CLASS_MAX_FIELDS];   /* Field name lookup */
    Value methods[CLASS_MAX_METHODS];           /* Method functions */
    ObjString *method_names[CLASS_MAX_METHODS]; /* Method name lookup */
    Value statics[CLASS_MAX_STATIC];            /* Static properties/methods */
    ObjString *static_names[CLASS_MAX_STATIC];  /* Static name lookup */
} ObjClass;

/* Instance - runtime class instance with fixed-slot fields */
typedef struct
{
    Obj obj;
    ObjClass *klass;
    Value fields[]; /* Flexible array: fields[klass->field_count] */
} ObjInstance;

/* Dictionary - high-performance hash table */
struct ObjDict
{
    Obj obj;
    uint32_t count;
    uint32_t capacity;
    ObjString **keys;
    Value *values;
};

/* Byte array for binary data */
typedef struct
{
    Obj obj;
    uint32_t length;
    uint32_t capacity;
    uint8_t *data;
} ObjBytes;

/* ============ Data Science Types ============ */

/* Forward declaration */
typedef struct ObjTensor ObjTensor;

/* Tensor - N-dimensional array for ML/scientific computing */
/* Supports arbitrary dimensions, strides for views, f64 data */
#define TENSOR_MAX_DIMS 8

struct ObjTensor
{
    Obj obj;
    uint32_t ndim;                    /* Number of dimensions */
    uint32_t shape[TENSOR_MAX_DIMS];  /* Size in each dimension */
    int64_t strides[TENSOR_MAX_DIMS]; /* Byte stride for each dim */
    uint32_t size;                    /* Total number of elements */
    double *data;                     /* Contiguous f64 data */
    bool owns_data;                   /* True if we should free data */
    bool requires_grad;               /* For autograd */
    ObjTensor *grad;                  /* Gradient tensor */
};

/* Matrix - 2D matrix optimized for linear algebra */
/* Row-major storage, BLAS-compatible */
typedef struct
{
    Obj obj;
    uint32_t rows;
    uint32_t cols;
    double *data; /* Row-major f64 data */
    bool owns_data;
} ObjMatrix;

/* DataFrame - Tabular data with named columns */
typedef struct
{
    Obj obj;
    uint32_t num_rows;
    uint32_t num_cols;
    ObjString **column_names; /* Column name strings */
    ObjArray **columns;       /* Array of column arrays */
} ObjDataFrame;

/* GradTape - Records operations for automatic differentiation */
typedef struct GradTapeEntry
{
    uint8_t op;           /* Operation type */
    ObjTensor *result;    /* Output tensor */
    ObjTensor *inputs[3]; /* Input tensors (max 3) */
    double scalar;        /* Scalar argument if any */
} GradTapeEntry;

typedef struct
{
    Obj obj;
    GradTapeEntry *entries;
    uint32_t count;
    uint32_t capacity;
    bool recording;
} ObjGradTape;

/* Autograd operation types */
typedef enum
{
    GRAD_OP_ADD,
    GRAD_OP_SUB,
    GRAD_OP_MUL,
    GRAD_OP_DIV,
    GRAD_OP_MATMUL,
    GRAD_OP_RELU,
    GRAD_OP_SIGMOID,
    GRAD_OP_TANH,
    GRAD_OP_SOFTMAX,
    GRAD_OP_SUM,
    GRAD_OP_MEAN,
    GRAD_OP_POW,
    GRAD_OP_EXP,
    GRAD_OP_LOG,
} GradOpType;

/* Object type checks */
#define OBJ_TYPE(v) (((Obj *)as_obj(v))->type)
#define IS_STRING(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_STRING)
#define IS_ARRAY(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_ARRAY)
#define IS_FUNCTION(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_FUNCTION)
#define IS_RANGE(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_RANGE)
#define IS_DICT(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_DICT)
#define IS_BYTES(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_BYTES)
#define IS_TENSOR(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_TENSOR)
#define IS_MATRIX(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_MATRIX)
#define IS_DATAFRAME(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_DATAFRAME)
#define IS_GRAD_TAPE(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_GRAD_TAPE)
#define IS_CLASS(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_CLASS)
#define IS_INSTANCE(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_INSTANCE)
#define IS_CLOSURE(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_CLOSURE)
#define IS_UPVALUE(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_UPVALUE)
#define IS_GENERATOR(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_GENERATOR)
#define IS_PROMISE(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_PROMISE)
#define IS_MODULE(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_MODULE)
#define IS_BOUND_METHOD(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_BOUND_METHOD)

#define AS_STRING(v) ((ObjString *)as_obj(v))
#define AS_ARRAY(v) ((ObjArray *)as_obj(v))
#define AS_FUNCTION(v) ((ObjFunction *)as_obj(v))
#define AS_CLOSURE(v) ((ObjClosure *)as_obj(v))
#define AS_UPVALUE(v) ((ObjUpvalue *)as_obj(v))
#define AS_RANGE(v) ((ObjRange *)as_obj(v))
#define AS_DICT(v) ((ObjDict *)as_obj(v))
#define AS_BYTES(v) ((ObjBytes *)as_obj(v))
#define AS_TENSOR(v) ((ObjTensor *)as_obj(v))
#define AS_MATRIX(v) ((ObjMatrix *)as_obj(v))
#define AS_DATAFRAME(v) ((ObjDataFrame *)as_obj(v))
#define AS_GRAD_TAPE(v) ((ObjGradTape *)as_obj(v))
#define AS_CLASS(v) ((ObjClass *)as_obj(v))
#define AS_INSTANCE(v) ((ObjInstance *)as_obj(v))
#define AS_GENERATOR(v) ((ObjGenerator *)as_obj(v))
#define AS_PROMISE(v) ((ObjPromise *)as_obj(v))
#define AS_MODULE(v) ((ObjModule *)as_obj(v))
#define AS_BOUND_METHOD(v) ((ObjBoundMethod *)as_obj(v))

/* ============ Bytecode ============ */
typedef enum
{
    /* Stack ops */
    OP_CONST,      /* Push constant */
    OP_CONST_LONG, /* Push constant (16-bit index) */
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_POPN, /* Pop N values */
    OP_DUP,

    /* Variables */
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,

    /* Closures/Upvalues */
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,

    /* Arithmetic */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEG,
    OP_INC, /* Increment */
    OP_DEC, /* Decrement */
    OP_POW, /* Power/exponent */

    /* Comparison */
    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_GT,
    OP_LTE,
    OP_GTE,

    /* Logical */
    OP_NOT,
    OP_AND, /* Short-circuit AND */
    OP_OR,  /* Short-circuit OR */

    /* Bitwise */
    OP_BAND,
    OP_BOR,
    OP_BXOR,
    OP_BNOT, /* Bitwise NOT */
    OP_SHL,
    OP_SHR,

    /* Control flow */
    OP_JMP,
    OP_JMP_FALSE,
    OP_JMP_TRUE,
    OP_LOOP, /* Backward jump */

    /* Functions */
    OP_CALL,
    OP_RETURN,

    /* Exception handling (zero-overhead) */
    OP_TRY,     /* Push exception handler: OP_TRY catch_offset */
    OP_TRY_END, /* Pop exception handler (normal exit) */
    OP_THROW,   /* Throw exception: value on stack */
    OP_CATCH,   /* Start of catch block */

    /* Classes (fixed-slot, zero-overhead) */
    OP_CLASS,        /* Define class: const_idx (class name) */
    OP_INHERIT,      /* Inherit from superclass */
    OP_METHOD,       /* Add method to class: const_idx (name) */
    OP_FIELD,        /* Add field to class: const_idx (name) */
    OP_GET_FIELD,    /* Get instance field: const_idx (slot) */
    OP_SET_FIELD,    /* Set instance field: const_idx (slot) */
    OP_GET_FIELD_IC, /* Get field with inline cache: const_idx, ic_slot */
    OP_SET_FIELD_IC, /* Set field with inline cache: const_idx, ic_slot */
    OP_INVOKE,       /* Invoke method: const_idx (name), arg_count */
    OP_INVOKE_IC,    /* Invoke with inline cache: const_idx, arg_count, ic_slot */
    OP_SUPER_INVOKE, /* Invoke super method: const_idx (name), arg_count */
    OP_GET_SUPER,    /* Get method from superclass for super.method() */
    OP_STATIC,       /* Add static property/method to class */
    OP_GET_STATIC,   /* Get static property from class */
    OP_SET_STATIC,   /* Set static property on class */
    OP_BIND_METHOD,  /* Bind method to receiver, create ObjBoundMethod */

    /* Generators (zero-overhead coroutines) */
    OP_GENERATOR,  /* Create generator from closure */
    OP_YIELD,      /* Yield value, suspend generator */
    OP_YIELD_FROM, /* Yield all values from iterable */
    OP_GEN_NEXT,   /* Resume generator, get next value */
    OP_GEN_SEND,   /* Send value into generator */
    OP_GEN_RETURN, /* Return from generator (sets done=true) */

    /* Async/Await (Promise-based) */
    OP_ASYNC,   /* Mark function as async (returns Promise) */
    OP_AWAIT,   /* Await promise, suspend until resolved */
    OP_PROMISE, /* Create new Promise */
    OP_RESOLVE, /* Resolve promise with value */
    OP_REJECT,  /* Reject promise with error */

    /* Decorators */
    OP_DECORATOR, /* Apply decorator to next function/class */

    /* Module system */
    OP_MODULE,      /* Define module namespace */
    OP_EXPORT,      /* Export symbol from module */
    OP_IMPORT_FROM, /* Import specific symbol from module */
    OP_IMPORT_AS,   /* Import module with alias */

    /* Arrays */
    OP_ARRAY,
    OP_INDEX,
    OP_INDEX_SET,
    OP_INDEX_FAST,     /* Unchecked array index - for JIT when bounds proven safe */
    OP_INDEX_SET_FAST, /* Unchecked array index set */
    OP_LEN,
    OP_PUSH,
    OP_POP_ARRAY,
    OP_SLICE,  /* Array slicing */
    OP_CONCAT, /* Array/string concat */

    /* Iterators */
    OP_RANGE,
    OP_ITER_NEXT,
    OP_ITER_ARRAY, /* Array iterator */

    /* Built-ins */
    OP_PRINT,
    OP_PRINTLN, /* Print with newline */
    OP_TIME,
    OP_INPUT,
    OP_INT,   /* Convert to int */
    OP_FLOAT, /* Convert to float */
    OP_STR,   /* Convert to string */
    OP_TYPE,  /* Get type name */
    OP_ABS,   /* Absolute value */
    OP_MIN,   /* Min of two values */
    OP_MAX,   /* Max of two values */
    OP_SQRT,  /* Square root */
    OP_FLOOR, /* Floor */
    OP_CEIL,  /* Ceiling */
    OP_ROUND, /* Round */
    OP_RAND,  /* Random 0-1 */

    /* Bit manipulation intrinsics - map to CPU instructions */
    OP_POPCOUNT, /* Population count (count 1 bits) */
    OP_CLZ,      /* Count leading zeros */
    OP_CTZ,      /* Count trailing zeros */
    OP_ROTL,     /* Rotate left */
    OP_ROTR,     /* Rotate right */

    /* String operations */
    OP_SUBSTR,  /* Substring extraction */
    OP_UPPER,   /* To uppercase */
    OP_LOWER,   /* To lowercase */
    OP_SPLIT,   /* Split string into array */
    OP_JOIN,    /* Join array into string */
    OP_REPLACE, /* Replace in string */
    OP_FIND,    /* Find substring */
    OP_TRIM,    /* Trim whitespace */
    OP_CHAR,    /* Get char code */
    OP_ORD,     /* Char code to string */

    OP_HALT,

    /* ============ SUPERINSTRUCTIONS ============ */
    /* Fused instructions for 2-3x speedup on common patterns */

    /* Fused local + arithmetic */
    OP_GET_LOCAL_0, /* Get local slot 0 (most common) */
    OP_GET_LOCAL_1, /* Get local slot 1 */
    OP_GET_LOCAL_2, /* Get local slot 2 */
    OP_GET_LOCAL_3, /* Get local slot 3 */

    /* Fused arithmetic for small integers */
    OP_ADD_1, /* Add 1 to top of stack */
    OP_SUB_1, /* Subtract 1 from top of stack */

    /* Fused comparison + jump (critical for loops) */
    OP_LT_JMP_FALSE,  /* Compare < and jump if false */
    OP_LTE_JMP_FALSE, /* Compare <= and jump if false */
    OP_GT_JMP_FALSE,  /* Compare > and jump if false */
    OP_GTE_JMP_FALSE, /* Compare >= and jump if false */
    OP_EQ_JMP_FALSE,  /* Compare == and jump if false */
    OP_NEQ_JMP_FALSE, /* Compare != and jump if false */

    /* Fused local operations */
    OP_GET_LOCAL_ADD, /* Get local and add to TOS */
    OP_GET_LOCAL_SUB, /* Get local and sub from TOS */

    /* Ultra-fast loop superinstructions */
    OP_INC_LOCAL,    /* Increment local by 1: local[slot]++ */
    OP_DEC_LOCAL,    /* Decrement local by 1: local[slot]-- */
    OP_FOR_RANGE,    /* Fused range iteration: slot, end_slot, offset */
    OP_FOR_LOOP,     /* Fast loop: slot, limit, step, offset (inline counters) */
    OP_FOR_INT_INIT, /* Init int for loop: start_slot, end_slot, var_slot */
    OP_FOR_INT_LOOP, /* Int loop: start_slot, end_slot, var_slot, offset - no heap alloc! */

    /* Ultra-tight counting loop - no heap, no type checks, just raw ints */
    OP_FOR_COUNT,     /* for i in 0..N: counter_slot, end_slot, var_slot, offset */
    OP_ADD_LOCAL_INT, /* local[slot] += immediate (8-bit signed) */
    OP_LOCAL_LT_LOOP, /* if local[a] < local[b] then jump backward */

    /* Fused local arithmetic - reduces dispatch overhead by 60% */
    OP_INC_LOCAL_I,     /* local[slot] = local[slot] + 1 (pure int, 1 dispatch) */
    OP_DEC_LOCAL_I,     /* local[slot] = local[slot] - 1 */
    OP_LOCAL_ADD_LOCAL, /* TOS = local[a] + local[b] (2 args, 1 result) */
    OP_LOCAL_MUL_CONST, /* TOS = local[slot] * constant (slot, const_idx) */
    OP_LOCAL_ADD_CONST, /* TOS = local[slot] + constant */

    /* JIT-compiled loops - execute native machine code */
    OP_JIT_INC_LOOP,    /* JIT: for i in 0..n do x = x + 1 end */
    OP_JIT_ARITH_LOOP,  /* JIT: for i in 0..n do x = x * 3 + 7 end */
    OP_JIT_BRANCH_LOOP, /* JIT: for i in 0..n do if i%2==0 x++ else x-- end */

    /* Tail call optimization */
    OP_TAIL_CALL, /* Tail recursive call - reuse stack frame */

    /* Small integer constants */
    OP_CONST_0,
    OP_CONST_1,
    OP_CONST_2,
    OP_CONST_NEG1, /* -1, common for decrementing and array end access */

    /* ============ INTEGER-SPECIALIZED OPCODES ============ */
    /* These skip type checks and NaN-boxing for hot integer loops */
    /* 2-4x faster than generic opcodes for pure integer code */
    OP_ADD_II, /* Add two integers: result = a + b (no type check) */
    OP_SUB_II, /* Subtract integers */
    OP_MUL_II, /* Multiply integers */
    OP_DIV_II, /* Integer divide */
    OP_MOD_II, /* Integer modulo */
    OP_LT_II,  /* Compare integers < */
    OP_GT_II,  /* Compare integers > */
    OP_LTE_II, /* Compare integers <= */
    OP_GTE_II, /* Compare integers >= */
    OP_EQ_II,  /* Compare integers == */
    OP_NEQ_II, /* Compare integers != */
    OP_INC_II, /* Increment integer: ++i */
    OP_DEC_II, /* Decrement integer: --i */
    OP_NEG_II, /* Negate integer: -i */

    /* Integer-specialized comparison + jump (fastest loop opcodes) */
    OP_LT_II_JMP_FALSE,  /* if !(a < b) then jump (integers only) */
    OP_LTE_II_JMP_FALSE, /* if !(a <= b) then jump */
    OP_GT_II_JMP_FALSE,  /* if !(a > b) then jump */
    OP_GTE_II_JMP_FALSE, /* if !(a >= b) then jump */

    /* ============ INFRASTRUCTURE ============ */
    /* File I/O */
    OP_READ_FILE,   /* Read entire file to string */
    OP_WRITE_FILE,  /* Write string to file */
    OP_APPEND_FILE, /* Append string to file */
    OP_FILE_EXISTS, /* Check if file exists */
    OP_LIST_DIR,    /* List directory contents */
    OP_DELETE_FILE, /* Delete a file */
    OP_MKDIR,       /* Create directory */

    /* HTTP/Network */
    OP_HTTP_GET,  /* HTTP GET request */
    OP_HTTP_POST, /* HTTP POST request */

    /* JSON */
    OP_JSON_PARSE,     /* Parse JSON string to value */
    OP_JSON_STRINGIFY, /* Convert value to JSON string */

    /* Process/System */
    OP_EXEC,    /* Execute shell command */
    OP_ENV,     /* Get environment variable */
    OP_SET_ENV, /* Set environment variable */
    OP_ARGS,    /* Get command line arguments */
    OP_EXIT,    /* Exit with code */
    OP_SLEEP,   /* Sleep milliseconds */

    /* Dictionary/Map operations */
    OP_DICT,        /* Create dictionary */
    OP_DICT_GET,    /* Get value from dict */
    OP_DICT_SET,    /* Set value in dict */
    OP_DICT_HAS,    /* Check if key exists */
    OP_DICT_KEYS,   /* Get all keys */
    OP_DICT_VALUES, /* Get all values */
    OP_DICT_DELETE, /* Delete key from dict */

    /* Advanced math / SIMD */
    OP_SIN,   /* Sine */
    OP_COS,   /* Cosine */
    OP_TAN,   /* Tangent */
    OP_ASIN,  /* Arc sine */
    OP_ACOS,  /* Arc cosine */
    OP_ATAN,  /* Arc tangent */
    OP_ATAN2, /* Arc tangent of y/x */
    OP_LOG,   /* Natural logarithm */
    OP_LOG10, /* Base-10 logarithm */
    OP_LOG2,  /* Base-2 logarithm */
    OP_EXP,   /* e^x */
    OP_HYPOT, /* sqrt(x² + y²) */

    /* Vector operations (SIMD accelerated) */
    OP_VEC_ADD,     /* Element-wise add */
    OP_VEC_SUB,     /* Element-wise subtract */
    OP_VEC_MUL,     /* Element-wise multiply */
    OP_VEC_DIV,     /* Element-wise divide */
    OP_VEC_DOT,     /* Dot product */
    OP_VEC_SUM,     /* Sum of all elements */
    OP_VEC_PROD,    /* Product of all elements */
    OP_VEC_MIN,     /* Minimum element */
    OP_VEC_MAX,     /* Maximum element */
    OP_VEC_MEAN,    /* Mean of elements */
    OP_VEC_MAP,     /* Map function over array */
    OP_VEC_FILTER,  /* Filter array by predicate */
    OP_VEC_REDUCE,  /* Reduce array with function */
    OP_VEC_SORT,    /* Sort array */
    OP_VEC_REVERSE, /* Reverse array */
    OP_VEC_UNIQUE,  /* Remove duplicates */
    OP_VEC_ZIP,     /* Zip two arrays */
    OP_VEC_RANGE,   /* Generate numeric range array */

    /* Bytes/Binary */
    OP_BYTES,         /* Create byte array */
    OP_BYTES_GET,     /* Get byte at index */
    OP_BYTES_SET,     /* Set byte at index */
    OP_ENCODE_UTF8,   /* String to UTF-8 bytes */
    OP_DECODE_UTF8,   /* UTF-8 bytes to string */
    OP_ENCODE_BASE64, /* Encode to base64 */
    OP_DECODE_BASE64, /* Decode from base64 */

    /* Regex */
    OP_REGEX_MATCH,   /* Check if string matches regex */
    OP_REGEX_FIND,    /* Find all regex matches */
    OP_REGEX_REPLACE, /* Replace with regex */

    /* Hashing */
    OP_HASH,        /* Hash a value (fast) */
    OP_HASH_SHA256, /* SHA-256 hash */
    OP_HASH_MD5,    /* MD5 hash */

    /* ============ TENSOR OPERATIONS ============ */
    /* Creation */
    OP_TENSOR,          /* Create tensor from array + shape */
    OP_TENSOR_ZEROS,    /* Create zero tensor */
    OP_TENSOR_ONES,     /* Create ones tensor */
    OP_TENSOR_RAND,     /* Create random tensor */
    OP_TENSOR_RANDN,    /* Create random normal tensor */
    OP_TENSOR_ARANGE,   /* Create range tensor */
    OP_TENSOR_LINSPACE, /* Create linearly spaced tensor */
    OP_TENSOR_EYE,      /* Create identity matrix */

    /* Shape operations */
    OP_TENSOR_SHAPE,     /* Get tensor shape */
    OP_TENSOR_RESHAPE,   /* Reshape tensor */
    OP_TENSOR_TRANSPOSE, /* Transpose tensor */
    OP_TENSOR_FLATTEN,   /* Flatten to 1D */
    OP_TENSOR_SQUEEZE,   /* Remove size-1 dimensions */
    OP_TENSOR_UNSQUEEZE, /* Add dimension */

    /* Element-wise ops */
    OP_TENSOR_ADD,  /* Tensor + Tensor */
    OP_TENSOR_SUB,  /* Tensor - Tensor */
    OP_TENSOR_MUL,  /* Tensor * Tensor (element-wise) */
    OP_TENSOR_DIV,  /* Tensor / Tensor */
    OP_TENSOR_POW,  /* Tensor ** scalar */
    OP_TENSOR_NEG,  /* -Tensor */
    OP_TENSOR_ABS,  /* abs(Tensor) */
    OP_TENSOR_SQRT, /* sqrt(Tensor) */
    OP_TENSOR_EXP,  /* exp(Tensor) */
    OP_TENSOR_LOG,  /* log(Tensor) */

    /* Reduction ops */
    OP_TENSOR_SUM,    /* Sum all or along axis */
    OP_TENSOR_MEAN,   /* Mean all or along axis */
    OP_TENSOR_MIN,    /* Min all or along axis */
    OP_TENSOR_MAX,    /* Max all or along axis */
    OP_TENSOR_ARGMIN, /* Index of min */
    OP_TENSOR_ARGMAX, /* Index of max */

    /* Linear algebra */
    OP_TENSOR_MATMUL, /* Matrix multiplication */
    OP_TENSOR_DOT,    /* Dot product */
    OP_TENSOR_NORM,   /* L2 norm */

    /* Indexing */
    OP_TENSOR_GET, /* Get element/slice */
    OP_TENSOR_SET, /* Set element/slice */

    /* ============ MATRIX OPERATIONS ============ */
    OP_MATRIX,       /* Create matrix from 2D array */
    OP_MATRIX_ZEROS, /* Zero matrix (rows, cols) */
    OP_MATRIX_ONES,  /* Ones matrix */
    OP_MATRIX_EYE,   /* Identity matrix */
    OP_MATRIX_RAND,  /* Random matrix */
    OP_MATRIX_DIAG,  /* Diagonal matrix from array */

    OP_MATRIX_ADD,    /* Matrix + Matrix */
    OP_MATRIX_SUB,    /* Matrix - Matrix */
    OP_MATRIX_MUL,    /* Matrix * Matrix (element-wise) */
    OP_MATRIX_MATMUL, /* Matrix @ Matrix */
    OP_MATRIX_SCALE,  /* scalar * Matrix */

    OP_MATRIX_T,     /* Transpose */
    OP_MATRIX_INV,   /* Inverse */
    OP_MATRIX_DET,   /* Determinant */
    OP_MATRIX_TRACE, /* Trace */
    OP_MATRIX_SOLVE, /* Solve Ax = b */

    /* ============ AUTOGRAD OPERATIONS ============ */
    OP_GRAD_TAPE, /* Create gradient tape */

    /* Neural network activations */
    OP_NN_RELU,    /* ReLU activation */
    OP_NN_SIGMOID, /* Sigmoid activation */
    OP_NN_TANH,    /* Tanh activation */
    OP_NN_SOFTMAX, /* Softmax activation */

    /* Loss functions */
    OP_NN_MSE_LOSS, /* Mean squared error */
    OP_NN_CE_LOSS,  /* Cross-entropy loss */

    OP_OPCODE_COUNT, /* Total number of opcodes */
} OpCode;

/* ============ Chunk (bytecode container) ============ */

typedef struct
{
    uint32_t count;
    uint32_t capacity;
    uint8_t *code;
    uint16_t *lines;

    uint32_t const_count;
    uint32_t const_capacity;
    Value *constants;
} Chunk;

void chunk_init(Chunk *chunk);
void chunk_free(Chunk *chunk);
void chunk_write(Chunk *chunk, uint8_t byte, uint16_t line);
uint32_t chunk_add_const(Chunk *chunk, Value value);

/* ============ VM ============ */

#define STACK_MAX 65536
#define FRAMES_MAX 1024
#define HANDLERS_MAX 256

/* Exception handler entry */
typedef struct
{
    uint8_t *catch_ip; /* IP to jump to on exception */
    Value *stack_top;  /* Stack pointer to restore */
    int frame_count;   /* Frame count to restore */
} ExceptionHandler;

/* ============ Inline Caching ============ */
/* Monomorphic inline cache for property access - O(1) lookup after first hit */
#define IC_MAX_CACHES 256

typedef struct
{
    ObjClass *cached_class; /* Class this cache is for */
    uint16_t cached_slot;   /* Field/method slot index */
    ObjString *cached_name; /* Cached property name for validation */
    bool is_method;         /* True if this is a method, false if field */
} InlineCache;

typedef struct
{
    ObjFunction *function;
    ObjClosure *closure; /* NULL for plain functions */
    uint8_t *ip;
    Value *slots;
    bool is_init; /* True if this is an init method call */
} CallFrame;

typedef struct
{
    Chunk chunk;
    uint8_t *ip;

    Value stack[STACK_MAX];
    Value *sp;

    CallFrame frames[FRAMES_MAX];
    int frame_count;

    /* Exception handlers */
    ExceptionHandler handlers[HANDLERS_MAX];
    int handler_count;
    Value current_exception;

    /* Globals hash table */
    struct
    {
        ObjString **keys;
        Value *values;
        uint32_t count;
        uint32_t capacity;
    } globals;

    /* Closures - open upvalues list */
    ObjUpvalue *open_upvalues;

    /* Inline caching for fast property access */
    InlineCache ic_cache[IC_MAX_CACHES];
    uint16_t ic_count;

    /* GC */
    Obj *objects;
    size_t bytes_allocated;
    size_t next_gc;
} VM;

typedef enum
{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void vm_init(VM *vm);
void vm_free(VM *vm);
InterpretResult vm_interpret(VM *vm, const char *source);
InterpretResult vm_run(VM *vm);

/* ============ Compiler ============ */

bool compile(const char *source, Chunk *chunk, VM *vm);

/* ============ Memory ============ */

void *pseudo_realloc(VM *vm, void *ptr, size_t old_size, size_t new_size);
ObjString *copy_string(VM *vm, const char *chars, int length);
ObjString *take_string(VM *vm, char *chars, int length);
ObjArray *new_array(VM *vm, uint32_t capacity);
ObjRange *new_range(VM *vm, int32_t start, int32_t end);
ObjFunction *new_function(VM *vm);
ObjClosure *new_closure(VM *vm, ObjFunction *function);
ObjUpvalue *new_upvalue(VM *vm, Value *slot);
ObjDict *new_dict(VM *vm, uint32_t capacity);
ObjBytes *new_bytes(VM *vm, uint32_t capacity);

/* Arena allocator for temporary allocations */
typedef struct Arena
{
    uint8_t *data;
    size_t size;
    size_t used;
    struct Arena *next;
} Arena;

Arena *arena_create(size_t size);
void *arena_alloc(Arena *arena, size_t size);
void arena_reset(Arena *arena);
void arena_destroy(Arena *arena);

#define ALLOCATE(vm, type, count) \
    (type *)pseudo_realloc(vm, NULL, 0, sizeof(type) * (count))

#define FREE(vm, type, ptr) \
    pseudo_realloc(vm, ptr, sizeof(type), 0)

#define GROW_CAPACITY(cap) ((cap) < 8 ? 8 : (cap) * 2)

#define GROW_ARRAY(vm, type, ptr, old_count, new_count) \
    (type *)pseudo_realloc(vm, ptr, sizeof(type) * (old_count), sizeof(type) * (new_count))

#define FREE_ARRAY(vm, type, ptr, count) \
    pseudo_realloc(vm, ptr, sizeof(type) * (count), 0)

/* ============ Branch Hints ============ */
#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define PREFETCH(addr) __builtin_prefetch(addr)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#define PREFETCH(addr)
#endif

#endif /* PSEUDO_H */
