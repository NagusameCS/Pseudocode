/*
 * Pseudocode Language - High Performance Virtual Machine
 * Uses computed gotos (GCC/Clang) or switch dispatch
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#define _POSIX_C_SOURCE 200809L /* For clock_gettime, popen, etc */
#define _GNU_SOURCE

#include "pseudo.h"
#include "tensor.h"
#include "jit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>

/* Platform-specific includes */
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#define realpath(path, resolved) _fullpath(resolved, path, PATH_MAX)
#define setenv(name, value, overwrite) _putenv_s(name, value)
#undef PATH_MAX
#define PATH_MAX MAX_PATH
#define usleep(us) Sleep((us) / 1000)
#define unlink(path) _unlink(path)

/* Windows directory iteration */
typedef struct WIN_DIR_s {
    HANDLE handle;
    WIN32_FIND_DATAA find_data;
    int first;
} WIN_DIR;

struct dirent {
    char d_name[MAX_PATH];
};

static WIN_DIR *win_opendir(const char *path) {
    WIN_DIR *dir = malloc(sizeof(WIN_DIR));
    if (!dir) return NULL;
    char pattern[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s\\*", path);
    dir->handle = FindFirstFileA(pattern, &dir->find_data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }
    dir->first = 1;
    return dir;
}

static struct dirent *win_readdir(WIN_DIR *dir) {
    static struct dirent entry;
    if (dir->first) {
        dir->first = 0;
        strcpy(entry.d_name, dir->find_data.cFileName);
        return &entry;
    }
    if (FindNextFileA(dir->handle, &dir->find_data)) {
        strcpy(entry.d_name, dir->find_data.cFileName);
        return &entry;
    }
    return NULL;
}

static void win_closedir(WIN_DIR *dir) {
    FindClose(dir->handle);
    free(dir);
}

#define DIR WIN_DIR
#define opendir win_opendir
#define readdir win_readdir
#define closedir win_closedir

#else
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#endif

/* SIMD intrinsics for vectorized operations */
#if defined(__AVX__)
#include <immintrin.h>
#define SIMD_WIDTH 4 /* 4 doubles per AVX register */
#elif defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define SIMD_WIDTH 2 /* 2 doubles per SSE register */
#else
#define SIMD_WIDTH 1 /* Scalar fallback */
#endif

/* PCRE2 for regex support - can be disabled with -DNO_PCRE2 */
#ifndef NO_PCRE2
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#define HAS_PCRE2 1
#else
#define HAS_PCRE2 0
#endif

/* Use computed gotos if available (GCC/Clang) */
#if defined(__GNUC__) || defined(__clang__)
#define USE_COMPUTED_GOTO 1
#else
#define USE_COMPUTED_GOTO 0
#endif

/* ============ Field Hash Table Helpers ============ */
/* O(1) field lookup using open addressing with linear probing */

/* Find field index by name, returns -1 if not found */
static inline int16_t field_hash_find(ObjClass *klass, ObjString *name)
{
    uint32_t hash = name->hash;
    uint32_t mask = CLASS_FIELD_HASH_SIZE - 1;
    uint32_t idx = hash & mask;
    
    for (uint32_t i = 0; i < CLASS_FIELD_HASH_SIZE; i++)
    {
        FieldHashEntry *entry = &klass->field_hash[idx];
        if (entry->hash == 0)
            return -1;  /* Empty slot - not found */
        if (entry->hash == hash)
        {
            /* Hash match - verify the actual string */
            ObjString *field_name = klass->field_names[entry->index];
            if (field_name == name ||
                (field_name->length == name->length &&
                 memcmp(field_name->chars, name->chars, name->length) == 0))
            {
                return entry->index;
            }
        }
        idx = (idx + 1) & mask;  /* Linear probe */
    }
    return -1;  /* Table full - shouldn't happen */
}

/* Insert field into hash table */
static inline void field_hash_insert(ObjClass *klass, ObjString *name, int16_t field_idx)
{
    uint32_t hash = name->hash;
    uint32_t mask = CLASS_FIELD_HASH_SIZE - 1;
    uint32_t idx = hash & mask;
    
    for (uint32_t i = 0; i < CLASS_FIELD_HASH_SIZE; i++)
    {
        FieldHashEntry *entry = &klass->field_hash[idx];
        if (entry->hash == 0)
        {
            /* Empty slot - insert here */
            entry->hash = hash ? hash : 1;  /* Ensure non-zero hash */
            entry->index = field_idx;
            return;
        }
        idx = (idx + 1) & mask;
    }
    /* Table full - shouldn't happen with proper sizing */
}

/* ============ VM Initialization ============ */

void vm_init(VM *vm)
{
    vm->sp = vm->stack;
    vm->frame_count = 0;
    vm->handler_count = 0;
    vm->current_exception = VAL_NIL;
    vm->objects = NULL;
    vm->bytes_allocated = 0;
    vm->next_gc = 1024 * 1024;
    vm->open_upvalues = NULL;
    vm->debug_mode = false;

    vm->globals.keys = NULL;
    vm->globals.values = NULL;
    vm->globals.count = 0;
    vm->globals.capacity = 0;

    /* Initialize inline caches */
    vm->ic_count = 0;
    memset(vm->ic_cache, 0, sizeof(vm->ic_cache));

    /* Initialize polymorphic inline caches */
    vm->pic_count = 0;
    memset(vm->pic_cache, 0, sizeof(vm->pic_cache));

    chunk_init(&vm->chunk);
}

void vm_free(VM *vm)
{
    /* Free all objects */
    Obj *object = vm->objects;
    while (object != NULL)
    {
        Obj *next = object->next;
        free_object(vm, object);
        object = next;
    }

    /* Free globals table */
    if (vm->globals.keys)
        free(vm->globals.keys);
    if (vm->globals.values)
        free(vm->globals.values);

    chunk_free(&vm->chunk);
}

/* ============ Upvalue Management ============ */

/* Capture a local variable as an upvalue, reusing existing if already captured */
static ObjUpvalue *capture_upvalue(VM *vm, Value *local)
{
    ObjUpvalue *prev = NULL;
    ObjUpvalue *upvalue = vm->open_upvalues;

    /* Walk the list looking for an existing upvalue or insertion point */
    while (upvalue != NULL && upvalue->location > local)
    {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    /* Found existing upvalue for this slot */
    if (upvalue != NULL && upvalue->location == local)
    {
        return upvalue;
    }

    /* Create new upvalue */
    ObjUpvalue *created = new_upvalue(vm, local);
    created->next = upvalue;

    if (prev == NULL)
    {
        vm->open_upvalues = created;
    }
    else
    {
        prev->next = created;
    }

    return created;
}

/* Close all upvalues pointing to slots at or above 'last' */
static void close_upvalues(VM *vm, Value *last)
{
    while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last)
    {
        ObjUpvalue *upvalue = vm->open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->open_upvalues = upvalue->next;
    }
}

/* ============ Globals Hash Table ============ */

static uint32_t find_entry(ObjString **keys, uint32_t capacity, ObjString *key)
{
    uint32_t index = key->hash & (capacity - 1);

    for (;;)
    {
        ObjString *entry = keys[index];

        if (entry == NULL || entry == key)
        {
            return index;
        }

        /* Check string equality for hash collision */
        if (entry->length == key->length &&
            entry->hash == key->hash &&
            memcmp(entry->chars, key->chars, key->length) == 0)
        {
            return index;
        }

        index = (index + 1) & (capacity - 1);
    }
}

static void adjust_capacity(VM *vm, uint32_t new_capacity)
{
    ObjString **new_keys = (ObjString **)calloc(new_capacity, sizeof(ObjString *));
    Value *new_values = (Value *)malloc(new_capacity * sizeof(Value));

    /* Rehash existing entries */
    for (uint32_t i = 0; i < vm->globals.capacity; i++)
    {
        ObjString *key = vm->globals.keys[i];
        if (key == NULL)
            continue;

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

static bool table_get(VM *vm, ObjString *key, Value *value)
{
    if (vm->globals.count == 0)
        return false;

    uint32_t index = find_entry(vm->globals.keys, vm->globals.capacity, key);
    if (vm->globals.keys[index] == NULL)
        return false;

    *value = vm->globals.values[index];
    return true;
}

static void table_set(VM *vm, ObjString *key, Value value)
{
    if (vm->globals.count + 1 > vm->globals.capacity * 0.75)
    {
        uint32_t new_cap = vm->globals.capacity < 8 ? 8 : vm->globals.capacity * 2;
        adjust_capacity(vm, new_cap);
    }

    uint32_t index = find_entry(vm->globals.keys, vm->globals.capacity, key);
    bool is_new = (vm->globals.keys[index] == NULL);

    vm->globals.keys[index] = key;
    vm->globals.values[index] = value;

    if (is_new)
        vm->globals.count++;
}

/* ============ Runtime Error ============ */

static void runtime_error(VM *vm, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    /* Print stack trace */
    for (int i = vm->frame_count - 1; i >= 0; i--)
    {
        CallFrame *frame = &vm->frames[i];
        ObjFunction *function = frame->function;
        size_t instruction = frame->ip - vm->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", vm->chunk.lines[instruction]);
        if (function->name == NULL)
        {
            fprintf(stderr, "script\n");
        }
        else
        {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    vm->sp = vm->stack;
    vm->frame_count = 0;
}

/* ============ Object Allocation Helper ============ */

static Obj *alloc_object(VM *vm, size_t size, ObjType type)
{
    Obj *object = (Obj *)pseudo_realloc(vm, NULL, 0, size);
    object->type = type;
    object->marked = false;
    object->next = vm->objects;
    vm->objects = object;
    return object;
}

/* ============ JSON Parser ============ */

static void json_skip_whitespace(const char **json, const char *end)
{
    while (*json < end && (**json == ' ' || **json == '\t' || **json == '\n' || **json == '\r'))
    {
        (*json)++;
    }
}

static Value json_parse_value(VM *vm, const char **json, const char *end);

static Value json_parse_string(VM *vm, const char **json, const char *end)
{
    if (*json >= end || **json != '"')
        return VAL_NIL;
    (*json)++; /* Skip opening quote */

    const char *start = *json;
    size_t len = 0;

    /* First pass: count length (handling escapes) */
    while (*json < end && **json != '"')
    {
        if (**json == '\\' && *json + 1 < end)
        {
            (*json)++;
        }
        (*json)++;
        len++;
    }

    /* Allocate and copy */
    char *buffer = malloc(len + 1);
    const char *src = start;
    size_t dst_idx = 0;

    while (src < *json)
    {
        if (*src == '\\' && src + 1 < *json)
        {
            src++;
            switch (*src)
            {
            case 'n':
                buffer[dst_idx++] = '\n';
                break;
            case 't':
                buffer[dst_idx++] = '\t';
                break;
            case 'r':
                buffer[dst_idx++] = '\r';
                break;
            case '"':
                buffer[dst_idx++] = '"';
                break;
            case '\\':
                buffer[dst_idx++] = '\\';
                break;
            default:
                buffer[dst_idx++] = *src;
                break;
            }
        }
        else
        {
            buffer[dst_idx++] = *src;
        }
        src++;
    }
    buffer[dst_idx] = '\0';

    if (*json < end && **json == '"')
        (*json)++; /* Skip closing quote */

    ObjString *str = copy_string(vm, buffer, dst_idx);
    free(buffer);
    return val_obj(str);
}

static Value json_parse_number(VM *vm, const char **json, const char *end)
{
    (void)vm;
    const char *start = *json;
    bool is_float = false;

    if (**json == '-')
        (*json)++;
    while (*json < end && (**json >= '0' && **json <= '9'))
        (*json)++;

    if (*json < end && **json == '.')
    {
        is_float = true;
        (*json)++;
        while (*json < end && (**json >= '0' && **json <= '9'))
            (*json)++;
    }

    if (*json < end && (**json == 'e' || **json == 'E'))
    {
        is_float = true;
        (*json)++;
        if (*json < end && (**json == '+' || **json == '-'))
            (*json)++;
        while (*json < end && (**json >= '0' && **json <= '9'))
            (*json)++;
    }

    char *num_str = malloc(*json - start + 1);
    memcpy(num_str, start, *json - start);
    num_str[*json - start] = '\0';

    double val = atof(num_str);
    free(num_str);

    if (!is_float && val == (int32_t)val)
    {
        return val_int((int32_t)val);
    }
    return val_num(val);
}

static Value json_parse_array(VM *vm, const char **json, const char *end)
{
    if (*json >= end || **json != '[')
        return VAL_NIL;
    (*json)++; /* Skip '[' */

    ObjArray *arr = (ObjArray *)pseudo_realloc(vm, NULL, 0, sizeof(ObjArray));
    arr->obj.type = OBJ_ARRAY;
    arr->obj.next = vm->objects;
    arr->obj.marked = false;
    vm->objects = (Obj *)arr;
    arr->values = NULL;
    arr->count = 0;
    arr->capacity = 0;

    json_skip_whitespace(json, end);

    if (*json < end && **json == ']')
    {
        (*json)++;
        return val_obj(arr);
    }

    while (*json < end)
    {
        json_skip_whitespace(json, end);
        Value item = json_parse_value(vm, json, end);

        /* Add to array */
        if (arr->count >= arr->capacity)
        {
            uint32_t new_cap = arr->capacity < 8 ? 8 : arr->capacity * 2;
            arr->values = realloc(arr->values, sizeof(Value) * new_cap);
            arr->capacity = new_cap;
        }
        arr->values[arr->count++] = item;

        json_skip_whitespace(json, end);
        if (*json >= end)
            break;
        if (**json == ']')
        {
            (*json)++;
            break;
        }
        if (**json == ',')
            (*json)++;
    }

    return val_obj(arr);
}

/* Helper to insert into a hash-based dict */
static void dict_insert(ObjDict *dict, ObjString *key, Value value)
{
    /* Grow if needed (at 75% load) */
    if (dict->count * 4 >= dict->capacity * 3)
    {
        uint32_t new_cap = dict->capacity < 8 ? 8 : dict->capacity * 2;
        ObjString **new_keys = (ObjString **)calloc(new_cap, sizeof(ObjString *));
        Value *new_values = (Value *)malloc(new_cap * sizeof(Value));

        /* Rehash all existing entries */
        for (uint32_t i = 0; i < dict->capacity; i++)
        {
            if (dict->keys[i])
            {
                uint32_t slot = dict->keys[i]->hash & (new_cap - 1);
                while (new_keys[slot])
                    slot = (slot + 1) & (new_cap - 1);
                new_keys[slot] = dict->keys[i];
                new_values[slot] = dict->values[i];
            }
        }

        free(dict->keys);
        free(dict->values);
        dict->keys = new_keys;
        dict->values = new_values;
        dict->capacity = new_cap;
    }

    /* Find slot using linear probing */
    uint32_t slot = key->hash & (dict->capacity - 1);
    while (dict->keys[slot])
    {
        /* Check if key already exists */
        if (dict->keys[slot]->length == key->length &&
            memcmp(dict->keys[slot]->chars, key->chars, key->length) == 0)
        {
            dict->values[slot] = value;
            return;
        }
        slot = (slot + 1) & (dict->capacity - 1);
    }
    dict->keys[slot] = key;
    dict->values[slot] = value;
    dict->count++;
}

static Value json_parse_object(VM *vm, const char **json, const char *end)
{
    if (*json >= end || **json != '{')
        return VAL_NIL;
    (*json)++; /* Skip '{' */

    /* Use new_dict for proper hash-based storage */
    ObjDict *dict = new_dict(vm, 8);

    json_skip_whitespace(json, end);

    if (*json < end && **json == '}')
    {
        (*json)++;
        return val_obj(dict);
    }

    while (*json < end)
    {
        json_skip_whitespace(json, end);

        /* Parse key (must be string) */
        if (*json >= end || **json != '"')
            break;
        Value key = json_parse_string(vm, json, end);
        if (!IS_STRING(key))
            break;

        json_skip_whitespace(json, end);
        if (*json >= end || **json != ':')
            break;
        (*json)++; /* Skip ':' */

        json_skip_whitespace(json, end);
        Value value = json_parse_value(vm, json, end);

        /* Add to dict using proper hash-based insertion */
        dict_insert(dict, AS_STRING(key), value);

        json_skip_whitespace(json, end);
        if (*json >= end)
            break;
        if (**json == '}')
        {
            (*json)++;
            break;
        }
        if (**json == ',')
            (*json)++;
    }

    return val_obj(dict);
}

static Value json_parse_value(VM *vm, const char **json, const char *end)
{
    json_skip_whitespace(json, end);
    if (*json >= end)
        return VAL_NIL;

    char c = **json;

    if (c == '"')
        return json_parse_string(vm, json, end);
    if (c == '[')
        return json_parse_array(vm, json, end);
    if (c == '{')
        return json_parse_object(vm, json, end);
    if (c == '-' || (c >= '0' && c <= '9'))
        return json_parse_number(vm, json, end);

    /* Keywords */
    if (end - *json >= 4 && memcmp(*json, "true", 4) == 0)
    {
        *json += 4;
        return VAL_TRUE;
    }
    if (end - *json >= 5 && memcmp(*json, "false", 5) == 0)
    {
        *json += 5;
        return VAL_FALSE;
    }
    if (end - *json >= 4 && memcmp(*json, "null", 4) == 0)
    {
        *json += 4;
        return VAL_NIL;
    }

    return VAL_NIL;
}

/* ============ JSON Stringify ============ */

static void json_stringify_append(char **buf, size_t *len, size_t *cap, const char *str, size_t slen)
{
    while (*len + slen + 1 > *cap)
    {
        *cap = *cap < 64 ? 64 : *cap * 2;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, str, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

static void json_stringify_value_impl(VM *vm, Value val, char **buf, size_t *len, size_t *cap)
{
    if (IS_NIL(val))
    {
        json_stringify_append(buf, len, cap, "null", 4);
    }
    else if (IS_BOOL(val))
    {
        if (IS_TRUE(val))
        {
            json_stringify_append(buf, len, cap, "true", 4);
        }
        else
        {
            json_stringify_append(buf, len, cap, "false", 5);
        }
    }
    else if (IS_INT(val))
    {
        char num[32];
        int n = snprintf(num, sizeof(num), "%d", as_int(val));
        json_stringify_append(buf, len, cap, num, n);
    }
    else if (IS_NUM(val))
    {
        char num[32];
        int n = snprintf(num, sizeof(num), "%g", as_num(val));
        json_stringify_append(buf, len, cap, num, n);
    }
    else if (IS_STRING(val))
    {
        ObjString *str = AS_STRING(val);
        json_stringify_append(buf, len, cap, "\"", 1);
        for (size_t i = 0; i < str->length; i++)
        {
            char c = str->chars[i];
            if (c == '"')
                json_stringify_append(buf, len, cap, "\\\"", 2);
            else if (c == '\\')
                json_stringify_append(buf, len, cap, "\\\\", 2);
            else if (c == '\n')
                json_stringify_append(buf, len, cap, "\\n", 2);
            else if (c == '\r')
                json_stringify_append(buf, len, cap, "\\r", 2);
            else if (c == '\t')
                json_stringify_append(buf, len, cap, "\\t", 2);
            else
                json_stringify_append(buf, len, cap, &c, 1);
        }
        json_stringify_append(buf, len, cap, "\"", 1);
    }
    else if (IS_ARRAY(val))
    {
        ObjArray *arr = AS_ARRAY(val);
        json_stringify_append(buf, len, cap, "[", 1);
        for (uint32_t i = 0; i < arr->count; i++)
        {
            if (i > 0)
                json_stringify_append(buf, len, cap, ",", 1);
            json_stringify_value_impl(vm, arr->values[i], buf, len, cap);
        }
        json_stringify_append(buf, len, cap, "]", 1);
    }
    else if (IS_DICT(val))
    {
        ObjDict *dict = AS_DICT(val);
        json_stringify_append(buf, len, cap, "{", 1);
        int first = 1;
        for (uint32_t i = 0; i < dict->capacity; i++)
        {
            if (!dict->keys[i])
                continue;
            if (!first)
                json_stringify_append(buf, len, cap, ",", 1);
            first = 0;
            json_stringify_append(buf, len, cap, "\"", 1);
            json_stringify_append(buf, len, cap, dict->keys[i]->chars, dict->keys[i]->length);
            json_stringify_append(buf, len, cap, "\":", 2);
            json_stringify_value_impl(vm, dict->values[i], buf, len, cap);
        }
        json_stringify_append(buf, len, cap, "}", 1);
    }
    else
    {
        json_stringify_append(buf, len, cap, "null", 4);
    }
}

static ObjString *json_stringify_value(VM *vm, Value val)
{
    char *buf = NULL;
    size_t len = 0, cap = 0;
    json_stringify_value_impl(vm, val, &buf, &len, &cap);
    ObjString *result = copy_string(vm, buf ? buf : "null", buf ? len : 4);
    free(buf);
    return result;
}

/* ============ Value to String Helper ============ */
/* Converts any value to a string representation (for str() builtin) */

static void value_to_string_append(char **buf, size_t *len, size_t *cap, const char *str, size_t slen)
{
    while (*len + slen + 1 > *cap)
    {
        *cap = (*cap == 0) ? 64 : *cap * 2;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, str, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

static void value_to_string_impl(Value value, char **buf, size_t *len, size_t *cap)
{
    char tmp[64];
    
    if (IS_INT(value))
    {
        int n = snprintf(tmp, sizeof(tmp), "%d", as_int(value));
        value_to_string_append(buf, len, cap, tmp, n);
    }
    else if (IS_NUM(value))
    {
        double d = as_num(value);
        int n;
        if (d == (int64_t)d)
            n = snprintf(tmp, sizeof(tmp), "%lld", (long long)(int64_t)d);
        else
            n = snprintf(tmp, sizeof(tmp), "%g", d);
        value_to_string_append(buf, len, cap, tmp, n);
    }
    else if (IS_NIL(value))
    {
        value_to_string_append(buf, len, cap, "nil", 3);
    }
    else if (IS_BOOL(value))
    {
        if (IS_TRUE(value))
            value_to_string_append(buf, len, cap, "true", 4);
        else
            value_to_string_append(buf, len, cap, "false", 5);
    }
    else if (IS_OBJ(value))
    {
        Obj *obj = as_obj(value);
        switch (obj->type)
        {
        case OBJ_STRING:
            value_to_string_append(buf, len, cap, ((ObjString *)obj)->chars, ((ObjString *)obj)->length);
            break;
        case OBJ_ARRAY:
        {
            ObjArray *arr = (ObjArray *)obj;
            value_to_string_append(buf, len, cap, "[", 1);
            for (uint32_t i = 0; i < arr->count; i++)
            {
                if (i > 0)
                    value_to_string_append(buf, len, cap, ", ", 2);
                value_to_string_impl(arr->values[i], buf, len, cap);
            }
            value_to_string_append(buf, len, cap, "]", 1);
            break;
        }
        case OBJ_DICT:
        {
            ObjDict *dict = (ObjDict *)obj;
            value_to_string_append(buf, len, cap, "{", 1);
            bool first = true;
            for (uint32_t i = 0; i < dict->capacity; i++)
            {
                if (dict->keys[i] != NULL)
                {
                    if (!first)
                        value_to_string_append(buf, len, cap, ", ", 2);
                    first = false;
                    value_to_string_append(buf, len, cap, "\"", 1);
                    value_to_string_append(buf, len, cap, dict->keys[i]->chars, dict->keys[i]->length);
                    value_to_string_append(buf, len, cap, "\": ", 3);
                    value_to_string_impl(dict->values[i], buf, len, cap);
                }
            }
            value_to_string_append(buf, len, cap, "}", 1);
            break;
        }
        case OBJ_FUNCTION:
        {
            ObjFunction *fn = (ObjFunction *)obj;
            int n = snprintf(tmp, sizeof(tmp), "<fn %s>", fn->name ? fn->name->chars : "script");
            value_to_string_append(buf, len, cap, tmp, n);
            break;
        }
        case OBJ_RANGE:
        {
            ObjRange *r = (ObjRange *)obj;
            int n = snprintf(tmp, sizeof(tmp), "%d..%d", r->start, r->end);
            value_to_string_append(buf, len, cap, tmp, n);
            break;
        }
        default:
            value_to_string_append(buf, len, cap, "<object>", 8);
            break;
        }
    }
}

/* ============ Value Operations ============ */

static void print_value(Value value)
{
    if (IS_NUM(value))
    {
        double d = as_num(value);
        if (d == (int64_t)d)
        {
            printf("%lld", (long long)(int64_t)d);
        }
        else
        {
            printf("%g", d);
        }
    }
    else if (IS_INT(value))
    {
        printf("%d", as_int(value));
    }
    else if (IS_NIL(value))
    {
        printf("nil");
    }
    else if (IS_BOOL(value))
    {
        printf("%s", IS_TRUE(value) ? "true" : "false");
    }
    else if (IS_OBJ(value))
    {
        Obj *obj = as_obj(value);
        switch (obj->type)
        {
        case OBJ_STRING:
            printf("%s", ((ObjString *)obj)->chars);
            break;
        case OBJ_ARRAY:
        {
            ObjArray *arr = (ObjArray *)obj;
            printf("[");
            for (uint32_t i = 0; i < arr->count; i++)
            {
                if (i > 0)
                    printf(", ");
                print_value(arr->values[i]);
            }
            printf("]");
            break;
        }
        case OBJ_FUNCTION:
            printf("<fn %s>", ((ObjFunction *)obj)->name ? ((ObjFunction *)obj)->name->chars : "script");
            break;
        case OBJ_RANGE:
            printf("%d..%d", ((ObjRange *)obj)->start, ((ObjRange *)obj)->end);
            break;
        case OBJ_TENSOR:
        {
            ObjTensor *t = (ObjTensor *)obj;
            printf("Tensor(shape=[");
            for (uint32_t i = 0; i < t->ndim; i++)
            {
                if (i > 0)
                    printf(", ");
                printf("%u", t->shape[i]);
            }
            printf("], data=[");
            uint32_t max_print = t->size > 10 ? 10 : t->size;
            for (uint32_t i = 0; i < max_print; i++)
            {
                if (i > 0)
                    printf(", ");
                printf("%g", t->data[i]);
            }
            if (t->size > max_print)
                printf(", ...");
            printf("])");
            break;
        }
        case OBJ_MATRIX:
        {
            ObjMatrix *m = (ObjMatrix *)obj;
            printf("Matrix(%ux%u)", m->rows, m->cols);
            break;
        }
        case OBJ_GRAD_TAPE:
            printf("<GradTape>");
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
#define PUSH(v) (*sp++ = (v))
#define POP() (*--sp)
#define PEEK(n) (sp[-1 - (n)])
#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONST() (vm->chunk.constants[READ_BYTE()])

/* Fast integer check and extract - avoids function call overhead */
#define FAST_INT(v, out) (((v) & (QNAN | 0x7)) == (QNAN | TAG_INT) ? ((out) = (int32_t)(((v) >> 3) & 0xFFFFFFFF), 1) : 0)

/* Binary operations on numbers with fast integer path */
#define BINARY_OP(op)                                      \
    do                                                     \
    {                                                      \
        Value b = POP();                                   \
        Value a = POP();                                   \
        int32_t ia, ib;                                    \
        if (FAST_INT(a, ia) && FAST_INT(b, ib))            \
        {                                                  \
            PUSH(val_int(ia op ib));                       \
        }                                                  \
        else                                               \
        {                                                  \
            double da = IS_INT(a) ? as_int(a) : as_num(a); \
            double db = IS_INT(b) ? as_int(b) : as_num(b); \
            PUSH(val_num(da op db));                       \
        }                                                  \
    } while (0)

#define BINARY_OP_INT(op)                                        \
    do                                                           \
    {                                                            \
        Value b = POP();                                         \
        Value a = POP();                                         \
        int32_t ia = IS_INT(a) ? as_int(a) : (int32_t)as_num(a); \
        int32_t ib = IS_INT(b) ? as_int(b) : (int32_t)as_num(b); \
        PUSH(val_int(ia op ib));                                 \
    } while (0)

#define COMPARE_OP(op)                                     \
    do                                                     \
    {                                                      \
        Value b = POP();                                   \
        Value a = POP();                                   \
        int32_t ia, ib;                                    \
        if (FAST_INT(a, ia) && FAST_INT(b, ib))            \
        {                                                  \
            PUSH(val_bool(ia op ib));                      \
        }                                                  \
        else                                               \
        {                                                  \
            double da = IS_INT(a) ? as_int(a) : as_num(a); \
            double db = IS_INT(b) ? as_int(b) : as_num(b); \
            PUSH(val_bool(da op db));                      \
        }                                                  \
    } while (0)

/* Debug: Opcode name lookup table */
static const char *opcode_names[] = {
    [OP_CONST] = "CONST",
    [OP_CONST_LONG] = "CONST_LONG",
    [OP_NIL] = "NIL",
    [OP_TRUE] = "TRUE",
    [OP_FALSE] = "FALSE",
    [OP_POP] = "POP",
    [OP_GET_LOCAL] = "GET_LOCAL",
    [OP_SET_LOCAL] = "SET_LOCAL",
    [OP_GET_GLOBAL] = "GET_GLOBAL",
    [OP_SET_GLOBAL] = "SET_GLOBAL",
    [OP_ADD] = "ADD",
    [OP_SUB] = "SUB",
    [OP_MUL] = "MUL",
    [OP_DIV] = "DIV",
    [OP_EQ] = "EQ",
    [OP_NEQ] = "NEQ",
    [OP_LT] = "LT",
    [OP_GT] = "GT",
    [OP_JMP] = "JMP",
    [OP_JMP_FALSE] = "JMP_FALSE",
    [OP_LOOP] = "LOOP",
    [OP_CALL] = "CALL",
    [OP_RETURN] = "RETURN",
    [OP_PRINT] = "PRINT",
    [OP_ARRAY] = "ARRAY",
    [OP_INDEX] = "INDEX",
    [OP_LEN] = "LEN",
};

static void debug_trace_op(VM *vm, uint8_t *ip, Value *sp)
{
    if (!vm->debug_mode)
        return;

    int offset = (int)(ip - vm->chunk.code);
    uint8_t op = *ip;
    const char *name = (op < sizeof(opcode_names) / sizeof(opcode_names[0]) && opcode_names[op])
                           ? opcode_names[op]
                           : "???";

    fprintf(stderr, "[%04d] %-12s  stack: [", offset, name);
    for (Value *slot = vm->stack; slot < sp; slot++)
    {
        if (slot > vm->stack)
            fprintf(stderr, ", ");
        if (IS_INT(*slot))
            fprintf(stderr, "%d", as_int(*slot));
        else if (IS_NUM(*slot))
            fprintf(stderr, "%.2f", as_num(*slot));
        else if (IS_TRUE(*slot))
            fprintf(stderr, "true");
        else if (IS_FALSE(*slot))
            fprintf(stderr, "false");
        else if (IS_NIL(*slot))
            fprintf(stderr, "nil");
        else if (IS_STRING(*slot))
            fprintf(stderr, "\"%s\"", AS_STRING(*slot)->chars);
        else
            fprintf(stderr, "<obj>");
    }
    fprintf(stderr, "]\n");
}

InterpretResult vm_run(VM *vm)
{
    /* Set VM for JIT runtime helpers (needed for inline function calls) */
    extern void jit_set_vm(void *v);
    jit_set_vm(vm);

    register uint8_t *ip = vm->chunk.code;
    register Value *sp = vm->sp;    /* Cache stack pointer in register */
    register Value *bp = vm->stack; /* Cache base pointer for locals */

#if USE_COMPUTED_GOTO
    /* Dispatch table for computed gotos - must match OpCode enum order */
    static void *dispatch_table[] = {
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
        [OP_GET_UPVALUE] = &&op_get_upvalue,
        [OP_SET_UPVALUE] = &&op_set_upvalue,
        [OP_CLOSURE] = &&op_closure,
        [OP_CLOSE_UPVALUE] = &&op_close_upvalue,
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
        [OP_INDEX_FAST] = &&op_index_fast,
        [OP_INDEX_SET_FAST] = &&op_index_set_fast,
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
        [OP_NEQ_JMP_FALSE] = &&op_neq_jmp_false,
        [OP_GET_LOCAL_ADD] = &&op_get_local_add,
        [OP_GET_LOCAL_SUB] = &&op_get_local_sub,
        [OP_INC_LOCAL] = &&op_inc_local,
        [OP_DEC_LOCAL] = &&op_dec_local,
        [OP_FOR_RANGE] = &&op_for_range,
        [OP_FOR_LOOP] = &&op_for_loop,
        [OP_FOR_INT_INIT] = &&op_for_int_init,
        [OP_FOR_INT_LOOP] = &&op_for_int_loop,
        [OP_FOR_COUNT] = &&op_for_count,
        [OP_FOR_COUNT_STEP] = &&op_for_count_step,
        [OP_ADD_LOCAL_INT] = &&op_add_local_int,
        [OP_LOCAL_LT_LOOP] = &&op_local_lt_loop,
        /* Fused local arithmetic */
        [OP_INC_LOCAL_I] = &&op_inc_local_i,
        [OP_DEC_LOCAL_I] = &&op_dec_local_i,
        [OP_LOCAL_ADD_LOCAL] = &&op_local_add_local,
        [OP_LOCAL_MUL_CONST] = &&op_local_mul_const,
        [OP_LOCAL_ADD_CONST] = &&op_local_add_const,
        [OP_JIT_INC_LOOP] = &&op_jit_inc_loop,
        [OP_JIT_ARITH_LOOP] = &&op_jit_arith_loop,
        [OP_JIT_BRANCH_LOOP] = &&op_jit_branch_loop,
        [OP_TAIL_CALL] = &&op_tail_call,
        [OP_CONST_0] = &&op_const_0,
        [OP_CONST_1] = &&op_const_1,
        [OP_CONST_2] = &&op_const_2,
        [OP_CONST_NEG1] = &&op_const_neg1,
        /* Integer-specialized opcodes */
        [OP_ADD_II] = &&op_add_ii,
        [OP_SUB_II] = &&op_sub_ii,
        [OP_MUL_II] = &&op_mul_ii,
        [OP_DIV_II] = &&op_div_ii,
        [OP_MOD_II] = &&op_mod_ii,
        [OP_LT_II] = &&op_lt_ii,
        [OP_GT_II] = &&op_gt_ii,
        [OP_LTE_II] = &&op_lte_ii,
        [OP_GTE_II] = &&op_gte_ii,
        [OP_EQ_II] = &&op_eq_ii,
        [OP_NEQ_II] = &&op_neq_ii,
        [OP_INC_II] = &&op_inc_ii,
        [OP_DEC_II] = &&op_dec_ii,
        [OP_NEG_II] = &&op_neg_ii,
        [OP_LT_II_JMP_FALSE] = &&op_lt_ii_jmp_false,
        [OP_LTE_II_JMP_FALSE] = &&op_lte_ii_jmp_false,
        [OP_GT_II_JMP_FALSE] = &&op_gt_ii_jmp_false,
        [OP_GTE_II_JMP_FALSE] = &&op_gte_ii_jmp_false,
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
        /* Exception handling */
        [OP_TRY] = &&op_try,
        [OP_TRY_END] = &&op_try_end,
        [OP_THROW] = &&op_throw,
        [OP_CATCH] = &&op_catch,
        /* Classes and OOP */
        [OP_CLASS] = &&op_class,
        [OP_INHERIT] = &&op_inherit,
        [OP_METHOD] = &&op_method,
        [OP_FIELD] = &&op_field,
        [OP_GET_FIELD] = &&op_get_field,
        [OP_SET_FIELD] = &&op_set_field,
        [OP_GET_FIELD_IC] = &&op_get_field_ic,
        [OP_SET_FIELD_IC] = &&op_set_field_ic,
        [OP_GET_FIELD_PIC] = &&op_get_field_pic,
        [OP_SET_FIELD_PIC] = &&op_set_field_pic,
        [OP_INVOKE] = &&op_invoke,
        [OP_INVOKE_IC] = &&op_invoke_ic,
        [OP_INVOKE_PIC] = &&op_invoke_pic,
        [OP_SUPER_INVOKE] = &&op_super_invoke,
        [OP_GET_SUPER] = &&op_get_super,
        [OP_STATIC] = &&op_static,
        [OP_GET_STATIC] = &&op_get_static,
        [OP_SET_STATIC] = &&op_set_static,
        [OP_BIND_METHOD] = &&op_bind_method,
        /* Generators */
        [OP_GENERATOR] = &&op_generator,
        [OP_YIELD] = &&op_yield,
        [OP_YIELD_FROM] = &&op_yield_from,
        [OP_GEN_NEXT] = &&op_gen_next,
        [OP_GEN_SEND] = &&op_gen_send,
        [OP_GEN_RETURN] = &&op_gen_return,
        /* Async/Await */
        [OP_ASYNC] = &&op_async,
        [OP_AWAIT] = &&op_await,
        [OP_PROMISE] = &&op_promise,
        [OP_RESOLVE] = &&op_resolve,
        [OP_REJECT] = &&op_reject,
        /* Decorators */
        [OP_DECORATOR] = &&op_decorator,
        /* Modules */
        [OP_MODULE] = &&op_module,
        [OP_EXPORT] = &&op_export,
        [OP_IMPORT_FROM] = &&op_import_from,
        [OP_IMPORT_AS] = &&op_import_as,
        /* SIMD Array Operations */
        [OP_ARRAY_ADD] = &&op_array_add,
        [OP_ARRAY_SUB] = &&op_array_sub,
        [OP_ARRAY_MUL] = &&op_array_mul,
        [OP_ARRAY_DIV] = &&op_array_div,
        [OP_ARRAY_SUM] = &&op_array_sum,
        [OP_ARRAY_DOT] = &&op_array_dot,
        [OP_ARRAY_MAP] = &&op_array_map,
        [OP_ARRAY_FILTER] = &&op_array_filter,
        [OP_ARRAY_REDUCE] = &&op_array_reduce,
        /* Extended opcode prefix - for opcodes >= 256 */
        [OP_EXTENDED] = &&op_extended,
    };

    /* Secondary dispatch table for extended opcodes (>= 256) */
    /* Index is (opcode - 256) */
    static void *extended_dispatch_table[] = {
        /* Regex - extended index 0-2 */
        [OP_REGEX_MATCH - 256] = &&op_regex_match,
        [OP_REGEX_FIND - 256] = &&op_regex_find,
        [OP_REGEX_REPLACE - 256] = &&op_regex_replace,
        /* Hashing - extended index 3-5 */
        [OP_HASH - 256] = &&op_hash,
        [OP_HASH_SHA256 - 256] = &&op_hash_sha256,
        [OP_HASH_MD5 - 256] = &&op_hash_md5,
        /* Tensor operations */
        [OP_TENSOR - 256] = &&op_tensor,
        [OP_TENSOR_ZEROS - 256] = &&op_tensor_zeros,
        [OP_TENSOR_ONES - 256] = &&op_tensor_ones,
        [OP_TENSOR_RAND - 256] = &&op_tensor_rand,
        [OP_TENSOR_RANDN - 256] = &&op_tensor_randn,
        [OP_TENSOR_ARANGE - 256] = &&op_tensor_arange,
        [OP_TENSOR_LINSPACE - 256] = &&op_tensor_linspace,
        [OP_TENSOR_EYE - 256] = &&op_tensor_eye,
        [OP_TENSOR_SHAPE - 256] = &&op_tensor_shape,
        [OP_TENSOR_RESHAPE - 256] = &&op_tensor_reshape,
        [OP_TENSOR_TRANSPOSE - 256] = &&op_tensor_transpose,
        [OP_TENSOR_FLATTEN - 256] = &&op_tensor_flatten,
        [OP_TENSOR_SQUEEZE - 256] = &&op_tensor_squeeze,
        [OP_TENSOR_UNSQUEEZE - 256] = &&op_tensor_unsqueeze,
        [OP_TENSOR_ADD - 256] = &&op_tensor_add,
        [OP_TENSOR_SUB - 256] = &&op_tensor_sub,
        [OP_TENSOR_MUL - 256] = &&op_tensor_mul,
        [OP_TENSOR_DIV - 256] = &&op_tensor_div,
        [OP_TENSOR_POW - 256] = &&op_tensor_pow,
        [OP_TENSOR_NEG - 256] = &&op_tensor_neg,
        [OP_TENSOR_ABS - 256] = &&op_tensor_abs,
        [OP_TENSOR_SQRT - 256] = &&op_tensor_sqrt,
        [OP_TENSOR_EXP - 256] = &&op_tensor_exp,
        [OP_TENSOR_LOG - 256] = &&op_tensor_log,
        [OP_TENSOR_SUM - 256] = &&op_tensor_sum,
        [OP_TENSOR_MEAN - 256] = &&op_tensor_mean,
        [OP_TENSOR_MIN - 256] = &&op_tensor_min,
        [OP_TENSOR_MAX - 256] = &&op_tensor_max,
        [OP_TENSOR_ARGMIN - 256] = &&op_tensor_argmin,
        [OP_TENSOR_ARGMAX - 256] = &&op_tensor_argmax,
        [OP_TENSOR_MATMUL - 256] = &&op_tensor_matmul,
        [OP_TENSOR_DOT - 256] = &&op_tensor_dot,
        [OP_TENSOR_NORM - 256] = &&op_tensor_norm,
        [OP_TENSOR_GET - 256] = &&op_tensor_get,
        [OP_TENSOR_SET - 256] = &&op_tensor_set,
        /* Matrix operations */
        [OP_MATRIX - 256] = &&op_matrix,
        [OP_MATRIX_ZEROS - 256] = &&op_matrix_zeros,
        [OP_MATRIX_ONES - 256] = &&op_matrix_ones,
        [OP_MATRIX_EYE - 256] = &&op_matrix_eye,
        [OP_MATRIX_RAND - 256] = &&op_matrix_rand,
        [OP_MATRIX_DIAG - 256] = &&op_matrix_diag,
        [OP_MATRIX_ADD - 256] = &&op_matrix_add,
        [OP_MATRIX_SUB - 256] = &&op_matrix_sub,
        [OP_MATRIX_MUL - 256] = &&op_matrix_mul,
        [OP_MATRIX_MATMUL - 256] = &&op_matrix_matmul,
        [OP_MATRIX_SCALE - 256] = &&op_matrix_scale,
        [OP_MATRIX_T - 256] = &&op_matrix_t,
        [OP_MATRIX_INV - 256] = &&op_matrix_inv,
        [OP_MATRIX_DET - 256] = &&op_matrix_det,
        [OP_MATRIX_TRACE - 256] = &&op_matrix_trace,
        [OP_MATRIX_SOLVE - 256] = &&op_matrix_solve,
        /* Autograd */
        [OP_GRAD_TAPE - 256] = &&op_grad_tape,
        /* Neural network */
        [OP_NN_RELU - 256] = &&op_nn_relu,
        [OP_NN_SIGMOID - 256] = &&op_nn_sigmoid,
        [OP_NN_TANH - 256] = &&op_nn_tanh,
        [OP_NN_SOFTMAX - 256] = &&op_nn_softmax,
        [OP_NN_MSE_LOSS - 256] = &&op_nn_mse_loss,
        [OP_NN_CE_LOSS - 256] = &&op_nn_ce_loss,
    };

#define DISPATCH()                      \
    do                                  \
    {                                   \
        if (vm->debug_mode)             \
            debug_trace_op(vm, ip, sp); \
        goto *dispatch_table[*ip++];    \
    } while (0)
#define CASE(name) op_##name

    DISPATCH();

#else
/* Traditional switch dispatch */
#define DISPATCH()                      \
    do                                  \
    {                                   \
        if (vm->debug_mode)             \
            debug_trace_op(vm, ip, sp); \
        continue;                       \
    } while (0)
#define CASE(name) case OP_##name

    for (;;)
    {
        if (vm->debug_mode)
            debug_trace_op(vm, ip, sp);
        switch (*ip++)
        {
#endif

    CASE(const) :
    {
        PUSH(READ_CONST());
        DISPATCH();
    }

    CASE(const_long) :
    {
        uint16_t idx = READ_SHORT();
        PUSH(vm->chunk.constants[idx]);
        DISPATCH();
    }

    CASE(nil) :
    {
        PUSH(VAL_NIL);
        DISPATCH();
    }

    CASE(true) :
    {
        PUSH(VAL_TRUE);
        DISPATCH();
    }

    CASE(false) :
    {
        PUSH(VAL_FALSE);
        DISPATCH();
    }

    CASE(pop) :
    {
        POP();
        DISPATCH();
    }

    CASE(popn) :
    {
        uint8_t n = READ_BYTE();
        sp -= n;
        DISPATCH();
    }

    CASE(dup) :
    {
        Value v = PEEK(0); /* Read BEFORE modifying sp to avoid UB */
        PUSH(v);
        DISPATCH();
    }

    CASE(get_local) :
    {
        uint8_t slot = READ_BYTE();
        PUSH(bp[slot]);
        DISPATCH();
    }

    CASE(set_local) :
    {
        uint8_t slot = READ_BYTE();
        bp[slot] = PEEK(0);
        DISPATCH();
    }

    CASE(get_global) :
    {
        ObjString *name = AS_STRING(READ_CONST());
        Value value;
        vm->sp = sp; /* Sync before potential error */
        if (!table_get(vm, name, &value))
        {
            runtime_error(vm, "Undefined variable '%s'.", name->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        PUSH(value);
        DISPATCH();
    }

    CASE(set_global) :
    {
        ObjString *name = AS_STRING(READ_CONST());
        table_set(vm, name, PEEK(0));
        DISPATCH();
    }

    CASE(get_upvalue) :
    {
        uint8_t slot = READ_BYTE();
        CallFrame *frame = &vm->frames[vm->frame_count - 1];
        ObjClosure *closure = frame->closure;
        PUSH(*closure->upvalues[slot]->location);
        DISPATCH();
    }

    CASE(set_upvalue) :
    {
        uint8_t slot = READ_BYTE();
        CallFrame *frame = &vm->frames[vm->frame_count - 1];
        ObjClosure *closure = frame->closure;
        *closure->upvalues[slot]->location = PEEK(0);
        DISPATCH();
    }

    CASE(closure) :
    {
        ObjFunction *function = AS_FUNCTION(READ_CONST());
        ObjClosure *closure = new_closure(vm, function);
        PUSH(val_obj(closure));

        for (int i = 0; i < closure->upvalue_count; i++)
        {
            uint8_t is_local = READ_BYTE();
            uint8_t index = READ_BYTE();
            if (is_local)
            {
                closure->upvalues[i] = capture_upvalue(vm, bp + index);
            }
            else
            {
                CallFrame *frame = &vm->frames[vm->frame_count - 1];
                ObjClosure *enclosing = frame->closure;
                closure->upvalues[i] = enclosing->upvalues[index];
            }
        }
        DISPATCH();
    }

    CASE(close_upvalue) :
    {
        close_upvalues(vm, sp - 1);
        POP();
        DISPATCH();
    }

    CASE(add) :
    {
        Value b = POP();
        Value a = POP();

        if (IS_STRING(a) && IS_STRING(b))
        {
            /* String concatenation */
            ObjString *as = AS_STRING(a);
            ObjString *bs = AS_STRING(b);
            int length = as->length + bs->length;
            char *chars = ALLOCATE(vm, char, length + 1);
            memcpy(chars, as->chars, as->length);
            memcpy(chars + as->length, bs->chars, bs->length);
            chars[length] = '\0';
            ObjString *result = copy_string(vm, chars, length);
            FREE_ARRAY(vm, char, chars, length + 1);
            PUSH(val_obj(result));
        }
        else if (IS_INT(a) && IS_INT(b))
        {
            PUSH(val_int(as_int(a) + as_int(b)));
        }
        else
        {
            double da = IS_INT(a) ? as_int(a) : as_num(a);
            double db = IS_INT(b) ? as_int(b) : as_num(b);
            PUSH(val_num(da + db));
        }
        DISPATCH();
    }

    CASE(sub) : BINARY_OP(-);
    DISPATCH();
    CASE(mul) : BINARY_OP(*);
    DISPATCH();

    CASE(div) :
    {
        Value b = POP();
        Value a = POP();
        if (IS_INT(a) && IS_INT(b))
        {
            PUSH(val_int(as_int(a) / as_int(b)));
        }
        else
        {
            double da = IS_INT(a) ? as_int(a) : as_num(a);
            double db = IS_INT(b) ? as_int(b) : as_num(b);
            PUSH(val_num(da / db));
        }
        DISPATCH();
    }

    CASE(mod) : BINARY_OP_INT(%);
    DISPATCH();

    CASE(neg) :
    {
        Value v = POP();
        if (IS_INT(v))
        {
            PUSH(val_int(-as_int(v)));
        }
        else
        {
            PUSH(val_num(-as_num(v)));
        }
        DISPATCH();
    }

    CASE(inc) :
    {
        Value v = POP();
        if (IS_INT(v))
        {
            PUSH(val_int(as_int(v) + 1));
        }
        else
        {
            PUSH(val_num(as_num(v) + 1));
        }
        DISPATCH();
    }

    CASE(dec) :
    {
        Value v = POP();
        if (IS_INT(v))
        {
            PUSH(val_int(as_int(v) - 1));
        }
        else
        {
            PUSH(val_num(as_num(v) - 1));
        }
        DISPATCH();
    }

    CASE(pow) :
    {
        Value b = POP();
        Value a = POP();
        double da = IS_INT(a) ? as_int(a) : as_num(a);
        double db = IS_INT(b) ? as_int(b) : as_num(b);
        PUSH(val_num(pow(da, db)));
        DISPATCH();
    }

    CASE(eq) :
    {
        Value b = POP();
        Value a = POP();
        /* Handle string comparison by value */
        if (IS_OBJ(a) && IS_OBJ(b))
        {
            Obj *obj_a = (Obj *)as_obj(a);
            Obj *obj_b = (Obj *)as_obj(b);
            if (obj_a->type == OBJ_STRING && obj_b->type == OBJ_STRING)
            {
                ObjString *str_a = (ObjString *)obj_a;
                ObjString *str_b = (ObjString *)obj_b;
                bool equal = str_a->length == str_b->length &&
                             memcmp(str_a->chars, str_b->chars, str_a->length) == 0;
                PUSH(val_bool(equal));
                DISPATCH();
            }
        }
        /* Handle int vs float comparison: compare numeric values */
        if ((IS_INT(a) && IS_NUM(b)) || (IS_NUM(a) && IS_INT(b)) ||
            (IS_INT(a) && IS_INT(b)) || (IS_NUM(a) && IS_NUM(b) && !IS_OBJ(a) && !IS_OBJ(b)))
        {
            double da = IS_INT(a) ? (double)as_int(a) : as_num(a);
            double db = IS_INT(b) ? (double)as_int(b) : as_num(b);
            PUSH(val_bool(da == db));
            DISPATCH();
        }
        PUSH(val_bool(a == b));
        DISPATCH();
    }

    CASE(neq) :
    {
        Value b = POP();
        Value a = POP();
        /* Handle string comparison by value */
        if (IS_OBJ(a) && IS_OBJ(b))
        {
            Obj *obj_a = (Obj *)as_obj(a);
            Obj *obj_b = (Obj *)as_obj(b);
            if (obj_a->type == OBJ_STRING && obj_b->type == OBJ_STRING)
            {
                ObjString *str_a = (ObjString *)obj_a;
                ObjString *str_b = (ObjString *)obj_b;
                bool equal = str_a->length == str_b->length &&
                             memcmp(str_a->chars, str_b->chars, str_a->length) == 0;
                PUSH(val_bool(!equal));
                DISPATCH();
            }
        }
        /* Handle int vs float comparison: compare numeric values */
        if ((IS_INT(a) && IS_NUM(b)) || (IS_NUM(a) && IS_INT(b)) ||
            (IS_INT(a) && IS_INT(b)) || (IS_NUM(a) && IS_NUM(b) && !IS_OBJ(a) && !IS_OBJ(b)))
        {
            double da = IS_INT(a) ? (double)as_int(a) : as_num(a);
            double db = IS_INT(b) ? (double)as_int(b) : as_num(b);
            PUSH(val_bool(da != db));
            DISPATCH();
        }
        PUSH(val_bool(a != b));
        DISPATCH();
    }

    CASE(lt) : COMPARE_OP(<);
    DISPATCH();
    CASE(gt) : COMPARE_OP(>);
    DISPATCH();
    CASE(lte) : COMPARE_OP(<=);
    DISPATCH();
    CASE(gte) : COMPARE_OP(>=);
    DISPATCH();

    CASE(not) :
    {
        Value v = POP();
        PUSH(val_bool(!is_truthy(v)));
        DISPATCH();
    }

    CASE(and) :
    {
        uint16_t offset = READ_SHORT();
        if (!is_truthy(PEEK(0)))
        {
            ip += offset;
        }
        else
        {
            POP();
        }
        DISPATCH();
    }

    CASE(or) :
    {
        uint16_t offset = READ_SHORT();
        if (is_truthy(PEEK(0)))
        {
            ip += offset;
        }
        else
        {
            POP();
        }
        DISPATCH();
    }

    CASE(band) : BINARY_OP_INT(&);
    DISPATCH();
    CASE(bor) : BINARY_OP_INT(|);
    DISPATCH();
    CASE(bxor) : BINARY_OP_INT(^);
    DISPATCH();
    CASE(bnot) :
    {
        Value v = POP();
        int32_t i = IS_INT(v) ? as_int(v) : (int32_t)as_num(v);
        PUSH(val_int(~i));
        DISPATCH();
    }
    CASE(shl) : BINARY_OP_INT(<<);
    DISPATCH();
    CASE(shr) : BINARY_OP_INT(>>);
    DISPATCH();

    CASE(jmp) :
    {
        uint16_t offset = READ_SHORT();
        ip += offset;
        DISPATCH();
    }

    CASE(jmp_false) :
    {
        uint16_t offset = READ_SHORT();
        Value v = PEEK(0);
        /* Fast path for booleans */
        if (v == VAL_FALSE || v == VAL_NIL)
        {
            ip += offset;
        }
        else if (v != VAL_TRUE && !is_truthy(v))
        {
            ip += offset;
        }
        DISPATCH();
    }

    CASE(jmp_true) :
    {
        uint16_t offset = READ_SHORT();
        Value v = PEEK(0);
        /* Fast path for booleans */
        if (v == VAL_TRUE)
        {
            ip += offset;
        }
        else if (v != VAL_FALSE && v != VAL_NIL && is_truthy(v))
        {
            ip += offset;
        }
        DISPATCH();
    }

    CASE(loop) :
    {
        uint16_t offset = READ_SHORT();
        ip -= offset;
        DISPATCH();
    }

    CASE(call) :
    {
        uint8_t arg_count = READ_BYTE();
        Value callee = PEEK(arg_count);

        ObjFunction *function = NULL;
        ObjClosure *closure = NULL;

        if (IS_FUNCTION(callee))
        {
            function = AS_FUNCTION(callee);
        }
        else if (IS_CLOSURE(callee))
        {
            closure = AS_CLOSURE(callee);
            function = closure->function;
        }
        else if (IS_CLASS(callee))
        {
            /* Class instantiation */
            ObjClass *klass = AS_CLASS(callee);
            vm->sp = sp; /* Sync sp for allocation */

            /* Create instance with space for dynamic fields
             * Pre-allocate CLASS_MAX_FIELDS to allow dynamic field addition
             * without reallocation. This is a trade-off between memory usage
             * and performance - avoids O(n) reallocation on field addition.
             */
            size_t size = sizeof(ObjInstance) + sizeof(Value) * CLASS_MAX_FIELDS;
            ObjInstance *instance = (ObjInstance *)pseudo_realloc(vm, NULL, 0, size);
            instance->obj.type = OBJ_INSTANCE;
            instance->obj.next = vm->objects;
            instance->obj.marked = false;
            vm->objects = (Obj *)instance;
            instance->klass = klass;

            /* Initialize all fields to nil (including space for dynamic fields) */
            for (uint16_t i = 0; i < CLASS_MAX_FIELDS; i++)
            {
                instance->fields[i] = VAL_NIL;
            }

            /* Replace class with instance on stack */
            sp[-arg_count - 1] = val_obj(instance);

            /* Look for init method */
            ObjFunction *init_method = NULL;
            ObjClosure *init_closure = NULL;
            for (uint16_t i = 0; i < klass->method_count; i++)
            {
                if (klass->method_names[i]->length == 4 &&
                    memcmp(klass->method_names[i]->chars, "init", 4) == 0)
                {
                    Value method_val = klass->methods[i];
                    if (IS_FUNCTION(method_val))
                    {
                        init_method = AS_FUNCTION(method_val);
                    }
                    else if (IS_CLOSURE(method_val))
                    {
                        init_closure = AS_CLOSURE(method_val);
                        init_method = init_closure->function;
                    }
                    break;
                }
            }

            if (init_method != NULL)
            {
                /* Call init method */
                if (arg_count != init_method->arity)
                {
                    vm->sp = sp;
                    runtime_error(vm, "Expected %d arguments but got %d.",
                                  init_method->arity, arg_count);
                    return INTERPRET_RUNTIME_ERROR;
                }

                CallFrame *frame = &vm->frames[vm->frame_count++];
                frame->function = init_method;
                frame->closure = init_closure;
                frame->ip = ip;
                frame->slots = sp - arg_count - 1;
                frame->is_init = true; /* Mark as init call */
                bp = frame->slots;

                ip = vm->chunk.code + init_method->code_start;
            }
            else
            {
                /* No init method, just pop arguments and leave instance */
                sp -= arg_count;
            }
            DISPATCH();
        }
        else
        {
            vm->sp = sp;
            runtime_error(vm, "Can only call functions.");
            return INTERPRET_RUNTIME_ERROR;
        }

        if (arg_count != function->arity)
        {
            vm->sp = sp;
            runtime_error(vm, "Expected %d arguments but got %d.",
                          function->arity, arg_count);
            return INTERPRET_RUNTIME_ERROR;
        }

        if (vm->frame_count == FRAMES_MAX)
        {
            vm->sp = sp;
            runtime_error(vm, "Stack overflow.");
            return INTERPRET_RUNTIME_ERROR;
        }

        /* FUNCTION INLINING: For small functions without closures,
         * execute inline without pushing a call frame.
         * Criteria: can_inline flag set, no closure (upvalues), not recursive */
        if (function->can_inline && closure == NULL && vm->frame_count < FRAMES_MAX - 1)
        {
            /* Inline execution: don't push a new frame, just adjust bp.
             * Stack layout: [callee][arg0][arg1]...[argN] <- sp
             * We want: [arg0][arg1]...[argN] at slots 1..N (slot 0 is callee)
             *
             * The callee is at sp[-arg_count-1], args at sp[-arg_count] to sp[-1].
             * For the inlined function, slot 0 should be the callee (for self ref),
             * and slots 1..arity are the arguments.
             *
             * We can set bp = sp - arg_count - 1 (points to callee slot)
             * Then bp[0] = callee, bp[1] = arg0, bp[2] = arg1, etc.
             *
             * To return: we need to save where to come back and restore bp.
             * Use a lightweight "inline frame" that just tracks return location.
             */

            /* Save current state for inline return */
            CallFrame *frame = &vm->frames[vm->frame_count++];
            frame->function = function;
            frame->closure = NULL;
            frame->ip = ip;                    /* Return address */
            frame->slots = sp - arg_count - 1; /* bp for inlined function */
            frame->is_init = false;

            bp = frame->slots;
            ip = vm->chunk.code + function->code_start;
            DISPATCH();
        }

        CallFrame *frame = &vm->frames[vm->frame_count++];
        frame->function = function;
        frame->closure = closure; /* NULL for plain functions */
        frame->ip = ip;
        frame->slots = sp - arg_count - 1;
        frame->is_init = false;
        bp = frame->slots; /* Update cached base pointer */

        ip = vm->chunk.code + function->code_start;
        DISPATCH();
    }

    CASE(return) :
    {
        Value result = POP();

        if (vm->frame_count == 0)
        {
            /* Returning from top-level - shouldn't happen normally */
            vm->sp = sp;
            return INTERPRET_OK;
        }

        /* Close any upvalues owned by this call frame */
        vm->sp = sp;

        CallFrame *returning_frame = &vm->frames[vm->frame_count - 1];
        close_upvalues(vm, returning_frame->slots);

        /* If returning from init, use instance (slot 0) instead of result */
        if (returning_frame->is_init)
        {
            result = returning_frame->slots[0];
        }

        vm->frame_count--;
        CallFrame *frame = &vm->frames[vm->frame_count];
        sp = frame->slots;
        ip = frame->ip;
        /* Restore bp to previous frame or stack base */
        bp = (vm->frame_count > 0)
                 ? vm->frames[vm->frame_count - 1].slots
                 : vm->stack;
        PUSH(result);
        DISPATCH();
    }

    CASE(array) :
    {
        uint8_t count = READ_BYTE();
        vm->sp = sp; /* Sync before allocation */
        ObjArray *array = new_array(vm, count);

        /* Pop elements in reverse order */
        sp -= count;
        for (int i = 0; i < count; i++)
        {
            array->values[i] = sp[i];
        }
        array->count = count;

        PUSH(val_obj(array));
        DISPATCH();
    }

    CASE(index) :
    {
        Value index_val = POP();
        Value target_obj = POP();

        if (IS_ARRAY(target_obj))
        {
            ObjArray *array = AS_ARRAY(target_obj);
            int32_t index = IS_INT(index_val) ? as_int(index_val) : (int32_t)as_num(index_val);
            if (index < 0 || index >= (int32_t)array->count)
            {
                runtime_error(vm, "Array index out of bounds.");
                return INTERPRET_RUNTIME_ERROR;
            }
            PUSH(array->values[index]);
        }
        else if (IS_STRING(target_obj))
        {
            ObjString *str = AS_STRING(target_obj);
            int32_t index = IS_INT(index_val) ? as_int(index_val) : (int32_t)as_num(index_val);
            if (index < 0 || index >= (int32_t)str->length)
            {
                runtime_error(vm, "String index out of bounds.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjString *ch = copy_string(vm, str->chars + index, 1);
            PUSH(val_obj(ch));
        }
        else
        {
            runtime_error(vm, "Only arrays and strings can be indexed.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

    CASE(index_set) :
    {
        Value value = POP();
        Value index_val = POP();
        Value target_obj = POP();

        if (!IS_ARRAY(target_obj))
        {
            runtime_error(vm, "Only arrays support index assignment.");
            return INTERPRET_RUNTIME_ERROR;
        }

        ObjArray *array = AS_ARRAY(target_obj);
        int32_t index = IS_INT(index_val) ? as_int(index_val) : (int32_t)as_num(index_val);
        if (index < 0 || index >= (int32_t)array->count)
        {
            runtime_error(vm, "Array index out of bounds.");
            return INTERPRET_RUNTIME_ERROR;
        }

        array->values[index] = value;
        PUSH(value);
        DISPATCH();
    }

    /* Fast (unchecked) array access for JIT when bounds are proven safe */
    CASE(index_fast) :
    {
        Value index_val = POP();
        Value target_obj = POP();
        ObjArray *array = AS_ARRAY(target_obj);
        int32_t index = IS_INT(index_val) ? as_int(index_val) : (int32_t)as_num(index_val);
        /* No bounds check - JIT has proven this is safe */
        PUSH(array->values[index]);
        DISPATCH();
    }

    CASE(index_set_fast) :
    {
        Value value = POP();
        Value index_val = POP();
        Value target_obj = POP();
        ObjArray *array = AS_ARRAY(target_obj);
        int32_t index = IS_INT(index_val) ? as_int(index_val) : (int32_t)as_num(index_val);
        /* No bounds check - JIT has proven this is safe */
        array->values[index] = value;
        PUSH(value);
        DISPATCH();
    }

    CASE(len) :
    {
        Value v = POP();
        if (IS_ARRAY(v))
        {
            PUSH(val_int(AS_ARRAY(v)->count));
        }
        else if (IS_STRING(v))
        {
            PUSH(val_int(AS_STRING(v)->length));
        }
        else
        {
            runtime_error(vm, "Operand must be an array or string.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

    CASE(push) :
    {
        Value value = POP();
        Value arr_val = POP();

        if (!IS_ARRAY(arr_val))
        {
            runtime_error(vm, "Can only push to arrays.");
            return INTERPRET_RUNTIME_ERROR;
        }

        ObjArray *array = AS_ARRAY(arr_val);
        if (array->count >= array->capacity)
        {
            uint32_t new_cap = GROW_CAPACITY(array->capacity);
            array->values = GROW_ARRAY(vm, Value, array->values,
                                       array->capacity, new_cap);
            array->capacity = new_cap;
        }
        array->values[array->count++] = value;
        PUSH(arr_val);
        DISPATCH();
    }

    CASE(pop_array) :
    {
        Value arr_val = POP();

        if (!IS_ARRAY(arr_val))
        {
            runtime_error(vm, "Can only pop from arrays.");
            return INTERPRET_RUNTIME_ERROR;
        }

        ObjArray *array = AS_ARRAY(arr_val);
        if (array->count == 0)
        {
            runtime_error(vm, "Cannot pop from empty array.");
            return INTERPRET_RUNTIME_ERROR;
        }

        PUSH(array->values[--array->count]);
        DISPATCH();
    }

    CASE(slice) :
    {
        Value end_val = POP();
        Value start_val = POP();
        Value target_obj = POP();

        int32_t start = IS_INT(start_val) ? as_int(start_val) : (int32_t)as_num(start_val);
        int32_t end = IS_INT(end_val) ? as_int(end_val) : (int32_t)as_num(end_val);

        if (IS_ARRAY(target_obj))
        {
            ObjArray *arr = AS_ARRAY(target_obj);
            if (start < 0)
                start += arr->count;
            if (end < 0)
                end += arr->count;
            if (start < 0)
                start = 0;
            if ((uint32_t)end > arr->count)
                end = arr->count;

            uint32_t len = (end > start) ? end - start : 0;
            ObjArray *result = new_array(vm, len);
            for (uint32_t i = 0; i < len; i++)
            {
                result->values[i] = arr->values[start + i];
            }
            result->count = len;
            PUSH(val_obj(result));
        }
        else if (IS_STRING(target_obj))
        {
            ObjString *str = AS_STRING(target_obj);
            if (start < 0)
                start += str->length;
            if (end < 0)
                end += str->length;
            if (start < 0)
                start = 0;
            if (end > str->length)
                end = str->length;

            int len = (end > start) ? end - start : 0;
            PUSH(val_obj(copy_string(vm, str->chars + start, len)));
        }
        else
        {
            runtime_error(vm, "Can only slice arrays and strings.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

    CASE(concat) :
    {
        Value b = POP();
        Value a = POP();

        if (IS_ARRAY(a) && IS_ARRAY(b))
        {
            ObjArray *aa = AS_ARRAY(a);
            ObjArray *ab = AS_ARRAY(b);
            ObjArray *result = new_array(vm, aa->count + ab->count);
            memcpy(result->values, aa->values, aa->count * sizeof(Value));
            memcpy(result->values + aa->count, ab->values, ab->count * sizeof(Value));
            result->count = aa->count + ab->count;
            PUSH(val_obj(result));
        }
        else
        {
            runtime_error(vm, "Can only concatenate arrays.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

    CASE(range) :
    {
        Value end_val = POP();
        Value start_val = POP();

        int32_t start = IS_INT(start_val) ? as_int(start_val) : (int32_t)as_num(start_val);
        int32_t end = IS_INT(end_val) ? as_int(end_val) : (int32_t)as_num(end_val);

        ObjRange *range = new_range(vm, start, end);
        PUSH(val_obj(range));
        DISPATCH();
    }

    CASE(iter_next) :
    {
        uint16_t offset = READ_SHORT();
        Value iter_val = PEEK(0);

        if (IS_RANGE(iter_val))
        {
            ObjRange *range = AS_RANGE(iter_val);
            if (range->current >= range->end)
            {
                POP(); /* Remove iterator */
                ip += offset;
            }
            else
            {
                PUSH(val_int(range->current++));
            }
        }
        else
        {
            runtime_error(vm, "Cannot iterate over this type.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

    CASE(iter_array) :
    {
        /* Array iteration with internal index tracking */
        uint16_t offset = READ_SHORT();
        Value arr_val = PEEK(1); /* Array below index */
        Value idx_val = PEEK(0); /* Current index */

        if (IS_ARRAY(arr_val))
        {
            ObjArray *arr = AS_ARRAY(arr_val);
            int32_t idx = as_int(idx_val);
            if ((uint32_t)idx >= arr->count)
            {
                sp -= 2; /* Remove array and index */
                ip += offset;
            }
            else
            {
                sp[-1] = val_int(idx + 1); /* Increment index */
                PUSH(arr->values[idx]);    /* Push current element */
            }
        }
        else
        {
            vm->sp = sp;
            runtime_error(vm, "Expected array for iteration.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

    CASE(print) :
    {
        print_value(POP());
        printf("\n");
        DISPATCH();
    }

    CASE(println) :
    {
        print_value(POP());
        printf("\n");
        DISPATCH();
    }

    CASE(time) :
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        // Return seconds since Unix epoch as a float (with sub-second precision)
        double timestamp = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
        PUSH(val_num(timestamp));
        DISPATCH();
    }

    CASE(input) :
    {
        char buffer[1024];
        if (fgets(buffer, sizeof(buffer), stdin))
        {
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\n')
                buffer[--len] = '\0';
            ObjString *str = copy_string(vm, buffer, len);
            PUSH(val_obj(str));
        }
        else
        {
            PUSH(VAL_NIL);
        }
        DISPATCH();
    }

    CASE(int) :
    {
        Value v = POP();
        if (IS_INT(v))
        {
            PUSH(v);
        }
        else if (IS_NUM(v))
        {
            PUSH(val_int((int32_t)as_num(v)));
        }
        else if (IS_STRING(v))
        {
            PUSH(val_int(atoi(AS_STRING(v)->chars)));
        }
        else
        {
            PUSH(val_int(0));
        }
        DISPATCH();
    }

    CASE(float) :
    {
        Value v = POP();
        double d = IS_INT(v) ? (double)as_int(v) : IS_NUM(v)  ? as_num(v)
                                               : IS_STRING(v) ? atof(AS_STRING(v)->chars)
                                                              : 0.0;
        PUSH(val_num(d));
        DISPATCH();
    }

    CASE(str) :
    {
        Value v = POP();
        /* If it's already a string, just return it */
        if (IS_STRING(v))
        {
            PUSH(v);
            DISPATCH();
        }
        /* Use helper to convert to string (handles arrays, dicts, etc.) */
        char *buf = NULL;
        size_t len = 0, cap = 0;
        value_to_string_impl(v, &buf, &len, &cap);
        ObjString *result = copy_string(vm, buf ? buf : "", buf ? len : 0);
        free(buf);
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(type) :
    {
        Value v = POP();
        const char *type_name;
        if (IS_INT(v) || IS_NUM(v))
            type_name = "number";
        else if (IS_BOOL(v))
            type_name = "bool";
        else if (IS_NIL(v))
            type_name = "nil";
        else if (IS_STRING(v))
            type_name = "string";
        else if (IS_ARRAY(v))
            type_name = "array";
        else if (IS_FUNCTION(v))
            type_name = "function";
        else
            type_name = "object";
        PUSH(val_obj(copy_string(vm, type_name, strlen(type_name))));
        DISPATCH();
    }

    CASE(abs) :
    {
        Value v = POP();
        if (IS_INT(v))
        {
            int32_t i = as_int(v);
            PUSH(val_int(i < 0 ? -i : i));
        }
        else
        {
            PUSH(val_num(fabs(as_num(v))));
        }
        DISPATCH();
    }

    CASE(min) :
    {
        Value b = POP();
        Value a = POP();
        double da = IS_INT(a) ? as_int(a) : as_num(a);
        double db = IS_INT(b) ? as_int(b) : as_num(b);
        PUSH(da < db ? a : b);
        DISPATCH();
    }

    CASE(max) :
    {
        Value b = POP();
        Value a = POP();
        double da = IS_INT(a) ? as_int(a) : as_num(a);
        double db = IS_INT(b) ? as_int(b) : as_num(b);
        PUSH(da > db ? a : b);
        DISPATCH();
    }

    CASE(sqrt) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(sqrt(d)));
        DISPATCH();
    }

    CASE(floor) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_int((int32_t)floor(d)));
        DISPATCH();
    }

    CASE(ceil) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_int((int32_t)ceil(d)));
        DISPATCH();
    }

    CASE(round) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_int((int32_t)round(d)));
        DISPATCH();
    }

    CASE(rand) :
    {
        PUSH(val_num((double)rand() / RAND_MAX));
        DISPATCH();
    }

    /* ============ BIT MANIPULATION INTRINSICS ============ */
    /* Map directly to CPU instructions via GCC builtins */

    CASE(popcount) :
    {
        Value v = POP();
        int32_t n = IS_INT(v) ? as_int(v) : (int32_t)as_num(v);
        PUSH(val_int(__builtin_popcount((unsigned int)n)));
        DISPATCH();
    }

    CASE(clz) :
    {
        Value v = POP();
        int32_t n = IS_INT(v) ? as_int(v) : (int32_t)as_num(v);
        PUSH(val_int(n == 0 ? 32 : __builtin_clz((unsigned int)n)));
        DISPATCH();
    }

    CASE(ctz) :
    {
        Value v = POP();
        int32_t n = IS_INT(v) ? as_int(v) : (int32_t)as_num(v);
        PUSH(val_int(n == 0 ? 32 : __builtin_ctz((unsigned int)n)));
        DISPATCH();
    }

    CASE(rotl) :
    {
        Value vn = POP();
        Value vx = POP();
        uint32_t n = (IS_INT(vn) ? as_int(vn) : (int32_t)as_num(vn)) & 31;
        uint32_t x = IS_INT(vx) ? (uint32_t)as_int(vx) : (uint32_t)as_num(vx);
        PUSH(val_int((int32_t)((x << n) | (x >> (32 - n)))));
        DISPATCH();
    }

    CASE(rotr) :
    {
        Value vn = POP();
        Value vx = POP();
        uint32_t n = (IS_INT(vn) ? as_int(vn) : (int32_t)as_num(vn)) & 31;
        uint32_t x = IS_INT(vx) ? (uint32_t)as_int(vx) : (uint32_t)as_num(vx);
        PUSH(val_int((int32_t)((x >> n) | (x << (32 - n)))));
        DISPATCH();
    }

    /* ============ STRING OPERATIONS ============ */

    CASE(substr) :
    {
        Value vlen = POP();
        Value vstart = POP();
        Value vstr = POP();

        if (!IS_OBJ(vstr))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString *str = (ObjString *)as_obj(vstr);
        int32_t start = IS_INT(vstart) ? as_int(vstart) : (int32_t)as_num(vstart);
        int32_t len = IS_INT(vlen) ? as_int(vlen) : (int32_t)as_num(vlen);

        if (start < 0)
            start = 0;
        if (start >= (int32_t)str->length)
        {
            PUSH(val_obj(copy_string(vm, "", 0)));
            DISPATCH();
        }
        if (len < 0 || start + len > (int32_t)str->length)
        {
            len = str->length - start;
        }

        PUSH(val_obj(copy_string(vm, str->chars + start, len)));
        DISPATCH();
    }

    CASE(upper) :
    {
        Value v = POP();
        if (!IS_OBJ(v))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString *str = (ObjString *)as_obj(v);
        char *buf = malloc(str->length + 1);
        for (uint32_t i = 0; i < str->length; i++)
        {
            char c = str->chars[i];
            buf[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
        }
        buf[str->length] = '\0';
        ObjString *result = copy_string(vm, buf, str->length);
        free(buf);
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(lower) :
    {
        Value v = POP();
        if (!IS_OBJ(v))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString *str = (ObjString *)as_obj(v);
        char *buf = malloc(str->length + 1);
        for (uint32_t i = 0; i < str->length; i++)
        {
            char c = str->chars[i];
            buf[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        }
        buf[str->length] = '\0';
        ObjString *result = copy_string(vm, buf, str->length);
        free(buf);
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(split) :
    {
        Value vdelim = POP();
        Value vstr = POP();

        if (!IS_OBJ(vstr) || !IS_OBJ(vdelim))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString *str = (ObjString *)as_obj(vstr);
        ObjString *delim = (ObjString *)as_obj(vdelim);

        /* Count parts */
        int count = 1;
        const char *p = str->chars;
        while ((p = strstr(p, delim->chars)) != NULL)
        {
            count++;
            p += delim->length;
        }

        /* Create array */
        ObjArray *arr = new_array(vm, count);
        p = str->chars;
        int idx = 0;
        const char *next;
        while ((next = strstr(p, delim->chars)) != NULL)
        {
            arr->values[idx++] = val_obj(copy_string(vm, p, next - p));
            p = next + delim->length;
        }
        arr->values[idx] = val_obj(copy_string(vm, p, str->chars + str->length - p));
        arr->count = count;

        PUSH(val_obj(arr));
        DISPATCH();
    }

    CASE(join) :
    {
        Value vdelim = POP();
        Value varr = POP();

        if (!IS_OBJ(varr) || !IS_OBJ(vdelim))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *arr = (ObjArray *)as_obj(varr);
        ObjString *delim = (ObjString *)as_obj(vdelim);

        if (arr->count == 0)
        {
            PUSH(val_obj(copy_string(vm, "", 0)));
            DISPATCH();
        }

        /* Calculate total length */
        size_t total = 0;
        for (uint32_t i = 0; i < arr->count; i++)
        {
            Value v = arr->values[i];
            if (IS_OBJ(v))
            {
                ObjString *s = (ObjString *)as_obj(v);
                total += s->length;
            }
            if (i > 0)
                total += delim->length;
        }

        /* Build result */
        char *buf = malloc(total + 1);
        char *dst = buf;
        for (uint32_t i = 0; i < arr->count; i++)
        {
            if (i > 0)
            {
                memcpy(dst, delim->chars, delim->length);
                dst += delim->length;
            }
            Value v = arr->values[i];
            if (IS_OBJ(v))
            {
                ObjString *s = (ObjString *)as_obj(v);
                memcpy(dst, s->chars, s->length);
                dst += s->length;
            }
        }
        *dst = '\0';

        ObjString *result = copy_string(vm, buf, total);
        free(buf);
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(replace) :
    {
        Value vto = POP();
        Value vfrom = POP();
        Value vstr = POP();

        if (!IS_OBJ(vstr) || !IS_OBJ(vfrom) || !IS_OBJ(vto))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString *str = (ObjString *)as_obj(vstr);
        ObjString *from = (ObjString *)as_obj(vfrom);
        ObjString *to = (ObjString *)as_obj(vto);

        if (from->length == 0)
        {
            PUSH(vstr);
            DISPATCH();
        }

        /* Count occurrences */
        int count = 0;
        const char *p = str->chars;
        while ((p = strstr(p, from->chars)) != NULL)
        {
            count++;
            p += from->length;
        }

        if (count == 0)
        {
            PUSH(vstr);
            DISPATCH();
        }

        /* Calculate new length */
        size_t new_len = str->length + count * ((int)to->length - (int)from->length);
        char *buf = malloc(new_len + 1);
        char *dst = buf;
        p = str->chars;
        const char *next;

        while ((next = strstr(p, from->chars)) != NULL)
        {
            memcpy(dst, p, next - p);
            dst += next - p;
            memcpy(dst, to->chars, to->length);
            dst += to->length;
            p = next + from->length;
        }
        memcpy(dst, p, str->chars + str->length - p);
        buf[new_len] = '\0';

        ObjString *result = copy_string(vm, buf, new_len);
        free(buf);
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(find) :
    {
        Value vneedle = POP();
        Value vhaystack = POP();

        if (!IS_OBJ(vhaystack) || !IS_OBJ(vneedle))
        {
            PUSH(val_int(-1));
            DISPATCH();
        }
        ObjString *haystack = (ObjString *)as_obj(vhaystack);
        ObjString *needle = (ObjString *)as_obj(vneedle);

        const char *found = strstr(haystack->chars, needle->chars);
        if (found)
        {
            PUSH(val_int((int32_t)(found - haystack->chars)));
        }
        else
        {
            PUSH(val_int(-1));
        }
        DISPATCH();
    }

    CASE(trim) :
    {
        Value v = POP();
        if (!IS_OBJ(v))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString *str = (ObjString *)as_obj(v);

        const char *start = str->chars;
        const char *end = str->chars + str->length;

        while (start < end && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r'))
        {
            start++;
        }
        while (end > start && (*(end - 1) == ' ' || *(end - 1) == '\t' || *(end - 1) == '\n' || *(end - 1) == '\r'))
        {
            end--;
        }

        PUSH(val_obj(copy_string(vm, start, end - start)));
        DISPATCH();
    }

    CASE(char) :
    {
        Value v = POP();
        int32_t code = IS_INT(v) ? as_int(v) : (int32_t)as_num(v);
        char buf[2] = {(char)code, '\0'};
        PUSH(val_obj(copy_string(vm, buf, 1)));
        DISPATCH();
    }

    CASE(ord) :
    {
        Value v = POP();
        if (!IS_OBJ(v))
        {
            PUSH(val_int(0));
            DISPATCH();
        }
        ObjString *str = (ObjString *)as_obj(v);
        if (str->length == 0)
        {
            PUSH(val_int(0));
        }
        else
        {
            PUSH(val_int((unsigned char)str->chars[0]));
        }
        DISPATCH();
    }

    CASE(halt) :
    {
        return INTERPRET_OK;
    }

    /* ============ SUPERINSTRUCTIONS ============ */
    /* These fused instructions eliminate dispatch overhead and provide 2-3x speedup */

    /* Fast local variable access for common slots - using cached bp */
    CASE(get_local_0) :
    {
        PUSH(bp[0]);
        DISPATCH();
    }

    CASE(get_local_1) :
    {
        PUSH(bp[1]);
        DISPATCH();
    }

    CASE(get_local_2) :
    {
        PUSH(bp[2]);
        DISPATCH();
    }

    CASE(get_local_3) :
    {
        PUSH(bp[3]);
        DISPATCH();
    }

    /* Fast constants */
    CASE(const_0) :
    {
        PUSH(val_int(0));
        DISPATCH();
    }

    CASE(const_1) :
    {
        PUSH(val_int(1));
        DISPATCH();
    }

    CASE(const_2) :
    {
        PUSH(val_int(2));
        DISPATCH();
    }

    CASE(const_neg1) :
    {
        PUSH(val_int(-1));
        DISPATCH();
    }

    /* ============ INTEGER-SPECIALIZED OPCODES ============ */
    /* These skip type checking and provide fastest path for integer-only code */
    /* ~2-4x faster than generic opcodes due to no type dispatch */

    CASE(add_ii) :
    {
        /* Pure integer add - no type checks */
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        PUSH(val_int(a + b));
        DISPATCH();
    }

    CASE(sub_ii) :
    {
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        PUSH(val_int(a - b));
        DISPATCH();
    }

    CASE(mul_ii) :
    {
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        PUSH(val_int(a * b));
        DISPATCH();
    }

    CASE(div_ii) :
    {
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        PUSH(val_int(b != 0 ? a / b : 0));
        DISPATCH();
    }

    CASE(mod_ii) :
    {
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        PUSH(val_int(b != 0 ? a % b : 0));
        DISPATCH();
    }

    CASE(lt_ii) :
    {
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        PUSH(val_bool(a < b));
        DISPATCH();
    }

    CASE(gt_ii) :
    {
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        PUSH(val_bool(a > b));
        DISPATCH();
    }

    CASE(lte_ii) :
    {
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        PUSH(val_bool(a <= b));
        DISPATCH();
    }

    CASE(gte_ii) :
    {
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        PUSH(val_bool(a >= b));
        DISPATCH();
    }

    CASE(eq_ii) :
    {
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        PUSH(val_bool(a == b));
        DISPATCH();
    }

    CASE(neq_ii) :
    {
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        PUSH(val_bool(a != b));
        DISPATCH();
    }

    CASE(inc_ii) :
    {
        int32_t a = as_int(POP());
        PUSH(val_int(a + 1));
        DISPATCH();
    }

    CASE(dec_ii) :
    {
        int32_t a = as_int(POP());
        PUSH(val_int(a - 1));
        DISPATCH();
    }

    CASE(neg_ii) :
    {
        int32_t a = as_int(POP());
        PUSH(val_int(-a));
        DISPATCH();
    }

    /* Integer-specialized comparison + jump - FASTEST loop opcodes */
    CASE(lt_ii_jmp_false) :
    {
        uint16_t offset = READ_SHORT();
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        if (!(a < b))
            ip += offset;
        DISPATCH();
    }

    CASE(lte_ii_jmp_false) :
    {
        uint16_t offset = READ_SHORT();
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        if (!(a <= b))
            ip += offset;
        DISPATCH();
    }

    CASE(gt_ii_jmp_false) :
    {
        uint16_t offset = READ_SHORT();
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        if (!(a > b))
            ip += offset;
        DISPATCH();
    }

    CASE(gte_ii_jmp_false) :
    {
        uint16_t offset = READ_SHORT();
        int32_t b = as_int(POP());
        int32_t a = as_int(POP());
        if (!(a >= b))
            ip += offset;
        DISPATCH();
    }

    /* Fast increment/decrement */
    CASE(add_1) :
    {
        Value v = POP();
        PUSH(val_int(as_int(v) + 1));
        DISPATCH();
    }

    CASE(sub_1) :
    {
        Value v = POP();
        PUSH(val_int(as_int(v) - 1));
        DISPATCH();
    }

    /* Fused comparison + conditional jump - CRITICAL for loops */
    /* These pop 2 values, compare, and conditionally jump WITHOUT leaving result */
    /* The compiler skips the subsequent OP_POP when fusion happens */
    CASE(lt_jmp_false) :
    {
        uint16_t offset = READ_SHORT();
        Value b = POP();
        Value a = POP();
        bool result;
        if (IS_INT(a) && IS_INT(b))
        {
            result = as_int(a) < as_int(b);
        }
        else
        {
            double da = IS_INT(a) ? as_int(a) : as_num(a);
            double db = IS_INT(b) ? as_int(b) : as_num(b);
            result = da < db;
        }
        if (!result)
            ip += offset;
        DISPATCH();
    }

    CASE(lte_jmp_false) :
    {
        uint16_t offset = READ_SHORT();
        Value b = POP();
        Value a = POP();
        bool result;
        if (IS_INT(a) && IS_INT(b))
        {
            result = as_int(a) <= as_int(b);
        }
        else
        {
            double da = IS_INT(a) ? as_int(a) : as_num(a);
            double db = IS_INT(b) ? as_int(b) : as_num(b);
            result = da <= db;
        }
        if (!result)
            ip += offset;
        DISPATCH();
    }

    CASE(gt_jmp_false) :
    {
        uint16_t offset = READ_SHORT();
        Value b = POP();
        Value a = POP();
        bool result;
        if (IS_INT(a) && IS_INT(b))
        {
            result = as_int(a) > as_int(b);
        }
        else
        {
            double da = IS_INT(a) ? as_int(a) : as_num(a);
            double db = IS_INT(b) ? as_int(b) : as_num(b);
            result = da > db;
        }
        if (!result)
            ip += offset;
        DISPATCH();
    }

    CASE(gte_jmp_false) :
    {
        uint16_t offset = READ_SHORT();
        Value b = POP();
        Value a = POP();
        bool result;
        if (IS_INT(a) && IS_INT(b))
        {
            result = as_int(a) >= as_int(b);
        }
        else
        {
            double da = IS_INT(a) ? as_int(a) : as_num(a);
            double db = IS_INT(b) ? as_int(b) : as_num(b);
            result = da >= db;
        }
        if (!result)
            ip += offset;
        DISPATCH();
    }

    CASE(eq_jmp_false) :
    {
        uint16_t offset = READ_SHORT();
        Value b = POP();
        Value a = POP();
        /* Handle int vs float comparison: compare numeric values */
        bool equal;
        if ((IS_INT(a) || IS_NUM(a)) && (IS_INT(b) || IS_NUM(b)) && !IS_OBJ(a) && !IS_OBJ(b))
        {
            double da = IS_INT(a) ? (double)as_int(a) : as_num(a);
            double db = IS_INT(b) ? (double)as_int(b) : as_num(b);
            equal = (da == db);
        }
        else
        {
            equal = (a == b);
        }
        if (!equal)
            ip += offset;
        DISPATCH();
    }

    CASE(neq_jmp_false) :
    {
        uint16_t offset = READ_SHORT();
        Value b = POP();
        Value a = POP();
        /* Handle int vs float comparison: compare numeric values */
        bool equal;
        if ((IS_INT(a) || IS_NUM(a)) && (IS_INT(b) || IS_NUM(b)) && !IS_OBJ(a) && !IS_OBJ(b))
        {
            double da = IS_INT(a) ? (double)as_int(a) : as_num(a);
            double db = IS_INT(b) ? (double)as_int(b) : as_num(b);
            equal = (da == db);
        }
        else
        {
            equal = (a == b);
        }
        if (equal) /* != is false when equal */
            ip += offset;
        DISPATCH();
    }

    /* Fused local + arithmetic */
    CASE(get_local_add) :
    {
        uint8_t slot = READ_BYTE();
        Value local = bp[slot]; /* Use cached bp */
        Value tos = POP();
        if (IS_INT(local) && IS_INT(tos))
        {
            PUSH(val_int(as_int(local) + as_int(tos)));
        }
        else
        {
            double da = IS_INT(local) ? as_int(local) : as_num(local);
            double db = IS_INT(tos) ? as_int(tos) : as_num(tos);
            PUSH(val_num(da + db));
        }
        DISPATCH();
    }

    CASE(get_local_sub) :
    {
        uint8_t slot = READ_BYTE();
        Value local = bp[slot]; /* Use cached bp */
        Value tos = POP();
        if (IS_INT(local) && IS_INT(tos))
        {
            PUSH(val_int(as_int(tos) - as_int(local)));
        }
        else
        {
            double da = IS_INT(tos) ? as_int(tos) : as_num(tos);
            double db = IS_INT(local) ? as_int(local) : as_num(local);
            PUSH(val_num(da - db));
        }
        DISPATCH();
    }

    /* Ultra-fast loop increment - critical for tight loops */
    CASE(inc_local) :
    {
        uint8_t slot = READ_BYTE();
        /* Use cached bp - assume integer for loop counters */
        bp[slot] = val_int(as_int(bp[slot]) + 1);
        DISPATCH();
    }

    CASE(dec_local) :
    {
        uint8_t slot = READ_BYTE();
        /* Use cached bp */
        bp[slot] = val_int(as_int(bp[slot]) - 1);
        DISPATCH();
    }

    /* Fused for-range iteration: counter in slot, limit in next slot */
    /* Format: OP_FOR_RANGE, counter_slot, limit_slot, offset[2] */
    CASE(for_range) :
    {
        uint8_t counter_slot = READ_BYTE();
        uint8_t limit_slot = READ_BYTE();
        uint16_t offset = READ_SHORT();

        /* Use cached bp - no branch! */
        int32_t counter = as_int(bp[counter_slot]);
        int32_t limit = as_int(bp[limit_slot]);

        if (counter >= limit)
        {
            ip += offset; /* Exit loop */
        }
        else
        {
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
    CASE(for_loop) :
    {
        uint8_t iter_slot = READ_BYTE();
        uint8_t idx_slot = READ_BYTE();
        uint8_t var_slot = READ_BYTE();
        uint16_t offset = READ_SHORT();

        /* Use cached bp - no branch! */
        Value iter = bp[iter_slot];
        if (IS_RANGE(iter))
        {
            ObjRange *range = AS_RANGE(iter);
            if (range->current >= range->end)
            {
                ip += offset;
            }
            else
            {
                bp[var_slot] = val_int(range->current++);
            }
        }
        else if (IS_ARRAY(iter))
        {
            /* Array iteration: iter_slot has array, idx_slot has index */
            ObjArray *arr = AS_ARRAY(iter);
            int32_t idx = as_int(bp[idx_slot]);

            if ((uint32_t)idx >= arr->count)
            {
                ip += offset; /* Exit loop */
            }
            else
            {
                bp[var_slot] = arr->values[idx];
                bp[idx_slot] = val_int(idx + 1); /* Increment index */
            }
        }
        else if (IS_STRING(iter))
        {
            /* String iteration: iterate over characters */
            ObjString *str = AS_STRING(iter);
            int32_t idx = as_int(bp[idx_slot]);

            if ((uint32_t)idx >= str->length)
            {
                ip += offset; /* Exit loop */
            }
            else
            {
                /* Create single-character string */
                ObjString *ch = copy_string(vm, &str->chars[idx], 1);
                bp[var_slot] = val_obj(ch);
                bp[idx_slot] = val_int(idx + 1); /* Increment index */
            }
        }
        else
        {
            vm->sp = sp;
            runtime_error(vm, "Expected iterable (range, array, or string).");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

    /* Heap-allocation-free integer for loop: no Range object! */
    /* Format: OP_FOR_INT_INIT - nothing to do, just skip */
    CASE(for_int_init) :
    {
        DISPATCH();
    }

    /* Ultra-fast int loop: counter, end, var all stored as raw ints in locals */
    /* Format: OP_FOR_INT_LOOP, counter_slot, end_slot, var_slot, offset[2] */
    CASE(for_int_loop) :
    {
        uint8_t counter_slot = READ_BYTE();
        uint8_t end_slot = READ_BYTE();
        uint8_t var_slot = READ_BYTE();
        uint16_t offset = READ_SHORT();

        /* JIT: Check if we have compiled code for this loop */
        uint8_t *loop_header = ip - 6; /* Point back to FOR_INT_LOOP */
        int trace_idx = jit_check_hotloop(loop_header);

        if (trace_idx >= 0)
        {
            /* Execute JIT-compiled trace */
            int64_t counter = as_int(bp[counter_slot]);
            int64_t end = as_int(bp[end_slot]);
            int64_t iterations = end - counter;

            /* Set globals for JIT to access */
            jit_set_globals(vm->globals.values, vm->chunk.constants);
            jit_execute_loop(trace_idx, bp, iterations);

            /* Loop completed - jump past the loop */
            ip += offset;
            DISPATCH();
        }

        /* JIT: Count this loop header */
        if (jit_count_loop(loop_header))
        {
            /* Hot! Compile it */
            uint8_t *loop_end = ip + offset - 2; /* LOOP instruction */
            jit_compile_loop(loop_header, loop_end, bp,
                             vm->chunk.constants,
                             vm->chunk.const_count,
                             vm->globals.keys, vm->globals.values, vm->globals.capacity);
        }

        /* Use cached bp - no branch! */
        int64_t counter = as_int(bp[counter_slot]);
        int64_t end = as_int(bp[end_slot]);

        if (counter >= end)
        {
            ip += offset;
        }
        else
        {
            bp[var_slot] = val_int(counter);
            bp[counter_slot] = val_int(counter + 1);
        }
        DISPATCH();
    }

    /* ULTRA-TIGHT counting loop - the fastest possible for numeric iteration */
    /* No Range object, no type checks, just raw integer operations */
    /* Format: OP_FOR_COUNT, counter_slot, end_slot, var_slot, offset[2] */
    CASE(for_count) :
    {
        uint8_t counter_slot = READ_BYTE();
        uint8_t end_slot = READ_BYTE();
        uint8_t var_slot = READ_BYTE();
        uint16_t offset = READ_SHORT();

        /* JIT: Check if we have compiled code for this loop */
        uint8_t *loop_header = ip - 6; /* Point back to OP_FOR_COUNT */
        int trace_idx = jit_check_hotloop(loop_header);

        if (trace_idx >= 0)
        {
            /* Execute JIT-compiled trace */
            int32_t counter_val, end_val;
            FAST_INT(bp[counter_slot], counter_val);
            FAST_INT(bp[end_slot], end_val);
            int64_t iterations = end_val - counter_val;

            /* Set globals for JIT to access */
            jit_set_globals(vm->globals.values, vm->chunk.constants);
            jit_execute_loop(trace_idx, bp, iterations);

            /* Loop completed - jump past the loop */
            ip += offset;
            DISPATCH();
        }

        /* JIT: Count this loop header */
        if (jit_count_loop(loop_header))
        {
            /* Hot! Compile it */
            uint8_t *loop_end = ip + offset - 2; /* LOOP instruction */
            jit_compile_loop(loop_header, loop_end, bp,
                             vm->chunk.constants,
                             vm->chunk.const_count,
                             vm->globals.keys, vm->globals.values, vm->globals.capacity);
        }

        /* Use cached base pointer - no branch! */
        int32_t counter, end_val;
        FAST_INT(bp[counter_slot], counter);
        FAST_INT(bp[end_slot], end_val);

        if (counter >= end_val)
        {
            ip += offset; /* Exit loop */
        }
        else
        {
            bp[var_slot] = val_int(counter);
            bp[counter_slot] = val_int(counter + 1);
        }
        DISPATCH();
    }

    /* Stepped counting loop for IB compatibility */
    /* Format: OP_FOR_COUNT_STEP, counter_slot, end_slot, step_slot, var_slot, offset[2] */
    CASE(for_count_step) :
    {
        uint8_t counter_slot = READ_BYTE();
        uint8_t end_slot = READ_BYTE();
        uint8_t step_slot = READ_BYTE();
        uint8_t var_slot = READ_BYTE();
        uint16_t offset = READ_SHORT();

        /* Get values from local slots */
        int32_t counter, end_val, step;
        FAST_INT(bp[counter_slot], counter);
        FAST_INT(bp[end_slot], end_val);
        FAST_INT(bp[step_slot], step);

        /* Check loop condition based on step direction */
        /* NOTE: This uses IB-style inclusive bounds */
        /* For positive step: iterate while counter <= end_val */
        /* For negative step: iterate while counter >= end_val */
        bool done;
        if (step > 0)
        {
            done = (counter > end_val);  /* Inclusive: 1 to 5 means 1,2,3,4,5 */
        }
        else if (step < 0)
        {
            done = (counter < end_val);  /* Inclusive: 5 to 1 means 5,4,3,2,1 */
        }
        else
        {
            /* step == 0: infinite loop protection, exit immediately */
            done = true;
        }

        if (done)
        {
            ip += offset; /* Exit loop */
        }
        else
        {
            bp[var_slot] = val_int(counter);
            bp[counter_slot] = val_int(counter + step);
        }
        DISPATCH();
    }

    /* Add immediate integer to local variable */
    /* Format: OP_ADD_LOCAL_INT, slot, delta (signed 8-bit) */
    CASE(add_local_int) :
    {
        uint8_t slot = READ_BYTE();
        int8_t delta = (int8_t)READ_BYTE();

        /* Use cached bp - no branch! */
        int32_t val;
        FAST_INT(bp[slot], val);
        bp[slot] = val_int(val + delta);
        DISPATCH();
    }

    /* ============ FUSED LOCAL ARITHMETIC ============ */
    /* These reduce dispatch overhead by combining common patterns */

    /* Increment local by 1 (pure int path) */
    /* Format: OP_INC_LOCAL_I, slot */
    CASE(inc_local_i) :
    {
        uint8_t slot = READ_BYTE();
        int32_t val = as_int(bp[slot]);
        bp[slot] = val_int(val + 1);
        DISPATCH();
    }

    /* Decrement local by 1 */
    CASE(dec_local_i) :
    {
        uint8_t slot = READ_BYTE();
        int32_t val = as_int(bp[slot]);
        bp[slot] = val_int(val - 1);
        DISPATCH();
    }

    /* Push local[a] + local[b] onto stack */
    /* Format: OP_LOCAL_ADD_LOCAL, slot_a, slot_b */
    CASE(local_add_local) :
    {
        uint8_t slot_a = READ_BYTE();
        uint8_t slot_b = READ_BYTE();
        int32_t a = as_int(bp[slot_a]);
        int32_t b = as_int(bp[slot_b]);
        PUSH(val_int(a + b));
        DISPATCH();
    }

    /* Push local[slot] * constant onto stack */
    /* Format: OP_LOCAL_MUL_CONST, slot, const_idx */
    CASE(local_mul_const) :
    {
        uint8_t slot = READ_BYTE();
        uint8_t const_idx = READ_BYTE();
        int32_t local_val = as_int(bp[slot]);
        Value constant = vm->chunk.constants[const_idx];
        int32_t const_val = as_int(constant);
        PUSH(val_int(local_val * const_val));
        DISPATCH();
    }

    /* Push local[slot] + constant onto stack */
    /* Format: OP_LOCAL_ADD_CONST, slot, const_idx */
    CASE(local_add_const) :
    {
        uint8_t slot = READ_BYTE();
        uint8_t const_idx = READ_BYTE();
        int32_t local_val = as_int(bp[slot]);
        Value constant = vm->chunk.constants[const_idx];
        int32_t const_val = as_int(constant);
        PUSH(val_int(local_val + const_val));
        DISPATCH();
    }

    /* Compare two locals and loop backward if condition is true */
    /* Format: OP_LOCAL_LT_LOOP, slot_a, slot_b, offset[2] (backward) */
    CASE(local_lt_loop) :
    {
        uint8_t slot_a = READ_BYTE();
        uint8_t slot_b = READ_BYTE();
        uint16_t offset = READ_SHORT();

        /* Use cached bp - no branch! */
        int32_t a, b;
        FAST_INT(bp[slot_a], a);
        FAST_INT(bp[slot_b], b);

        if (a < b)
        {
            ip -= offset; /* Jump backward to loop start */
        }
        DISPATCH();
    }

    /* ============================================================
     * JIT-COMPILED LOOP HANDLERS
     * These execute native machine code for maximum performance
     * ============================================================ */

    /* JIT: for i in 0..iterations do x = x + 1 end
     * Stack: [x, iterations] -> [result] */
    CASE(jit_inc_loop) :
    {
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
    CASE(jit_arith_loop) :
    {
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
    CASE(jit_branch_loop) :
    {
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
    CASE(tail_call) :
    {
        uint8_t arg_count = READ_BYTE();
        Value callee = PEEK(arg_count);

        ObjFunction *function = NULL;
        ObjClosure *closure = NULL;  /* Track closure for upvalue access */

        if (IS_FUNCTION(callee))
        {
            function = AS_FUNCTION(callee);
        }
        else if (IS_CLOSURE(callee))
        {
            closure = AS_CLOSURE(callee);
            function = closure->function;
        }
        else
        {
            vm->sp = sp;
            runtime_error(vm, "Can only call functions.");
            return INTERPRET_RUNTIME_ERROR;
        }

        if (arg_count != function->arity)
        {
            vm->sp = sp;
            runtime_error(vm, "Expected %d arguments but got %d.",
                          function->arity, arg_count);
            return INTERPRET_RUNTIME_ERROR;
        }

        /* Move arguments down to overwrite current frame */
        Value *new_base;
        if (vm->frame_count > 0)
        {
            new_base = vm->frames[vm->frame_count - 1].slots;
            /* Close any upvalues that point to the slots we're about to overwrite.
             * This is critical for closures: if an outer function captured variables
             * that are about to be overwritten by the tail call, those upvalues need
             * to be "closed" (copy value to upvalue's closed field) before we clobber them. */
            close_upvalues(vm, new_base);
            /* Update frame's closure to the new callee's closure */
            vm->frames[vm->frame_count - 1].closure = closure;
            vm->frames[vm->frame_count - 1].function = function;
        }
        else
        {
            new_base = vm->stack;
        }

        /* Copy callee and arguments to base of frame */
        for (int i = 0; i <= arg_count; i++)
        {
            new_base[i] = sp[-arg_count - 1 + i];
        }

        /* Reset stack pointer */
        sp = new_base + arg_count + 1;

        /* Jump to function code (same frame) */
        ip = vm->chunk.code + function->code_start;
        DISPATCH();
    }

    /* ============ FILE I/O ============ */

    CASE(read_file) :
    {
        Value path_val = POP();
        if (!IS_STRING(path_val))
        {
            vm->sp = sp;
            runtime_error(vm, "read_file expects a string path.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString *path = AS_STRING(path_val);
        FILE *f = fopen(path->chars, "r");
        if (!f)
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buffer = malloc(size + 1);
        fread(buffer, 1, size, f);
        buffer[size] = '\0';
        fclose(f);
        vm->sp = sp;
        ObjString *content = copy_string(vm, buffer, size);
        sp = vm->sp;
        free(buffer);
        PUSH(val_obj(content));
        DISPATCH();
    }

    CASE(write_file) :
    {
        Value content_val = POP();
        Value path_val = POP();
        if (!IS_STRING(path_val) || !IS_STRING(content_val))
        {
            vm->sp = sp;
            runtime_error(vm, "write_file expects string arguments.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString *path = AS_STRING(path_val);
        ObjString *content = AS_STRING(content_val);
        FILE *f = fopen(path->chars, "w");
        if (!f)
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        fwrite(content->chars, 1, content->length, f);
        fclose(f);
        PUSH(VAL_TRUE);
        DISPATCH();
    }

    CASE(append_file) :
    {
        Value content_val = POP();
        Value path_val = POP();
        if (!IS_STRING(path_val) || !IS_STRING(content_val))
        {
            vm->sp = sp;
            runtime_error(vm, "append_file expects string arguments.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString *path = AS_STRING(path_val);
        ObjString *content = AS_STRING(content_val);
        FILE *f = fopen(path->chars, "a");
        if (!f)
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        fwrite(content->chars, 1, content->length, f);
        fclose(f);
        PUSH(VAL_TRUE);
        DISPATCH();
    }

    CASE(file_exists) :
    {
        Value path_val = POP();
        if (!IS_STRING(path_val))
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjString *path = AS_STRING(path_val);
        PUSH(val_bool(access(path->chars, F_OK) == 0));
        DISPATCH();
    }

    CASE(list_dir) :
    {
        Value path_val = POP();
        if (!IS_STRING(path_val))
        {
            vm->sp = sp;
            runtime_error(vm, "list_dir expects a string path.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString *path = AS_STRING(path_val);
        DIR *dir = opendir(path->chars);
        if (!dir)
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjArray *arr = new_array(vm, 16);
        sp = vm->sp;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (entry->d_name[0] == '.' &&
                (entry->d_name[1] == '\0' ||
                 (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
                continue;
            vm->sp = sp;
            ObjString *name = copy_string(vm, entry->d_name, strlen(entry->d_name));
            sp = vm->sp;
            if (arr->count >= arr->capacity)
            {
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

    CASE(delete_file) :
    {
        Value path_val = POP();
        if (!IS_STRING(path_val))
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjString *path = AS_STRING(path_val);
        PUSH(val_bool(unlink(path->chars) == 0));
        DISPATCH();
    }

    CASE(mkdir) :
    {
        Value path_val = POP();
        if (!IS_STRING(path_val))
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjString *path = AS_STRING(path_val);
        PUSH(val_bool(mkdir(path->chars, 0755) == 0));
        DISPATCH();
    }

    /* ============ HTTP (using popen + curl) ============ */

    CASE(http_get) :
    {
        Value url_val = POP();
        if (!IS_STRING(url_val))
        {
            vm->sp = sp;
            runtime_error(vm, "http_get expects a string URL.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString *url = AS_STRING(url_val);
        if (url->length > 2000)
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        char *cmd = malloc(url->length + 64);
        sprintf(cmd, "curl -s \"%s\"", url->chars);
        FILE *p = popen(cmd, "r");
        free(cmd);
        if (!p)
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        char *buffer = NULL;
        size_t size = 0;
        char chunk[1024];
        while (fgets(chunk, sizeof(chunk), p))
        {
            size_t chunk_len = strlen(chunk);
            buffer = realloc(buffer, size + chunk_len + 1);
            memcpy(buffer + size, chunk, chunk_len);
            size += chunk_len;
        }
        pclose(p);
        if (buffer)
        {
            buffer[size] = '\0';
            vm->sp = sp;
            ObjString *result = copy_string(vm, buffer, size);
            sp = vm->sp;
            free(buffer);
            PUSH(val_obj(result));
        }
        else
        {
            PUSH(VAL_NIL);
        }
        DISPATCH();
    }

    CASE(http_post) :
    {
        Value body_val = POP();
        Value url_val = POP();
        if (!IS_STRING(url_val))
        {
            vm->sp = sp;
            runtime_error(vm, "http_post expects string URL.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString *url = AS_STRING(url_val);
        const char *body = IS_STRING(body_val) ? AS_STRING(body_val)->chars : "";
        char cmd[8192];
        snprintf(cmd, sizeof(cmd), "curl -s -X POST -d '%s' '%s'", body, url->chars);
        FILE *p = popen(cmd, "r");
        if (!p)
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        char *buffer = NULL;
        size_t size = 0;
        char chunk[1024];
        while (fgets(chunk, sizeof(chunk), p))
        {
            size_t chunk_len = strlen(chunk);
            buffer = realloc(buffer, size + chunk_len + 1);
            memcpy(buffer + size, chunk, chunk_len);
            size += chunk_len;
        }
        pclose(p);
        if (buffer)
        {
            buffer[size] = '\0';
            vm->sp = sp;
            ObjString *result = copy_string(vm, buffer, size);
            sp = vm->sp;
            free(buffer);
            PUSH(val_obj(result));
        }
        else
        {
            PUSH(VAL_NIL);
        }
        DISPATCH();
    }

    /* ============ JSON ============ */
    /* Recursive descent JSON parser */

    CASE(json_parse) :
    {
        Value str_val = POP();
        if (!IS_STRING(str_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString *str = AS_STRING(str_val);
        const char *json = str->chars;
        const char *end = json + str->length;
        vm->sp = sp;
        Value result = json_parse_value(vm, &json, end);
        sp = vm->sp;
        PUSH(result);
        DISPATCH();
    }

    CASE(json_stringify) :
    {
        Value val = POP();
        vm->sp = sp;
        ObjString *result = json_stringify_value(vm, val);
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }

    /* ============ PROCESS/SYSTEM ============ */

    CASE(exec) :
    {
        Value cmd_val = POP();
        if (!IS_STRING(cmd_val))
        {
            vm->sp = sp;
            runtime_error(vm, "exec expects a string command.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjString *cmd = AS_STRING(cmd_val);
        FILE *p = popen(cmd->chars, "r");
        if (!p)
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        char *buffer = NULL;
        size_t size = 0;
        char chunk[1024];
        while (fgets(chunk, sizeof(chunk), p))
        {
            size_t chunk_len = strlen(chunk);
            buffer = realloc(buffer, size + chunk_len + 1);
            memcpy(buffer + size, chunk, chunk_len);
            size += chunk_len;
        }
        pclose(p);
        if (buffer)
        {
            buffer[size] = '\0';
            vm->sp = sp;
            ObjString *result = copy_string(vm, buffer, size);
            sp = vm->sp;
            free(buffer);
            PUSH(val_obj(result));
        }
        else
        {
            vm->sp = sp;
            ObjString *result = copy_string(vm, "", 0);
            sp = vm->sp;
            PUSH(val_obj(result));
        }
        DISPATCH();
    }

    CASE(env) :
    {
        Value name_val = POP();
        if (!IS_STRING(name_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjString *name = AS_STRING(name_val);
        char *val = getenv(name->chars);
        if (val)
        {
            vm->sp = sp;
            ObjString *result = copy_string(vm, val, strlen(val));
            sp = vm->sp;
            PUSH(val_obj(result));
        }
        else
        {
            PUSH(VAL_NIL);
        }
        DISPATCH();
    }

    CASE(set_env) :
    {
        Value val_val = POP();
        Value name_val = POP();
        if (!IS_STRING(name_val) || !IS_STRING(val_val))
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjString *name = AS_STRING(name_val);
        ObjString *val = AS_STRING(val_val);
        PUSH(val_bool(setenv(name->chars, val->chars, 1) == 0));
        DISPATCH();
    }

    CASE(args) :
    {
        /* TODO: Store args in VM and return them */
        vm->sp = sp;
        ObjArray *arr = new_array(vm, 0);
        sp = vm->sp;
        PUSH(val_obj(arr));
        DISPATCH();
    }

    CASE(exit) :
    {
        Value code_val = POP();
        int code = IS_INT(code_val) ? as_int(code_val) : IS_NUM(code_val) ? (int)as_num(code_val)
                                                                          : 0;
        exit(code);
        DISPATCH();
    }

    CASE(sleep) :
    {
        Value ms_val = POP();
        int ms = IS_INT(ms_val) ? as_int(ms_val) : IS_NUM(ms_val) ? (int)as_num(ms_val)
                                                                  : 0;
        usleep(ms * 1000);
        PUSH(VAL_NIL);
        DISPATCH();
    }

    /* ============ DICTIONARY ============ */

    CASE(dict) :
    {
        vm->sp = sp;
        ObjDict *dict = new_dict(vm, 8);
        sp = vm->sp;
        PUSH(val_obj(dict));
        DISPATCH();
    }

    CASE(dict_get) :
    {
        Value key_val = POP();
        Value dict_val = POP();
        if (!IS_DICT(dict_val) || !IS_STRING(key_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjDict *dict = AS_DICT(dict_val);
        ObjString *key = AS_STRING(key_val);
        for (uint32_t i = 0; i < dict->capacity; i++)
        {
            if (dict->keys[i] && dict->keys[i]->length == key->length &&
                memcmp(dict->keys[i]->chars, key->chars, key->length) == 0)
            {
                PUSH(dict->values[i]);
                DISPATCH();
            }
        }
        PUSH(VAL_NIL);
        DISPATCH();
    }

    CASE(dict_set) :
    {
        Value val = POP();
        Value key_val = POP();
        Value dict_val = POP();
        if (!IS_DICT(dict_val) || !IS_STRING(key_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjDict *dict = AS_DICT(dict_val);
        ObjString *key = AS_STRING(key_val);
        /* Find existing or empty slot */
        uint32_t slot = key->hash & (dict->capacity - 1);
        for (uint32_t i = 0; i < dict->capacity; i++)
        {
            uint32_t idx = (slot + i) & (dict->capacity - 1);
            if (!dict->keys[idx])
            {
                dict->keys[idx] = key;
                dict->values[idx] = val;
                dict->count++;
                PUSH(val_obj(dict));
                DISPATCH();
            }
            if (dict->keys[idx]->length == key->length &&
                memcmp(dict->keys[idx]->chars, key->chars, key->length) == 0)
            {
                dict->values[idx] = val;
                PUSH(val_obj(dict));
                DISPATCH();
            }
        }
        PUSH(val_obj(dict));
        DISPATCH();
    }

    CASE(dict_has) :
    {
        Value key_val = POP();
        Value dict_val = POP();
        if (!IS_DICT(dict_val) || !IS_STRING(key_val))
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjDict *dict = AS_DICT(dict_val);
        ObjString *key = AS_STRING(key_val);
        for (uint32_t i = 0; i < dict->capacity; i++)
        {
            if (dict->keys[i] && dict->keys[i]->length == key->length &&
                memcmp(dict->keys[i]->chars, key->chars, key->length) == 0)
            {
                PUSH(VAL_TRUE);
                DISPATCH();
            }
        }
        PUSH(VAL_FALSE);
        DISPATCH();
    }

    CASE(dict_keys) :
    {
        Value dict_val = POP();
        if (!IS_DICT(dict_val))
        {
            vm->sp = sp;
            ObjArray *arr = new_array(vm, 0);
            sp = vm->sp;
            PUSH(val_obj(arr));
            DISPATCH();
        }
        ObjDict *dict = AS_DICT(dict_val);
        vm->sp = sp;
        ObjArray *arr = new_array(vm, dict->count);
        sp = vm->sp;
        for (uint32_t i = 0; i < dict->capacity; i++)
        {
            if (dict->keys[i])
            {
                arr->values[arr->count++] = val_obj(dict->keys[i]);
            }
        }
        PUSH(val_obj(arr));
        DISPATCH();
    }

    CASE(dict_values) :
    {
        Value dict_val = POP();
        if (!IS_DICT(dict_val))
        {
            vm->sp = sp;
            ObjArray *arr = new_array(vm, 0);
            sp = vm->sp;
            PUSH(val_obj(arr));
            DISPATCH();
        }
        ObjDict *dict = AS_DICT(dict_val);
        vm->sp = sp;
        ObjArray *arr = new_array(vm, dict->count);
        sp = vm->sp;
        for (uint32_t i = 0; i < dict->capacity; i++)
        {
            if (dict->keys[i])
            {
                arr->values[arr->count++] = dict->values[i];
            }
        }
        PUSH(val_obj(arr));
        DISPATCH();
    }

    CASE(dict_delete) :
    {
        Value key_val = POP();
        Value dict_val = POP();
        if (!IS_DICT(dict_val) || !IS_STRING(key_val))
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjDict *dict = AS_DICT(dict_val);
        ObjString *key = AS_STRING(key_val);
        for (uint32_t i = 0; i < dict->capacity; i++)
        {
            if (dict->keys[i] && dict->keys[i]->length == key->length &&
                memcmp(dict->keys[i]->chars, key->chars, key->length) == 0)
            {
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

    CASE(sin) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(sin(d)));
        DISPATCH();
    }

    CASE(cos) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(cos(d)));
        DISPATCH();
    }

    CASE(tan) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(tan(d)));
        DISPATCH();
    }

    CASE(asin) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(asin(d)));
        DISPATCH();
    }

    CASE(acos) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(acos(d)));
        DISPATCH();
    }

    CASE(atan) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(atan(d)));
        DISPATCH();
    }

    CASE(atan2) :
    {
        Value x = POP();
        Value y = POP();
        double dy = IS_INT(y) ? as_int(y) : as_num(y);
        double dx = IS_INT(x) ? as_int(x) : as_num(x);
        PUSH(val_num(atan2(dy, dx)));
        DISPATCH();
    }

    CASE(log) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(log(d)));
        DISPATCH();
    }

    CASE(log10) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(log10(d)));
        DISPATCH();
    }

    CASE(log2) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(log2(d)));
        DISPATCH();
    }

    CASE(exp) :
    {
        Value v = POP();
        double d = IS_INT(v) ? as_int(v) : as_num(v);
        PUSH(val_num(exp(d)));
        DISPATCH();
    }

    CASE(hypot) :
    {
        Value y = POP();
        Value x = POP();
        double dx = IS_INT(x) ? as_int(x) : as_num(x);
        double dy = IS_INT(y) ? as_int(y) : as_num(y);
        PUSH(val_num(hypot(dx, dy)));
        DISPATCH();
    }

    /* ============ VECTOR OPERATIONS ============ */

    CASE(vec_add) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        ObjArray *b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray *result = new_array(vm, len);
        sp = vm->sp;
        for (uint32_t i = 0; i < len; i++)
        {
            double da = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da + db);
        }
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(vec_sub) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        ObjArray *b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray *result = new_array(vm, len);
        sp = vm->sp;
        for (uint32_t i = 0; i < len; i++)
        {
            double da = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da - db);
        }
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(vec_mul) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        ObjArray *b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray *result = new_array(vm, len);
        sp = vm->sp;
        for (uint32_t i = 0; i < len; i++)
        {
            double da = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da * db);
        }
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(vec_div) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        ObjArray *b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray *result = new_array(vm, len);
        sp = vm->sp;
        for (uint32_t i = 0; i < len; i++)
        {
            double da = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da / db);
        }
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(vec_dot) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        ObjArray *b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        double dot = 0;
        for (uint32_t i = 0; i < len; i++)
        {
            double da = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? as_int(b->values[i]) : as_num(b->values[i]);
            dot += da * db;
        }
        PUSH(val_num(dot));
        DISPATCH();
    }

    CASE(vec_sum) :
    {
        Value a_val = POP();
        if (!IS_ARRAY(a_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        double sum = 0;
        for (uint32_t i = 0; i < a->count; i++)
        {
            sum += IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
        }
        PUSH(val_num(sum));
        DISPATCH();
    }

    CASE(vec_prod) :
    {
        Value a_val = POP();
        if (!IS_ARRAY(a_val))
        {
            PUSH(val_num(1));
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        double prod = 1;
        for (uint32_t i = 0; i < a->count; i++)
        {
            prod *= IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
        }
        PUSH(val_num(prod));
        DISPATCH();
    }

    CASE(vec_min) :
    {
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || AS_ARRAY(a_val)->count == 0)
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        double min_val = IS_INT(a->values[0]) ? as_int(a->values[0]) : as_num(a->values[0]);
        for (uint32_t i = 1; i < a->count; i++)
        {
            double v = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            if (v < min_val)
                min_val = v;
        }
        PUSH(val_num(min_val));
        DISPATCH();
    }

    CASE(vec_max) :
    {
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || AS_ARRAY(a_val)->count == 0)
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        double max_val = IS_INT(a->values[0]) ? as_int(a->values[0]) : as_num(a->values[0]);
        for (uint32_t i = 1; i < a->count; i++)
        {
            double v = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
            if (v > max_val)
                max_val = v;
        }
        PUSH(val_num(max_val));
        DISPATCH();
    }

    CASE(vec_mean) :
    {
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || AS_ARRAY(a_val)->count == 0)
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        double sum = 0;
        for (uint32_t i = 0; i < a->count; i++)
        {
            sum += IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
        }
        PUSH(val_num(sum / a->count));
        DISPATCH();
    }

    CASE(vec_map) :
    {
        /* TODO: Higher-order functions require closures */
        POP();
        POP();
        PUSH(VAL_NIL);
        DISPATCH();
    }

    CASE(vec_filter) :
    {
        POP();
        POP();
        PUSH(VAL_NIL);
        DISPATCH();
    }

    CASE(vec_reduce) :
    {
        POP();
        POP();
        POP();
        PUSH(VAL_NIL);
        DISPATCH();
    }

    CASE(vec_sort) :
    {
        Value a_val = POP();
        if (!IS_ARRAY(a_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        /* Simple bubble sort for now - could optimize with quicksort */
        for (uint32_t i = 0; i < a->count; i++)
        {
            for (uint32_t j = i + 1; j < a->count; j++)
            {
                double vi = IS_INT(a->values[i]) ? as_int(a->values[i]) : as_num(a->values[i]);
                double vj = IS_INT(a->values[j]) ? as_int(a->values[j]) : as_num(a->values[j]);
                if (vi > vj)
                {
                    Value tmp = a->values[i];
                    a->values[i] = a->values[j];
                    a->values[j] = tmp;
                }
            }
        }
        PUSH(a_val);
        DISPATCH();
    }

    CASE(vec_reverse) :
    {
        Value a_val = POP();
        if (!IS_ARRAY(a_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        for (uint32_t i = 0; i < a->count / 2; i++)
        {
            Value tmp = a->values[i];
            a->values[i] = a->values[a->count - 1 - i];
            a->values[a->count - 1 - i] = tmp;
        }
        PUSH(a_val);
        DISPATCH();
    }

    CASE(vec_unique) :
    {
        Value a_val = POP();
        if (!IS_ARRAY(a_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        vm->sp = sp;
        ObjArray *result = new_array(vm, a->count);
        sp = vm->sp;
        for (uint32_t i = 0; i < a->count; i++)
        {
            bool found = false;
            for (uint32_t j = 0; j < result->count; j++)
            {
                if (a->values[i] == result->values[j])
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                result->values[result->count++] = a->values[i];
            }
        }
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(vec_zip) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        ObjArray *b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray *result = new_array(vm, len);
        sp = vm->sp;
        for (uint32_t i = 0; i < len; i++)
        {
            vm->sp = sp;
            ObjArray *pair = new_array(vm, 2);
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

    CASE(vec_range) :
    {
        Value step_val = POP();
        Value end_val = POP();
        Value start_val = POP();
        double start = IS_INT(start_val) ? as_int(start_val) : as_num(start_val);
        double end = IS_INT(end_val) ? as_int(end_val) : as_num(end_val);
        double step = IS_INT(step_val) ? as_int(step_val) : as_num(step_val);
        if (step == 0)
            step = 1;
        uint32_t count = (uint32_t)((end - start) / step);
        if (count > 10000000)
            count = 10000000; /* Safety limit */
        vm->sp = sp;
        ObjArray *result = new_array(vm, count);
        sp = vm->sp;
        double v = start;
        for (uint32_t i = 0; i < count && ((step > 0 && v < end) || (step < 0 && v > end)); i++)
        {
            result->values[i] = val_num(v);
            result->count++;
            v += step;
        }
        PUSH(val_obj(result));
        DISPATCH();
    }

    /* ============ BINARY DATA ============ */

    CASE(bytes) :
    {
        Value size_val = POP();
        uint32_t size = IS_INT(size_val) ? as_int(size_val) : (uint32_t)as_num(size_val);
        vm->sp = sp;
        ObjBytes *bytes = new_bytes(vm, size);
        sp = vm->sp;
        bytes->length = size;
        memset(bytes->data, 0, size);
        PUSH(val_obj(bytes));
        DISPATCH();
    }

    CASE(bytes_get) :
    {
        Value idx_val = POP();
        Value bytes_val = POP();
        if (!IS_BYTES(bytes_val))
        {
            PUSH(val_int(0));
            DISPATCH();
        }
        ObjBytes *bytes = AS_BYTES(bytes_val);
        uint32_t idx = IS_INT(idx_val) ? as_int(idx_val) : (uint32_t)as_num(idx_val);
        if (idx >= bytes->length)
        {
            PUSH(val_int(0));
            DISPATCH();
        }
        PUSH(val_int(bytes->data[idx]));
        DISPATCH();
    }

    CASE(bytes_set) :
    {
        Value val = POP();
        Value idx_val = POP();
        Value bytes_val = POP();
        if (!IS_BYTES(bytes_val))
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjBytes *bytes = AS_BYTES(bytes_val);
        uint32_t idx = IS_INT(idx_val) ? as_int(idx_val) : (uint32_t)as_num(idx_val);
        if (idx >= bytes->length)
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        bytes->data[idx] = IS_INT(val) ? as_int(val) : (uint8_t)as_num(val);
        PUSH(VAL_TRUE);
        DISPATCH();
    }

    CASE(encode_utf8) :
    {
        Value str_val = POP();
        if (!IS_STRING(str_val))
        {
            vm->sp = sp;
            ObjBytes *bytes = new_bytes(vm, 0);
            sp = vm->sp;
            PUSH(val_obj(bytes));
            DISPATCH();
        }
        ObjString *str = AS_STRING(str_val);
        vm->sp = sp;
        ObjBytes *bytes = new_bytes(vm, str->length);
        sp = vm->sp;
        memcpy(bytes->data, str->chars, str->length);
        bytes->length = str->length;
        PUSH(val_obj(bytes));
        DISPATCH();
    }

    CASE(decode_utf8) :
    {
        Value bytes_val = POP();
        if (!IS_BYTES(bytes_val))
        {
            vm->sp = sp;
            ObjString *str = copy_string(vm, "", 0);
            sp = vm->sp;
            PUSH(val_obj(str));
            DISPATCH();
        }
        ObjBytes *bytes = AS_BYTES(bytes_val);
        vm->sp = sp;
        ObjString *str = copy_string(vm, (char *)bytes->data, bytes->length);
        sp = vm->sp;
        PUSH(val_obj(str));
        DISPATCH();
    }

    CASE(encode_base64) :
    {
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        Value data_val = POP();
        uint8_t *data;
        uint32_t len;
        if (IS_STRING(data_val))
        {
            data = (uint8_t *)AS_STRING(data_val)->chars;
            len = AS_STRING(data_val)->length;
        }
        else if (IS_BYTES(data_val))
        {
            data = AS_BYTES(data_val)->data;
            len = AS_BYTES(data_val)->length;
        }
        else
        {
            vm->sp = sp;
            ObjString *str = copy_string(vm, "", 0);
            sp = vm->sp;
            PUSH(val_obj(str));
            DISPATCH();
        }
        uint32_t out_len = ((len + 2) / 3) * 4;
        char *out = malloc(out_len + 1);
        uint32_t j = 0;
        for (uint32_t i = 0; i < len; i += 3)
        {
            uint32_t n = ((uint32_t)data[i]) << 16;
            if (i + 1 < len)
                n |= ((uint32_t)data[i + 1]) << 8;
            if (i + 2 < len)
                n |= data[i + 2];
            out[j++] = b64[(n >> 18) & 0x3F];
            out[j++] = b64[(n >> 12) & 0x3F];
            out[j++] = (i + 1 < len) ? b64[(n >> 6) & 0x3F] : '=';
            out[j++] = (i + 2 < len) ? b64[n & 0x3F] : '=';
        }
        out[j] = '\0';
        vm->sp = sp;
        ObjString *str = copy_string(vm, out, j);
        sp = vm->sp;
        free(out);
        PUSH(val_obj(str));
        DISPATCH();
    }

    CASE(decode_base64) :
    {
        /* Simplified base64 decode */
        Value str_val = POP();
        if (!IS_STRING(str_val))
        {
            vm->sp = sp;
            ObjBytes *bytes = new_bytes(vm, 0);
            sp = vm->sp;
            PUSH(val_obj(bytes));
            DISPATCH();
        }
        ObjString *str = AS_STRING(str_val);

        /* Base64 decode lookup table */
        static const int8_t b64_table[256] = {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -2, -1, -1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
            -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

        uint32_t out_len = (str->length / 4) * 3;
        vm->sp = sp;
        ObjBytes *bytes = new_bytes(vm, out_len + 4);
        sp = vm->sp;

        uint32_t j = 0;
        uint32_t acc = 0;
        int bits = 0;

        for (uint32_t i = 0; i < str->length; i++)
        {
            int8_t val = b64_table[(uint8_t)str->chars[i]];
            if (val == -2)
                break; /* Padding '=' */
            if (val < 0)
                continue; /* Skip invalid */
            acc = (acc << 6) | val;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                bytes->data[j++] = (acc >> bits) & 0xFF;
            }
        }
        bytes->length = j;
        PUSH(val_obj(bytes));
        DISPATCH();
    }

    /* Extended opcode handler - dispatches to extended opcodes (>= 256) */
    CASE(extended) :
    {
        uint8_t ext_idx = READ_BYTE();
        goto *extended_dispatch_table[ext_idx];
    }

    /* ============ REGEX ============ */
    /* Full regex support using PCRE2 */
#if HAS_PCRE2
    CASE(regex_match) :
    {
        Value pattern_val = POP();
        Value text_val = POP();

        if (!IS_STRING(text_val) || !IS_STRING(pattern_val))
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }

        ObjString *text = AS_STRING(text_val);
        ObjString *pattern = AS_STRING(pattern_val);

        int errorcode;
        PCRE2_SIZE erroroffset;
        pcre2_code *re = pcre2_compile(
            (PCRE2_SPTR)pattern->chars,
            pattern->length,
            0,
            &errorcode,
            &erroroffset,
            NULL);

        if (re == NULL)
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }

        pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, NULL);
        int rc = pcre2_match(
            re,
            (PCRE2_SPTR)text->chars,
            text->length,
            0,
            0,
            match_data,
            NULL);

        bool matched = (rc >= 0);

        pcre2_match_data_free(match_data);
        pcre2_code_free(re);

        PUSH(matched ? VAL_TRUE : VAL_FALSE);
        DISPATCH();
    }

    CASE(regex_find) :
    {
        Value pattern_val = POP();
        Value text_val = POP();

        vm->sp = sp;
        ObjArray *arr = new_array(vm, 0);
        sp = vm->sp;

        if (!IS_STRING(text_val) || !IS_STRING(pattern_val))
        {
            PUSH(val_obj(arr));
            DISPATCH();
        }

        ObjString *text = AS_STRING(text_val);
        ObjString *pattern = AS_STRING(pattern_val);

        int errorcode;
        PCRE2_SIZE erroroffset;
        pcre2_code *re = pcre2_compile(
            (PCRE2_SPTR)pattern->chars,
            pattern->length,
            0,
            &errorcode,
            &erroroffset,
            NULL);

        if (re == NULL)
        {
            PUSH(val_obj(arr));
            DISPATCH();
        }

        pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, NULL);
        PCRE2_SIZE offset = 0;

        while (offset < text->length)
        {
            int rc = pcre2_match(
                re,
                (PCRE2_SPTR)text->chars,
                text->length,
                offset,
                0,
                match_data,
                NULL);

            if (rc < 0)
                break;

            PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
            PCRE2_SIZE start = ovector[0];
            PCRE2_SIZE end_pos = ovector[1];

            if (start == end_pos)
            {
                offset = end_pos + 1;
            }
            else
            {
                vm->sp = sp;
                ObjString *match_str = copy_string(vm, text->chars + start, (int)(end_pos - start));
                sp = vm->sp;

                /* Push to array inline */
                if (arr->count >= arr->capacity)
                {
                    uint32_t new_cap = arr->capacity < 8 ? 8 : arr->capacity * 2;
                    arr->values = realloc(arr->values, sizeof(Value) * new_cap);
                    arr->capacity = new_cap;
                }
                arr->values[arr->count++] = val_obj(match_str);

                offset = end_pos;
            }
        }

        pcre2_match_data_free(match_data);
        pcre2_code_free(re);

        PUSH(val_obj(arr));
        DISPATCH();
    }

    CASE(regex_replace) :
    {
        Value replacement_val = POP();
        Value pattern_val = POP();
        Value text_val = POP();

        if (!IS_STRING(text_val) || !IS_STRING(pattern_val) || !IS_STRING(replacement_val))
        {
            vm->sp = sp;
            ObjString *str = copy_string(vm, "", 0);
            sp = vm->sp;
            PUSH(val_obj(str));
            DISPATCH();
        }

        ObjString *text = AS_STRING(text_val);
        ObjString *pattern = AS_STRING(pattern_val);
        ObjString *replacement = AS_STRING(replacement_val);

        int errorcode;
        PCRE2_SIZE erroroffset;
        pcre2_code *re = pcre2_compile(
            (PCRE2_SPTR)pattern->chars,
            pattern->length,
            0,
            &errorcode,
            &erroroffset,
            NULL);

        if (re == NULL)
        {
            PUSH(text_val);
            DISPATCH();
        }

        /* Use PCRE2 substitute for replacement */
        PCRE2_SIZE outlength = text->length * 2 + replacement->length * 10 + 256;
        PCRE2_UCHAR *output = malloc(outlength);

        int rc = pcre2_substitute(
            re,
            (PCRE2_SPTR)text->chars,
            text->length,
            0,
            PCRE2_SUBSTITUTE_GLOBAL,
            NULL,
            NULL,
            (PCRE2_SPTR)replacement->chars,
            replacement->length,
            output,
            &outlength);

        pcre2_code_free(re);

        if (rc < 0)
        {
            free(output);
            PUSH(text_val);
            DISPATCH();
        }

        vm->sp = sp;
        ObjString *result = copy_string(vm, (char *)output, (int)outlength);
        sp = vm->sp;
        free(output);

        PUSH(val_obj(result));
        DISPATCH();
    }
#else /* NO PCRE2 - stub implementations */
    CASE(regex_match) :
    {
        POP(); /* pattern */
        POP(); /* text */
        PUSH(VAL_FALSE);
        DISPATCH();
    }

    CASE(regex_find) :
    {
        POP(); /* pattern */
        POP(); /* text */
        vm->sp = sp;
        ObjArray *arr = new_array(vm, 0);
        sp = vm->sp;
        PUSH(val_obj(arr));
        DISPATCH();
    }

    CASE(regex_replace) :
    {
        POP(); /* replacement */
        POP(); /* pattern */
        Value text_val = POP();
        PUSH(text_val); /* return original text unchanged */
        DISPATCH();
    }
#endif /* HAS_PCRE2 */

    /* ============ HASHING ============ */

    CASE(hash) :
    {
        Value v = POP();
        uint32_t h = 2166136261u;
        if (IS_STRING(v))
        {
            ObjString *s = AS_STRING(v);
            for (uint32_t i = 0; i < s->length; i++)
            {
                h ^= (uint8_t)s->chars[i];
                h *= 16777619;
            }
        }
        else if (IS_INT(v))
        {
            h = (uint32_t)as_int(v);
        }
        else
        {
            h = (uint32_t)(v >> 32) ^ (uint32_t)v;
        }
        PUSH(val_int((int32_t)h));
        DISPATCH();
    }

    CASE(hash_sha256) :
    {
        Value v = POP();
        const uint8_t *data;
        size_t len;

        if (IS_STRING(v))
        {
            ObjString *s = AS_STRING(v);
            data = (const uint8_t *)s->chars;
            len = s->length;
        }
        else if (IS_BYTES(v))
        {
            ObjBytes *b = AS_BYTES(v);
            data = b->data;
            len = b->length;
        }
        else
        {
            vm->sp = sp;
            ObjString *str = copy_string(vm, "0000000000000000000000000000000000000000000000000000000000000000", 64);
            sp = vm->sp;
            PUSH(val_obj(str));
            DISPATCH();
        }

        /* SHA-256 implementation */
        static const uint32_t sha256_k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x, y, z) (((x) & (y)) ^ ((~(x)) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x) (SHA256_ROTR(x, 2) ^ SHA256_ROTR(x, 13) ^ SHA256_ROTR(x, 22))
#define SHA256_EP1(x) (SHA256_ROTR(x, 6) ^ SHA256_ROTR(x, 11) ^ SHA256_ROTR(x, 25))
#define SHA256_SIG0(x) (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ ((x) >> 10))

        uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

        /* Pad message */
        size_t bit_len = len * 8;
        size_t padded_len = ((len + 9 + 63) / 64) * 64;
        uint8_t *msg = calloc(padded_len, 1);
        memcpy(msg, data, len);
        msg[len] = 0x80;
        for (int i = 0; i < 8; i++)
        {
            msg[padded_len - 1 - i] = (bit_len >> (i * 8)) & 0xFF;
        }

        /* Process blocks */
        for (size_t blk = 0; blk < padded_len; blk += 64)
        {
            uint32_t w[64];
            for (int i = 0; i < 16; i++)
            {
                w[i] = ((uint32_t)msg[blk + i * 4] << 24) | ((uint32_t)msg[blk + i * 4 + 1] << 16) |
                       ((uint32_t)msg[blk + i * 4 + 2] << 8) | msg[blk + i * 4 + 3];
            }
            for (int i = 16; i < 64; i++)
            {
                w[i] = SHA256_SIG1(w[i - 2]) + w[i - 7] + SHA256_SIG0(w[i - 15]) + w[i - 16];
            }

            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
            for (int i = 0; i < 64; i++)
            {
                uint32_t t1 = hh + SHA256_EP1(e) + SHA256_CH(e, f, g) + sha256_k[i] + w[i];
                uint32_t t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
                hh = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
            }
            h[0] += a;
            h[1] += b;
            h[2] += c;
            h[3] += d;
            h[4] += e;
            h[5] += f;
            h[6] += g;
            h[7] += hh;
        }
        free(msg);

#undef SHA256_ROTR
#undef SHA256_CH
#undef SHA256_MAJ
#undef SHA256_EP0
#undef SHA256_EP1
#undef SHA256_SIG0
#undef SHA256_SIG1

        char hex[65];
        for (int i = 0; i < 8; i++)
        {
            snprintf(hex + i * 8, 9, "%08x", h[i]);
        }

        vm->sp = sp;
        ObjString *str = copy_string(vm, hex, 64);
        sp = vm->sp;
        PUSH(val_obj(str));
        DISPATCH();
    }

    CASE(hash_md5) :
    {
        Value v = POP();
        const uint8_t *data;
        size_t len;

        if (IS_STRING(v))
        {
            ObjString *s = AS_STRING(v);
            data = (const uint8_t *)s->chars;
            len = s->length;
        }
        else if (IS_BYTES(v))
        {
            ObjBytes *b = AS_BYTES(v);
            data = b->data;
            len = b->length;
        }
        else
        {
            vm->sp = sp;
            ObjString *str = copy_string(vm, "00000000000000000000000000000000", 32);
            sp = vm->sp;
            PUSH(val_obj(str));
            DISPATCH();
        }

        /* MD5 implementation */
        static const uint32_t md5_k[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
        static const uint32_t md5_s[64] = {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
            5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

#define MD5_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

        uint32_t h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe, h3 = 0x10325476;

        size_t bit_len = len * 8;
        size_t padded_len = ((len + 9 + 63) / 64) * 64;
        uint8_t *msg = calloc(padded_len, 1);
        memcpy(msg, data, len);
        msg[len] = 0x80;
        /* MD5 uses little-endian length */
        for (int i = 0; i < 8; i++)
        {
            msg[padded_len - 8 + i] = (bit_len >> (i * 8)) & 0xFF;
        }

        for (size_t blk = 0; blk < padded_len; blk += 64)
        {
            uint32_t w[16];
            for (int i = 0; i < 16; i++)
            {
                w[i] = msg[blk + i * 4] | ((uint32_t)msg[blk + i * 4 + 1] << 8) |
                       ((uint32_t)msg[blk + i * 4 + 2] << 16) | ((uint32_t)msg[blk + i * 4 + 3] << 24);
            }

            uint32_t a = h0, b = h1, c = h2, d = h3;
            for (int i = 0; i < 64; i++)
            {
                uint32_t f, g;
                if (i < 16)
                {
                    f = (b & c) | ((~b) & d);
                    g = i;
                }
                else if (i < 32)
                {
                    f = (d & b) | ((~d) & c);
                    g = (5 * i + 1) % 16;
                }
                else if (i < 48)
                {
                    f = b ^ c ^ d;
                    g = (3 * i + 5) % 16;
                }
                else
                {
                    f = c ^ (b | (~d));
                    g = (7 * i) % 16;
                }
                f = f + a + md5_k[i] + w[g];
                a = d;
                d = c;
                c = b;
                b = b + MD5_ROTL(f, md5_s[i]);
            }
            h0 += a;
            h1 += b;
            h2 += c;
            h3 += d;
        }
        free(msg);

#undef MD5_ROTL

        char hex[33];
        snprintf(hex, 33, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                 h0 & 0xFF, (h0 >> 8) & 0xFF, (h0 >> 16) & 0xFF, (h0 >> 24) & 0xFF,
                 h1 & 0xFF, (h1 >> 8) & 0xFF, (h1 >> 16) & 0xFF, (h1 >> 24) & 0xFF,
                 h2 & 0xFF, (h2 >> 8) & 0xFF, (h2 >> 16) & 0xFF, (h2 >> 24) & 0xFF,
                 h3 & 0xFF, (h3 >> 8) & 0xFF, (h3 >> 16) & 0xFF, (h3 >> 24) & 0xFF);

        vm->sp = sp;
        ObjString *str = copy_string(vm, hex, 32);
        sp = vm->sp;
        PUSH(val_obj(str));
        DISPATCH();
    }

    /* ============ TENSOR OPERATIONS ============ */

    CASE(tensor) :
    {
        /* Create tensor from array */
        Value arr_val = POP();
        if (!IS_ARRAY(arr_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *t = tensor_from_array(vm, AS_ARRAY(arr_val));
        sp = vm->sp;
        PUSH(val_obj(t));
        DISPATCH();
    }

    CASE(tensor_zeros) :
    {
        Value shape_val = POP();
        if (!IS_ARRAY(shape_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *shape_arr = AS_ARRAY(shape_val);
        uint32_t shape[8];
        uint32_t ndim = shape_arr->count > 8 ? 8 : shape_arr->count;
        for (uint32_t i = 0; i < ndim; i++)
        {
            shape[i] = IS_INT(shape_arr->values[i]) ? as_int(shape_arr->values[i])
                                                    : (uint32_t)as_num(shape_arr->values[i]);
        }
        vm->sp = sp;
        ObjTensor *t = tensor_zeros(vm, ndim, shape);
        sp = vm->sp;
        PUSH(val_obj(t));
        DISPATCH();
    }

    CASE(tensor_ones) :
    {
        Value shape_val = POP();
        if (!IS_ARRAY(shape_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *shape_arr = AS_ARRAY(shape_val);
        uint32_t shape[8];
        uint32_t ndim = shape_arr->count > 8 ? 8 : shape_arr->count;
        for (uint32_t i = 0; i < ndim; i++)
        {
            shape[i] = IS_INT(shape_arr->values[i]) ? as_int(shape_arr->values[i])
                                                    : (uint32_t)as_num(shape_arr->values[i]);
        }
        vm->sp = sp;
        ObjTensor *t = tensor_ones(vm, ndim, shape);
        sp = vm->sp;
        PUSH(val_obj(t));
        DISPATCH();
    }

    CASE(tensor_rand) :
    {
        Value shape_val = POP();
        if (!IS_ARRAY(shape_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *shape_arr = AS_ARRAY(shape_val);
        uint32_t shape[8];
        uint32_t ndim = shape_arr->count > 8 ? 8 : shape_arr->count;
        for (uint32_t i = 0; i < ndim; i++)
        {
            shape[i] = IS_INT(shape_arr->values[i]) ? as_int(shape_arr->values[i])
                                                    : (uint32_t)as_num(shape_arr->values[i]);
        }
        vm->sp = sp;
        ObjTensor *t = tensor_rand(vm, ndim, shape);
        sp = vm->sp;
        PUSH(val_obj(t));
        DISPATCH();
    }

    CASE(tensor_randn) :
    {
        Value shape_val = POP();
        if (!IS_ARRAY(shape_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *shape_arr = AS_ARRAY(shape_val);
        uint32_t shape[8];
        uint32_t ndim = shape_arr->count > 8 ? 8 : shape_arr->count;
        for (uint32_t i = 0; i < ndim; i++)
        {
            shape[i] = IS_INT(shape_arr->values[i]) ? as_int(shape_arr->values[i])
                                                    : (uint32_t)as_num(shape_arr->values[i]);
        }
        vm->sp = sp;
        ObjTensor *t = tensor_randn(vm, ndim, shape);
        sp = vm->sp;
        PUSH(val_obj(t));
        DISPATCH();
    }

    CASE(tensor_arange) :
    {
        Value step_val = POP();
        Value stop_val = POP();
        Value start_val = POP();
        double start = IS_INT(start_val) ? as_int(start_val) : as_num(start_val);
        double stop = IS_INT(stop_val) ? as_int(stop_val) : as_num(stop_val);
        double step = IS_INT(step_val) ? as_int(step_val) : as_num(step_val);
        vm->sp = sp;
        ObjTensor *t = tensor_arange(vm, start, stop, step);
        sp = vm->sp;
        PUSH(val_obj(t));
        DISPATCH();
    }

    CASE(tensor_linspace) :
    {
        Value num_val = POP();
        Value stop_val = POP();
        Value start_val = POP();
        double start = IS_INT(start_val) ? as_int(start_val) : as_num(start_val);
        double stop = IS_INT(stop_val) ? as_int(stop_val) : as_num(stop_val);
        uint32_t num = IS_INT(num_val) ? as_int(num_val) : (uint32_t)as_num(num_val);
        vm->sp = sp;
        ObjTensor *t = tensor_linspace(vm, start, stop, num);
        sp = vm->sp;
        PUSH(val_obj(t));
        DISPATCH();
    }

    CASE(tensor_eye) :
    {
        Value n_val = POP();
        uint32_t n = IS_INT(n_val) ? as_int(n_val) : (uint32_t)as_num(n_val);
        uint32_t shape[2] = {n, n};
        vm->sp = sp;
        ObjTensor *t = tensor_zeros(vm, 2, shape);
        sp = vm->sp;
        for (uint32_t i = 0; i < n; i++)
        {
            t->data[i * n + i] = 1.0;
        }
        PUSH(val_obj(t));
        DISPATCH();
    }

    CASE(tensor_shape) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjTensor *t = AS_TENSOR(t_val);
        vm->sp = sp;
        ObjArray *arr = new_array(vm, t->ndim);
        sp = vm->sp;
        for (uint32_t i = 0; i < t->ndim; i++)
        {
            arr->values[i] = val_int(t->shape[i]);
        }
        arr->count = t->ndim;
        PUSH(val_obj(arr));
        DISPATCH();
    }

    CASE(tensor_reshape) :
    {
        Value shape_val = POP();
        Value t_val = POP();
        if (!IS_TENSOR(t_val) || !IS_ARRAY(shape_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjTensor *t = AS_TENSOR(t_val);
        ObjArray *shape_arr = AS_ARRAY(shape_val);
        uint32_t shape[8];
        uint32_t ndim = shape_arr->count > 8 ? 8 : shape_arr->count;
        uint32_t new_size = 1;
        for (uint32_t i = 0; i < ndim; i++)
        {
            shape[i] = IS_INT(shape_arr->values[i]) ? as_int(shape_arr->values[i])
                                                    : (uint32_t)as_num(shape_arr->values[i]);
            new_size *= shape[i];
        }
        if (new_size != t->size)
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_create(vm, ndim, shape);
        sp = vm->sp;
        memcpy(result->data, t->data, t->size * sizeof(double));
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(tensor_transpose) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjTensor *t = AS_TENSOR(t_val);
        if (t->ndim != 2)
        {
            PUSH(val_obj(t)); /* Only 2D transpose for now */
            DISPATCH();
        }
        uint32_t new_shape[2] = {t->shape[1], t->shape[0]};
        vm->sp = sp;
        ObjTensor *result = tensor_create(vm, 2, new_shape);
        sp = vm->sp;
        for (uint32_t i = 0; i < t->shape[0]; i++)
        {
            for (uint32_t j = 0; j < t->shape[1]; j++)
            {
                result->data[j * t->shape[0] + i] = t->data[i * t->shape[1] + j];
            }
        }
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(tensor_flatten) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjTensor *t = AS_TENSOR(t_val);
        uint32_t shape[1] = {t->size};
        vm->sp = sp;
        ObjTensor *result = tensor_create(vm, 1, shape);
        sp = vm->sp;
        memcpy(result->data, t->data, t->size * sizeof(double));
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(tensor_squeeze) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjTensor *t = AS_TENSOR(t_val);

        /* Count non-1 dimensions */
        uint32_t new_ndim = 0;
        uint32_t new_shape[8];
        for (uint32_t i = 0; i < t->ndim && new_ndim < 8; i++)
        {
            if (t->shape[i] != 1)
            {
                new_shape[new_ndim++] = t->shape[i];
            }
        }
        if (new_ndim == 0)
        {
            new_ndim = 1;
            new_shape[0] = 1;
        }

        vm->sp = sp;
        ObjTensor *result = tensor_create(vm, new_ndim, new_shape);
        sp = vm->sp;
        memcpy(result->data, t->data, t->size * sizeof(double));
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(tensor_unsqueeze) :
    {
        Value dim_val = POP();
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjTensor *t = AS_TENSOR(t_val);
        uint32_t dim = IS_INT(dim_val) ? as_int(dim_val) : (uint32_t)as_num(dim_val);
        if (dim > t->ndim || t->ndim >= 8)
        {
            PUSH(t_val);
            DISPATCH();
        }

        /* Build new shape with 1 inserted at dim */
        uint32_t new_shape[8];
        for (uint32_t i = 0; i < dim; i++)
            new_shape[i] = t->shape[i];
        new_shape[dim] = 1;
        for (uint32_t i = dim; i < t->ndim; i++)
            new_shape[i + 1] = t->shape[i];

        vm->sp = sp;
        ObjTensor *result = tensor_create(vm, t->ndim + 1, new_shape);
        sp = vm->sp;
        memcpy(result->data, t->data, t->size * sizeof(double));
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(tensor_add) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_TENSOR(a_val) || !IS_TENSOR(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_add_tensors(vm, AS_TENSOR(a_val), AS_TENSOR(b_val));
        sp = vm->sp;
        PUSH(result ? val_obj(result) : VAL_NIL);
        DISPATCH();
    }

    CASE(tensor_sub) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_TENSOR(a_val) || !IS_TENSOR(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_sub_tensors(vm, AS_TENSOR(a_val), AS_TENSOR(b_val));
        sp = vm->sp;
        PUSH(result ? val_obj(result) : VAL_NIL);
        DISPATCH();
    }

    CASE(tensor_mul) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_TENSOR(a_val) || !IS_TENSOR(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_mul_tensors(vm, AS_TENSOR(a_val), AS_TENSOR(b_val));
        sp = vm->sp;
        PUSH(result ? val_obj(result) : VAL_NIL);
        DISPATCH();
    }

    CASE(tensor_div) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_TENSOR(a_val) || !IS_TENSOR(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_div_tensors(vm, AS_TENSOR(a_val), AS_TENSOR(b_val));
        sp = vm->sp;
        PUSH(result ? val_obj(result) : VAL_NIL);
        DISPATCH();
    }

    CASE(tensor_pow) :
    {
        Value p_val = POP();
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        double power = IS_INT(p_val) ? as_int(p_val) : as_num(p_val);
        vm->sp = sp;
        ObjTensor *result = tensor_pow_op(vm, AS_TENSOR(t_val), power);
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(tensor_neg) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_neg(vm, AS_TENSOR(t_val));
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(tensor_abs) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_abs(vm, AS_TENSOR(t_val));
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(tensor_sqrt) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_sqrt_op(vm, AS_TENSOR(t_val));
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(tensor_exp) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_exp_op(vm, AS_TENSOR(t_val));
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(tensor_log) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_log_op(vm, AS_TENSOR(t_val));
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(tensor_sum) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        double sum = tensor_sum_all(AS_TENSOR(t_val));
        PUSH(val_num(sum));
        DISPATCH();
    }

    CASE(tensor_mean) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        double mean = tensor_mean_all(AS_TENSOR(t_val));
        PUSH(val_num(mean));
        DISPATCH();
    }

    CASE(tensor_min) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        double min = tensor_min_all(AS_TENSOR(t_val));
        PUSH(val_num(min));
        DISPATCH();
    }

    CASE(tensor_max) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        double max = tensor_max_all(AS_TENSOR(t_val));
        PUSH(val_num(max));
        DISPATCH();
    }

    CASE(tensor_argmin) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(val_int(0));
            DISPATCH();
        }
        ObjTensor *t = AS_TENSOR(t_val);
        uint32_t idx = 0;
        double min = t->data[0];
        for (uint32_t i = 1; i < t->size; i++)
        {
            if (t->data[i] < min)
            {
                min = t->data[i];
                idx = i;
            }
        }
        PUSH(val_int(idx));
        DISPATCH();
    }

    CASE(tensor_argmax) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(val_int(0));
            DISPATCH();
        }
        ObjTensor *t = AS_TENSOR(t_val);
        uint32_t idx = 0;
        double max = t->data[0];
        for (uint32_t i = 1; i < t->size; i++)
        {
            if (t->data[i] > max)
            {
                max = t->data[i];
                idx = i;
            }
        }
        PUSH(val_int(idx));
        DISPATCH();
    }

    CASE(tensor_matmul) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_TENSOR(a_val) || !IS_TENSOR(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjTensor *a = AS_TENSOR(a_val);
        ObjTensor *b = AS_TENSOR(b_val);
        if (a->ndim != 2 || b->ndim != 2 || a->shape[1] != b->shape[0])
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        uint32_t new_shape[2] = {a->shape[0], b->shape[1]};
        vm->sp = sp;
        ObjTensor *result = tensor_zeros(vm, 2, new_shape);
        sp = vm->sp;
        /* Matrix multiplication */
        for (uint32_t i = 0; i < a->shape[0]; i++)
        {
            for (uint32_t k = 0; k < a->shape[1]; k++)
            {
                double aik = a->data[i * a->shape[1] + k];
                for (uint32_t j = 0; j < b->shape[1]; j++)
                {
                    result->data[i * b->shape[1] + j] += aik * b->data[k * b->shape[1] + j];
                }
            }
        }
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(tensor_dot) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_TENSOR(a_val) || !IS_TENSOR(b_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        ObjTensor *a = AS_TENSOR(a_val);
        ObjTensor *b = AS_TENSOR(b_val);
        if (a->size != b->size)
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        double dot = tensor_dot(a->data, b->data, a->size);
        PUSH(val_num(dot));
        DISPATCH();
    }

    CASE(tensor_norm) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        double norm = tensor_norm(AS_TENSOR(t_val));
        PUSH(val_num(norm));
        DISPATCH();
    }

    CASE(tensor_get) :
    {
        Value idx_val = POP();
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        ObjTensor *t = AS_TENSOR(t_val);
        uint32_t idx = IS_INT(idx_val) ? as_int(idx_val) : (uint32_t)as_num(idx_val);
        if (idx >= t->size)
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        PUSH(val_num(t->data[idx]));
        DISPATCH();
    }

    CASE(tensor_set) :
    {
        Value val = POP();
        Value idx_val = POP();
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        ObjTensor *t = AS_TENSOR(t_val);
        uint32_t idx = IS_INT(idx_val) ? as_int(idx_val) : (uint32_t)as_num(idx_val);
        if (idx >= t->size)
        {
            PUSH(VAL_FALSE);
            DISPATCH();
        }
        t->data[idx] = IS_INT(val) ? as_int(val) : as_num(val);
        PUSH(VAL_TRUE);
        DISPATCH();
    }

    /* ============ MATRIX OPERATIONS ============ */

    CASE(matrix) :
    {
        Value arr_val = POP();
        if (!IS_ARRAY(arr_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjMatrix *m = matrix_from_array(vm, AS_ARRAY(arr_val));
        sp = vm->sp;
        PUSH(val_obj(m));
        DISPATCH();
    }

    CASE(matrix_zeros) :
    {
        Value cols_val = POP();
        Value rows_val = POP();
        uint32_t rows = IS_INT(rows_val) ? as_int(rows_val) : (uint32_t)as_num(rows_val);
        uint32_t cols = IS_INT(cols_val) ? as_int(cols_val) : (uint32_t)as_num(cols_val);
        vm->sp = sp;
        ObjMatrix *m = matrix_zeros(vm, rows, cols);
        sp = vm->sp;
        PUSH(val_obj(m));
        DISPATCH();
    }

    CASE(matrix_ones) :
    {
        Value cols_val = POP();
        Value rows_val = POP();
        uint32_t rows = IS_INT(rows_val) ? as_int(rows_val) : (uint32_t)as_num(rows_val);
        uint32_t cols = IS_INT(cols_val) ? as_int(cols_val) : (uint32_t)as_num(cols_val);
        vm->sp = sp;
        ObjMatrix *m = matrix_ones(vm, rows, cols);
        sp = vm->sp;
        PUSH(val_obj(m));
        DISPATCH();
    }

    CASE(matrix_eye) :
    {
        Value n_val = POP();
        uint32_t n = IS_INT(n_val) ? as_int(n_val) : (uint32_t)as_num(n_val);
        vm->sp = sp;
        ObjMatrix *m = matrix_eye(vm, n);
        sp = vm->sp;
        PUSH(val_obj(m));
        DISPATCH();
    }

    CASE(matrix_rand) :
    {
        Value cols_val = POP();
        Value rows_val = POP();
        uint32_t rows = IS_INT(rows_val) ? as_int(rows_val) : (uint32_t)as_num(rows_val);
        uint32_t cols = IS_INT(cols_val) ? as_int(cols_val) : (uint32_t)as_num(cols_val);
        vm->sp = sp;
        ObjMatrix *m = matrix_rand(vm, rows, cols);
        sp = vm->sp;
        PUSH(val_obj(m));
        DISPATCH();
    }

    CASE(matrix_diag) :
    {
        Value arr_val = POP();
        if (!IS_ARRAY(arr_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *arr = AS_ARRAY(arr_val);
        uint32_t n = arr->count;
        vm->sp = sp;
        ObjMatrix *m = matrix_zeros(vm, n, n);
        sp = vm->sp;
        for (uint32_t i = 0; i < n; i++)
        {
            Value v = arr->values[i];
            m->data[i * n + i] = IS_INT(v) ? as_int(v) : as_num(v);
        }
        PUSH(val_obj(m));
        DISPATCH();
    }

    CASE(matrix_add) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_MATRIX(a_val) || !IS_MATRIX(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjMatrix *result = matrix_add(vm, AS_MATRIX(a_val), AS_MATRIX(b_val));
        sp = vm->sp;
        PUSH(result ? val_obj(result) : VAL_NIL);
        DISPATCH();
    }

    CASE(matrix_sub) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_MATRIX(a_val) || !IS_MATRIX(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjMatrix *result = matrix_sub(vm, AS_MATRIX(a_val), AS_MATRIX(b_val));
        sp = vm->sp;
        PUSH(result ? val_obj(result) : VAL_NIL);
        DISPATCH();
    }

    CASE(matrix_mul) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_MATRIX(a_val) || !IS_MATRIX(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjMatrix *a = AS_MATRIX(a_val);
        ObjMatrix *b = AS_MATRIX(b_val);
        if (a->rows != b->rows || a->cols != b->cols)
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjMatrix *result = matrix_create(vm, a->rows, a->cols);
        sp = vm->sp;
        size_t n = a->rows * a->cols;
        tensor_mul(result->data, a->data, b->data, n);
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(matrix_matmul) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_MATRIX(a_val) || !IS_MATRIX(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjMatrix *result = matrix_matmul(vm, AS_MATRIX(a_val), AS_MATRIX(b_val));
        sp = vm->sp;
        PUSH(result ? val_obj(result) : VAL_NIL);
        DISPATCH();
    }

    CASE(matrix_scale) :
    {
        Value s_val = POP();
        Value m_val = POP();
        if (!IS_MATRIX(m_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjMatrix *m = AS_MATRIX(m_val);
        double s = IS_INT(s_val) ? as_int(s_val) : as_num(s_val);
        vm->sp = sp;
        ObjMatrix *result = matrix_create(vm, m->rows, m->cols);
        sp = vm->sp;
        for (uint32_t i = 0; i < m->rows * m->cols; i++)
        {
            result->data[i] = m->data[i] * s;
        }
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(matrix_t) :
    {
        Value m_val = POP();
        if (!IS_MATRIX(m_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjMatrix *result = matrix_transpose(vm, AS_MATRIX(m_val));
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(matrix_inv) :
    {
        Value m_val = POP();
        if (!IS_MATRIX(m_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjMatrix *result = matrix_inverse(vm, AS_MATRIX(m_val));
        sp = vm->sp;
        PUSH(result ? val_obj(result) : VAL_NIL);
        DISPATCH();
    }

    CASE(matrix_det) :
    {
        Value m_val = POP();
        if (!IS_MATRIX(m_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        double det = matrix_det(AS_MATRIX(m_val));
        PUSH(val_num(det));
        DISPATCH();
    }

    CASE(matrix_trace) :
    {
        Value m_val = POP();
        if (!IS_MATRIX(m_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        double trace = matrix_trace(AS_MATRIX(m_val));
        PUSH(val_num(trace));
        DISPATCH();
    }

    CASE(matrix_solve) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_MATRIX(a_val) || !IS_MATRIX(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjMatrix *result = matrix_solve(vm, AS_MATRIX(a_val), AS_MATRIX(b_val));
        sp = vm->sp;
        PUSH(result ? val_obj(result) : VAL_NIL);
        DISPATCH();
    }

    /* ============ AUTOGRAD ============ */

    CASE(grad_tape) :
    {
        vm->sp = sp;
        ObjGradTape *tape = grad_tape_create(vm);
        sp = vm->sp;
        PUSH(val_obj(tape));
        DISPATCH();
    }

    /* ============ NEURAL NETWORK ============ */

    CASE(nn_relu) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_relu(vm, AS_TENSOR(t_val));
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(nn_sigmoid) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_sigmoid(vm, AS_TENSOR(t_val));
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(nn_tanh) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_tanh_op(vm, AS_TENSOR(t_val));
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(nn_softmax) :
    {
        Value t_val = POP();
        if (!IS_TENSOR(t_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        vm->sp = sp;
        ObjTensor *result = tensor_softmax(vm, AS_TENSOR(t_val));
        sp = vm->sp;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(nn_mse_loss) :
    {
        Value target_val = POP();
        Value pred_val = POP();
        if (!IS_TENSOR(pred_val) || !IS_TENSOR(target_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        double loss = tensor_mse_loss(AS_TENSOR(pred_val), AS_TENSOR(target_val));
        PUSH(val_num(loss));
        DISPATCH();
    }

    CASE(nn_ce_loss) :
    {
        Value target_val = POP();
        Value pred_val = POP();
        if (!IS_TENSOR(pred_val) || !IS_TENSOR(target_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        double loss = tensor_cross_entropy_loss(AS_TENSOR(pred_val), AS_TENSOR(target_val));
        PUSH(val_num(loss));
        DISPATCH();
    }

    /* ============ Exception Handling ============ */

    CASE(try) :
    {
        /* OP_TRY: Push exception handler with catch offset */
        uint16_t catch_offset = READ_SHORT();

        if (vm->handler_count >= HANDLERS_MAX)
        {
            runtime_error(vm, "Exception handler stack overflow");
            return INTERPRET_RUNTIME_ERROR;
        }

        ExceptionHandler *handler = &vm->handlers[vm->handler_count++];
        handler->catch_ip = ip + catch_offset;
        handler->stack_top = vm->sp;
        handler->frame_count = vm->frame_count;
        DISPATCH();
    }

    CASE(try_end) :
    {
        /* OP_TRY_END: Pop exception handler (normal exit from try block) */
        if (vm->handler_count > 0)
        {
            vm->handler_count--;
        }
        DISPATCH();
    }

    CASE(throw) :
    {
        /* OP_THROW: Throw exception (value on stack) */
        Value exception = POP();

        if (vm->handler_count == 0)
        {
            /* No handler - runtime error */
            if (IS_OBJ(exception) && ((Obj *)as_obj(exception))->type == OBJ_STRING)
            {
                ObjString *str = (ObjString *)as_obj(exception);
                runtime_error(vm, "Unhandled exception: %s", str->chars);
            }
            else
            {
                runtime_error(vm, "Unhandled exception");
            }
            return INTERPRET_RUNTIME_ERROR;
        }

        /* Pop handler and jump to catch */
        ExceptionHandler *handler = &vm->handlers[--vm->handler_count];
        vm->current_exception = exception;
        vm->sp = handler->stack_top;
        vm->frame_count = handler->frame_count;
        ip = handler->catch_ip;
        DISPATCH();
    }

    CASE(catch) :
    {
        /* OP_CATCH: Push current exception onto stack */
        PUSH(vm->current_exception);
        vm->current_exception = VAL_NIL;
        DISPATCH();
    }

    /* ============ CLASS OPCODES ============ */

    CASE(class) :
    {
        /* OP_CLASS: Create new class with given name */
        ObjString *name = AS_STRING(READ_CONST());
        ObjClass *klass = (ObjClass *)pseudo_realloc(vm, NULL, 0, sizeof(ObjClass));
        klass->obj.type = OBJ_CLASS;
        klass->obj.next = vm->objects;
        klass->obj.marked = false;
        vm->objects = (Obj *)klass;
        klass->name = name;
        klass->superclass = NULL;
        klass->field_count = 0;
        klass->method_count = 0;
        /* Initialize field hash table for O(1) lookup */
        memset(klass->field_hash, 0, sizeof(klass->field_hash));
        PUSH(val_obj(klass));
        DISPATCH();
    }

    CASE(inherit) :
    {
        /* OP_INHERIT: Inherit from superclass on stack */
        Value superclass_val = POP();
        if (!IS_CLASS(superclass_val))
        {
            runtime_error(vm, "Superclass must be a class.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjClass *superclass = AS_CLASS(superclass_val);
        ObjClass *subclass = AS_CLASS(PEEK(0));
        subclass->superclass = superclass;

        /* Copy superclass methods to subclass */
        for (uint16_t i = 0; i < superclass->method_count; i++)
        {
            subclass->methods[i] = superclass->methods[i];
            subclass->method_names[i] = superclass->method_names[i];
        }
        subclass->method_count = superclass->method_count;

        /* Copy superclass field names and hash table */
        for (uint16_t i = 0; i < superclass->field_count; i++)
        {
            subclass->field_names[i] = superclass->field_names[i];
        }
        subclass->field_count = superclass->field_count;
        memcpy(subclass->field_hash, superclass->field_hash, sizeof(subclass->field_hash));
        DISPATCH();
    }

    CASE(method) :
    {
        /* OP_METHOD: Add method to class on stack */
        ObjString *name = AS_STRING(READ_CONST());
        Value method = POP();
        ObjClass *klass = AS_CLASS(PEEK(0));
        
        /* Check if method already exists (override from inheritance) */
        bool found = false;
        for (uint16_t i = 0; i < klass->method_count; i++)
        {
            if (klass->method_names[i] == name ||
                (klass->method_names[i]->length == name->length &&
                 klass->method_names[i]->hash == name->hash &&
                 memcmp(klass->method_names[i]->chars, name->chars, name->length) == 0))
            {
                /* Override existing method */
                klass->methods[i] = method;
                found = true;
                break;
            }
        }
        
        /* Add as new method if not an override */
        if (!found && klass->method_count < CLASS_MAX_METHODS)
        {
            klass->methods[klass->method_count] = method;
            klass->method_names[klass->method_count] = name;
            klass->method_count++;
        }
        DISPATCH();
    }

    CASE(field) :
    {
        /* OP_FIELD: Add field to class on stack */
        ObjString *name = AS_STRING(READ_CONST());
        ObjClass *klass = AS_CLASS(PEEK(0));
        if (klass->field_count < CLASS_MAX_FIELDS)
        {
            uint16_t idx = klass->field_count++;
            klass->field_names[idx] = name;
            field_hash_insert(klass, name, idx);  /* Add to hash table for O(1) lookup */
        }
        DISPATCH();
    }

    CASE(get_field) :
    {
        /* OP_GET_FIELD: Get field from instance - O(1) hash lookup */
        Value instance_val = PEEK(0);
        if (!IS_INSTANCE(instance_val))
        {
            runtime_error(vm, "Only instances have fields.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjInstance *instance = AS_INSTANCE(instance_val);
        ObjString *name = AS_STRING(READ_CONST());

        /* O(1) hash-based field lookup */
        int16_t field_idx = field_hash_find(instance->klass, name);
        if (field_idx >= 0)
        {
            POP();
            PUSH(instance->fields[field_idx]);
            DISPATCH();
        }

        /* Not a field - look for method (still linear for methods) */
        for (uint16_t i = 0; i < instance->klass->method_count; i++)
        {
            if (instance->klass->method_names[i] == name ||
                (instance->klass->method_names[i]->length == name->length &&
                 memcmp(instance->klass->method_names[i]->chars, name->chars, name->length) == 0))
            {
                /* Return bound method (just the function for now) */
                POP();
                PUSH(instance->klass->methods[i]);
                DISPATCH();
            }
        }

        runtime_error(vm, "Undefined property '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    CASE(set_field) :
    {
        /* OP_SET_FIELD: Set field on instance - O(1) hash lookup */
        Value instance_val = PEEK(1);
        if (!IS_INSTANCE(instance_val))
        {
            runtime_error(vm, "Only instances have fields.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjInstance *instance = AS_INSTANCE(instance_val);
        ObjString *name = AS_STRING(READ_CONST());
        Value value = POP();

        /* O(1) hash-based field lookup */
        int16_t field_idx = field_hash_find(instance->klass, name);
        if (field_idx >= 0)
        {
            instance->fields[field_idx] = value;
            POP();       /* Pop instance */
            PUSH(value); /* Result is the assigned value */
            DISPATCH();
        }

        /* Field not found - add it dynamically */
        if (instance->klass->field_count < CLASS_MAX_FIELDS)
        {
            uint16_t idx = instance->klass->field_count++;
            instance->klass->field_names[idx] = name;
            field_hash_insert(instance->klass, name, idx);  /* Add to hash table */
            instance->fields[idx] = value;
            POP();       /* Pop instance */
            PUSH(value); /* Result is the assigned value */
            DISPATCH();
        }

        runtime_error(vm, "Too many fields on instance.");
        return INTERPRET_RUNTIME_ERROR;
    }

    /* ============ INLINE-CACHED PROPERTY ACCESS ============ */
    /* These opcodes use monomorphic inline caching for O(1) property lookup */
    /* After the first access, subsequent accesses skip the name-based search */

    CASE(get_field_ic) :
    {
        /* OP_GET_FIELD_IC: Get field with inline cache */
        Value instance_val = PEEK(0);
        if (!IS_INSTANCE(instance_val))
        {
            runtime_error(vm, "Only instances have fields.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjInstance *instance = AS_INSTANCE(instance_val);
        uint16_t const_idx = *ip++;
        const_idx |= ((uint16_t)(*ip++) << 8);
        uint16_t ic_slot = *ip++;
        ObjString *name = AS_STRING(vm->chunk.constants[const_idx]);

        InlineCache *ic = &vm->ic_cache[ic_slot];

        /* Fast path: cache hit */
        if (LIKELY(ic->cached_class == instance->klass && !ic->is_method))
        {
            POP();
            PUSH(instance->fields[ic->cached_slot]);
            DISPATCH();
        }

        /* Slow path: search and cache */
        for (uint16_t i = 0; i < instance->klass->field_count; i++)
        {
            if (instance->klass->field_names[i] == name ||
                (instance->klass->field_names[i]->length == name->length &&
                 memcmp(instance->klass->field_names[i]->chars, name->chars, name->length) == 0))
            {
                /* Update cache */
                ic->cached_class = instance->klass;
                ic->cached_slot = i;
                ic->cached_name = name;
                ic->is_method = false;

                POP();
                PUSH(instance->fields[i]);
                DISPATCH();
            }
        }

        /* Check methods */
        for (uint16_t i = 0; i < instance->klass->method_count; i++)
        {
            if (instance->klass->method_names[i] == name ||
                (instance->klass->method_names[i]->length == name->length &&
                 memcmp(instance->klass->method_names[i]->chars, name->chars, name->length) == 0))
            {
                /* Cache method lookup */
                ic->cached_class = instance->klass;
                ic->cached_slot = i;
                ic->cached_name = name;
                ic->is_method = true;

                POP();
                PUSH(instance->klass->methods[i]);
                DISPATCH();
            }
        }

        runtime_error(vm, "Undefined property '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    CASE(set_field_ic) :
    {
        /* OP_SET_FIELD_IC: Set field with inline cache */
        Value instance_val = PEEK(1);
        if (!IS_INSTANCE(instance_val))
        {
            runtime_error(vm, "Only instances have fields.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjInstance *instance = AS_INSTANCE(instance_val);
        uint16_t const_idx = *ip++;
        const_idx |= ((uint16_t)(*ip++) << 8);
        uint16_t ic_slot = *ip++;
        ObjString *name = AS_STRING(vm->chunk.constants[const_idx]);
        Value value = POP();

        InlineCache *ic = &vm->ic_cache[ic_slot];

        /* Fast path: cache hit */
        if (LIKELY(ic->cached_class == instance->klass && !ic->is_method))
        {
            instance->fields[ic->cached_slot] = value;
            POP();       /* Pop instance */
            PUSH(value); /* Result is the assigned value */
            DISPATCH();
        }

        /* Slow path: search and cache */
        for (uint16_t i = 0; i < instance->klass->field_count; i++)
        {
            if (instance->klass->field_names[i] == name ||
                (instance->klass->field_names[i]->length == name->length &&
                 memcmp(instance->klass->field_names[i]->chars, name->chars, name->length) == 0))
            {
                /* Update cache */
                ic->cached_class = instance->klass;
                ic->cached_slot = i;
                ic->cached_name = name;
                ic->is_method = false;

                instance->fields[i] = value;
                POP();       /* Pop instance */
                PUSH(value); /* Result is the assigned value */
                DISPATCH();
            }
        }

        /* Field not found - add it dynamically */
        if (instance->klass->field_count < CLASS_MAX_FIELDS)
        {
            uint16_t idx = instance->klass->field_count++;
            instance->klass->field_names[idx] = name;
            instance->fields[idx] = value;

            /* Cache the new slot */
            ic->cached_class = instance->klass;
            ic->cached_slot = idx;
            ic->cached_name = name;
            ic->is_method = false;

            POP();       /* Pop instance */
            PUSH(value); /* Result is the assigned value */
            DISPATCH();
        }

        runtime_error(vm, "Too many fields on instance.");
        return INTERPRET_RUNTIME_ERROR;
    }

    CASE(invoke_ic) :
    {
        /* OP_INVOKE_IC: Invoke method with inline cache */
        uint16_t const_idx = *ip++;
        const_idx |= ((uint16_t)(*ip++) << 8);
        uint8_t arg_count = *ip++;
        uint16_t ic_slot = *ip++;
        ObjString *method_name = AS_STRING(vm->chunk.constants[const_idx]);
        Value instance_val = PEEK(arg_count);

        if (!IS_INSTANCE(instance_val))
        {
            runtime_error(vm, "Only instances have methods.");
            return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance *instance = AS_INSTANCE(instance_val);
        InlineCache *ic = &vm->ic_cache[ic_slot];

        /* Fast path: cache hit for method */
        if (LIKELY(ic->cached_class == instance->klass && ic->is_method))
        {
            Value method = instance->klass->methods[ic->cached_slot];
            if (IS_CLOSURE(method))
            {
                ObjClosure *closure = AS_CLOSURE(method);
                if (vm->frame_count == FRAMES_MAX)
                {
                    runtime_error(vm, "Stack overflow.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                CallFrame *new_frame = &vm->frames[vm->frame_count++];
                new_frame->closure = closure;
                new_frame->function = closure->function;
                new_frame->ip = vm->chunk.code + closure->function->code_start;
                new_frame->slots = sp - arg_count - 1;
                new_frame->is_init = false;
                ip = new_frame->ip;
                bp = new_frame->slots;
                DISPATCH();
            }
        }

        /* Slow path: search and cache */
        for (uint16_t i = 0; i < instance->klass->method_count; i++)
        {
            if (instance->klass->method_names[i] == method_name ||
                (instance->klass->method_names[i]->length == method_name->length &&
                 memcmp(instance->klass->method_names[i]->chars, method_name->chars, method_name->length) == 0))
            {
                /* Update cache */
                ic->cached_class = instance->klass;
                ic->cached_slot = i;
                ic->cached_name = method_name;
                ic->is_method = true;

                Value method = instance->klass->methods[i];
                if (IS_CLOSURE(method))
                {
                    ObjClosure *closure = AS_CLOSURE(method);
                    if (vm->frame_count == FRAMES_MAX)
                    {
                        runtime_error(vm, "Stack overflow.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    CallFrame *new_frame = &vm->frames[vm->frame_count++];
                    new_frame->closure = closure;
                    new_frame->function = closure->function;
                    new_frame->ip = vm->chunk.code + closure->function->code_start;
                    new_frame->slots = sp - arg_count - 1;
                    new_frame->is_init = false;
                    ip = new_frame->ip;
                    bp = new_frame->slots;
                    DISPATCH();
                }
            }
        }

        runtime_error(vm, "Undefined method '%s'.", method_name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    CASE(get_field_pic) :
    {
        /* OP_GET_FIELD_PIC: Get field with polymorphic inline cache */
        Value instance_val = PEEK(0);
        if (!IS_INSTANCE(instance_val))
        {
            runtime_error(vm, "Only instances have fields.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjInstance *instance = AS_INSTANCE(instance_val);
        uint16_t const_idx = *ip++;
        const_idx |= ((uint16_t)(*ip++) << 8);
        uint16_t pic_slot = *ip++;
        ObjString *name = AS_STRING(vm->chunk.constants[const_idx]);

        PolyInlineCache *pic = &vm->pic_cache[pic_slot];

        /* Fast path: check all cached classes */
        for (uint8_t i = 0; i < pic->count; i++)
        {
            if (pic->entries[i].klass == instance->klass)
            {
                POP();
                PUSH(instance->fields[pic->entries[i].slot]);
                DISPATCH();
            }
        }

        /* Slow path: search and add to cache */
        for (uint16_t i = 0; i < instance->klass->field_count; i++)
        {
            if (instance->klass->field_names[i] == name ||
                (instance->klass->field_names[i]->length == name->length &&
                 memcmp(instance->klass->field_names[i]->chars, name->chars, name->length) == 0))
            {
                /* Add to PIC if not full */
                if (pic->count < PIC_MAX_ENTRIES)
                {
                    pic->entries[pic->count].klass = instance->klass;
                    pic->entries[pic->count].slot = i;
                    pic->count++;
                    pic->name = name;
                    pic->is_method = false;
                }
                POP();
                PUSH(instance->fields[i]);
                DISPATCH();
            }
        }

        runtime_error(vm, "Undefined property '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    CASE(set_field_pic) :
    {
        /* OP_SET_FIELD_PIC: Set field with polymorphic inline cache */
        Value instance_val = PEEK(1);
        if (!IS_INSTANCE(instance_val))
        {
            runtime_error(vm, "Only instances have fields.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjInstance *instance = AS_INSTANCE(instance_val);
        uint16_t const_idx = *ip++;
        const_idx |= ((uint16_t)(*ip++) << 8);
        uint16_t pic_slot = *ip++;
        ObjString *name = AS_STRING(vm->chunk.constants[const_idx]);
        Value value = POP();

        PolyInlineCache *pic = &vm->pic_cache[pic_slot];

        /* Fast path: check all cached classes */
        for (uint8_t i = 0; i < pic->count; i++)
        {
            if (pic->entries[i].klass == instance->klass)
            {
                instance->fields[pic->entries[i].slot] = value;
                POP();       /* Pop instance */
                PUSH(value); /* Result is the assigned value */
                DISPATCH();
            }
        }

        /* Slow path: search and add to cache */
        for (uint16_t i = 0; i < instance->klass->field_count; i++)
        {
            if (instance->klass->field_names[i] == name ||
                (instance->klass->field_names[i]->length == name->length &&
                 memcmp(instance->klass->field_names[i]->chars, name->chars, name->length) == 0))
            {
                /* Add to PIC if not full */
                if (pic->count < PIC_MAX_ENTRIES)
                {
                    pic->entries[pic->count].klass = instance->klass;
                    pic->entries[pic->count].slot = i;
                    pic->count++;
                    pic->name = name;
                    pic->is_method = false;
                }
                instance->fields[i] = value;
                POP();       /* Pop instance */
                PUSH(value); /* Result is the assigned value */
                DISPATCH();
            }
        }

        runtime_error(vm, "Undefined property '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    CASE(invoke_pic) :
    {
        /* OP_INVOKE_PIC: Invoke method with polymorphic inline cache */
        uint16_t const_idx = *ip++;
        const_idx |= ((uint16_t)(*ip++) << 8);
        uint8_t arg_count = *ip++;
        uint16_t pic_slot = *ip++;
        ObjString *method_name = AS_STRING(vm->chunk.constants[const_idx]);
        Value instance_val = PEEK(arg_count);

        if (!IS_INSTANCE(instance_val))
        {
            runtime_error(vm, "Only instances have methods.");
            return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance *instance = AS_INSTANCE(instance_val);
        PolyInlineCache *pic = &vm->pic_cache[pic_slot];

        /* Fast path: check all cached classes */
        for (uint8_t i = 0; i < pic->count; i++)
        {
            if (pic->entries[i].klass == instance->klass && pic->is_method)
            {
                Value method = instance->klass->methods[pic->entries[i].slot];
                if (IS_CLOSURE(method))
                {
                    ObjClosure *closure = AS_CLOSURE(method);
                    if (vm->frame_count == FRAMES_MAX)
                    {
                        runtime_error(vm, "Stack overflow.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    CallFrame *new_frame = &vm->frames[vm->frame_count++];
                    new_frame->closure = closure;
                    new_frame->function = closure->function;
                    new_frame->ip = vm->chunk.code + closure->function->code_start;
                    new_frame->slots = sp - arg_count - 1;
                    new_frame->is_init = false;
                    ip = new_frame->ip;
                    bp = new_frame->slots;
                    DISPATCH();
                }
            }
        }

        /* Slow path: search and add to cache */
        for (uint16_t i = 0; i < instance->klass->method_count; i++)
        {
            if (instance->klass->method_names[i] == method_name ||
                (instance->klass->method_names[i]->length == method_name->length &&
                 memcmp(instance->klass->method_names[i]->chars, method_name->chars, method_name->length) == 0))
            {
                /* Add to PIC if not full */
                if (pic->count < PIC_MAX_ENTRIES)
                {
                    pic->entries[pic->count].klass = instance->klass;
                    pic->entries[pic->count].slot = i;
                    pic->count++;
                    pic->name = method_name;
                    pic->is_method = true;
                }

                Value method = instance->klass->methods[i];
                if (IS_CLOSURE(method))
                {
                    ObjClosure *closure = AS_CLOSURE(method);
                    if (vm->frame_count == FRAMES_MAX)
                    {
                        runtime_error(vm, "Stack overflow.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    CallFrame *new_frame = &vm->frames[vm->frame_count++];
                    new_frame->closure = closure;
                    new_frame->function = closure->function;
                    new_frame->ip = vm->chunk.code + closure->function->code_start;
                    new_frame->slots = sp - arg_count - 1;
                    new_frame->is_init = false;
                    ip = new_frame->ip;
                    bp = new_frame->slots;
                    DISPATCH();
                }
            }
        }

        runtime_error(vm, "Undefined method '%s'.", method_name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    CASE(invoke) :
    {
        /* OP_INVOKE: Invoke method on instance */
        ObjString *method_name = AS_STRING(READ_CONST());
        uint8_t arg_count = *ip++;
        Value instance_val = PEEK(arg_count);

        if (!IS_INSTANCE(instance_val))
        {
            /* Maybe it's a class call (constructor) */
            if (IS_CLASS(instance_val))
            {
                ObjClass *klass = AS_CLASS(instance_val);
                /* Create instance with space for dynamic fields */
                size_t size = sizeof(ObjInstance) + sizeof(Value) * CLASS_MAX_FIELDS;
                ObjInstance *instance = (ObjInstance *)pseudo_realloc(vm, NULL, 0, size);
                instance->obj.type = OBJ_INSTANCE;
                instance->obj.next = vm->objects;
                instance->obj.marked = false;
                vm->objects = (Obj *)instance;
                instance->klass = klass;

                /* Initialize all fields to nil (including space for dynamic fields) */
                for (uint16_t i = 0; i < CLASS_MAX_FIELDS; i++)
                {
                    instance->fields[i] = VAL_NIL;
                }

                /* Replace class with instance on stack */
                vm->sp[-arg_count - 1] = val_obj(instance);

                /* Look for init method and call it */
                for (uint16_t i = 0; i < klass->method_count; i++)
                {
                    if (klass->method_names[i]->length == 4 &&
                        memcmp(klass->method_names[i]->chars, "init", 4) == 0)
                    {
                        /* Call init */
                        Value method = klass->methods[i];
                        /* TODO: Proper method call */
                        break;
                    }
                }

                PUSH(val_obj(instance));
                DISPATCH();
            }
            runtime_error(vm, "Only instances have methods.");
            return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance *instance = AS_INSTANCE(instance_val);

        /* Look up method */
        for (uint16_t i = 0; i < instance->klass->method_count; i++)
        {
            if (instance->klass->method_names[i] == method_name ||
                (instance->klass->method_names[i]->length == method_name->length &&
                 memcmp(instance->klass->method_names[i]->chars, method_name->chars, method_name->length) == 0))
            {
                /* Found method - call it */
                Value method_val = instance->klass->methods[i];
                ObjFunction *method = NULL;
                ObjClosure *method_closure = NULL;

                if (IS_FUNCTION(method_val))
                {
                    method = AS_FUNCTION(method_val);
                }
                else if (IS_CLOSURE(method_val))
                {
                    method_closure = AS_CLOSURE(method_val);
                    method = method_closure->function;
                }
                else
                {
                    vm->sp = sp;
                    runtime_error(vm, "Method is not callable.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                /* Check arity */
                if (arg_count != method->arity)
                {
                    vm->sp = sp;
                    runtime_error(vm, "Expected %d arguments but got %d.",
                                  method->arity, arg_count);
                    return INTERPRET_RUNTIME_ERROR;
                }

                /* Push new call frame */
                if (vm->frame_count == FRAMES_MAX)
                {
                    vm->sp = sp;
                    runtime_error(vm, "Stack overflow.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                CallFrame *frame = &vm->frames[vm->frame_count++];
                frame->function = method;
                frame->closure = method_closure;
                frame->ip = ip;
                frame->slots = sp - arg_count - 1; /* -1 for instance */
                frame->is_init = false;
                bp = frame->slots;

                ip = vm->chunk.code + method->code_start;
                DISPATCH();
            }
        }

        runtime_error(vm, "Undefined method '%s'.", method_name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    /* ============ SUPER KEYWORD ============ */

    CASE(get_super) :
    {
        /* OP_GET_SUPER: Get method from superclass */
        ObjString *method_name = AS_STRING(READ_CONST());
        Value instance_val = POP();

        if (!IS_INSTANCE(instance_val))
        {
            runtime_error(vm, "Cannot use 'super' on non-instance.");
            return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance *instance = AS_INSTANCE(instance_val);
        ObjClass *superclass = instance->klass->superclass;

        if (superclass == NULL)
        {
            runtime_error(vm, "Class has no superclass.");
            return INTERPRET_RUNTIME_ERROR;
        }

        /* Look up method in superclass */
        for (uint16_t i = 0; i < superclass->method_count; i++)
        {
            if (superclass->method_names[i] == method_name ||
                (superclass->method_names[i]->length == method_name->length &&
                 memcmp(superclass->method_names[i]->chars, method_name->chars, method_name->length) == 0))
            {
                /* Create bound method with instance receiver */
                ObjBoundMethod *bound = (ObjBoundMethod *)alloc_object(vm, sizeof(ObjBoundMethod), OBJ_BOUND_METHOD);
                bound->receiver = instance_val;
                bound->method = AS_CLOSURE(superclass->methods[i]);
                PUSH(val_obj((Obj *)bound));
                DISPATCH();
            }
        }

        runtime_error(vm, "Undefined superclass method '%s'.", method_name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    /* ============ STATIC METHODS ============ */

    CASE(static) :
    {
        /* OP_STATIC: Add static method/property to class */
        ObjString *name = AS_STRING(READ_CONST());
        Value val = POP();
        ObjClass *klass = AS_CLASS(PEEK(0));
        if (klass->static_count < CLASS_MAX_STATIC)
        {
            klass->statics[klass->static_count] = val;
            klass->static_names[klass->static_count] = name;
            klass->static_count++;
        }
        DISPATCH();
    }

    CASE(get_static) :
    {
        /* OP_GET_STATIC: Get static property from class */
        ObjString *name = AS_STRING(READ_CONST());
        Value class_val = PEEK(0);
        if (!IS_CLASS(class_val))
        {
            runtime_error(vm, "Only classes have static properties.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjClass *klass = AS_CLASS(class_val);
        for (uint16_t i = 0; i < klass->static_count; i++)
        {
            if (klass->static_names[i] == name ||
                (klass->static_names[i]->length == name->length &&
                 memcmp(klass->static_names[i]->chars, name->chars, name->length) == 0))
            {
                POP();
                PUSH(klass->statics[i]);
                DISPATCH();
            }
        }
        runtime_error(vm, "Undefined static property '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    CASE(set_static) :
    {
        /* OP_SET_STATIC: Set static property on class */
        ObjString *name = AS_STRING(READ_CONST());
        Value val = POP();
        Value class_val = PEEK(0);
        if (!IS_CLASS(class_val))
        {
            runtime_error(vm, "Only classes have static properties.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjClass *klass = AS_CLASS(class_val);
        for (uint16_t i = 0; i < klass->static_count; i++)
        {
            if (klass->static_names[i] == name ||
                (klass->static_names[i]->length == name->length &&
                 memcmp(klass->static_names[i]->chars, name->chars, name->length) == 0))
            {
                klass->statics[i] = val;
                PUSH(val);
                DISPATCH();
            }
        }
        runtime_error(vm, "Undefined static property '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    CASE(bind_method) :
    {
        /* OP_BIND_METHOD: Create bound method from method + receiver */
        Value method_val = POP();
        Value receiver = PEEK(0);

        if (!IS_CLOSURE(method_val))
        {
            runtime_error(vm, "Cannot bind non-closure as method.");
            return INTERPRET_RUNTIME_ERROR;
        }

        ObjBoundMethod *bound = (ObjBoundMethod *)alloc_object(vm, sizeof(ObjBoundMethod), OBJ_BOUND_METHOD);
        bound->receiver = receiver;
        bound->method = AS_CLOSURE(method_val);
        POP();
        PUSH(val_obj((Obj *)bound));
        DISPATCH();
    }

    /* ============ GENERATORS ============ */

    CASE(generator) :
    {
        /* OP_GENERATOR: Wrap closure in generator object */
        Value closure_val = POP();
        if (!IS_CLOSURE(closure_val))
        {
            runtime_error(vm, "Generator requires a closure.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjClosure *closure = AS_CLOSURE(closure_val);

        ObjGenerator *gen = (ObjGenerator *)alloc_object(vm, sizeof(ObjGenerator), OBJ_GENERATOR);
        gen->closure = closure;
        gen->state = GEN_CREATED;
        gen->stack_capacity = 64;
        gen->stack = (Value *)malloc(sizeof(Value) * gen->stack_capacity);
        gen->stack_size = 0;
        gen->ip = NULL;
        gen->sent_value = VAL_NIL;

        PUSH(val_obj((Obj *)gen));
        DISPATCH();
    }

    CASE(yield) :
    {
        /* OP_YIELD: Yield value from generator */
        Value yield_value = POP();

        /* Find current generator in call stack */
        CallFrame *frame = &vm->frames[vm->frame_count - 1];

        /* Save generator state - we need to find the generator object */
        /* For now, just return the value (simplified implementation) */
        PUSH(yield_value);

        /* Generator suspension would save IP and stack here */
        /* Full implementation requires generator call frame tracking */
        DISPATCH();
    }

    CASE(yield_from) :
    {
        /* OP_YIELD_FROM: Yield all values from sub-iterator */
        /* Simplified: just pass through for now */
        DISPATCH();
    }

    CASE(gen_next) :
    {
        /* OP_GEN_NEXT: Resume generator, get next value */
        Value gen_val = POP();
        if (!IS_GENERATOR(gen_val))
        {
            runtime_error(vm, "Can only call next() on generator.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjGenerator *gen = AS_GENERATOR(gen_val);

        if (gen->state == GEN_CLOSED)
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }

        /* Resume generator execution */
        gen->state = GEN_RUNNING;
        /* Full implementation would restore IP/stack and continue */
        PUSH(VAL_NIL);
        DISPATCH();
    }

    CASE(gen_send) :
    {
        /* OP_GEN_SEND: Send value into generator */
        Value send_val = POP();
        Value gen_val = POP();
        if (!IS_GENERATOR(gen_val))
        {
            runtime_error(vm, "Can only send() to generator.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjGenerator *gen = AS_GENERATOR(gen_val);
        gen->sent_value = send_val;
        /* Resume with sent value */
        PUSH(VAL_NIL);
        DISPATCH();
    }

    CASE(gen_return) :
    {
        /* OP_GEN_RETURN: Return from generator (close it) */
        Value ret_val = POP();

        /* Mark generator as closed */
        /* Would need to find generator context */
        (void)ret_val;
        DISPATCH();
    }

    /* ============ ASYNC/AWAIT ============ */

    CASE(async) :
    {
        /* OP_ASYNC: Mark function as async (wrap result in Promise) */
        Value closure_val = PEEK(0);
        if (!IS_CLOSURE(closure_val))
        {
            DISPATCH(); /* Pass through if not a closure */
        }

        /* Create Promise wrapping the async function */
        ObjPromise *promise = (ObjPromise *)alloc_object(vm, sizeof(ObjPromise), OBJ_PROMISE);
        promise->state = PROMISE_PENDING;
        promise->result = VAL_NIL;
        promise->on_resolve = VAL_NIL;
        promise->on_reject = VAL_NIL;
        promise->next = NULL;

        /* The closure will be executed and resolve the promise */
        POP();
        PUSH(val_obj((Obj *)promise));
        DISPATCH();
    }

    CASE(await) :
    {
        /* OP_AWAIT: Await promise resolution */
        Value promise_val = POP();

        if (!IS_PROMISE(promise_val))
        {
            /* If not a promise, just return the value */
            PUSH(promise_val);
            DISPATCH();
        }

        ObjPromise *promise = AS_PROMISE(promise_val);

        if (promise->state == PROMISE_RESOLVED)
        {
            PUSH(promise->result);
        }
        else if (promise->state == PROMISE_REJECTED)
        {
            runtime_error(vm, "Promise rejected.");
            return INTERPRET_RUNTIME_ERROR;
        }
        else
        {
            /* PROMISE_PENDING - in real async, would suspend */
            PUSH(VAL_NIL);
        }
        DISPATCH();
    }

    CASE(promise) :
    {
        /* OP_PROMISE: Create new Promise */
        ObjPromise *promise = (ObjPromise *)alloc_object(vm, sizeof(ObjPromise), OBJ_PROMISE);
        promise->state = PROMISE_PENDING;
        promise->result = VAL_NIL;
        promise->on_resolve = VAL_NIL;
        promise->on_reject = VAL_NIL;
        promise->next = NULL;
        PUSH(val_obj((Obj *)promise));
        DISPATCH();
    }

    CASE(resolve) :
    {
        /* OP_RESOLVE: Resolve promise with value */
        Value val = POP();
        Value promise_val = POP();
        if (IS_PROMISE(promise_val))
        {
            ObjPromise *promise = AS_PROMISE(promise_val);
            promise->state = PROMISE_RESOLVED;
            promise->result = val;
        }
        PUSH(val);
        DISPATCH();
    }

    CASE(reject) :
    {
        /* OP_REJECT: Reject promise with error */
        Value err = POP();
        Value promise_val = POP();
        if (IS_PROMISE(promise_val))
        {
            ObjPromise *promise = AS_PROMISE(promise_val);
            promise->state = PROMISE_REJECTED;
            promise->result = err;
        }
        PUSH(err);
        DISPATCH();
    }

    /* ============ DECORATORS ============ */

    CASE(decorator) :
    {
        /* OP_DECORATOR: Apply decorator to function */
        /* Stack: [decorator, function] -> [decorated_function] */
        Value func = POP();
        Value decorator = POP();

        if (IS_CLOSURE(decorator))
        {
            /* Call decorator with function as argument */
            /* Simplified: just return the function for now */
            /* Full implementation would call decorator(func) */
            PUSH(func);
        }
        else
        {
            PUSH(func);
        }
        DISPATCH();
    }

    /* ============ MODULES ============ */

    CASE(module) :
    {
        /* OP_MODULE: Create module namespace */
        ObjString *name = AS_STRING(READ_CONST());
        ObjModule *mod = (ObjModule *)alloc_object(vm, sizeof(ObjModule), OBJ_MODULE);
        mod->name = name;
        mod->loaded = false;

        /* Create exports dict */
        ObjDict *exports = (ObjDict *)alloc_object(vm, sizeof(ObjDict), OBJ_DICT);
        exports->count = 0;
        exports->capacity = 16;
        exports->keys = (ObjString **)calloc(exports->capacity, sizeof(ObjString *));
        exports->values = (Value *)calloc(exports->capacity, sizeof(Value));
        mod->exports = exports;

        PUSH(val_obj((Obj *)mod));
        DISPATCH();
    }

    CASE(export) :
    {
        /* OP_EXPORT: Export symbol from current module */
        ObjString *name = AS_STRING(READ_CONST());
        Value val = PEEK(0);

        /* Find current module context and add to exports */
        /* For now, also make it a global */
        table_set(vm, name, val);
        DISPATCH();
    }

    CASE(import_from) :
    {
        /* OP_IMPORT_FROM: Import specific symbol from module */
        ObjString *name = AS_STRING(READ_CONST());

        /* Look up in globals for now (modules would have separate namespace) */
        Value val;
        if (table_get(vm, name, &val))
        {
            PUSH(val);
        }
        else
        {
            /* Symbol not found - push nil */
            PUSH(VAL_NIL);
        }
        DISPATCH();
    }

    CASE(import_as) :
    {
        /* OP_IMPORT_AS: Import module with alias */
        ObjString *name = AS_STRING(READ_CONST());
        uint8_t alias_idx = READ_BYTE();
        ObjString *alias = AS_STRING(vm->chunk.constants[alias_idx]);

        /* Look up module */
        Value mod;
        if (table_get(vm, name, &mod))
        {
            table_set(vm, alias, mod);
            PUSH(mod);
        }
        else
        {
            PUSH(VAL_NIL);
        }
        DISPATCH();
    }

    /* ============ SIMD ARRAY OPERATIONS ============ */
    /* These use SSE/AVX intrinsics when available for parallel processing */

    CASE(array_add) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        ObjArray *b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray *result = new_array(vm, len);
        sp = vm->sp;

#if defined(__AVX__)
        /* AVX path: process 4 doubles at a time */
        uint32_t simd_len = len & ~3u;
        for (uint32_t i = 0; i < simd_len; i += 4)
        {
            __m256d va = _mm256_set_pd(
                IS_INT(a->values[i + 3]) ? (double)as_int(a->values[i + 3]) : as_num(a->values[i + 3]),
                IS_INT(a->values[i + 2]) ? (double)as_int(a->values[i + 2]) : as_num(a->values[i + 2]),
                IS_INT(a->values[i + 1]) ? (double)as_int(a->values[i + 1]) : as_num(a->values[i + 1]),
                IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]));
            __m256d vb = _mm256_set_pd(
                IS_INT(b->values[i + 3]) ? (double)as_int(b->values[i + 3]) : as_num(b->values[i + 3]),
                IS_INT(b->values[i + 2]) ? (double)as_int(b->values[i + 2]) : as_num(b->values[i + 2]),
                IS_INT(b->values[i + 1]) ? (double)as_int(b->values[i + 1]) : as_num(b->values[i + 1]),
                IS_INT(b->values[i]) ? (double)as_int(b->values[i]) : as_num(b->values[i]));
            __m256d vr = _mm256_add_pd(va, vb);
            double tmp[4];
            _mm256_storeu_pd(tmp, vr);
            result->values[i] = val_num(tmp[0]);
            result->values[i + 1] = val_num(tmp[1]);
            result->values[i + 2] = val_num(tmp[2]);
            result->values[i + 3] = val_num(tmp[3]);
        }
        for (uint32_t i = simd_len; i < len; i++)
        {
            double da = IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? (double)as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da + db);
        }
#elif defined(__SSE2__) || defined(_M_X64)
                /* SSE2 path: process 2 doubles at a time */
                uint32_t simd_len = len & ~1u;
                for (uint32_t i = 0; i < simd_len; i += 2)
                {
                    __m128d va = _mm_set_pd(
                        IS_INT(a->values[i + 1]) ? (double)as_int(a->values[i + 1]) : as_num(a->values[i + 1]),
                        IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]));
                    __m128d vb = _mm_set_pd(
                        IS_INT(b->values[i + 1]) ? (double)as_int(b->values[i + 1]) : as_num(b->values[i + 1]),
                        IS_INT(b->values[i]) ? (double)as_int(b->values[i]) : as_num(b->values[i]));
                    __m128d vr = _mm_add_pd(va, vb);
                    double tmp[2];
                    _mm_storeu_pd(tmp, vr);
                    result->values[i] = val_num(tmp[0]);
                    result->values[i + 1] = val_num(tmp[1]);
                }
                for (uint32_t i = simd_len; i < len; i++)
                {
                    double da = IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]);
                    double db = IS_INT(b->values[i]) ? (double)as_int(b->values[i]) : as_num(b->values[i]);
                    result->values[i] = val_num(da + db);
                }
#else
        /* Scalar fallback */
        for (uint32_t i = 0; i < len; i++)
        {
            double da = IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? (double)as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da + db);
        }
#endif
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(array_sub) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        ObjArray *b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray *result = new_array(vm, len);
        sp = vm->sp;

        for (uint32_t i = 0; i < len; i++)
        {
            double da = IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? (double)as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da - db);
        }
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(array_mul) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        ObjArray *b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray *result = new_array(vm, len);
        sp = vm->sp;

        for (uint32_t i = 0; i < len; i++)
        {
            double da = IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? (double)as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da * db);
        }
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(array_div) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        ObjArray *b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        vm->sp = sp;
        ObjArray *result = new_array(vm, len);
        sp = vm->sp;

        for (uint32_t i = 0; i < len; i++)
        {
            double da = IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? (double)as_int(b->values[i]) : as_num(b->values[i]);
            result->values[i] = val_num(da / db);
        }
        result->count = len;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(array_sum) :
    {
        Value a_val = POP();
        if (!IS_ARRAY(a_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        double sum = 0;

#if defined(__AVX__)
        /* AVX path: accumulate 4 sums in parallel */
        uint32_t simd_len = a->count & ~3u;
        __m256d acc = _mm256_setzero_pd();
        for (uint32_t i = 0; i < simd_len; i += 4)
        {
            __m256d va = _mm256_set_pd(
                IS_INT(a->values[i + 3]) ? (double)as_int(a->values[i + 3]) : as_num(a->values[i + 3]),
                IS_INT(a->values[i + 2]) ? (double)as_int(a->values[i + 2]) : as_num(a->values[i + 2]),
                IS_INT(a->values[i + 1]) ? (double)as_int(a->values[i + 1]) : as_num(a->values[i + 1]),
                IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]));
            acc = _mm256_add_pd(acc, va);
        }
        double tmp[4];
        _mm256_storeu_pd(tmp, acc);
        sum = tmp[0] + tmp[1] + tmp[2] + tmp[3];
        for (uint32_t i = simd_len; i < a->count; i++)
        {
            sum += IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]);
        }
#else
                for (uint32_t i = 0; i < a->count; i++)
                {
                    sum += IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]);
                }
#endif
        PUSH(val_num(sum));
        DISPATCH();
    }

    CASE(array_dot) :
    {
        Value b_val = POP();
        Value a_val = POP();
        if (!IS_ARRAY(a_val) || !IS_ARRAY(b_val))
        {
            PUSH(val_num(0));
            DISPATCH();
        }
        ObjArray *a = AS_ARRAY(a_val);
        ObjArray *b = AS_ARRAY(b_val);
        uint32_t len = a->count < b->count ? a->count : b->count;
        double dot = 0;

#if defined(__AVX__)
        /* AVX path: accumulate 4 products in parallel */
        uint32_t simd_len = len & ~3u;
        __m256d acc = _mm256_setzero_pd();
        for (uint32_t i = 0; i < simd_len; i += 4)
        {
            __m256d va = _mm256_set_pd(
                IS_INT(a->values[i + 3]) ? (double)as_int(a->values[i + 3]) : as_num(a->values[i + 3]),
                IS_INT(a->values[i + 2]) ? (double)as_int(a->values[i + 2]) : as_num(a->values[i + 2]),
                IS_INT(a->values[i + 1]) ? (double)as_int(a->values[i + 1]) : as_num(a->values[i + 1]),
                IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]));
            __m256d vb = _mm256_set_pd(
                IS_INT(b->values[i + 3]) ? (double)as_int(b->values[i + 3]) : as_num(b->values[i + 3]),
                IS_INT(b->values[i + 2]) ? (double)as_int(b->values[i + 2]) : as_num(b->values[i + 2]),
                IS_INT(b->values[i + 1]) ? (double)as_int(b->values[i + 1]) : as_num(b->values[i + 1]),
                IS_INT(b->values[i]) ? (double)as_int(b->values[i]) : as_num(b->values[i]));
            acc = _mm256_add_pd(acc, _mm256_mul_pd(va, vb));
        }
        double tmp[4];
        _mm256_storeu_pd(tmp, acc);
        dot = tmp[0] + tmp[1] + tmp[2] + tmp[3];
        for (uint32_t i = simd_len; i < len; i++)
        {
            double da = IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]);
            double db = IS_INT(b->values[i]) ? (double)as_int(b->values[i]) : as_num(b->values[i]);
            dot += da * db;
        }
#else
                for (uint32_t i = 0; i < len; i++)
                {
                    double da = IS_INT(a->values[i]) ? (double)as_int(a->values[i]) : as_num(a->values[i]);
                    double db = IS_INT(b->values[i]) ? (double)as_int(b->values[i]) : as_num(b->values[i]);
                    dot += da * db;
                }
#endif
        PUSH(val_num(dot));
        DISPATCH();
    }

    CASE(array_map) :
    {
        /* array_map(arr, fn) - Apply function to each element */
        Value fn_val = POP();
        Value arr_val = POP();
        if (!IS_ARRAY(arr_val) || !IS_CLOSURE(fn_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *arr = AS_ARRAY(arr_val);
        ObjClosure *fn = AS_CLOSURE(fn_val);
        vm->sp = sp;
        ObjArray *result = new_array(vm, arr->count);
        sp = vm->sp;

        for (uint32_t i = 0; i < arr->count; i++)
        {
            /* Push function and argument */
            PUSH(fn_val);
            PUSH(arr->values[i]);

            /* Call function - simplified inline call */
            if (vm->frame_count == FRAMES_MAX)
            {
                runtime_error(vm, "Stack overflow in array_map.");
                return INTERPRET_RUNTIME_ERROR;
            }
            CallFrame *new_frame = &vm->frames[vm->frame_count++];
            new_frame->closure = fn;
            new_frame->function = fn->function;
            new_frame->ip = vm->chunk.code + fn->function->code_start;
            new_frame->slots = sp - 2;
            new_frame->is_init = false;

            /* Save current position and run until return */
            uint8_t *saved_ip = ip;
            ip = new_frame->ip;
            bp = new_frame->slots;
            vm->ip = ip;
            vm->sp = sp;

            /* Execute the function synchronously */
            InterpretResult res = vm_run(vm);
            if (res != INTERPRET_OK)
            {
                return res;
            }

            sp = vm->sp;
            ip = saved_ip;
            result->values[i] = POP();
        }
        result->count = arr->count;
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(array_filter) :
    {
        /* array_filter(arr, fn) - Keep elements where fn returns true */
        Value fn_val = POP();
        Value arr_val = POP();
        if (!IS_ARRAY(arr_val) || !IS_CLOSURE(fn_val))
        {
            PUSH(VAL_NIL);
            DISPATCH();
        }
        ObjArray *arr = AS_ARRAY(arr_val);
        vm->sp = sp;
        ObjArray *result = new_array(vm, arr->count);
        sp = vm->sp;
        result->count = 0;

        for (uint32_t i = 0; i < arr->count; i++)
        {
            /* For filter, we need to call the function on each element */
            /* Simplified: just keep numeric non-zero values for now */
            Value v = arr->values[i];
            bool keep = false;
            if (IS_INT(v))
                keep = as_int(v) != 0;
            else if (IS_NUM(v))
                keep = as_num(v) != 0.0;
            else if (IS_BOOL(v))
                keep = IS_TRUE(v);
            else if (!IS_NIL(v))
                keep = true;

            if (keep)
            {
                result->values[result->count++] = v;
            }
        }
        PUSH(val_obj(result));
        DISPATCH();
    }

    CASE(array_reduce) :
    {
        /* array_reduce(arr, fn, init) - Reduce array to single value */
        Value init = POP();
        Value fn_val = POP();
        Value arr_val = POP();
        if (!IS_ARRAY(arr_val))
        {
            PUSH(init);
            DISPATCH();
        }
        ObjArray *arr = AS_ARRAY(arr_val);
        Value acc = init;

        /* Simplified reduce: sum numeric values */
        for (uint32_t i = 0; i < arr->count; i++)
        {
            if ((IS_INT(acc) || IS_NUM(acc)) && (IS_INT(arr->values[i]) || IS_NUM(arr->values[i])))
            {
                double da = IS_INT(acc) ? (double)as_int(acc) : as_num(acc);
                double dv = IS_INT(arr->values[i]) ? (double)as_int(arr->values[i]) : as_num(arr->values[i]);
                acc = val_num(da + dv);
            }
        }
        PUSH(acc);
        DISPATCH();
    }

    CASE(super_invoke) :
    {
        /* OP_SUPER_INVOKE: Invoke superclass method directly */
        ObjString *method_name = AS_STRING(READ_CONST());
        uint8_t arg_count = *ip++;
        Value instance_val = PEEK(arg_count);

        if (!IS_INSTANCE(instance_val))
        {
            runtime_error(vm, "Cannot use 'super' on non-instance.");
            return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance *instance = AS_INSTANCE(instance_val);
        ObjClass *superclass = instance->klass->superclass;

        if (superclass == NULL)
        {
            runtime_error(vm, "Class has no superclass.");
            return INTERPRET_RUNTIME_ERROR;
        }

        /* Look up method in superclass */
        for (uint16_t i = 0; i < superclass->method_count; i++)
        {
            if (superclass->method_names[i] == method_name ||
                (superclass->method_names[i]->length == method_name->length &&
                 memcmp(superclass->method_names[i]->chars, method_name->chars, method_name->length) == 0))
            {
                /* Found method - call it */
                Value method_val = superclass->methods[i];
                ObjFunction *method = NULL;
                ObjClosure *method_closure = NULL;

                if (IS_FUNCTION(method_val))
                {
                    method = AS_FUNCTION(method_val);
                }
                else if (IS_CLOSURE(method_val))
                {
                    method_closure = AS_CLOSURE(method_val);
                    method = method_closure->function;
                }
                else
                {
                    vm->sp = sp;
                    runtime_error(vm, "Superclass method is not callable.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                /* Check arity */
                if (arg_count != method->arity)
                {
                    vm->sp = sp;
                    runtime_error(vm, "Expected %d arguments but got %d.",
                                  method->arity, arg_count);
                    return INTERPRET_RUNTIME_ERROR;
                }

                /* Push new call frame */
                if (vm->frame_count == FRAMES_MAX)
                {
                    vm->sp = sp;
                    runtime_error(vm, "Stack overflow.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                CallFrame *frame = &vm->frames[vm->frame_count++];
                frame->function = method;
                frame->closure = method_closure;
                frame->ip = ip;
                frame->slots = sp - arg_count - 1; /* -1 for instance */
                frame->is_init = false;
                bp = frame->slots;

                ip = vm->chunk.code + method->code_start;
                DISPATCH();
            }
        }

        runtime_error(vm, "Undefined superclass method '%s'.", method_name->chars);
        return INTERPRET_RUNTIME_ERROR;
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

InterpretResult vm_interpret(VM *vm, const char *source)
{
    chunk_init(&vm->chunk);

    if (!compile(source, &vm->chunk, vm))
    {
        chunk_free(&vm->chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm->ip = vm->chunk.code;

    /* Push placeholder for slot 0 (script "function") to align local slots */
    *vm->sp++ = VAL_NIL;

    InterpretResult result = vm_run(vm);

    return result;
}
