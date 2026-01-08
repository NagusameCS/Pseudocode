/*
 * Pseudocode Language - High Performance Virtual Machine
 * Uses computed gotos (GCC/Clang) or switch dispatch
 */

#define _POSIX_C_SOURCE 199309L  /* For clock_gettime */

#include "pseudo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>

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

#define PUSH(v)    (*vm->sp++ = (v))
#define POP()      (*--vm->sp)
#define PEEK(n)    (vm->sp[-1 - (n)])
#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONST() (vm->chunk.constants[READ_BYTE()])

/* Binary operations on numbers */
#define BINARY_OP(op) \
    do { \
        Value b = POP(); \
        Value a = POP(); \
        if (IS_INT(a) && IS_INT(b)) { \
            PUSH(val_int(as_int(a) op as_int(b))); \
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
        if (IS_INT(a) && IS_INT(b)) { \
            PUSH(val_bool(as_int(a) op as_int(b))); \
        } else { \
            double da = IS_INT(a) ? as_int(a) : as_num(a); \
            double db = IS_INT(b) ? as_int(b) : as_num(b); \
            PUSH(val_bool(da op db)); \
        } \
    } while (0)

InterpretResult vm_run(VM* vm) {
    register uint8_t* ip = vm->chunk.code;
    
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
        [OP_HALT] = &&op_halt,
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
        vm->sp -= n;
        DISPATCH();
    }
    
    CASE(dup): {
        PUSH(PEEK(0));
        DISPATCH();
    }
    
    CASE(get_local): {
        uint8_t slot = READ_BYTE();
        if (vm->frame_count > 0) {
            PUSH(vm->frames[vm->frame_count - 1].slots[slot]);
        } else {
            PUSH(vm->stack[slot]);
        }
        DISPATCH();
    }
    
    CASE(set_local): {
        uint8_t slot = READ_BYTE();
        if (vm->frame_count > 0) {
            vm->frames[vm->frame_count - 1].slots[slot] = PEEK(0);
        } else {
            vm->stack[slot] = PEEK(0);
        }
        DISPATCH();
    }
    
    CASE(get_global): {
        ObjString* name = AS_STRING(READ_CONST());
        Value value;
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
        PUSH(val_bool(a == b));
        DISPATCH();
    }
    
    CASE(neq): {
        Value b = POP();
        Value a = POP();
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
        if (!is_truthy(PEEK(0))) ip += offset;
        DISPATCH();
    }
    
    CASE(jmp_true): {
        uint16_t offset = READ_SHORT();
        if (is_truthy(PEEK(0))) ip += offset;
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
            runtime_error(vm, "Can only call functions.");
            return INTERPRET_RUNTIME_ERROR;
        }
        
        ObjFunction* function = AS_FUNCTION(callee);
        if (arg_count != function->arity) {
            runtime_error(vm, "Expected %d arguments but got %d.",
                function->arity, arg_count);
            return INTERPRET_RUNTIME_ERROR;
        }
        
        if (vm->frame_count == FRAMES_MAX) {
            runtime_error(vm, "Stack overflow.");
            return INTERPRET_RUNTIME_ERROR;
        }
        
        CallFrame* frame = &vm->frames[vm->frame_count++];
        frame->function = function;
        frame->ip = ip;
        frame->slots = vm->sp - arg_count - 1;
        
        ip = vm->chunk.code + function->code_start;
        DISPATCH();
    }
    
    CASE(return): {
        Value result = POP();
        
        if (vm->frame_count == 0) {
            /* Returning from top-level - shouldn't happen normally */
            return INTERPRET_OK;
        }
        
        vm->frame_count--;
        CallFrame* frame = &vm->frames[vm->frame_count];
        vm->sp = frame->slots;
        ip = frame->ip;
        PUSH(result);
        DISPATCH();
    }
    
    CASE(array): {
        uint8_t count = READ_BYTE();
        ObjArray* array = new_array(vm, count);
        
        /* Pop elements in reverse order */
        vm->sp -= count;
        for (int i = 0; i < count; i++) {
            array->values[i] = vm->sp[i];
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
                vm->sp -= 2;  /* Remove array and index */
                ip += offset;
            } else {
                vm->sp[-1] = val_int(idx + 1);  /* Increment index */
                PUSH(arr->values[idx]);  /* Push current element */
            }
        } else {
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
    
    CASE(halt): {
        return INTERPRET_OK;
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
