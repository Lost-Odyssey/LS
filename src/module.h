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

    /* L-009.1 / A2: name of the module currently being type-checked recursively
       (set by the checker's import handler around each recursive checker_check).
       NULL while checking the root/main program. checker_check copies this into
       Checker.module_name so generic instantiations can be module-prefixed. */
    const char *current_check_module;

    /* Re-entrancy guard for the std.core.math merge: true while the LS-derived
       half (lib/std/core/math.ls) is being checked. During that window an
       `import std.core.math` inside the derived file itself must resolve to the
       built-in PRIMITIVES ONLY (no merge), otherwise merge_math_derived_exports
       would recurse forever. Lets derived helpers (to_db/...) call math.log10. */
    bool merging_math;
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

/* Returns true if a source file exists for the given import path,
   relative to current_file's directory. Used by the import handler to
   decide whether a user file shadows a built-in stdlib module. */
bool module_user_file_exists(const char *import_path, const char *current_file);

/* Resolve `import_path` to the file that module_load() would actually parse:
   user-relative-to-current_file first (module_resolve_path), falling back to
   <LS_HOME>/lib/... (the same private resolve_stdlib_path() module_load()
   itself falls back to). Unlike module_resolve_path() (which only searches the
   importer's own directory), this also falls back to <LS_HOME>/lib/ and
   returns NULL if neither location has the file — callers that just want
   "the real path or nothing" (e.g. `ls symbol`'s import expansion) don't need
   to duplicate module_load()'s two-step order or its registry bookkeeping.
   Caller owns the returned string. */
char *module_resolve_import_path(const char *import_path, const char *current_file);

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
