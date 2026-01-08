/*
 * Pseudocode Language - Main Entry Point
 */

#include "pseudo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }
    
    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);
    
    char* buffer = (char*)malloc(file_size + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }
    
    size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
    if (bytes_read < file_size) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }
    
    buffer[bytes_read] = '\0';
    fclose(file);
    return buffer;
}

static void run_file(const char* path) {
    char* source = read_file(path);
    
    VM vm;
    vm_init(&vm);
    
    InterpretResult result = vm_interpret(&vm, source);
    
    vm_free(&vm);
    free(source);
    
    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

static void repl(void) {
    VM vm;
    vm_init(&vm);
    
    char line[1024];
    printf("Pseudocode 0.1.0 (C VM) - Type 'exit' to quit\n");
    
    for (;;) {
        printf(">>> ");
        
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        
        if (strcmp(line, "exit\n") == 0) break;
        
        vm_interpret(&vm, line);
    }
    
    vm_free(&vm);
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        repl();
    } else if (argc == 2) {
        run_file(argv[1]);
    } else {
        fprintf(stderr, "Usage: pseudo [script]\n");
        exit(64);
    }
    
    return 0;
}
