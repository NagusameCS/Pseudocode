/*
 * Pseudocode Language - Memory Management
 */

#include "pseudo.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void* pseudo_realloc(VM* vm, void* ptr, size_t old_size, size_t new_size) {
    if (vm) {
        vm->bytes_allocated += new_size - old_size;
    }
    
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    
    void* result = realloc(ptr, new_size);
    if (result == NULL) {
        fprintf(stderr, "Out of memory!\n");
        exit(1);
    }
    return result;
}

/* FNV-1a hash */
static uint32_t hash_string(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static Obj* allocate_object(VM* vm, size_t size, ObjType type) {
    Obj* object = (Obj*)pseudo_realloc(vm, NULL, 0, size);
    object->type = type;
    object->marked = false;
    object->next = vm->objects;
    vm->objects = object;
    return object;
}

ObjString* allocate_string(VM* vm, char* chars, int length, uint32_t hash) {
    ObjString* string = (ObjString*)allocate_object(vm, 
        sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = length;
    string->hash = hash;
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    return string;
}

ObjString* copy_string(VM* vm, const char* chars, int length) {
    uint32_t hash = hash_string(chars, length);
    ObjString* string = (ObjString*)allocate_object(vm,
        sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = length;
    string->hash = hash;
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    return string;
}

ObjArray* new_array(VM* vm, uint32_t capacity) {
    ObjArray* array = (ObjArray*)allocate_object(vm, sizeof(ObjArray), OBJ_ARRAY);
    array->count = 0;
    array->capacity = capacity;
    array->values = capacity > 0 ? ALLOCATE(vm, Value, capacity) : NULL;
    return array;
}

ObjRange* new_range(VM* vm, int32_t start, int32_t end) {
    ObjRange* range = (ObjRange*)allocate_object(vm, sizeof(ObjRange), OBJ_RANGE);
    range->start = start;
    range->current = start;
    range->end = end;
    return range;
}

ObjFunction* new_function(VM* vm) {
    ObjFunction* function = (ObjFunction*)allocate_object(vm, sizeof(ObjFunction), OBJ_FUNCTION);
    function->arity = 0;
    function->locals_count = 0;
    function->code_start = 0;
    function->name = NULL;
    return function;
}

ObjDict* new_dict(VM* vm, uint32_t capacity) {
    ObjDict* dict = (ObjDict*)allocate_object(vm, sizeof(ObjDict), OBJ_DICT);
    dict->count = 0;
    dict->capacity = capacity < 8 ? 8 : capacity;
    dict->keys = (ObjString**)calloc(dict->capacity, sizeof(ObjString*));
    dict->values = (Value*)malloc(dict->capacity * sizeof(Value));
    return dict;
}

ObjBytes* new_bytes(VM* vm, uint32_t capacity) {
    ObjBytes* bytes = (ObjBytes*)allocate_object(vm, sizeof(ObjBytes), OBJ_BYTES);
    bytes->length = 0;
    bytes->capacity = capacity;
    bytes->data = capacity > 0 ? (uint8_t*)malloc(capacity) : NULL;
    return bytes;
}

/* ============ Arena Allocator ============ */
/* Ultra-fast bump allocator for temporary allocations */

Arena* arena_create(size_t size) {
    Arena* arena = (Arena*)malloc(sizeof(Arena));
    arena->size = size;
    arena->used = 0;
    arena->data = (uint8_t*)malloc(size);
    arena->next = NULL;
    return arena;
}

void* arena_alloc(Arena* arena, size_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~7;
    
    if (arena->used + size > arena->size) {
        /* Allocate new block */
        size_t new_size = arena->size * 2;
        if (new_size < size) new_size = size * 2;
        
        Arena* new_arena = arena_create(new_size);
        new_arena->next = arena->next;
        arena->next = new_arena;
        
        void* ptr = new_arena->data;
        new_arena->used = size;
        return ptr;
    }
    
    void* ptr = arena->data + arena->used;
    arena->used += size;
    return ptr;
}

void arena_reset(Arena* arena) {
    arena->used = 0;
    Arena* next = arena->next;
    while (next) {
        next->used = 0;
        next = next->next;
    }
}

void arena_destroy(Arena* arena) {
    Arena* current = arena;
    while (current) {
        Arena* next = current->next;
        free(current->data);
        free(current);
        current = next;
    }
}

void free_object(VM* vm, Obj* object) {
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            pseudo_realloc(vm, object, sizeof(ObjString) + string->length + 1, 0);
            break;
        }
        case OBJ_ARRAY: {
            ObjArray* array = (ObjArray*)object;
            FREE_ARRAY(vm, Value, array->values, array->capacity);
            FREE(vm, ObjArray, object);
            break;
        }
        case OBJ_RANGE:
            FREE(vm, ObjRange, object);
            break;
        case OBJ_FUNCTION:
            FREE(vm, ObjFunction, object);
            break;
        case OBJ_CLOSURE:
            FREE(vm, Obj, object);
            break;
        case OBJ_DICT: {
            ObjDict* dict = (ObjDict*)object;
            free(dict->keys);
            free(dict->values);
            FREE(vm, ObjDict, object);
            break;
        }
        case OBJ_BYTES: {
            ObjBytes* bytes = (ObjBytes*)object;
            free(bytes->data);
            FREE(vm, ObjBytes, object);
            break;
        }
    }
}

/* ============ Chunk ============ */

void chunk_init(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->const_count = 0;
    chunk->const_capacity = 0;
    chunk->constants = NULL;
}

void chunk_free(Chunk* chunk) {
    free(chunk->code);
    free(chunk->lines);
    free(chunk->constants);
    chunk_init(chunk);
}

void chunk_write(Chunk* chunk, uint8_t byte, uint16_t line) {
    if (chunk->capacity < chunk->count + 1) {
        uint32_t old_cap = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(old_cap);
        chunk->code = (uint8_t*)realloc(chunk->code, chunk->capacity);
        chunk->lines = (uint16_t*)realloc(chunk->lines, chunk->capacity * sizeof(uint16_t));
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

uint32_t chunk_add_const(Chunk* chunk, Value value) {
    if (chunk->const_capacity < chunk->const_count + 1) {
        uint32_t old_cap = chunk->const_capacity;
        chunk->const_capacity = GROW_CAPACITY(old_cap);
        chunk->constants = (Value*)realloc(chunk->constants, chunk->const_capacity * sizeof(Value));
    }
    chunk->constants[chunk->const_count] = value;
    return chunk->const_count++;
}
