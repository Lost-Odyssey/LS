/* module.h — Module system: path resolution, loading, and registry */
#ifndef LS_MODULE_H
#define LS_MODULE_H

#include "ast.h"
#include "types.h"
#include "common.h"

/* Information about a loaded module */
typedef struct {
    char *name;         /* Module name (e.g., "math") */
    char *file_path;    /* Resolved file path */
    char *source;       /* Source text (owned) */
    AstNode *ast;       /* Parsed AST (owned) */
    bool checked;       /* Has been type-checked */
} ModuleInfo;

/* Registry of all loaded modules */
typedef struct ModuleRegistry {
    ModuleInfo *modules;
    int count;
    int cap;

    /* Import stack for circular dependency detection */
    const char **import_stack;
    int stack_depth;
    int stack_cap;
} ModuleRegistry;

/* Create a new module registry */
ModuleRegistry *module_registry_new(void);

/* Free the registry and all loaded modules */
void module_registry_free(ModuleRegistry *reg);

/* Resolve an import path to a filesystem path.
   import_path: e.g., "math" or "utils.math"
   current_file: path of the importing file
   Returns: allocated string, or NULL on failure. Caller owns. */
char *module_resolve_path(const char *import_path, const char *current_file);

/* Find a module by name in the registry. Returns NULL if not found */
ModuleInfo *module_find(ModuleRegistry *reg, const char *name);

/* Load a module: resolve path, parse, add to registry.
   Does NOT type-check (caller should do that).
   Returns the ModuleInfo, or NULL on failure. */
ModuleInfo *module_load(ModuleRegistry *reg, const char *import_path,
                        const char *current_file);

/* Push a module name onto the import stack. Returns false if circular. */
bool module_push_import(ModuleRegistry *reg, const char *name);

/* Pop the top of the import stack */
void module_pop_import(ModuleRegistry *reg);

/* Check if a module is currently being imported (on the stack) */
bool module_is_importing(ModuleRegistry *reg, const char *name);

#endif /* LS_MODULE_H */
