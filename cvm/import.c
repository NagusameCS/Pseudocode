/*
 * Pseudocode Language - Import Preprocessor
 * Zero-overhead compile-time import resolution
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#include "pseudo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <limits.h>

#define MAX_IMPORTS 256
#define MAX_PATH_LEN 4096
#define INITIAL_BUFFER_SIZE 65536

/* Track imported files to prevent circular imports */
typedef struct
{
    char *paths[MAX_IMPORTS];
    int count;
} ImportSet;

static ImportSet imported_files = {.count = 0};

/* Standard library search paths */
static const char *std_lib_paths[] = {
    "/usr/local/lib/pseudocode/",
    "/usr/lib/pseudocode/",
    "~/.pseudocode/lib/",
    "../lib/",
    "./lib/",
    NULL};

/* ============ Helper Functions ============ */

static char *read_import_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return NULL;

    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    char *buffer = (char *)malloc(file_size + 1);
    if (buffer == NULL)
    {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
    buffer[bytes_read] = '\0';
    fclose(file);
    return buffer;
}

static bool is_already_imported(const char *path)
{
    for (int i = 0; i < imported_files.count; i++)
    {
        if (strcmp(imported_files.paths[i], path) == 0)
        {
            return true;
        }
    }
    return false;
}

static void mark_imported(const char *path)
{
    if (imported_files.count >= MAX_IMPORTS)
    {
        fprintf(stderr, "Error: Too many imports (max %d)\n", MAX_IMPORTS);
        return;
    }
    imported_files.paths[imported_files.count] = strdup(path);
    imported_files.count++;
}

static void clear_imported(void)
{
    for (int i = 0; i < imported_files.count; i++)
    {
        free(imported_files.paths[i]);
        imported_files.paths[i] = NULL;
    }
    imported_files.count = 0;
}

/* Resolve a path relative to base_dir, or search standard library */
static char *resolve_import_path(const char *import_path, const char *base_dir)
{
    static char resolved[MAX_PATH_LEN];

    /* If it's a quoted string path (starts with . or /) - relative/absolute import */
    if (import_path[0] == '.' || import_path[0] == '/')
    {
        if (import_path[0] == '/')
        {
            /* Absolute path */
            strncpy(resolved, import_path, MAX_PATH_LEN - 1);
        }
        else
        {
            /* Relative path */
            snprintf(resolved, MAX_PATH_LEN, "%s/%s", base_dir, import_path);
        }

        /* Add .pseudo extension if missing */
        size_t len = strlen(resolved);
        if (len < 7 || (strcmp(resolved + len - 7, ".pseudo") != 0 &&
                        strcmp(resolved + len - 4, ".psc") != 0))
        {
            strncat(resolved, ".pseudo", MAX_PATH_LEN - len - 1);
        }

        if (access(resolved, F_OK) == 0)
        {
            return resolved;
        }
        return NULL;
    }

    /* Module name - search standard library paths */
    char module_path[MAX_PATH_LEN];

    /* First, try current directory */
    snprintf(resolved, MAX_PATH_LEN, "%s/%s.pseudo", base_dir, import_path);
    if (access(resolved, F_OK) == 0)
        return resolved;

    snprintf(resolved, MAX_PATH_LEN, "%s/%s.psc", base_dir, import_path);
    if (access(resolved, F_OK) == 0)
        return resolved;

    /* Then search standard library paths */
    for (int i = 0; std_lib_paths[i] != NULL; i++)
    {
        const char *lib_path = std_lib_paths[i];

        /* Expand ~ to home directory */
        if (lib_path[0] == '~')
        {
            const char *home = getenv("HOME");
            if (home)
            {
                snprintf(module_path, MAX_PATH_LEN, "%s%s", home, lib_path + 1);
                lib_path = module_path;
            }
        }

        snprintf(resolved, MAX_PATH_LEN, "%s%s.pseudo", lib_path, import_path);
        if (access(resolved, F_OK) == 0)
            return resolved;

        snprintf(resolved, MAX_PATH_LEN, "%s%s.psc", lib_path, import_path);
        if (access(resolved, F_OK) == 0)
            return resolved;
    }

    /* Check PSEUDO_PATH environment variable */
    const char *pseudo_path = getenv("PSEUDO_PATH");
    if (pseudo_path)
    {
        char *path_copy = strdup(pseudo_path);
        char *token = strtok(path_copy, ":");
        while (token)
        {
            snprintf(resolved, MAX_PATH_LEN, "%s/%s.pseudo", token, import_path);
            if (access(resolved, F_OK) == 0)
            {
                free(path_copy);
                return resolved;
            }
            token = strtok(NULL, ":");
        }
        free(path_copy);
    }

    return NULL;
}

/* Extract directory from a file path */
static void get_directory(const char *path, char *dir, size_t size)
{
    char *path_copy = strdup(path);
    char *dir_name = dirname(path_copy);
    strncpy(dir, dir_name, size - 1);
    dir[size - 1] = '\0';
    free(path_copy);
}

/* ============ Import Processing ============ */

/* Buffer for building the combined source */
typedef struct
{
    char *data;
    size_t size;
    size_t capacity;
} StringBuilder;

static void sb_init(StringBuilder *sb)
{
    sb->capacity = INITIAL_BUFFER_SIZE;
    sb->data = (char *)malloc(sb->capacity);
    sb->data[0] = '\0';
    sb->size = 0;
}

static void sb_append(StringBuilder *sb, const char *str)
{
    size_t len = strlen(str);
    while (sb->size + len + 1 > sb->capacity)
    {
        sb->capacity *= 2;
        sb->data = (char *)realloc(sb->data, sb->capacity);
    }
    memcpy(sb->data + sb->size, str, len + 1);
    sb->size += len;
}

static void sb_append_n(StringBuilder *sb, const char *str, size_t n)
{
    while (sb->size + n + 1 > sb->capacity)
    {
        sb->capacity *= 2;
        sb->data = (char *)realloc(sb->data, sb->capacity);
    }
    memcpy(sb->data + sb->size, str, n);
    sb->size += n;
    sb->data[sb->size] = '\0';
}

static void sb_free(StringBuilder *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->size = 0;
    sb->capacity = 0;
}

/* Skip whitespace, return pointer to next non-whitespace */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

/* Check if line starts with "import" keyword */
static bool starts_with_import(const char *line)
{
    const char *p = skip_ws(line);
    return strncmp(p, "import", 6) == 0 &&
           (p[6] == ' ' || p[6] == '\t' || p[6] == '"' || p[6] == '\'');
}

/* Check if line starts with "from" keyword (selective import) */
static bool starts_with_from(const char *line)
{
    const char *p = skip_ws(line);
    return strncmp(p, "from", 4) == 0 &&
           (p[4] == ' ' || p[4] == '\t' || p[4] == '"' || p[4] == '\'');
}

/* Parse an import statement, extract path and optional alias */
typedef struct
{
    char path[MAX_PATH_LEN];
    char alias[256];
    char selected_names[32][128]; /* Names for selective import */
    int selected_count;           /* Number of selected names (0 = all) */
    bool has_alias;
    bool is_valid;
    bool is_selective; /* true if "from X import Y" style */
} ImportInfo;

static ImportInfo parse_import(const char *line)
{
    ImportInfo info = {.is_valid = false, .has_alias = false, .is_selective = false, .selected_count = 0};

    const char *p = skip_ws(line);

    /* Check for "from X import Y" style */
    if (strncmp(p, "from", 4) == 0 && (p[4] == ' ' || p[4] == '\t' || p[4] == '"'))
    {
        info.is_selective = true;
        p += 4;
        p = skip_ws(p);

        /* Parse path */
        if (*p == '"' || *p == '\'')
        {
            char quote = *p++;
            const char *start = p;
            while (*p && *p != quote && *p != '\n')
                p++;
            if (*p != quote)
                return info;
            size_t len = p - start;
            if (len >= MAX_PATH_LEN)
                return info;
            strncpy(info.path, start, len);
            info.path[len] = '\0';
            p++;
        }
        else
        {
            /* Module name */
            const char *start = p;
            while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                          (*p >= '0' && *p <= '9') || *p == '_' || *p == '.'))
            {
                p++;
            }
            size_t len = p - start;
            if (len == 0 || len >= MAX_PATH_LEN)
                return info;
            strncpy(info.path, start, len);
            info.path[len] = '\0';
        }

        /* Expect "import" keyword */
        p = skip_ws(p);
        if (strncmp(p, "import", 6) != 0)
            return info;
        p += 6;
        p = skip_ws(p);

        /* Parse comma-separated names */
        while (*p && *p != '\n' && info.selected_count < 32)
        {
            const char *start = p;
            while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                          (*p >= '0' && *p <= '9') || *p == '_'))
            {
                p++;
            }
            size_t len = p - start;
            if (len > 0 && len < 128)
            {
                strncpy(info.selected_names[info.selected_count], start, len);
                info.selected_names[info.selected_count][len] = '\0';
                info.selected_count++;
            }
            p = skip_ws(p);
            if (*p == ',')
            {
                p++;
                p = skip_ws(p);
            }
            else
            {
                break;
            }
        }

        if (info.selected_count == 0)
            return info;
        info.is_valid = true;
        return info;
    }

    /* Skip "import" */
    if (strncmp(p, "import", 6) != 0)
        return info;
    p += 6;
    p = skip_ws(p);

    /* Check for quoted path or module name */
    if (*p == '"' || *p == '\'')
    {
        /* Quoted path: import "path/to/file.pseudo" */
        char quote = *p++;
        const char *start = p;
        while (*p && *p != quote && *p != '\n')
            p++;
        if (*p != quote)
            return info;

        size_t len = p - start;
        if (len >= MAX_PATH_LEN)
            return info;
        strncpy(info.path, start, len);
        info.path[len] = '\0';
        p++;
    }
    else
    {
        /* Module name: import math */
        const char *start = p;
        while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                      (*p >= '0' && *p <= '9') || *p == '_' || *p == '.'))
        {
            p++;
        }
        size_t len = p - start;
        if (len == 0 || len >= MAX_PATH_LEN)
            return info;
        strncpy(info.path, start, len);
        info.path[len] = '\0';
    }

    /* Check for "as alias" */
    p = skip_ws(p);
    if (strncmp(p, "as", 2) == 0 && (p[2] == ' ' || p[2] == '\t'))
    {
        p += 2;
        p = skip_ws(p);

        const char *start = p;
        while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                      (*p >= '0' && *p <= '9') || *p == '_'))
        {
            p++;
        }
        size_t len = p - start;
        if (len > 0 && len < 256)
        {
            strncpy(info.alias, start, len);
            info.alias[len] = '\0';
            info.has_alias = true;
        }
    }

    info.is_valid = true;
    return info;
}

/* Filter source to only include specified function definitions */
static char *filter_selective_imports(const char *source, const ImportInfo *info)
{
    StringBuilder filtered;
    sb_init(&filtered);

    const char *p = source;
    while (*p)
    {
        const char *line_start = p;

        /* Skip whitespace */
        while (*p == ' ' || *p == '\t')
            p++;

        /* Check if this is a function definition we want */
        bool include_block = false;
        bool is_fn_def = false;
        bool is_let_def = false;

        if (strncmp(p, "fn ", 3) == 0 || strncmp(p, "fn\t", 3) == 0)
        {
            is_fn_def = true;
            p += 3;
            while (*p == ' ' || *p == '\t')
                p++;

            /* Extract function name */
            const char *name_start = p;
            while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                          (*p >= '0' && *p <= '9') || *p == '_'))
            {
                p++;
            }
            size_t name_len = p - name_start;

            /* Check if this name is in selected list */
            for (int i = 0; i < info->selected_count; i++)
            {
                if (strlen(info->selected_names[i]) == name_len &&
                    strncmp(info->selected_names[i], name_start, name_len) == 0)
                {
                    include_block = true;
                    break;
                }
            }
        }
        else if (strncmp(p, "let ", 4) == 0 || strncmp(p, "let\t", 4) == 0)
        {
            is_let_def = true;
            p += 4;
            while (*p == ' ' || *p == '\t')
                p++;

            /* Extract variable name */
            const char *name_start = p;
            while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                          (*p >= '0' && *p <= '9') || *p == '_'))
            {
                p++;
            }
            size_t name_len = p - name_start;

            /* Check if this name is in selected list */
            for (int i = 0; i < info->selected_count; i++)
            {
                if (strlen(info->selected_names[i]) == name_len &&
                    strncmp(info->selected_names[i], name_start, name_len) == 0)
                {
                    include_block = true;
                    break;
                }
            }
        }

        /* Find end of current construct */
        p = line_start;
        if (is_fn_def)
        {
            /* Find matching 'end' */
            int depth = 0;
            bool in_fn = false;
            while (*p)
            {
                /* Skip to meaningful content */
                while (*p == ' ' || *p == '\t')
                    p++;

                if (strncmp(p, "fn ", 3) == 0 || strncmp(p, "fn\t", 3) == 0)
                {
                    if (!in_fn)
                        in_fn = true;
                    else
                        depth++;
                }
                else if (strncmp(p, "if ", 3) == 0 || strncmp(p, "if\t", 3) == 0 ||
                         strncmp(p, "for ", 4) == 0 || strncmp(p, "for\t", 4) == 0 ||
                         strncmp(p, "while ", 6) == 0 || strncmp(p, "while\t", 6) == 0 ||
                         strncmp(p, "match ", 6) == 0 || strncmp(p, "match\t", 6) == 0)
                {
                    depth++;
                }
                else if (strncmp(p, "end", 3) == 0 &&
                         (p[3] == '\n' || p[3] == '\r' || p[3] == ' ' || p[3] == '\t' || p[3] == '\0'))
                {
                    if (depth == 0)
                    {
                        /* Found matching end */
                        while (*p && *p != '\n')
                            p++;
                        if (*p == '\n')
                            p++;
                        break;
                    }
                    depth--;
                }

                /* Skip to end of line */
                while (*p && *p != '\n')
                    p++;
                if (*p == '\n')
                    p++;
            }

            if (include_block)
            {
                sb_append_n(&filtered, line_start, p - line_start);
            }
        }
        else if (is_let_def)
        {
            /* Just copy the single line */
            while (*p && *p != '\n')
                p++;
            if (*p == '\n')
                p++;

            if (include_block)
            {
                sb_append_n(&filtered, line_start, p - line_start);
            }
        }
        else
        {
            /* Skip line (not a definition we care about) */
            while (*p && *p != '\n')
                p++;
            if (*p == '\n')
                p++;
        }
    }

    return filtered.data;
}

/* Forward declaration for recursive processing */
static bool process_imports_recursive(const char *source, const char *base_dir,
                                      StringBuilder *output, int depth);

/* Process a single import and append its contents */
static bool process_single_import(const ImportInfo *info, const char *base_dir,
                                  StringBuilder *output, int depth)
{
    if (depth > 32)
    {
        fprintf(stderr, "Error: Import depth exceeded (possible circular import)\n");
        return false;
    }

    /* Resolve the import path */
    char *resolved_path = resolve_import_path(info->path, base_dir);
    if (!resolved_path)
    {
        fprintf(stderr, "Error: Cannot find import '%s'\n", info->path);
        return false;
    }

    /* Check for circular import */
    char abs_path[MAX_PATH_LEN];
    if (realpath(resolved_path, abs_path) == NULL)
    {
        strncpy(abs_path, resolved_path, MAX_PATH_LEN - 1);
    }

    if (is_already_imported(abs_path))
    {
        /* Already imported, skip silently */
        return true;
    }
    mark_imported(abs_path);

    /* Read the imported file */
    char *import_source = read_import_file(resolved_path);
    if (!import_source)
    {
        fprintf(stderr, "Error: Cannot read import '%s'\n", resolved_path);
        return false;
    }

    /* Get the directory of the imported file for nested imports */
    char import_dir[MAX_PATH_LEN];
    get_directory(resolved_path, import_dir, MAX_PATH_LEN);

    /* Add a comment marking the import source */
    char comment[MAX_PATH_LEN + 128];
    if (info->is_selective)
    {
        snprintf(comment, sizeof(comment), "\n// [selective import: %s (", info->path);
        sb_append(output, comment);
        for (int i = 0; i < info->selected_count; i++)
        {
            if (i > 0)
                sb_append(output, ", ");
            sb_append(output, info->selected_names[i]);
        }
        sb_append(output, ")]\n");
    }
    else
    {
        snprintf(comment, sizeof(comment), "\n// [import: %s]\n", info->path);
        sb_append(output, comment);
    }

    /* If there's an alias, we'll prefix all top-level function definitions */
    if (info->has_alias)
    {
        /* For aliased imports, we need to rename functions */
        /* This is a simple approach - prefix function names */
        /* TODO: More sophisticated namespace handling */
        char prefix_comment[512];
        snprintf(prefix_comment, sizeof(prefix_comment),
                 "// [namespace: %s]\n", info->alias);
        sb_append(output, prefix_comment);
    }

    /* For selective imports, filter the source first */
    char *source_to_process = import_source;
    char *filtered_source = NULL;
    if (info->is_selective && info->selected_count > 0)
    {
        filtered_source = filter_selective_imports(import_source, info);
        source_to_process = filtered_source;
    }

    /* Recursively process the imported file */
    bool success = process_imports_recursive(source_to_process, import_dir, output, depth + 1);

    sb_append(output, "\n// [end import]\n");

    free(import_source);
    if (filtered_source)
        free(filtered_source);
    return success;
}

/* Recursively process all imports in source */
static bool process_imports_recursive(const char *source, const char *base_dir,
                                      StringBuilder *output, int depth)
{
    const char *line_start = source;
    const char *p = source;

    while (*p)
    {
        /* Find end of line */
        const char *line_end = p;
        while (*line_end && *line_end != '\n')
            line_end++;

        /* Check if this line is an import statement (import or from...import) */
        if (starts_with_import(line_start) || starts_with_from(line_start))
        {
            ImportInfo info = parse_import(line_start);
            if (info.is_valid)
            {
                if (!process_single_import(&info, base_dir, output, depth))
                {
                    return false;
                }
            }
            else
            {
                fprintf(stderr, "Error: Invalid import syntax\n");
                return false;
            }
        }
        else
        {
            /* Not an import - copy the line as-is */
            size_t line_len = line_end - line_start;
            sb_append_n(output, line_start, line_len);
            if (*line_end == '\n')
            {
                sb_append(output, "\n");
            }
        }

        /* Move to next line */
        if (*line_end == '\n')
        {
            p = line_end + 1;
        }
        else
        {
            p = line_end;
        }
        line_start = p;
    }

    return true;
}

/* ============ Public API ============ */

char *preprocess_imports(const char *source, const char *base_path)
{
    /* Clear previous import tracking */
    clear_imported();

    /* Mark the main file as imported */
    if (base_path && strlen(base_path) > 0)
    {
        char abs_path[MAX_PATH_LEN];
        if (realpath(base_path, abs_path) != NULL)
        {
            mark_imported(abs_path);
        }
    }

    /* Get base directory */
    char base_dir[MAX_PATH_LEN];
    if (base_path && strlen(base_path) > 0)
    {
        get_directory(base_path, base_dir, MAX_PATH_LEN);
    }
    else
    {
        if (getcwd(base_dir, MAX_PATH_LEN) == NULL)
        {
            strcpy(base_dir, ".");
        }
    }

    /* Build combined source */
    StringBuilder output;
    sb_init(&output);

    if (!process_imports_recursive(source, base_dir, &output, 0))
    {
        sb_free(&output);
        return NULL;
    }

    /* Return ownership of the buffer */
    return output.data;
}

void free_preprocessed(char *source)
{
    if (source)
    {
        free(source);
    }
}

/* Check if source has any imports (quick check to skip preprocessing) */
bool has_imports(const char *source)
{
    const char *p = source;
    while (*p)
    {
        /* Skip to start of line */
        while (*p == ' ' || *p == '\t')
            p++;

        /* Check for "import" */
        if (strncmp(p, "import", 6) == 0 &&
            (p[6] == ' ' || p[6] == '\t' || p[6] == '"' || p[6] == '\''))
        {
            return true;
        }

        /* Check for "from" (selective import) */
        if (strncmp(p, "from", 4) == 0 &&
            (p[4] == ' ' || p[4] == '\t' || p[4] == '"' || p[4] == '\''))
        {
            return true;
        }

        /* Skip to end of line */
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            p++;
    }
    return false;
}
