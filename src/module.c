/* module.c — Module path resolution and loading */
#include "module.h"
#include "parser.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
#else
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#endif

/* Portable strdup */
static char *dup_str(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc_safe(len);
    memcpy(copy, s, len);
    return copy;
}

/* Extract directory part of a file path */
static char *get_directory(const char *file_path) {
    const char *last_sep = NULL;
    for (const char *p = file_path; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    if (last_sep == NULL) {
        return dup_str(".");
    }
    size_t len = (size_t)(last_sep - file_path);
    char *dir = (char *)malloc_safe(len + 1);
    memcpy(dir, file_path, len);
    dir[len] = '\0';
    return dir;
}

/* Read an entire file into a malloc'd buffer. Returns NULL on failure. */
static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buf = (char *)malloc_safe((size_t)size + 1);
    size_t rd = fread(buf, 1, (size_t)size, file);
    buf[rd] = '\0';
    fclose(file);
    return buf;
}

/* ---- Public API ---- */

ModuleRegistry *module_registry_new(void) {
    ModuleRegistry *reg = (ModuleRegistry *)malloc_safe(sizeof(ModuleRegistry));
    memset(reg, 0, sizeof(ModuleRegistry));
    return reg;
}

void module_registry_free(ModuleRegistry *reg) {
    if (reg == NULL) return;
    for (int i = 0; i < reg->count; i++) {
        free(reg->modules[i].name);
        free(reg->modules[i].file_path);
        if (reg->modules[i].ast) ast_free(reg->modules[i].ast);
        free(reg->modules[i].source);
    }
    free(reg->modules);
    free(reg->import_stack);
    free(reg);
}

ModuleInfo *module_find(ModuleRegistry *reg, const char *name) {
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->modules[i].name, name) == 0) return &reg->modules[i];
    }
    return NULL;
}

char *module_resolve_path(const char *import_path, const char *current_file) {
    char *dir = get_directory(current_file);

    /* Convert dots in import path to path separators */
    size_t path_len = strlen(import_path);
    char *rel_path = (char *)malloc_safe(path_len + 1);
    for (size_t i = 0; i < path_len; i++) {
        rel_path[i] = (import_path[i] == '.') ? PATH_SEP : import_path[i];
    }
    rel_path[path_len] = '\0';

    /* Build full path: dir/rel_path.ls */
    size_t full_len = strlen(dir) + 1 + strlen(rel_path) + 3 + 1;
    char *full = (char *)malloc_safe(full_len);
    snprintf(full, full_len, "%s%s%s.ls", dir, PATH_SEP_STR, rel_path);

    free(dir);
    free(rel_path);
    return full;
}

ModuleInfo *module_load(ModuleRegistry *reg, const char *import_path,
                        const char *current_file) {
    /* Check if already loaded */
    ModuleInfo *existing = module_find(reg, import_path);
    if (existing) return existing;

    /* Resolve file path */
    char *file_path = module_resolve_path(import_path, current_file);
    if (file_path == NULL) {
        fprintf(stderr, "[module] cannot resolve path for '%s'\n", import_path);
        return NULL;
    }

    /* Read file */
    char *source = read_file(file_path);
    if (source == NULL) {
        fprintf(stderr, "[module] cannot open '%s'\n", file_path);
        free(file_path);
        return NULL;
    }

    /* Parse */
    AstNode *ast = parse(source, file_path);
    if (ast == NULL) {
        fprintf(stderr, "[module] parse error in '%s'\n", file_path);
        free(source);
        free(file_path);
        return NULL;
    }

    /* Add to registry */
    if (reg->count >= reg->cap) {
        reg->cap = GROW_CAPACITY(reg->cap);
        reg->modules = GROW_ARRAY(ModuleInfo, reg->modules, reg->cap);
    }
    int idx = reg->count++;
    reg->modules[idx].name = dup_str(import_path);
    reg->modules[idx].file_path = file_path;
    reg->modules[idx].source = source;
    reg->modules[idx].ast = ast;
    reg->modules[idx].checked = false;

    return &reg->modules[idx];
}

bool module_push_import(ModuleRegistry *reg, const char *name) {
    if (module_is_importing(reg, name)) return false;
    if (reg->stack_depth >= reg->stack_cap) {
        reg->stack_cap = GROW_CAPACITY(reg->stack_cap);
        reg->import_stack = GROW_ARRAY(const char *, reg->import_stack, reg->stack_cap);
    }
    reg->import_stack[reg->stack_depth++] = name;
    return true;
}

void module_pop_import(ModuleRegistry *reg) {
    if (reg->stack_depth > 0) reg->stack_depth--;
}

bool module_is_importing(ModuleRegistry *reg, const char *name) {
    for (int i = 0; i < reg->stack_depth; i++) {
        if (strcmp(reg->import_stack[i], name) == 0) return true;
    }
    return false;
}
