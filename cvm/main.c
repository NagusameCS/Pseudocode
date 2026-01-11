/*
 * Pseudocode Language - Main Entry Point
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#include "pseudo.h"
#include "jit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

/* Import preprocessor */
extern char* preprocess_imports(const char* source, const char* base_path);
extern void free_preprocessed(char* source);
extern bool has_imports(const char* source);

/* Version info */
#define PSEUDO_VERSION "1.2.0"
#define PSEUDO_BUILD_DATE __DATE__

/* Global VM for signal handling */
static VM* global_vm = NULL;
static bool debug_mode = false;

static void signal_handler(int sig) {
    (void)sig;
    printf("\n");
    if (global_vm) {
        vm_free(global_vm);
    }
    jit_cleanup();
    exit(0);
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    char *buffer = (char *)malloc(file_size + 1);
    if (buffer == NULL)
    {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
    if (bytes_read < file_size)
    {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytes_read] = '\0';
    fclose(file);
    return buffer;
}

static void run_file(const char *path)
{
    char *source = read_file(path);
    
    /* Preprocess imports at compile time (zero runtime overhead) */
    char *processed = NULL;
    if (has_imports(source)) {
        processed = preprocess_imports(source, path);
        if (processed == NULL) {
            fprintf(stderr, "Error processing imports.\n");
            free(source);
            exit(65);
        }
        free(source);
        source = processed;
    }

    VM vm;
    vm_init(&vm);
    vm.debug_mode = debug_mode;
    global_vm = &vm;

    InterpretResult result = vm_interpret(&vm, source);

    global_vm = NULL;
    vm_free(&vm);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR)
        exit(65);
    if (result == INTERPRET_RUNTIME_ERROR)
        exit(70);
}

static void print_help(void) {
    printf("\nPseudocode REPL Commands:\n");
    printf("  .help          Show this help message\n");
    printf("  .load <file>   Load and run a .pseudo file\n");
    printf("  .clear         Clear all variables and functions\n");
    printf("  .version       Show version information\n");
    printf("  .quit / exit   Exit the REPL\n");
    printf("\nExamples:\n");
    printf("  let x = 42\n");
    printf("  print(x * 2)\n");
    printf("  fn greet(name) print(\"Hello, \" + name) end\n");
    printf("\n");
}

static void repl(void)
{
    VM vm;
    vm_init(&vm);
    global_vm = &vm;
    
    /* Set up signal handler for Ctrl+C */
    signal(SIGINT, signal_handler);

    char line[4096];
    char multi_line[65536];
    int multi_line_depth = 0;
    bool in_multi_line = false;
    
    printf("\033[1;35mPseudocode %s\033[0m (C VM with JIT)\n", PSEUDO_VERSION);
    printf("Type '.help' for commands, 'exit' to quit\n\n");

    for (;;)
    {
        if (in_multi_line) {
            printf("... ");
        } else {
            printf("\033[1;32m>>>\033[0m ");
        }
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
        {
            printf("\n");
            break;
        }
        
        /* Remove trailing newline for command checking */
        size_t len = strlen(line);
        
        /* Handle REPL commands */
        if (!in_multi_line && line[0] == '.') {
            /* .quit command */
            if (strncmp(line, ".quit", 5) == 0 || strncmp(line, ".exit", 5) == 0) {
                break;
            }
            
            /* .help command */
            if (strncmp(line, ".help", 5) == 0) {
                print_help();
                continue;
            }
            
            /* .version command */
            if (strncmp(line, ".version", 8) == 0) {
                printf("Pseudocode %s (built %s)\n", PSEUDO_VERSION, PSEUDO_BUILD_DATE);
                printf("JIT: x86-64 trace compiler\n");
                continue;
            }
            
            /* .clear command */
            if (strncmp(line, ".clear", 6) == 0) {
                vm_free(&vm);
                vm_init(&vm);
                printf("Cleared.\n");
                continue;
            }
            
            /* .load <file> command */
            if (strncmp(line, ".load ", 6) == 0) {
                char* path = line + 6;
                /* Trim whitespace */
                while (*path == ' ') path++;
                char* end = path + strlen(path) - 1;
                while (end > path && (*end == '\n' || *end == ' ')) *end-- = '\0';
                
                if (strlen(path) == 0) {
                    printf("Usage: .load <filename>\n");
                    continue;
                }
                
                char* source = read_file(path);
                if (source) {
                    /* Process imports */
                    char* processed = NULL;
                    if (has_imports(source)) {
                        processed = preprocess_imports(source, path);
                        if (processed) {
                            free(source);
                            source = processed;
                        }
                    }
                    
                    printf("Loading '%s'...\n", path);
                    InterpretResult result = vm_interpret(&vm, source);
                    free(source);
                    
                    if (result == INTERPRET_OK) {
                        printf("\033[32mLoaded successfully.\033[0m\n");
                    }
                }
                continue;
            }
            
            printf("Unknown command. Type '.help' for available commands.\n");
            continue;
        }

        /* Handle exit command */
        if (strcmp(line, "exit\n") == 0 || strcmp(line, "quit\n") == 0)
            break;
        
        /* Multi-line detection: count fn/if/for/while/match vs end */
        if (!in_multi_line) {
            multi_line[0] = '\0';
        }
        
        /* Append to multi-line buffer */
        strncat(multi_line, line, sizeof(multi_line) - strlen(multi_line) - 1);
        
        /* Count block openers and closers */
        const char* p = line;
        while (*p) {
            /* Skip strings */
            if (*p == '"' || *p == '\'') {
                char quote = *p++;
                while (*p && *p != quote) {
                    if (*p == '\\' && *(p+1)) p++;
                    p++;
                }
                if (*p) p++;
                continue;
            }
            
            /* Check for keywords */
            if (strncmp(p, "fn ", 3) == 0 || strncmp(p, "fn(", 3) == 0) {
                multi_line_depth++;
            } else if (strncmp(p, "if ", 3) == 0) {
                multi_line_depth++;
            } else if (strncmp(p, "for ", 4) == 0) {
                multi_line_depth++;
            } else if (strncmp(p, "while ", 6) == 0) {
                multi_line_depth++;
            } else if (strncmp(p, "match ", 6) == 0) {
                multi_line_depth++;
            } else if (strncmp(p, "end", 3) == 0 && 
                       (p[3] == '\n' || p[3] == '\0' || p[3] == ' ' || p[3] == '\r')) {
                multi_line_depth--;
            }
            p++;
        }
        
        if (multi_line_depth > 0) {
            in_multi_line = true;
            continue;
        }
        
        in_multi_line = false;
        multi_line_depth = 0;
        
        vm_interpret(&vm, multi_line);
        multi_line[0] = '\0';
    }

    global_vm = NULL;
    vm_free(&vm);
}

static void print_usage(void) {
    printf("Pseudocode %s - Fast, intuitive programming language\n\n", PSEUDO_VERSION);
    printf("Usage: pseudo [options] [script.pseudo]\n\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --version  Show version information\n");
    printf("  -j, --jit      Enable JIT compilation (default)\n");
    printf("  -d, --debug    Enable debug mode\n");
    printf("  -e <code>      Execute code from command line\n");
    printf("\nExamples:\n");
    printf("  pseudo                    Start interactive REPL\n");
    printf("  pseudo script.pseudo      Run a script file\n");
    printf("  pseudo -e 'print(42)'     Execute inline code\n");
    printf("\n");
}

int main(int argc, char *argv[])
{
    /* Initialize JIT compiler */
    jit_init();

    if (argc == 1)
    {
        repl();
    }
    else if (argc == 2)
    {
        /* Check for flags */
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage();
            jit_cleanup();
            return 0;
        }
        if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
            printf("Pseudocode %s (built %s)\n", PSEUDO_VERSION, PSEUDO_BUILD_DATE);
            jit_cleanup();
            return 0;
        }
        run_file(argv[1]);
    }
    else if (argc == 3 && (strcmp(argv[1], "-e") == 0 || strcmp(argv[1], "--eval") == 0))
    {
        /* Execute inline code */
        VM vm;
        vm_init(&vm);
        InterpretResult result = vm_interpret(&vm, argv[2]);
        vm_free(&vm);
        jit_cleanup();
        if (result == INTERPRET_COMPILE_ERROR) return 65;
        if (result == INTERPRET_RUNTIME_ERROR) return 70;
        return 0;
    }
    else if (argc >= 3)
    {
        /* Check for flags before filename */
        int file_arg = 1;
        for (int i = 1; i < argc - 1; i++) {
            if (strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--jit") == 0) {
                /* JIT is default, but accept the flag */
                file_arg = i + 1;
            } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
                /* Debug mode - enable bytecode tracing */
                debug_mode = true;
                file_arg = i + 1;
            }
        }
        run_file(argv[file_arg]);
    }
    else
    {
        fprintf(stderr, "Usage: pseudo [script]\n");
        jit_cleanup();
        exit(64);
    }

    /* Cleanup JIT */
    jit_cleanup();

    return 0;
}
