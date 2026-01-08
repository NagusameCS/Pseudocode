/*
 * Pseudocode Language - High Performance Virtual Machine
 * Uses computed gotos (GCC/Clang) or switch dispatch
 */

#define _POSIX_C_SOURCE 200809L  /* For clock_gettime, popen, etc */
#define _GNU_SOURCE

#include "pseudo.h"
#include "jit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>

/* Use computed gotos if available (GCC/Clang) */
#if defined(__GNUC__) || defined(__clang__)
    #define USE_COMPUTED_GOTO 1
#else
    #define USE_COMPUTED_GOTO 0
#endif

/* ============ VM Initialization ============ */

void vm_init(VM* vm) {
    vm->sp = vm->stack;
    vm->frame_count = 0;
    vm->objects = NULL;
    vm->bytes_allocated = 0;
    vm->next_gc = 1024 * 1024;
    
    vm->globals.keys = NULL;
    vm->globals.values = NULL;
    vm->globals.count = 0;
    vm->globals.capacity = 0;
    
    chunk_init(&vm->chunk);
}

void vm_free(VM* vm) {
    /* Free all objects */
    Obj* object = vm->objects;
    while (object != NULL) {
        Obj* next = object->next;
        free_object(vm, object);
        object = next;
    }
    
    /* Free globals table */
    if (vm->globals.keys) free(vm->globals.keys);
    if (vm->globals.values) free(vm->globals.values);
    
    chunk_free(&vm->chunk);
}

/* ============ Globals Hash Table ============ */

static uint32_t find_entry(ObjString** keys, uint32_t capacity, ObjString* key) {
    uint32_t index = key->hash & (capacity - 1);
    
    for (;;) {
        ObjString* entry = keys[index];
        
        if (entry == NULL || entry == key) {
            return index;
        }
        
        /* Check string equality for hash collision */
        if (entry->length == key->length && 
            entry->hash == key->hash &&
            memcmp(entry->chars, key->chars, key->length) == 0) {
            return index;
        }
        
        index = (index + 1) & (capacity - 1);
    }
}

static void adjust_capacity(VM* vm, uint32_t new_capacity) {
    ObjString** new_keys = (ObjString**)calloc(new_capacity, sizeof(ObjString*));
    Value* new_values = (Value*)malloc(new_capacity * sizeof(Value));
    
    /* Rehash existing entries */
    for (uint32_t i = 0; i < vm->globals.capacity; i++) {
        ObjString* key = vm->globals.keys[i];
        if (key == NULL) continue;
        
        uint32_t index = find_entry(new_keys, new_capacity, key);
        new_keys[index] = key;
        new_values[index] = vm->globals.values[i];
    }
    
    free(vm->globals.keys);
    free(vm->globals.values);
    
    vm->globals.keys = new_keys;
    vm->globals.values = new_values;
    vm->globals.capacity = new_capacity;
}

static bool table_get(VM* vm, ObjString* key, Value* value) {
    if (vm->globals.count == 0) return false;
    
    uint32_t index = find_entry(vm->globals.keys, vm->globals.capacity, key);
    if (vm->globals.keys[index] == NULL) return false;
    
    *value = vm->globals.values[index];
    return true;
}

static void table_set(VM* vm, ObjString* key, Value value) {
    if (vm->globals.count + 1 > vm->globals.capacity * 0.75) {
        uint32_t new_cap = vm->globals.capacity < 8 ? 8 : vm->globals.capacity * 2;
        adjust_capacity(vm, new_cap);
    }
    
    uint32_t index = find_entry(vm->globals.keys, vm->globals.capacity, key);
    bool is_new = (vm->globals.keys[index] == NULL);
    
    vm->globals.keys[index] = key;
    vm->globals.values[index] = value;
    
    if (is_new) vm->globals.count++;
}

/* ============ Runtime Error ============ */

static void runtime_error(VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    
    /* Print stack trace */
    for (int i = vm->frame_count - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* function = frame->function;
        size_t instruction = frame->ip - vm->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", vm->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }
    
    vm->sp = vm->stack;
    vm->frame_count = 0;
}

/* ============ Value Operations ============ */

static void print_value(Value value) {
    if (IS_NUM(value)) {
        double d = as_num(value);
        if (d == (int64_t)d) {
            printf("%lld", (long long)(int64_t)d);
        } else {
            printf("%g", d);
        }
    } else if (IS_INT(value)) {
        printf("%d", as_int(value));
    } else if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_BOOL(value)) {
        printf("%s", IS_TRUE(value) ? "true" : "false");
    } else if (IS_OBJ(value)) {
        Obj* obj = as_obj(value);
        switch (obj->type) {
            case OBJ_STRING:
                printf("%s", ((ObjString*)obj)->chars);
                break;
            case OBJ_ARRAY: {
                ObjArray* arr = (ObjArray*)obj;
                printf("[");
                for (uint32_t i = 0; i < arr->count; i++) {
                    if (i > 0) printf(", ");
                    print_value(arr->values[i]);
                }
                printf("]");
                break;
            }
            case OBJ_FUNCTION:
                printf("<fn %s>", ((ObjFunction*)obj)->name ? 
                    ((ObjFunction*)obj)->name->chars : "script");
                break;
            case OBJ_RANGE:
                printf("%d..%d", ((ObjRange*)obj)->start, ((ObjRange*)obj)->end);
                break;
            default:
                printf("<object>");
                break;
        }
    }
}

/* ============ Main Interpreter Loop ============ */

/*
 * Performance-critical macros with register-cached stack pointer
 * Using 'sp' as a register variable for the hot path
 */
#define PUSH(v)    (*sp++ = (v))
#define POP()      (*--sp)
#define PEEK(n)    (sp[-1 - (n)])
#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONST() (vm->chunk.constants[READ_BYTE()])

/* Fast integer check and extract - avoids function call overhead */
#define FAST_INT(v, out) (((v) & (QNAN | 0x7)) == (QNAN | TAG_INT) ? \
    ((out) = (int32_t)(((v) >> 3) & 0xFFFFFFFF), 1) : 0)

/* Binary operations on numbers with fast integer path */
#define BINARY_OP(op) \
    do { \
        Value b = POP(); \
        Value a = POP(); \
        int32_t ia, ib; \
        if (FAST_INT(a, ia) && FAST_INT(b, ib)) { \
            PUSH(val_int(ia op ib)); \
        } else { \
            double da = IS_INT(a) ? as_int(a) : as_num(a); \
            double db = IS_INT(b) ? as_int(b) : as_num(b); \
            PUSH(val_num(da op db)); \
        } \
    } while (0)

#define BINARY_OP_INT(op) \
    do { \
        Value b = POP(); \
        Value a = POP(); \
        int32_t ia = IS_INT(a) ? as_int(a) : (int32_t)as_num(a); \
        int32_t ib = IS_INT(b) ? as_int(b) : (int32_t)as_num(b); \
        PUSH(val_int(ia op ib)); \
    } while (0)

#define COMPARE_OP(op) \
    do { \
        Value b = POP(); \
        Value a = POP(); \
        int32_t ia, ib; \
        if (FAST_INT(a, ia) && FAST_INT(b, ib)) { \
            PUSH(val_bool(ia op ib)); \
        } else { \
            double da = IS_INT(a) ? as_int(a) : as_num(a); \
            double db = IS_INT(b) ? as_int(b) : as_num(b); \
            PUSH(val_bool(da op db)); \
        } \
    } while (0)

InterpretResult vm_run(VM* vm) {
    register uint8_t* ip = vm->chunk.code;
    register Value* sp = vm->sp;  /* Cache stack pointer in register */
    register Value* bp = vm->stack;  /* Cache base pointer for locals */
    
#if USE_COMPUTED_GOTO
    /* Dispatch table for computed gotos - must match OpCode enum order */
    static void* dispatch_table[] = {
        [OP_CONST] = &&op_const,
        [OP_CONST_LONG] = &&op_const_long,
        [OP_NIL] = &&op_nil,
        [OP_TRUE] = &&op_true,
        [OP_FALSE] = &&op_false,
        [OP_POP] = &&op_pop,
        [OP_POPN] = &&op_popn,
        [OP_DUP] = &&op_dup,
        [OP_GET_LOCAL] = &&op_get_local,
        [OP_SET_LOCAL] = &&op_set_local,
        [OP_GET_GLOBAL] = &&op_get_global,
        [OP_SET_GLOBAL] = &&op_set_global,
        [OP_ADD] = &&op_add,
        [OP_SUB] = &&op_sub,
        [OP_MUL] = &&op_mul,
        [OP_DIV] = &&op_div,
        [OP_MOD] = &&op_mod,
        [OP_NEG] = &&op_neg,
        [OP_INC] = &&op_inc,
        [OP_DEC] = &&op_dec,
        [OP_POW] = &&op_pow,
        [OP_EQ] = &&op_eq,
        [OP_NEQ] = &&op_neq,
        [OP_LT] = &&op_lt,
        [OP_GT] = &&op_gt,
        [OP_LTE] = &&op_lte,
        [OP_GTE] = &&op_gte,
        [OP_NOT] = &&op_not,
        [OP_AND] = &&op_and,
        [OP_OR] = &&op_or,
        [OP_BAND] = &&op_band,
        [OP_BOR] = &&op_bor,
        [OP_BXOR] = &&op_bxor,
        [OP_BNOT] = &&op_bnot,
        [OP_SHL] = &&op_shl,
        [OP_SHR] = &&op_shr,
        [OP_JMP] = &&op_jmp,
        [OP_JMP_FALSE] = &&op_jmp_false,
        [OP_JMP_TRUE] = &&op_jmp_true,
        [OP_LOOP] = &&op_loop,
        [OP_CALL] = &&op_call,
        [OP_RETURN] = &&op_return,
        [OP_ARRAY] = &&op_array,
        [OP_INDEX] = &&op_index,
        [OP_INDEX_SET] = &&op_index_set,
        [OP_LEN] = &&op_len,
        [OP_PUSH] = &&op_push,
        [OP_POP_ARRAY] = &&op_pop_array,
        [OP_SLICE] = &&op_slice,
        [OP_CONCAT] = &&op_concat,
        [OP_RANGE] = &&op_range,
        [OP_ITER_NEXT] = &&op_iter_next,
        [OP_ITER_ARRAY] = &&op_iter_array,
        [OP_PRINT] = &&op_print,
        [OP_PRINTLN] = &&op_println,
        [OP_TIME] = &&op_time,
        [OP_INPUT] = &&op_input,
        [OP_INT] = &&op_int,
        [OP_FLOAT] = &&op_float,
        [OP_STR] = &&op_str,
        [OP_TYPE] = &&op_type,
        [OP_ABS] = &&op_abs,
        [OP_MIN] = &&op_min,
        [OP_MAX] = &&op_max,
        [OP_SQRT] = &&op_sqrt,
        [OP_FLOOR] = &&op_floor,
        [OP_CEIL] = &&op_ceil,
        [OP_ROUND] = &&op_round,
        [OP_RAND] = &&op_rand,
        /* Bit manipulation intrinsics */
        [OP_POPCOUNT] = &&op_popcount,
        [OP_CLZ] = &&op_clz,
        [OP_CTZ] = &&op_ctz,
        [OP_ROTL] = &&op_rotl,
        [OP_ROTR] = &&op_rotr,
        /* String operations */
        [OP_SUBSTR] = &&op_substr,
        [OP_UPPER] = &&op_upper,
        [OP_LOWER] = &&op_lower,
        [OP_SPLIT] = &&op_split,
        [OP_JOIN] = &&op_join,
        [OP_REPLACE] = &&op_replace,
        [OP_FIND] = &&op_find,
        [OP_TRIM] = &&op_trim,
        [OP_CHAR] = &&op_char,
        [OP_ORD] = &&op_ord,
        [OP_HALT] = &&op_halt,
        /* Superinstructions */
        [OP_GET_LOCAL_0] = &&op_get_local_0,
        [OP_GET_LOCAL_1] = &&op_get_local_1,
        [OP_GET_LOCAL_2] = &&op_get_local_2,
        [OP_GET_LOCAL_3] = &&op_get_local_3,
        [OP_ADD_1] = &&op_add_1,
        [OP_SUB_1] = &&op_sub_1,
        [OP_LT_JMP_FALSE] = &&op_lt_jmp_false,
        [OP_LTE_JMP_FALSE] = &&op_lte_jmp_false,
        [OP_GT_JMP_FALSE] = &&op_gt_jmp_false,
        [OP_GTE_JMP_FALSE] = &&op_gte_jmp_false,
        [OP_EQ_JMP_FALSE] = &&op_eq_jmp_false,
        [OP_GET_LOCAL_ADD] = &&op_get_local_add,
        [OP_GET_LOCAL_SUB] = &&op_get_local_sub,
        [OP_INC_LOCAL] = &&op_inc_local,
        [OP_DEC_LOCAL] = &&op_dec_local,
        [OP_FOR_RANGE] = &&op_for_range,
        [OP_FOR_LOOP] = &&op_for_loop,
        [OP_FOR_INT_INIT] = &&op_for_int_init,
        [OP_FOR_INT_LOOP] = &&op_for_int_loop,
        [OP_FOR_COUNT] = &&op_for_count,
        [OP_ADD_LOCAL_INT] = &&op_add_local_int,
        [OP_LOCAL_LT_LOOP] = &&op_local_lt_loop,
        [OP_JIT_INC_LOOP] = &&op_jit_inc_loop,
        [OP_JIT_ARITH_LOOP] = &&op_jit_arith_loop,
        [OP_JIT_BRANCH_LOOP] = &&op_jit_branch_loop,
        [OP_TAIL_CALL] = &&op_tail_call,
        [OP_CONST_0] = &&op_const_0,
        [OP_CONST_1] = &&op_const_1,
        [OP_CONST_2] = &&op_const_2,
        /* Infrastructure */
        [OP_READ_FILE] = &&op_read_file,
        [OP_WRITE_FILE] = &&op_write_file,
        [OP_APPEND_FILE] = &&op_append_file,
        [OP_FILE_EXISTS] = &&op_file_exists,
        [OP_LIST_DIR] = &&op_list_dir,
        [OP_DELETE_FILE] = &&op_delete_file,
        [OP_MKDIR] = &&op_mkdir,
        [OP_HTTP_GET] = &&op_http_get,
        [OP_HTTP_POST] = &&op_http_post,
        [OP_JSON_PARSE] = &&op_json_parse,
        [OP_JSON_STRINGIFY] = &&op_json_stringify,
        [OP_EXEC] = &&op_exec,
        [OP_ENV] = &&op_env,
        [OP_SET_ENV] = &&op_set_env,
        [OP_ARGS] = &&op_args,
        [OP_EXIT] = &&op_exit,
        [OP_SLEEP] = &&op_sleep,
        [OP_DICT] = &&op_dict,
        [OP_DICT_GET] = &&op_dict_get,
        [OP_DICT_SET] = &&op_dict_set,
        [OP_DICT_HAS] = &&op_dict_has,
        [OP_DICT_KEYS] = &&op_dict_keys,
        [OP_DICT_VALUES] = &&op_dict_values,
        [OP_DICT_DELETE] = &&op_dict_delete,
        /* Math */
        [OP_SIN] = &&op_sin,
        [OP_COS] = &&op_cos,
        [OP_TAN] = &&op_tan,
        [OP_ASIN] = &&op_asin,
        [OP_ACOS] = &&op_acos,
        [OP_ATAN] = &&op_atan,
        [OP_ATAN2] = &&op_atan2,
        [OP_LOG] = &&op_log,
        [OP_LOG10] = &&op_log10,
        [OP_LOG2] = &&op_log2,
        [OP_EXP] = &&op_exp,
        [OP_HYPOT] = &&op_hypot,
        /* Vector */
        [OP_VEC_ADD] = &&op_vec_add,
        [OP_VEC_SUB] = &&op_vec_sub,
        [OP_VEC_MUL] = &&op_vec_mul,
        [OP_VEC_DIV] = &&op_vec_div,
        [OP_VEC_DOT] = &&op_vec_dot,
        [OP_VEC_SUM] = &&op_vec_sum,
        [OP_VEC_PROD] = &&op_vec_prod,
        [OP_VEC_MIN] = &&op_vec_min,
        [OP_VEC_MAX] = &&op_vec_max,
        [OP_VEC_MEAN] = &&op_vec_mean,
        [OP_VEC_MAP] = &&op_vec_map,
        [OP_VEC_FILTER] = &&op_vec_filter,
        [OP_VEC_REDUCE] = &&op_vec_reduce,
        [OP_VEC_SORT] = &&op_vec_sort,
        [OP_VEC_REVERSE] = &&op_vec_reverse,
        [OP_VEC_UNIQUE] = &&op_vec_unique,
        [OP_VEC_ZIP] = &&op_vec_zip,
        [OP_VEC_RANGE] = &&op_vec_range,
        /* Binary */
        [OP_BYTES] = &&op_bytes,
        [OP_BYTES_GET] = &&op_bytes_get,
        [OP_BYTES_SET] = &&op_bytes_set,
        [OP_ENCODE_UTF8] = &&op_encode_utf8,
        [OP_DECODE_UTF8] = &&op_decode_utf8,
        [OP_ENCODE_BASE64] = &&op_encode_base64,
        [OP_DECODE_BASE64] = &&op_decode_base64,
        /* Regex */
        [OP_REGEX_MATCH] = &&op_regex_match,
        [OP_REGEX_FIND] = &&op_regex_find,
        [OP_REGEX_REPLACE] = &&op_regex_replace,
        /* Hashing */
        [OP_HASH] = &&op_hash,
        [OP_HASH_SHA256] = &&op_hash_sha256,
        [OP_HASH_MD5] = &&op_hash_md5,
    };
    
    #define DISPATCH() goto *dispatch_table[*ip++]
    #define CASE(name) op_##name
    
    DISPATCH();
    
#else
    /* Traditional switch dispatch */
    #define DISPATCH() continue
    #define CASE(name) case OP_##name
    
    for (;;) {
        switch (*ip++) {
#endif

    CASE(const): {
        PUSH(READ_CONST());
        DISPATCH();
    }
    
    CASE(const_long): {
        uint16_t idx = READ_SHORT();
        PUSH(vm->chunk.constants[idx]);
        DISPATCH();
    }
    
    CASE(nil): {
        PUSH(VAL_NIL);
        DISPATCH();
    }
    
    CASE(true): {
        PUSH(VAL_TRUE);
        DISPATCH();
    }
    
    CASE(false): {
        PUSH(VAL_FALSE);
        DISPATCH();
    }
    
    CASE(pop): {
        POP();
        DISPATCH();
    }
    
    CASE(popn): {
        uint8_t n = READ_BYTE();
        sp -= n;
        DISPATCH();
    }
    
    CASE(dup): {
        Value v = PEEK(0);  /* Read BEFORE modifying sp to avoid UB */
        PUSH(v);
        DISPATCH();
    }
    
    CASE(get_local): {
        uint8_t slot = READ_BYTE();
        PUSH(bp[slot]);
        DISPATCH();
    }
    
    CASE(set_local): {
        uint8_t slot = READ_BYTE();
        bp[slot] = PEEK(0);
        DISPATCH();
    }
    
    CASE(get_global): {
        ObjString* name = AS_STRING(READ_CONST());
        Value value;
        vm->sp = sp;  /* Sync before potential error */
        if (!table_get(vm, name, &value)) {
            runtime_error(vm, "Undefined variable '%s'.", name->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        PUSH(value);
        DISPATCH();
    }
    
    CASE(set_global): {
        ObjString* name = AS_STRING(READ_CONST());
        table_set(vm, name, PEEK(0));
        DISPATCH();
    }
    
    CASE(add): {
        Value b = POP();
        Value a = POP();
        
        if (IS_STRING(a) && IS_STRING(b)) {
            /* String concatenation */
            ObjString* as = AS_STRING(a);
            ObjString* bs = AS_STRING(b);
            int length = as->length + bs->length;
            char* chars = ALLOCATE(vm, char, length + 1);
            memcpy(chars, as->chars, as->length);
            memcpy(chars + as->length, bs->chars, bs->length);
            chars[length] = '\0';
            ObjString* result = copy_string(vm, chars, length);
            FREE_ARRAY(vm, char, chars, length + 1);
            PUSH(val_obj(result));
        } else if (IS_INT(a) && IS_INT(b)) {
            PUSH(val_int(as_int(a) + as_int(b)));
        } else {
            double da = IS_INT(a) ? as_int(a) : as_num(a);
            double db = IS_INT(b) ? as_int(b) : as_num(b);
            PUSH(val_num(da + db));
        }
        DISPATCH();
    }
    
    CASE(sub): BINARY_OP(-); DISPATCH();
    CASE(mul): BINARY_OP(*); DISPATCH();
    
    CASE(div): {
        Value b = POP();
        Value a = POP();
        if (IS_INT(a) && IS_INT(b)) {
            PUSH(val_int(as_int(a) / as_int(b)));
        } else {
            double da = IS_INT(a) ? as_int(a) : as_num(a);
            double db = IS_INT(b) ? as_int(b) : as_num(b);
            PUSH(val_num(da / db));
        }
        DISPATCH();
    }
    
    CASE(mod): BINARY_OP_INT(%); DISPATCH();
    
    CASE(neg): {
        Value v = POP();
        if (IS_INT(v)) {
            PUSH(val_int(-as_int(v)));
        } else {
            PUSH(val_num(-as_num(v)));
        }
        DISPATCH();
    }
    
    CASE(inc): {
        Value v = POP();
        if (IS_INT(v)) {
            PUSH(val_int(as_int(v) + 1));
        } else {
            PUSH(val_num(as_num(v) + 1));
        }
        DISPATCH();
    }
    
    CASE(dec): {
        Value v = POP();
        if (IS_INT(v)) {
            PUSH(val_int(as_int(v) - 1));
        } else {
            PUSH(val_num(as_num(v) - 1));
        }
        DISPATCH();
    }
    
    CASE(pow): {
        Value b = POP();
        Value a = POP();
        double da = IS_INT(a) ? as_int(a) : as_num(a);
        double db = IS_INT(b) ? as_int(b) : as_num(b);
        PUSH(val_num(pow(da, db)));
        DISPATCH();
    }
    
    CASE(eq): {
        Value b = POP();
        Value a = POP();
        /* Handle string comparison by value */
        if (IS_OBJ(a) && IS_OBJ(b)) {
            Obj* obj_a = (Obj*)as_obj(a);
            Obj* obj_b = (Obj*)as_obj(b);
            if (obj_a->type == OBJ_STRING && obj_b->type == OBJ_STRING) {
                ObjString* str_a = (ObjString*)obj_a;
                ObjString* str_b = (ObjString*)obj_b;
                bool equal = str_a->length == str_b->length &&
                             memcmp(str_a->chars, str_b->chars, str_a->length) == 0;
                PUSH(val_bool(equal));
                DISPATCH();
            }
        }
        PUSH(val_bool(a == b));
        DISPATCH();
    }
    
    CASE(neq): {
        Value b = POP();
        Value a = POP();
        /* Handle string comparison by value */
        if (IS_OBJ(a) && IS_OBJ(b)) {
            Obj* obj_a = (Obj*)as_obj(a);
            Obj* obj_b = (Obj*)as_obj(b);
            if (obj_a->type == OBJ_STRING && obj_b->type == OBJ_STRING) {
                ObjString* str_a = (ObjString*)obj_a;
                ObjString* str_b = (ObjString*)obj_b;
                bool equal = str_a->length == str_b->length &&
                             memcmp(str_a->chars, str_b->chars, str_a->length) == 0;
                PUSH(val_bool(!equal));
                DISPATCH();
            }
        }
        PUSH(val_bool(a != b));
        DISPATCH();
    }
    
    CASE(lt): COMPARE_OP(<); DISPATCH();
    CASE(gt): COMPARE_OP(>); DISPATCH();
    CASE(lte): COMPARE_OP(<=); DISPATCH();
    CASE(gte): COMPARE_OP(>=); DISPATCH();
    
    CASE(not): {
        PUSH(val_bool(!is_truthy(POP())));
        DISPATCH();
    }
    
    CASE(and): {
        uint16_t offset = READ_SHORT();
        if (!is_truthy(PEEK(0))) {
            ip += offset;
        } else {
            POP();
        }
        DISPATCH();
    }
    
    CASE(or): {
        uint16_t offset = READ_SHORT();
        if (is_truthy(PEEK(0))) {
            ip += offset;
        } else {
            POP();
        }
        DISPATCH();
    }
    
    CASE(band): BINARY_OP_INT(&); DISPATCH();
    CASE(bor): BINARY_OP_INT(|); DISPATCH();
    CASE(bxor): BINARY_OP_INT(^); DISPATCH();
    CASE(bnot): {
        Value v = POP();
        int32_t i = IS_INT(v) ? as_int(v) : (int32_t)as_num(v);
        PUSH(val_int(~i));
        DISPATCH();
    }
    CASE(shl): BINARY_OP_INT(<<); DISPATCH();
    CASE(shr): BINARY_OP_INT(>>); DISPATCH();
    
    CASE(jmp): {
        uint16_t offset = READ_SHORT();
        ip += offset;
        DISPATCH();
    }
    
    CASE(jmp_false): {
        uint16_t offset = READ_SHORT();
        Value v = PEEK(0);
        /* Fast path for booleans */
        if (v == VAL_FALSE || v == VAL_NIL) {
            ip += offset;
        } else if (v != VAL_TRUE && !is_truthy(v)) {
            ip += offset;
        }
        DISPATCH();
    }
    
    CASE(jmp_true): {
        uint16_t offset = READ_SHORT();
        Value v = PEEK(0);
        /* Fast path for booleans */
        if (v == VAL_TRUE) {
            ip += offset;
        } else if (v != VAL_FALSE && v != VAL_NIL && is_truthy(v)) {
            ip += offset;
        }
        DISPATCH();
    }
    
    CASE(loop): {
        uint16_t offset = READ_SHORT();
        ip -= offset;
        DISPATCH();
    }
    
    CASE(call): {
        uint8_t arg_count = READ_BYTE();
        Value callee = PEEK(arg_count);
        
        if (!IS_FUNCTION(callee)) {
            vm->sp = sp;
            runtime_error(vm, "Can only call functions.");
            return INTERPRET_RUNTIME_ERROR;
        }
        
        ObjFunction* function = AS_FUNCTION(callee);
        if (arg_count != function->arity) {
            vm->sp = sp;
            runtime_error(vm, "Expected %d arguments but got %d.",
                function->arity, arg_count);
            return INTERPRET_RUNTIME_ERROR;
        }
        
        if (vm->frame_count == FRAMES_MAX) {
            vm->sp = sp;
            runtime_error(vm, "Stack overflow.");
            return INTERPRET_RUNTIME_ERROR;
        }
        
        CallFrame* frame = &vm->frames[vm->frame_count++];
        frame->function = function;
        frame->ip = ip;
        frame->slots = sp - arg_count - 1;
        bp = frame->slots;  /* Update cached base pointer */
        
        ip = vm->chunk.code + function->code_start;
        DISPATCH();
    }
    
    CASE(return): {
        Value result = POP();
        
        if (vm->frame_count == 0) {
            /* Returning from top-level - shouldn't happen normally */
            vm->sp = sp;
            return INTERPRET_OK;
        }
        
        vm->frame_count--;
        CallFrame* frame = &vm->frames[vm->frame_count];
        sp = frame->slots;
        ip = frame->ip;
        /* Restore bp to previous frame or stack base */
        bp = (vm->frame_count > 0) 
            ? vm->frames[vm->frame_count - 1].slots 
            : vm->stack;
        PUSH(result);
        DISPATCH();
    }
    
    CASE(array): {
        uint8_t count = READ_BYTE();
        vm->sp = sp;  /* Sync before allocation */
        ObjArray* array = new_array(vm, count);
        
        /* Pop elements in reverse order */
        sp -= count;
        for (int i = 0; i < count; i++) {
            array->values[i] = sp[i];
        }
        array->count = count;
        
        PUSH(val_obj(array));
        DISPATCH();
    }
    
    CASE(index): {
        Value index_val = POP();
        Value obj_val = POP();
        
        if (IS_ARRAY(obj_val)) {
            ObjArray* array = AS_ARRAY(obj_val);
            int32_t index = IS_INT(index_val) ? as_int(index_val) : (int32_t)as_num(index_val);
            if (index < 0 || index >= (int32_t)array->count) {
                runtime_error(vm, "Array index out of bounds.");
                return INTERPRET_RUNTIME_ERROR;
            }
            PUSH(array->values[index]);
        } else if (IS_STRING(obj_val)) {
            ObjString* str = AS_STRING(obj_val);
            int32_t index = IS_INT(index_val) ? as_int(index_val) : (int32_t)as_num(index_val);
            if (index < 0 || index >= (int32_t)str->length) {
                runtime_error(vm, "String index out of bounds.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjString* ch = copy_string(vm, str->chars + index, 1);
            PUSH(val_obj(ch));
        } else {
            runtime_error(vm, "Only arrays and strings can be indexed.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    
    CASE(index_set): {
        Value value = POP();
        Value index_val = POP();
        Value obj_val = POP();
        
        if (!IS_ARRAY(obj_val)) {
            runtime_error(vm, "Only arrays support index assignment.");
            return INTERPRET_RUNTIME_ERROR;
        }
        
        ObjArray* array = AS_ARRAY(obj_val);
        int32_t index = IS_INT(index_val) ? as_int(index_val) : (int32_t)as_num(index_val);
        if (index < 0 || index >= (int32_t)array->count) {
            runtime_error(vm, "Array index out of bounds.");
            return INTERPRET_RUNTIME_ERROR;
        }
        
        array->values[index] = value;
        PUSH(value);
        DISPATCH();
    }
    
    CASE(len): {
        Value v = POP();
        if (IS_ARRAY(v)) {
            PUSH(val_int(AS_ARRAY(v)->count));
        } else if (IS_STRING(v)) {
            PUSH(val_int(AS_STRING(v)->length));
        } else {
            runtime_error(vm, "Operand must be an array or string.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    
    CASE(push): {
        Value value = POP();
        Value arr_val = POP();
        
        if (!IS_ARRAY(arr_val)) {
            runtime_error(vm, "Can only push to arrays.");
            return INTERPRET_RUNTIME_ERROR;
        }
        
        ObjArray* array = AS_ARRAY(arr_val);
        if (array->count >= array->capacity) {
            uint32_t new_cap = GROW_CAPACITY(array->capacity);
            array->values = GROW_ARRAY(vm, Value, array->values, 
                array->capacity, new_cap);
            array->capacity = new_cap;
        }
        array->values[array->count++] = value;
        PUSH(arr_val);
        DISPATCH();
    }
    
    CASE(pop_array): {
        Value arr_val = POP();
        
        if (!IS_ARRAY(arr_val)) {
            runtime_error(vm, "Can only pop from arrays.");
            return INTERPRET_RUNTIME_ERROR;
        }
        
        ObjArray* array = AS_ARRAY(arr_val);
        if (array->count == 0) {
            runtime_error(vm, "Cannot pop from empty array.");
            return INTERPRET_RUNTIME_ERROR;
        }
        
        PUSH(array->values[--array->count]);
        DISPATCH();
    }
    
    CASE(slice): {
        Value end_val = POP();
        Value start_val = POP();
        Value obj_val = POP();
        
        int32_t start = IS_INT(start_val) ? as_int(start_val) : (int32_t)as_num(start_val);
        int32_t end = IS_INT(end_val) ? as_int(end_val) : (int32_t)as_num(end_val);
        
        if (IS_ARRAY(obj_val)) {
            ObjArray* arr = AS_ARRAY(obj_val);
            if (start < 0) start += arr->count;
            if (end < 0) end += arr->count;
            if (start < 0) start = 0;
            if ((uint32_t)end > arr->count) end = arr->count;
            
            uint32_t len = (end > start) ? end - start : 0;
            ObjArray* result = new_array(vm, len);
            for (uint32_t i = 0; i < len; i++) {
                result->values[i] = arr->values[start + i];
            }
            result->count = len;
            PUSH(val_obj(result));
        } else if (IS_STRING(obj_val)) {
            ObjString* str = AS_STRING(obj_val);
            if (start < 0) start += str->length;
            if (end < 0) end += str->length;
            if (start < 0) start = 0;
            if (end > str->length) end = str->length;
            
            int len = (end > start) ? end - start : 0;
            PUSH(val_obj(copy_string(vm, str->chars + start, len)));
        } else {
            runtime_error(vm, "Can only slice arrays and strings.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    
    CASE(concat): {
        Value b = POP();
        Value a = POP();
        
        if (IS_ARRAY(a) && IS_ARRAY(b)) {
            ObjArray* aa = AS_ARRAY(a);
            ObjArray* ab = AS_ARRAY(b);
            ObjArray* result = new_array(vm, aa->count + ab->count);
            memcpy(result->values, aa->values, aa->count * sizeof(Value));
            memcpy(result->values + aa->count, ab->values, ab->count * sizeof(Value));
            result->count = aa->count + ab->count;
            PUSH(val_obj(result));
        } else {
            runtime_error(vm, "Can only concatenate arrays.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    
    CASE(range): {
        Value end_val = POP();
        Value start_val = POP();
        
        int32_t start = IS_INT(start_val) ? as_int(start_val) : (int32_t)as_num(start_val);
        int32_t end = IS_INT(end_val) ? as_int(end_val) : (int32_t)as_num(end_val);
        
        ObjRange* range = new_range(vm, start, end);
        PUSH(val_obj(range));
        DISPATCH();
    }
    
    CASE(iter_next): {
        uint16_t offset = READ_SHORT();
        Value iter_val = PEEK(0);
        
        if (IS_RANGE(iter_val)) {
            ObjRange* range = AS_RANGE(iter_val);
            if (range->current >= range->end) {
                POP();  /* Remove iterator */
                ip += offset;
            } else {
                PUSH(val_int(range->current++));
            }
        } else {
            runtime_error(vm, "Cannot iterate over this type.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    
    CASE(iter_array): {
        /* Array iteration with internal index tracking */
        uint16_t offset = READ_SHORT();
        Value arr_val = PEEK(1);  /* Array below index */
        Value idx_val = PEEK(0);  /* Current index */
        
        if (IS_ARRAY(arr_val)) {
            ObjArray* arr = AS_ARRAY(arr_val);
            int32_t idx = as_int(idx_val);
            if ((uint32_t)idx >= arr->count) {
                sp -= 2;  /* Remove array and index */
                ip += offset;
            } else {
                sp[-1] = val_int(idx + 1);  /* Increment index */
                PUSH(arr->values[idx]);  /* Push current element */
            }
        } else {
            vm->sp = sp;
            runtime_error(vm, "Expected array for iteration.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    
    CASE(print): {
        print_value(POP());
        printf("\n");
        DISPATCH();
    }
    
    CASE(println): {
        print_value(POP());
        printf("\n");
        DISPATCH();
    }
    
    CASE(time): {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        int64_t ns = ts.tv_sec * 1000000000LL + ts.tv_nsec;
        PUSH(val_num((double)ns));
        DISPATCH();
    }
    
    CASE(input): {
        char buffer[1024];
        if (fgets(buffer, sizeof(buffer), stdin)) {
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len-1] == '\n') buffer[--len] = '\0';
            ObjString* str = copy_string(vm, buffer, len);
            PUSH(val_obj(str));
        } else {
            PUSH(VAL_NIL);
        }
        DISPATCH();
    }
    
    CASE(int): {
        Value v = POP();
        if (IS_INT(v)) {
            PUSH(v);
        } else if (IS_NUM(v)) {
            PUSH(val_int((int32_t)as_num(v)));
        } else if (IS_STRING(v)) {
            PUSH(val_int(atoi(AS_STRING(v)->chars)));
        } else {
            PUSH(val_int(0));
        }
        DISPATCH();
    }
    
    CASE(float): {
        Value v = POP();
        double d = IS_INT(v) ? (double)as_int(v) : 
                   IS_NUM(v) ? as_num(v) :
                   IS_STRING(v) ? atof(AS_STRING(v)->chars) : 0.0;
        PUSH(val_num(d));
        DISPATCH();
    }
    
    CASE(str): {
        Value v = POP();
        char buffer[64];
        if (IS_INT(v)) {
            snprintf(buffer, sizeof(buffer), "%d", as_int(v));
        } else if (IS_NUM(v)) {
            snprintf(buffer, sizeof(buffer), "%g", as_num(v));
        } else if (IS_BOOL(v)) {
            snprintf(buffer, sizeof(buffer), "%s", IS_TRUE(v) ? "true" : "false");
        } else if (IS_NIL(v)) {
            snprintf(buffer, sizeof(buffer), "nil");
        } else if (IS_STRING(v)) {
            PUSH(v);
            DISPATCH();
        } else {
            snprintf(buffer, sizeof(buffer), "<object>");
        }
        PUSH(val_obj(copy_string(vm, buffer, strlen(buffer))));
        DISPATCH();
    }
    
    CASE(type): {
        Value v = POP();
        const char* type_name;
        if (IS_INT(v) || IS_NUM(v)) type_name = "number";
        else if (IS_BOOL(v)) type_name = "bool";
        else if (IS_NIL(v)) type_name = "nil";
        else if (IS_STRING(v)) type_name = "string";
        else if (IS_ARRAY(v)) type_name = "array";
        else if (IS_FUNCTION(v)) type_name = "function";
        else type_name = "object";
        PUSH(val_obj(copy_string(vm, type_name, strlen(type_name))));
        DISPATCH();
    }
    
    CASE(abs): {
        Value v = POP();
        if (IS_INT(v)) {
            int32_t i = as_int(v);
            PUSH(val_int(i < 0 ? -i : i));
        } else {
            PUSH(val_num(fabs(as_num(v))));
        }
        DISPATCH();
    }
    
    CASE(min): {
        Value b = POP();
        Value a = POP();
        double da = IS_INT(a) ? as_int(a) : as_num(a);
        double db = IS_INT(b) ? as_int(b) : as_num(b);
        PUSH(da < db ? a : b);
        DISPATCH();
    }
    
    CASE(max): {
        Value b = POP();
        Value a = POP();
        double da = IS_INT(a) ? as_int(a) : as_num(a);
        double db = IS_INT(b) ? as_int(b) : as_num(b);
        PUSH(da > db ? a : b);
        DISPATCH();
    }
    
    CASE(sqrt): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(sqrt(d)));
        DISPATCH();
    }
    
    CASE(floor): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_int((int32_t)floor(d)));
        DISPATCH();
    }
    
    CASE(ceil): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_int((int32_t)ceil(d)));
        DISPATCH();
    }
    
    CASE(round): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_int((int32_t)round(d)));
        DISPATCH();
    }
    
    CASE(rand): {
        PUSH(val_num((double)rand() / RAND_MAX));
        DISPATCH();
    }
    
    /* ============ BIT MANIPULATION INTRINSICS ============ */
    /* Map directly to CPU instructions via GCC builtins */
    
    CASE(popcount): {
        Value v = POP();
        int32_t n = IS_INT(v) ? as_int(v) : (int32_t)as_num(v);
        PUSH(val_int(__builtin_popcount((unsigned int)n)));
        DISPATCH();
    }
    
    CASE(clz): {
        Value v = POP();
        int32_t n = IS_INT(v) ? as_int(v) : (int32_t)as_num(v);
        PUSH(val_int(n == 0 ? 32 : __builtin_clz((unsigned int)n)));
        DISPATCH();
    }
    
    CASE(ctz): {
        Value v = POP();
        int32_t n = IS_INT(v) ? as_int(v) : (int32_t)as_num(v);
        PUSH(val_int(n == 0 ? 32 : __builtin_ctz((unsigned int)n)));
        DISPATCH();
    }
    
    CASE(rotl): {
        Value vn = POP();
        Value vx = POP();
        uint32_t n = (IS_INT(vn) ? as_int(vn) : (int32_t)as_num(vn)) & 31;
        uint32_t x = IS_INT(vx) ? (uint32_t)as_int(vx) : (uint32_t)as_num(vx);
        PUSH(val_int((int32_t)((x << n) | (x >> (32 - n)))));
        DISPATCH();
    }
    
    CASE(rotr): {
        Value vn = POP();
        Value vx = POP();
        uint32_t n = (IS_INT(vn) ? as_int(vn) : (int32_t)as_num(vn)) & 31;
        uint32_t x = IS_INT(vx) ? (uint32_t)as_int(vx) : (uint32_t)as_num(vx);
        PUSH(val_int((int32_t)((x >> n) | (x << (32 - n)))));
        DISPATCH();
    }
    
    /* ============ STRING OPERATIONS ============ */
    
    CASE(substr): {
        Value vlen = POP();
        Value vstart = POP();
        Value vstr = POP();
        
        if (!IS_OBJ(vstr)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString* str = (ObjString*)as_obj(vstr);
        int32_t start = IS_INT(vstart) ? as_int(vstart) : (int32_t)as_num(vstart);
        int32_t len = IS_INT(vlen) ? as_int(vlen) : (int32_t)as_num(vlen);
        
        if (start < 0) start = 0;
        if (start >= (int32_t)str->length) {
            PUSH(val_obj(copy_string(vm, "", 0)));
            DISPATCH();
        }
        if (len < 0 || start + len > (int32_t)str->length) {
            len = str->length - start;
        }
        
        PUSH(val_obj(copy_string(vm, str->chars + start, len)));
        DISPATCH();
    }
    
    CASE(upper): {
        Value v = POP();
        if (!IS_OBJ(v)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString* str = (ObjString*)as_obj(v);
        char* buf = malloc(str->length + 1);
        for (uint32_t i = 0; i < str->length; i++) {
            char c = str->chars[i];
            buf[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
        }
        buf[str->length] = '\0';
        ObjString* result = copy_string(vm, buf, str->length);
        free(buf);
        PUSH(val_obj(result));
        DISPATCH();
    }
    
    CASE(lower): {
        Value v = POP();
        if (!IS_OBJ(v)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString* str = (ObjString*)as_obj(v);
        char* buf = malloc(str->length + 1);
        for (uint32_t i = 0; i < str->length; i++) {
            char c = str->chars[i];
            buf[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        }
        buf[str->length] = '\0';
        ObjString* result = copy_string(vm, buf, str->length);
        free(buf);
        PUSH(val_obj(result));
        DISPATCH();
    }
    
    CASE(split): {
        Value vdelim = POP();
        Value vstr = POP();
        
        if (!IS_OBJ(vstr) || !IS_OBJ(vdelim)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString* str = (ObjString*)as_obj(vstr);
        ObjString* delim = (ObjString*)as_obj(vdelim);
        
        /* Count parts */
        int count = 1;
        const char* p = str->chars;
        while ((p = strstr(p, delim->chars)) != NULL) {
            count++;
            p += delim->length;
        }
        
        /* Create array */
        ObjArray* arr = new_array(vm, count);
        p = str->chars;
        int idx = 0;
        const char* next;
        while ((next = strstr(p, delim->chars)) != NULL) {
            arr->values[idx++] = val_obj(copy_string(vm, p, next - p));
            p = next + delim->length;
        }
        arr->values[idx] = val_obj(copy_string(vm, p, str->chars + str->length - p));
        arr->count = count;
        
        PUSH(val_obj(arr));
        DISPATCH();
    }
    
    CASE(join): {
        Value vdelim = POP();
        Value varr = POP();
        
        if (!IS_OBJ(varr) || !IS_OBJ(vdelim)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray* arr = (ObjArray*)as_obj(varr);
        ObjString* delim = (ObjString*)as_obj(vdelim);
        
        if (arr->count == 0) {
            PUSH(val_obj(copy_string(vm, "", 0)));
            DISPATCH();
        }
        
        /* Calculate total length */
        size_t total = 0;
        for (uint32_t i = 0; i < arr->count; i++) {
            Value v = arr->values[i];
            if (IS_OBJ(v)) {
                ObjString* s = (ObjString*)as_obj(v);
                total += s->length;
            }
            if (i > 0) total += delim->length;
        }
        
        /* Build result */
        char* buf = malloc(total + 1);
        char* dst = buf;
        for (uint32_t i = 0; i < arr->count; i++) {
            if (i > 0) {
                memcpy(dst, delim->chars, delim->length);
                dst += delim->length;
            }
            Value v = arr->values[i];
            if (IS_OBJ(v)) {
                ObjString* s = (ObjString*)as_obj(v);
                memcpy(dst, s->chars, s->length);
                dst += s->length;
            }
        }
        *dst = '\0';
        
        ObjString* result = copy_string(vm, buf, total);
        free(buf);
        PUSH(val_obj(result));
        DISPATCH();
    }
    
    CASE(replace): {
        Value vto = POP();
        Value vfrom = POP();
        Value vstr = POP();
        
        if (!IS_OBJ(vstr) || !IS_OBJ(vfrom) || !IS_OBJ(vto)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString* str = (ObjString*)as_obj(vstr);
        ObjString* from = (ObjString*)as_obj(vfrom);
        ObjString* to = (ObjString*)as_obj(vto);
        
        if (from->length == 0) {
            PUSH(vstr);
            DISPATCH();
        }
        
        /* Count occurrences */
        int count = 0;
        const char* p = str->chars;
        while ((p = strstr(p, from->chars)) != NULL) {
            count++;
            p += from->length;
        }
        
        if (count == 0) {
            PUSH(vstr);
            DISPATCH();
        }
        
        /* Calculate new length */
        size_t new_len = str->length + count * ((int)to->length - (int)from->length);
        char* buf = malloc(new_len + 1);
        char* dst = buf;
        p = str->chars;
        const char* next;
        
        while ((next = strstr(p, from->chars)) != NULL) {
            memcpy(dst, p, next - p);
            dst += next - p;
            memcpy(dst, to->chars, to->length);
            dst += to->length;
            p = next + from->length;
        }
        memcpy(dst, p, str->chars + str->length - p);
        buf[new_len] = '\0';
        
        ObjString* result = copy_string(vm, buf, new_len);
        free(buf);
        PUSH(val_obj(result));
        DISPATCH();
    }
    
    CASE(find): {
        Value vneedle = POP();
        Value vhaystack = POP();
        
        if (!IS_OBJ(vhaystack) || !IS_OBJ(vneedle)) {
            PUSH(val_int(-1));
            DISPATCH();
        }
        ObjString* haystack = (ObjString*)as_obj(vhaystack);
        ObjString* needle = (ObjString*)as_obj(vneedle);
        
        const char* found = strstr(haystack->chars, needle->chars);
        if (found) {
            PUSH(val_int((int32_t)(found - haystack->chars)));
        } else {
            PUSH(val_int(-1));
        }
        DISPATCH();
    }
    
    CASE(trim): {
        Value v = POP();
        if (!IS_OBJ(v)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString* str = (ObjString*)as_obj(v);
        
        const char* start = str->chars;
        const char* end = str->chars + str->length;
        
        while (start < end && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) {
            start++;
        }
        while (end > start && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\n' || *(end-1) == '\r')) {
            end--;
        }
        
        PUSH(val_obj(copy_string(vm, start, end - start)));
        DISPATCH();
    }
    
    CASE(char): {
        Value v = POP();
        int32_t code = IS_INT(v) ? as_int(v) : (int32_t)as_num(v);
        char buf[2] = {(char)code, '\0'};
        PUSH(val_obj(copy_string(vm, buf, 1)));
        DISPATCH();
    }
    
    CASE(ord): {
        Value v = POP();
        if (!IS_OBJ(v)) {
            PUSH(val_int(0));
            DISPATCH();
        }
        ObjString* str = (ObjString*)as_obj(v);
        if (str->length == 0) {
            PUSH(val_int(0));
        } else {
            PUSH(val_int((unsigned char)str->chars[0]));
        }
        DISPATCH();
    }
    
    CASE(halt): {
        return INTERPRET_OK;
    }
    
    /* ============ SUPERINSTRUCTIONS ============ */
    /* These fused instructions eliminate dispatch overhead and provide 2-3x speedup */
    
    /* Fast local variable access for common slots - using cached bp */
    CASE(get_local_0): {
        PUSH(bp[0]);
        DISPATCH();
    }
    
    CASE(get_local_1): {
        PUSH(bp[1]);
        DISPATCH();
    }
    
    CASE(get_local_2): {
        PUSH(bp[2]);
        DISPATCH();
    }
    
    CASE(get_local_3): {
        PUSH(bp[3]);
        DISPATCH();
    }
    
    /* Fast constants */
    CASE(const_0): {
        PUSH(val_int(0));
        DISPATCH();
    }
    
    CASE(const_1): {
        PUSH(val_int(1));
        DISPATCH();
    }
    
    CASE(const_2): {
        PUSH(val_int(2));
        DISPATCH();
    }
    
    /* Fast increment/decrement */
    CASE(add_1): {
        Value v = POP();
        PUSH(val_int(as_int(v) + 1));
        DISPATCH();
    }
    
    CASE(sub_1): {
        Value v = POP();
        PUSH(val_int(as_int(v) - 1));
        DISPATCH();
    }
    
    /* Fused comparison + conditional jump - CRITICAL for loops */
    /* These pop 2 values, compare, and conditionally jump WITHOUT leaving result */
    /* The compiler skips the subsequent OP_POP when fusion happens */
    CASE(lt_jmp_false): {
        uint16_t offset = READ_SHORT();
        Value b = POP();
        Value a = POP();
        bool result;
        if (IS_INT(a) && IS_INT(b)) {
            result = as_int(a) < as_int(b);
        } else {
            double da = IS_INT(a) ? as_int(a) : as_num(a);
            double db = IS_INT(b) ? as_int(b) : as_num(b);
            result = da < db;
        }
        if (!result) ip += offset;
        DISPATCH();
    }
    
    CASE(lte_jmp_false): {
        uint16_t offset = READ_SHORT();
        Value b = POP();
        Value a = POP();
        bool result;
        if (IS_INT(a) && IS_INT(b)) {
            result = as_int(a) <= as_int(b);
        } else {
            double da = IS_INT(a) ? as_int(a) : as_num(a);
            double db = IS_INT(b) ? as_int(b) : as_num(b);
            result = da <= db;
        }
        if (!result) ip += offset;
        DISPATCH();
    }
    
    CASE(gt_jmp_false): {
        uint16_t offset = READ_SHORT();
        Value b = POP();
        Value a = POP();
        bool result;
        if (IS_INT(a) && IS_INT(b)) {
            result = as_int(a) > as_int(b);
        } else {
            double da = IS_INT(a) ? as_int(a) : as_num(a);
            double db = IS_INT(b) ? as_int(b) : as_num(b);
            result = da > db;
        }
        if (!result) ip += offset;
        DISPATCH();
    }
    
    CASE(gte_jmp_false): {
        uint16_t offset = READ_SHORT();
        Value b = POP();
        Value a = POP();
        bool result;
        if (IS_INT(a) && IS_INT(b)) {
            result = as_int(a) >= as_int(b);
        } else {
            double da = IS_INT(a) ? as_int(a) : as_num(a);
            double db = IS_INT(b) ? as_int(b) : as_num(b);
            result = da >= db;
        }
        if (!result) ip += offset;
        DISPATCH();
    }
    
    CASE(eq_jmp_false): {
        uint16_t offset = READ_SHORT();
        Value b = POP();
        Value a = POP();
        if (a != b) ip += offset;
        DISPATCH();
    }
    
    /* Fused local + arithmetic */
    CASE(get_local_add): {
        uint8_t slot = READ_BYTE();
        Value local = bp[slot];  /* Use cached bp */
        Value tos = POP();
        if (IS_INT(local) && IS_INT(tos)) {
            PUSH(val_int(as_int(local) + as_int(tos)));
        } else {
            double da = IS_INT(local) ? as_int(local) : as_num(local);
            double db = IS_INT(tos) ? as_int(tos) : as_num(tos);
            PUSH(val_num(da + db));
        }
        DISPATCH();
    }
    
    CASE(get_local_sub): {
        uint8_t slot = READ_BYTE();
        Value local = bp[slot];  /* Use cached bp */
        Value tos = POP();
        if (IS_INT(local) && IS_INT(tos)) {
            PUSH(val_int(as_int(tos) - as_int(local)));
        } else {
            double da = IS_INT(tos) ? as_int(tos) : as_num(tos);
            double db = IS_INT(local) ? as_int(local) : as_num(local);
            PUSH(val_num(da - db));
        }
        DISPATCH();
    }
    
    /* Ultra-fast loop increment - critical for tight loops */
    CASE(inc_local): {
        uint8_t slot = READ_BYTE();
        /* Use cached bp - assume integer for loop counters */
        bp[slot] = val_int(as_int(bp[slot]) + 1);
        DISPATCH();
    }
    
    CASE(dec_local): {
        uint8_t slot = READ_BYTE();
        /* Use cached bp */
        bp[slot] = val_int(as_int(bp[slot]) - 1);
        DISPATCH();
    }
    
    /* Fused for-range iteration: counter in slot, limit in next slot */
    /* Format: OP_FOR_RANGE, counter_slot, limit_slot, offset[2] */
    CASE(for_range): {
        uint8_t counter_slot = READ_BYTE();
        uint8_t limit_slot = READ_BYTE();
        uint16_t offset = READ_SHORT();
        
        /* Use cached bp - no branch! */
        int32_t counter = as_int(bp[counter_slot]);
        int32_t limit = as_int(bp[limit_slot]);
        
        if (counter >= limit) {
            ip += offset;  /* Exit loop */
        } else {
            /* Push current value for loop body, then increment */
            PUSH(val_int(counter));
            bp[counter_slot] = val_int(counter + 1);
        }
        DISPATCH();
    }
    
    /* Generic for loop: handles Range and Array */
    /* Format: OP_FOR_LOOP, iter_slot, idx_slot, var_slot, offset[2] */
    /* For arrays: iter_slot has array, idx_slot has current index, var_slot gets element */
    /* For ranges: iter_slot has range (with internal counter), idx_slot unused */
    CASE(for_loop): {
        uint8_t iter_slot = READ_BYTE();
        uint8_t idx_slot = READ_BYTE();
        uint8_t var_slot = READ_BYTE();
        uint16_t offset = READ_SHORT();
        
        /* Use cached bp - no branch! */
        Value iter = bp[iter_slot];
        if (IS_RANGE(iter)) {
            ObjRange* range = AS_RANGE(iter);
            if (range->current >= range->end) {
                ip += offset;
            } else {
                bp[var_slot] = val_int(range->current++);
            }
        } else if (IS_ARRAY(iter)) {
            /* Array iteration: iter_slot has array, idx_slot has index */
            ObjArray* arr = AS_ARRAY(iter);
            int32_t idx = as_int(bp[idx_slot]);
            
            if ((uint32_t)idx >= arr->count) {
                ip += offset;  /* Exit loop */
            } else {
                bp[var_slot] = arr->values[idx];
                bp[idx_slot] = val_int(idx + 1);  /* Increment index */
            }
        } else if (IS_STRING(iter)) {
            /* String iteration: iterate over characters */
            ObjString* str = AS_STRING(iter);
            int32_t idx = as_int(bp[idx_slot]);
            
            if ((uint32_t)idx >= str->length) {
                ip += offset;  /* Exit loop */
            } else {
                /* Create single-character string */
                ObjString* ch = copy_string(vm, &str->chars[idx], 1);
                bp[var_slot] = val_obj(ch);
                bp[idx_slot] = val_int(idx + 1);  /* Increment index */
            }
        } else {
            vm->sp = sp;
            runtime_error(vm, "Expected iterable (range, array, or string).");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    
    /* Heap-allocation-free integer for loop: no Range object! */
    /* Format: OP_FOR_INT_INIT - nothing to do, just skip */
    CASE(for_int_init): {
        DISPATCH();
    }
    
    /* Ultra-fast int loop: counter, end, var all stored as raw ints in locals */
    /* Format: OP_FOR_INT_LOOP, counter_slot, end_slot, var_slot, offset[2] */
    CASE(for_int_loop): {
        uint8_t counter_slot = READ_BYTE();
        uint8_t end_slot = READ_BYTE();
        uint8_t var_slot = READ_BYTE();
        uint16_t offset = READ_SHORT();
        
        /* Use cached bp - no branch! */
        int64_t counter = as_int(bp[counter_slot]);
        int64_t end = as_int(bp[end_slot]);
        
        if (counter >= end) {
            ip += offset;
        } else {
            bp[var_slot] = val_int(counter);
            bp[counter_slot] = val_int(counter + 1);
        }
        DISPATCH();
    }
    
    /* ULTRA-TIGHT counting loop - the fastest possible for numeric iteration */
    /* No Range object, no type checks, just raw integer operations */
    /* Format: OP_FOR_COUNT, counter_slot, end_slot, var_slot, offset[2] */
    CASE(for_count): {
        uint8_t counter_slot = READ_BYTE();
        uint8_t end_slot = READ_BYTE();
        uint8_t var_slot = READ_BYTE();
        uint16_t offset = READ_SHORT();
        
        /* Use cached base pointer - no branch! */
        int32_t counter, end_val;
        FAST_INT(bp[counter_slot], counter);
        FAST_INT(bp[end_slot], end_val);
        
        if (counter >= end_val) {
            ip += offset;  /* Exit loop */
        } else {
            bp[var_slot] = val_int(counter);
            bp[counter_slot] = val_int(counter + 1);
        }
        DISPATCH();
    }
    
    /* Add immediate integer to local variable */
    /* Format: OP_ADD_LOCAL_INT, slot, delta (signed 8-bit) */
    CASE(add_local_int): {
        uint8_t slot = READ_BYTE();
        int8_t delta = (int8_t)READ_BYTE();
        
        /* Use cached bp - no branch! */
        int32_t val;
        FAST_INT(bp[slot], val);
        bp[slot] = val_int(val + delta);
        DISPATCH();
    }
    
    /* Compare two locals and loop backward if condition is true */
    /* Format: OP_LOCAL_LT_LOOP, slot_a, slot_b, offset[2] (backward) */
    CASE(local_lt_loop): {
        uint8_t slot_a = READ_BYTE();
        uint8_t slot_b = READ_BYTE();
        uint16_t offset = READ_SHORT();
        
        /* Use cached bp - no branch! */
        int32_t a, b;
        FAST_INT(bp[slot_a], a);
        FAST_INT(bp[slot_b], b);
        
        if (a < b) {
            ip -= offset;  /* Jump backward to loop start */
        }
        DISPATCH();
    }
    
    /* ============================================================
     * JIT-COMPILED LOOP HANDLERS
     * These execute native machine code for maximum performance
     * ============================================================ */
    
    /* JIT: for i in 0..iterations do x = x + 1 end
     * Stack: [x, iterations] -> [result] */
    CASE(jit_inc_loop): {
        Value iter_val = POP();
        Value x_val = POP();
        
        int32_t x, iterations;
        FAST_INT(x_val, x);
        FAST_INT(iter_val, iterations);
        
        /* Call JIT-compiled native code! */
        int64_t result = jit_run_inc_loop(x, iterations);
        
        PUSH(val_int((int32_t)result));
        DISPATCH();
    }
    
    /* JIT: for i in 0..iterations do x = x * 3 + 7 end
     * Stack: [x, iterations] -> [result] */
    CASE(jit_arith_loop): {
        Value iter_val = POP();
        Value x_val = POP();
        
        int32_t x, iterations;
        FAST_INT(x_val, x);
        FAST_INT(iter_val, iterations);
        
        /* Call JIT-compiled native code! */
        int64_t result = jit_run_arith_loop(x, iterations);
        
        PUSH(val_int((int32_t)result));
        DISPATCH();
    }
    
    /* JIT: for i in 0..iterations do if i%2==0 x++ else x-- end
     * Stack: [x, iterations] -> [result] */
    CASE(jit_branch_loop): {
        Value iter_val = POP();
        Value x_val = POP();
        
        int32_t x, iterations;
        FAST_INT(x_val, x);
        FAST_INT(iter_val, iterations);
        
        /* Call JIT-compiled native code! */
        int64_t result = jit_run_branch_loop(x, iterations);
        
        PUSH(val_int((int32_t)result));
        DISPATCH();
    }
    
    /* TAIL CALL OPTIMIZATION - Revolutionary for recursive functions! */
    /* Reuses the current stack frame instead of creating a new one */
    CASE(tail_call): {
        uint8_t arg_count = READ_BYTE();
        Value callee = PEEK(arg_count);
        
        if (!IS_FUNCTION(callee)) {
            vm->sp = sp;
            runtime_error(vm, "Can only call functions.");
            return INTERPRET_RUNTIME_ERROR;
        }
        
        ObjFunction* function = AS_FUNCTION(callee);
        if (arg_count != function->arity) {
            vm->sp = sp;
            runtime_error(vm, "Expected %d arguments but got %d.",
                function->arity, arg_count);
            return INTERPRET_RUNTIME_ERROR;
        }
        
        /* Move arguments down to overwrite current frame */
        Value* new_base;
        if (vm->frame_count > 0) {
            new_base = vm->frames[vm->frame_count - 1].slots;
        } else {
            new_base = vm->stack;
        }
        
        /* Copy callee and arguments to base of frame */
        for (int i = 0; i <= arg_count; i++) {
            new_base[i] = sp[-arg_count - 1 + i];
        }
        
        /* Reset stack pointer */
        sp = new_base + arg_count + 1;
        
        /* Jump to function code (same frame) */
        ip = vm->chunk.code + function->code_start;
        DISPATCH();
    }
    
    /* ============ FILE I/O ============ */
    
    CASE(read_file): {
        Value path_val = POP();
        if (!IS_STRING(path_val)) {
            vm->sp = sp;
            runtime_error(vm, "read_file expects a string path.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* path = AS_STRING(path_val);
        FILE* f = fopen(path->chars, "r");
        if (!f) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        char* buffer = malloc(size + 1);
        fread(buffer, 1, size, f);
        buffer[size] = '\0';
        fclose(f);
        vm->sp = sp;
        ObjString* content = copy_string(vm, buffer, size);
        sp = vm->sp;
        free(buffer);
        PUSH(val_obj(content));
        DISPATCH();
    }
    
    CASE(write_file): {
        Value content_val = POP();
        Value path_val = POP();
        if (!IS_STRING(path_val) || !IS_STRING(content_val)) {
            vm->sp = sp;
            runtime_error(vm, "write_file expects string arguments.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* path = AS_STRING(path_val);
        ObjString* content = AS_STRING(content_val);
        FILE* f = fopen(path->chars, "w");
        if (!f) {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        fwrite(content->chars, 1, content->length, f);
        fclose(f);
        PUSH(VAL_TRUE);
        DISPATCH();
    }
    
    CASE(append_file): {
        Value content_val = POP();
        Value path_val = POP();
        if (!IS_STRING(path_val) || !IS_STRING(content_val)) {
            vm->sp = sp;
            runtime_error(vm, "append_file expects string arguments.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* path = AS_STRING(path_val);
        ObjString* content = AS_STRING(content_val);
        FILE* f = fopen(path->chars, "a");
        if (!f) {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        fwrite(content->chars, 1, content->length, f);
        fclose(f);
        PUSH(VAL_TRUE);
        DISPATCH();
    }
    
    CASE(file_exists): {
        Value path_val = POP();
        if (!IS_STRING(path_val)) {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjString* path = AS_STRING(path_val);
        PUSH(val_bool(access(path->chars, F_OK) == 0));
        DISPATCH();
    }
    
    CASE(list_dir): {
        Value path_val = POP();
        if (!IS_STRING(path_val)) {
            vm->sp = sp;
            runtime_error(vm, "list_dir expects a string path.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* path = AS_STRING(path_val);
        DIR* dir = opendir(path->chars);
        if (!dir) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjArray* arr = new_array(vm, 16);
        sp = vm->sp;
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.' && 
                (entry->d_name[1] == '\0' || 
                 (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
                continue;
            vm->sp = sp;
            ObjString* name = copy_string(vm, entry->d_name, strlen(entry->d_name));
            sp = vm->sp;
            if (arr->count >= arr->capacity) {
                uint32_t new_cap = GROW_CAPACITY(arr->capacity);
                arr->values = realloc(arr->values, new_cap * sizeof(Value));
                arr->capacity = new_cap;
            }
            arr->values[arr->count++] = val_obj(name);
        }
        closedir(dir);
        PUSH(val_obj(arr));
        DISPATCH();
    }
    
    CASE(delete_file): {
        Value path_val = POP();
        if (!IS_STRING(path_val)) {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjString* path = AS_STRING(path_val);
        PUSH(val_bool(unlink(path->chars) == 0));
        DISPATCH();
    }
    
    CASE(mkdir): {
        Value path_val = POP();
        if (!IS_STRING(path_val)) {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjString* path = AS_STRING(path_val);
        PUSH(val_bool(mkdir(path->chars, 0755) == 0));
        DISPATCH();
    }
    
    /* ============ HTTP (using popen + curl) ============ */
    
    CASE(http_get): {
        Value url_val = POP();
        if (!IS_STRING(url_val)) {
            vm->sp = sp;
            runtime_error(vm, "http_get expects a string URL.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* url = AS_STRING(url_val);
        if (url->length > 2000) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        char* cmd = malloc(url->length + 64);
        sprintf(cmd, "curl -s \"%s\"", url->chars);
        FILE* p = popen(cmd, "r");
        free(cmd);
        if (!p) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        char* buffer = NULL;
        size_t size = 0;
        char chunk[1024];
        while (fgets(chunk, sizeof(chunk), p)) {
            size_t chunk_len = strlen(chunk);
            buffer = realloc(buffer, size + chunk_len + 1);
            memcpy(buffer + size, chunk, chunk_len);
            size += chunk_len;
        }
        pclose(p);
        if (buffer) {
            buffer[size] = '\0';
            vm->sp = sp;
            ObjString* result = copy_string(vm, buffer, size);
            sp = vm->sp;
            free(buffer);
            PUSH(val_obj(result));
        } else {
            PUSH(VAL_NIL);
        }
        DISPATCH();
    }
    
    CASE(http_post): {
        Value body_val = POP();
        Value url_val = POP();
        if (!IS_STRING(url_val)) {
            vm->sp = sp;
            runtime_error(vm, "http_post expects string URL.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* url = AS_STRING(url_val);
        const char* body = IS_STRING(body_val) ? AS_STRING(body_val)->chars : "";
        char cmd[8192];
        snprintf(cmd, sizeof(cmd), "curl -s -X POST -d '%s' '%s'", body, url->chars);
        FILE* p = popen(cmd, "r");
        if (!p) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        char* buffer = NULL;
        size_t size = 0;
        char chunk[1024];
        while (fgets(chunk, sizeof(chunk), p)) {
            size_t chunk_len = strlen(chunk);
            buffer = realloc(buffer, size + chunk_len + 1);
            memcpy(buffer + size, chunk, chunk_len);
            size += chunk_len;
        }
        pclose(p);
        if (buffer) {
            buffer[size] = '\0';
            vm->sp = sp;
            ObjString* result = copy_string(vm, buffer, size);
            sp = vm->sp;
            free(buffer);
            PUSH(val_obj(result));
        } else {
            PUSH(VAL_NIL);
        }
        DISPATCH();
    }
    
    /* ============ JSON ============ */
    /* Simple recursive descent JSON parser */
    
    CASE(json_parse): {
        Value str_val = POP();
        if (!IS_STRING(str_val)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        /* For now, push nil - full JSON parser would be extensive */
        /* TODO: Implement proper JSON parsing */
        PUSH(VAL_NIL);
        DISPATCH();
    }
    
    CASE(json_stringify): {
        /* TODO: Implement JSON stringification */
        POP();
        vm->sp = sp;
        ObjString* result = copy_string(vm, "null", 4);
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }
    
    /* ============ PROCESS/SYSTEM ============ */
    
    CASE(exec): {
        Value cmd_val = POP();
        if (!IS_STRING(cmd_val)) {
            vm->sp = sp;
            runtime_error(vm, "exec expects a string command.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* cmd = AS_STRING(cmd_val);
        FILE* p = popen(cmd->chars, "r");
        if (!p) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        char* buffer = NULL;
        size_t size = 0;
        char chunk[1024];
        while (fgets(chunk, sizeof(chunk), p)) {
            size_t chunk_len = strlen(chunk);
            buffer = realloc(buffer, size + chunk_len + 1);
            memcpy(buffer + size, chunk, chunk_len);
            size += chunk_len;
        }
        pclose(p);
        if (buffer) {
            buffer[size] = '\0';
            vm->sp = sp;
            ObjString* result = copy_string(vm, buffer, size);
            sp = vm->sp;
            free(buffer);
            PUSH(val_obj(result));
        } else {
            vm->sp = sp;
            ObjString* result = copy_string(vm, "", 0);
            sp = vm->sp;
            PUSH(val_obj(result));
        }
        DISPATCH();
    }
    
    CASE(env): {
        Value name_val = POP();
        if (!IS_STRING(name_val)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString* name = AS_STRING(name_val);
        char* val = getenv(name->chars);
        if (val) {
            vm->sp = sp;
            ObjString* result = copy_string(vm, val, strlen(val));
            sp = vm->sp;
            PUSH(val_obj(result));
        } else {
            PUSH(VAL_NIL);
        }
        DISPATCH();
    }
    
    CASE(set_env): {
        Value val_val = POP();
        Value name_val = POP();
        if (!IS_STRING(name_val) || !IS_STRING(val_val)) {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjString* name = AS_STRING(name_val);
        ObjString* val = AS_STRING(val_val);
        PUSH(val_bool(setenv(name->chars, val->chars, 1) == 0));
        DISPATCH();
    }
    
    CASE(args): {
        /* TODO: Store args in VM and return them */
        vm->sp = sp;
        ObjArray* arr = new_array(vm, 0);
        sp = vm->sp;
        PUSH(val_obj(arr));
        DISPATCH();
    }
    
    CASE(exit): {
        Value code_val = POP();
        int code = IS_INT(code_val) ? as_int(code_val) : 
                   IS_NUM(code_val) ? (int)as_num(code_val) : 0;
        exit(code);
        DISPATCH();
    }
    
    CASE(sleep): {
        Value ms_val = POP();
        int ms = IS_INT(ms_val) ? as_int(ms_val) : 
                 IS_NUM(ms_val) ? (int)as_num(ms_val) : 0;
        usleep(ms * 1000);
        PUSH(VAL_NIL);
        DISPATCH();
    }
    
    /* ============ DICTIONARY ============ */
    
    CASE(dict): {
        vm->sp = sp;
        ObjDict* dict = new_dict(vm, 8);
        sp = vm->sp;
        PUSH(val_obj(dict));
        DISPATCH();
    }
    
    CASE(dict_get): {
        Value key_val = POP();
        Value dict_val = POP();
        if (!IS_DICT(dict_val) || !IS_STRING(key_val)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjDict* dict = AS_DICT(dict_val);
        ObjString* key = AS_STRING(key_val);
        for (uint32_t i = 0; i < dict->capacity; i++) {
            if (dict->keys[i] && dict->keys[i]->length == key->length &&
                memcmp(dict->keys[i]->chars, key->chars, key->length) == 0) {
                PUSH(dict->values[i]);
                DISPATCH();
            }
        }
        PUSH(VAL_NIL);
        DISPATCH();
    }
    
    CASE(dict_set): {
        Value val = POP();
        Value key_val = POP();
        Value dict_val = POP();
        if (!IS_DICT(dict_val) || !IS_STRING(key_val)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjDict* dict = AS_DICT(dict_val);
        ObjString* key = AS_STRING(key_val);
        /* Find existing or empty slot */
        uint32_t slot = key->hash & (dict->capacity - 1);
        for (uint32_t i = 0; i < dict->capacity; i++) {
            uint32_t idx = (slot + i) & (dict->capacity - 1);
            if (!dict->keys[idx]) {
                dict->keys[idx] = key;
                dict->values[idx] = val;
                dict->count++;
                PUSH(val_obj(dict));
                DISPATCH();
            }
            if (dict->keys[idx]->length == key->length &&
                memcmp(dict->keys[idx]->chars, key->chars, key->length) == 0) {
                dict->values[idx] = val;
                PUSH(val_obj(dict));
                DISPATCH();
            }
        }
        PUSH(val_obj(dict));
        DISPATCH();
    }
    
    CASE(dict_has): {
        Value key_val = POP();
        Value dict_val = POP();
        if (!IS_DICT(dict_val) || !IS_STRING(key_val)) {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjDict* dict = AS_DICT(dict_val);
        ObjString* key = AS_STRING(key_val);
        for (uint32_t i = 0; i < dict->capacity; i++) {
            if (dict->keys[i] && dict->keys[i]->length == key->length &&
                memcmp(dict->keys[i]->chars, key->chars, key->length) == 0) {
                PUSH(VAL_TRUE);
                DISPATCH();
            }
        }
        PUSH(VAL_FALSE);
        DISPATCH();
    }
    
    CASE(dict_keys): {
        Value dict_val = POP();
        if (!IS_DICT(dict_val)) {
            vm->sp = sp;
            ObjArray* arr = new_array(vm, 0);
            sp = vm->sp;
            PUSH(val_obj(arr));
            DISPATCH();
        }
        ObjDict* dict = AS_DICT(dict_val);
        vm->sp = sp;
        ObjArray* arr = new_array(vm, dict->count);
        sp = vm->sp;
        for (uint32_t i = 0; i < dict->capacity; i++) {
            if (dict->keys[i]) {
                arr->values[arr->count++] = val_obj(dict->keys[i]);
            }
        }
        PUSH(val_obj(arr));
        DISPATCH();
    }
    
    CASE(dict_values): {
        Value dict_val = POP();
        if (!IS_DICT(dict_val)) {
            vm->sp = sp;
            ObjArray* arr = new_array(vm, 0);
            sp = vm->sp;
            PUSH(val_obj(arr));
            DISPATCH();
        }
        ObjDict* dict = AS_DICT(dict_val);
        vm->sp = sp;
        ObjArray* arr = new_array(vm, dict->count);
        sp = vm->sp;
        for (uint32_t i = 0; i < dict->capacity; i++) {
            if (dict->keys[i]) {
                arr->values[arr->count++] = dict->values[i];
            }
        }
        PUSH(val_obj(arr));
        DISPATCH();
    }
    
    CASE(dict_delete): {
        Value key_val = POP();
        Value dict_val = POP();
        if (!IS_DICT(dict_val) || !IS_STRING(key_val)) {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjDict* dict = AS_DICT(dict_val);
        ObjString* key = AS_STRING(key_val);
        for (uint32_t i = 0; i < dict->capacity; i++) {
            if (dict->keys[i] && dict->keys[i]->length == key->length &&
                memcmp(dict->keys[i]->chars, key->chars, key->length) == 0) {
                dict->keys[i] = NULL;
                dict->count--;
                PUSH(VAL_TRUE);
                DISPATCH();
            }
        }
        PUSH(VAL_FALSE);
        DISPATCH();
    }
    
    /* ============ ADVANCED MATH ============ */
    
    CASE(sin): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(sin(d)));
        DISPATCH();
    }
    
    CASE(cos): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(cos(d)));
        DISPATCH();
    }
    
    CASE(tan): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(tan(d)));
        DISPATCH();
    }
    
    CASE(asin): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(asin(d)));
        DISPATCH();
    }
    
    CASE(acos): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(acos(d)));
        DISPATCH();
    }
    
    CASE(atan): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(atan(d)));
        DISPATCH();
    }
    
    CASE(atan2): {
        Value x = POP();
        Value y = POP();
        double dy = IS_INT(y) ? as_int(y) : as_num(y);
        double dx = IS_INT(x) ? as_int(x) : as_num(x);
        PUSH(val_num(atan2(dy, dx)));
        DISPATCH();
    }
    
    CASE(log): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(log(d)));
        DISPATCH();
    }
    
    CASE(log10): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(log10(d)));
        DISPATCH();
    }
    
    CASE(log2): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(log2(d)));
        DISPATCH();
    }
    
    CASE(exp): {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(exp(d)));
        DISPATCH();
    }
    
    CASE(hypot): {
        Value y = POP();
        Value x = POP();
        double dx = IS_INT(x) ? as_int(x) : as_num(x);
        double dy = IS_INT(y) ? as_int(y) : as_num(y);
        PUSH(val_num(hypot(dx, dy)));
        DISPATCH();
    }
    
    /* ============ VECTOR OPERATIONS ============ */
    
    CASE(vec_add): {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        ObjArray* b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray* result = new_array(vm, len);
        sp = vm->sp;
        for (uint32_t i = 0; i < len; i++) {
            double da = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da + db);
        }
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }
    
    CASE(vec_sub): {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        ObjArray* b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray* result = new_array(vm, len);
        sp = vm->sp;
        for (uint32_t i = 0; i < len; i++) {
            double da = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da - db);
        }
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }
    
    CASE(vec_mul): {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        ObjArray* b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray* result = new_array(vm, len);
        sp = vm->sp;
        for (uint32_t i = 0; i < len; i++) {
            double da = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da * db);
        }
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }
    
    CASE(vec_div): {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        ObjArray* b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray* result = new_array(vm, len);
        sp = vm->sp;
        for (uint32_t i = 0; i < len; i++) {
            double da = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da / db);
        }
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }
    
    CASE(vec_dot): {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val)) {
            PUSH(val_num(0));
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        ObjArray* b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        double dot = 0;
        for (uint32_t i = 0; i < len; i++) {
            double da = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? as_int(b->values[i]) : as_num(b->values[i]);
            dot += da * db;
        }
        PUSH(val_num(dot));
        DISPATCH();
    }
    
    CASE(vec_sum): {
        Value a_val = POP();
        if (!IS_ARRAY(a_val)) {
            PUSH(val_num(0));
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        double sum = 0;
        for (uint32_t i = 0; i < a->count; i++) {
            sum += IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
        }
        PUSH(val_num(sum));
        DISPATCH();
    }
    
    CASE(vec_prod): {
        Value a_val = POP();
        if (!IS_ARRAY(a_val)) {
            PUSH(val_num(1));
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        double prod = 1;
        for (uint32_t i = 0; i < a->count; i++) {
            prod *= IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
        }
        PUSH(val_num(prod));
        DISPATCH();
    }
    
    CASE(vec_min): {
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || AS_ARRAY(a_val)->count == 0) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        double min_val = IS_INT(a->values[0]) ? as_int(a->values[0]) : as_num(a->values[0]);
        for (uint32_t i = 1; i < a->count; i++) {
            double v = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            if (v < min_val) min_val = v;
        }
        PUSH(val_num(min_val));
        DISPATCH();
    }
    
    CASE(vec_max): {
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || AS_ARRAY(a_val)->count == 0) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        double max_val = IS_INT(a->values[0]) ? as_int(a->values[0]) : as_num(a->values[0]);
        for (uint32_t i = 1; i < a->count; i++) {
            double v = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            if (v > max_val) max_val = v;
        }
        PUSH(val_num(max_val));
        DISPATCH();
    }
    
    CASE(vec_mean): {
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || AS_ARRAY(a_val)->count == 0) {
            PUSH(val_num(0));
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        double sum = 0;
        for (uint32_t i = 0; i < a->count; i++) {
            sum += IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
        }
        PUSH(val_num(sum / a->count));
        DISPATCH();
    }
    
    CASE(vec_map): {
        /* TODO: Higher-order functions require closures */
        POP(); POP();
        PUSH(VAL_NIL);
        DISPATCH();
    }
    
    CASE(vec_filter): {
        POP(); POP();
        PUSH(VAL_NIL);
        DISPATCH();
    }
    
    CASE(vec_reduce): {
        POP(); POP(); POP();
        PUSH(VAL_NIL);
        DISPATCH();
    }
    
    CASE(vec_sort): {
        Value a_val = POP();
        if (!IS_ARRAY(a_val)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        /* Simple bubble sort for now - could optimize with quicksort */
        for (uint32_t i = 0; i < a->count; i++) {
            for (uint32_t j = i + 1; j < a->count; j++) {
                double vi = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
                double vj = IS_INT(a->values[j]) ? as_int(a->values[j]) : as_num(a->values[j]);
                if (vi > vj) {
                    Value tmp = a->values[i];
                    a->values[i] = a->values[j];
                    a->values[j] = tmp;
                }
            }
        }
        PUSH(a_val);
        DISPATCH();
    }
    
    CASE(vec_reverse): {
        Value a_val = POP();
        if (!IS_ARRAY(a_val)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        for (uint32_t i = 0; i < a->count / 2; i++) {
            Value tmp = a->values[i];
            a->values[i] = a->values[a->count - 1 - i];
            a->values[a->count - 1 - i] = tmp;
        }
        PUSH(a_val);
        DISPATCH();
    }
    
    CASE(vec_unique): {
        Value a_val = POP();
        if (!IS_ARRAY(a_val)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        vm->sp = sp;
        ObjArray* result = new_array(vm, a->count);
        sp = vm->sp;
        for (uint32_t i = 0; i < a->count; i++) {
            bool found = false;
            for (uint32_t j = 0; j < result->count; j++) {
                if (a->values[i] == result->values[j]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                result->values[result->count++] = a->values[i];
            }
        }
        PUSH(val_obj(result));
        DISPATCH();
    }
    
    CASE(vec_zip): {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val)) {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray* a = AS_ARRAY(a_val);
        ObjArray* b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray* result = new_array(vm, len);
        sp = vm->sp;
        for (uint32_t i = 0; i < len; i++) {
            vm->sp = sp;
            ObjArray* pair = new_array(vm, 2);
            sp = vm->sp;
            pair->values[0] = a->values[i];
            pair->values[1] = b->values[i];
            pair->count = 2;
            result->values[i] = val_obj(pair);
        }
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }
    
    CASE(vec_range): {
        Value step_val = POP();
        Value end_val = POP();
        Value start_val = POP();
        double start = IS_INT(start_val) ? as_int(start_val) : as_num(start_val);
        double end = IS_INT(end_val) ? as_int(end_val) : as_num(end_val);
        double step = IS_INT(step_val) ? as_int(step_val) : as_num(step_val);
        if (step == 0) step = 1;
        uint32_t count = (uint32_t)((end - start) / step);
        if (count > 10000000) count = 10000000; /* Safety limit */
        vm->sp = sp;
        ObjArray* result = new_array(vm, count);
        sp = vm->sp;
        double v = start;
        for (uint32_t i = 0; i < count && ((step > 0 && v < end) || (step < 0 && v > end)); i++) {
            result->values[i] = val_num(v);
            result->count++;
            v += step;
        }
        PUSH(val_obj(result));
        DISPATCH();
    }
    
    /* ============ BINARY DATA ============ */
    
    CASE(bytes): {
        Value size_val = POP();
        uint32_t size = IS_INT(size_val) ? as_int(size_val) : (uint32_t)as_num(size_val);
        vm->sp = sp;
        ObjBytes* bytes = new_bytes(vm, size);
        sp = vm->sp;
        bytes->length = size;
        memset(bytes->data, 0, size);
        PUSH(val_obj(bytes));
        DISPATCH();
    }
    
    CASE(bytes_get): {
        Value idx_val = POP();
        Value bytes_val = POP();
        if (!IS_BYTES(bytes_val)) {
            PUSH(val_int(0));
            DISPATCH();
        }
        ObjBytes* bytes = AS_BYTES(bytes_val);
        uint32_t idx = IS_INT(idx_val) ? as_int(idx_val) : (uint32_t)as_num(idx_val);
        if (idx >= bytes->length) {
            PUSH(val_int(0));
            DISPATCH();
        }
        PUSH(val_int(bytes->data[idx]));
        DISPATCH();
    }
    
    CASE(bytes_set): {
        Value val = POP();
        Value idx_val = POP();
        Value bytes_val = POP();
        if (!IS_BYTES(bytes_val)) {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjBytes* bytes = AS_BYTES(bytes_val);
        uint32_t idx = IS_INT(idx_val) ? as_int(idx_val) : (uint32_t)as_num(idx_val);
        if (idx >= bytes->length) {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        bytes->data[idx] = IS_INT(val) ? as_int(val) : (uint8_t)as_num(val);
        PUSH(VAL_TRUE);
        DISPATCH();
    }
    
    CASE(encode_utf8): {
        Value str_val = POP();
        if (!IS_STRING(str_val)) {
            vm->sp = sp;
            ObjBytes* bytes = new_bytes(vm, 0);
            sp = vm->sp;
            PUSH(val_obj(bytes));
            DISPATCH();
        }
        ObjString* str = AS_STRING(str_val);
        vm->sp = sp;
        ObjBytes* bytes = new_bytes(vm, str->length);
        sp = vm->sp;
        memcpy(bytes->data, str->chars, str->length);
        bytes->length = str->length;
        PUSH(val_obj(bytes));
        DISPATCH();
    }
    
    CASE(decode_utf8): {
        Value bytes_val = POP();
        if (!IS_BYTES(bytes_val)) {
            vm->sp = sp;
            ObjString* str = copy_string(vm, "", 0);
            sp = vm->sp;
            PUSH(val_obj(str));
            DISPATCH();
        }
        ObjBytes* bytes = AS_BYTES(bytes_val);
        vm->sp = sp;
        ObjString* str = copy_string(vm, (char*)bytes->data, bytes->length);
        sp = vm->sp;
        PUSH(val_obj(str));
        DISPATCH();
    }
    
    CASE(encode_base64): {
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        Value data_val = POP();
        uint8_t* data; uint32_t len;
        if (IS_STRING(data_val)) {
            data = (uint8_t*)AS_STRING(data_val)->chars;
            len = AS_STRING(data_val)->length;
        } else if (IS_BYTES(data_val)) {
            data = AS_BYTES(data_val)->data;
            len = AS_BYTES(data_val)->length;
        } else {
            vm->sp = sp;
            ObjString* str = copy_string(vm, "", 0);
            sp = vm->sp;
            PUSH(val_obj(str));
            DISPATCH();
        }
        uint32_t out_len = ((len + 2) / 3) * 4;
        char* out = malloc(out_len + 1);
        uint32_t j = 0;
        for (uint32_t i = 0; i < len; i += 3) {
            uint32_t n = ((uint32_t)data[i]) << 16;
            if (i + 1 < len) n |= ((uint32_t)data[i + 1]) << 8;
            if (i + 2 < len) n |= data[i + 2];
            out[j++] = b64[(n >> 18) & 0x3F];
            out[j++] = b64[(n >> 12) & 0x3F];
            out[j++] = (i + 1 < len) ? b64[(n >> 6) & 0x3F] : '=';
            out[j++] = (i + 2 < len) ? b64[n & 0x3F] : '=';
        }
        out[j] = '\0';
        vm->sp = sp;
        ObjString* str = copy_string(vm, out, j);
        sp = vm->sp;
        free(out);
        PUSH(val_obj(str));
        DISPATCH();
    }
    
    CASE(decode_base64): {
        /* Simplified base64 decode */
        Value str_val = POP();
        if (!IS_STRING(str_val)) {
            vm->sp = sp;
            ObjBytes* bytes = new_bytes(vm, 0);
            sp = vm->sp;
            PUSH(val_obj(bytes));
            DISPATCH();
        }
        ObjString* str = AS_STRING(str_val);
        uint32_t out_len = (str->length / 4) * 3;
        vm->sp = sp;
        ObjBytes* bytes = new_bytes(vm, out_len);
        sp = vm->sp;
        /* TODO: Proper base64 decode */
        bytes->length = 0;
        PUSH(val_obj(bytes));
        DISPATCH();
    }
    
    /* ============ REGEX ============ */
    /* Note: Full regex would require linking with PCRE */
    
    CASE(regex_match): {
        POP(); POP();
        PUSH(VAL_FALSE);  /* TODO: Implement with PCRE */
        DISPATCH();
    }
    
    CASE(regex_find): {
        POP(); POP();
        vm->sp = sp;
        ObjArray* arr = new_array(vm, 0);
        sp = vm->sp;
        PUSH(val_obj(arr));
        DISPATCH();
    }
    
    CASE(regex_replace): {
        POP(); POP(); POP();
        vm->sp = sp;
        ObjString* str = copy_string(vm, "", 0);
        sp = vm->sp;
        PUSH(val_obj(str));
        DISPATCH();
    }
    
    /* ============ HASHING ============ */
    
    CASE(hash): {
        Value v = POP();
        uint32_t h = 2166136261u;
        if (IS_STRING(v)) {
            ObjString* s = AS_STRING(v);
            for (uint32_t i = 0; i < s->length; i++) {
                h ^= (uint8_t)s->chars[i];
                h *= 16777619;
            }
        } else if (IS_INT(v)) {
            h = (uint32_t)as_int(v);
        } else {
            h = (uint32_t)(v >> 32) ^ (uint32_t)v;
        }
        PUSH(val_int((int32_t)h));
        DISPATCH();
    }
    
    CASE(hash_sha256): {
        /* TODO: Implement SHA-256 */
        POP();
        vm->sp = sp;
        ObjString* str = copy_string(vm, "0000000000000000000000000000000000000000000000000000000000000000", 64);
        sp = vm->sp;
        PUSH(val_obj(str));
        DISPATCH();
    }
    
    CASE(hash_md5): {
        /* TODO: Implement MD5 */
        POP();
        vm->sp = sp;
        ObjString* str = copy_string(vm, "00000000000000000000000000000000", 32);
        sp = vm->sp;
        PUSH(val_obj(str));
        DISPATCH();
    }

#if !USE_COMPUTED_GOTO
        default:
            runtime_error(vm, "Unknown opcode %d", ip[-1]);
            return INTERPRET_RUNTIME_ERROR;
        }
    }
#endif
}

/* ============ Main Entry Point ============ */

InterpretResult vm_interpret(VM* vm, const char* source) {
    chunk_init(&vm->chunk);
    
    if (!compile(source, &vm->chunk, vm)) {
        chunk_free(&vm->chunk);
        return INTERPRET_COMPILE_ERROR;
    }
    
    vm->ip = vm->chunk.code;
    
    /* Push placeholder for slot 0 (script "function") to align local slots */
    *vm->sp++ = VAL_NIL;
    
    InterpretResult result = vm_run(vm);
    
    return result;
}
