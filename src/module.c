/* module.c — Module path resolution and loading */
#include "module.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
  /* Mirror main.c's forward-declare to avoid <windows.h> TokenType collision. */
  typedef unsigned long DWORD_W32_M;
  __declspec(dllimport) DWORD_W32_M __stdcall GetModuleFileNameA(
      void *hModule, char *lpFilename, DWORD_W32_M nSize);
  __declspec(dllimport) void __stdcall Sleep(DWORD_W32_M dwMilliseconds);
#else
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
  #include <unistd.h>
  #if defined(__APPLE__)
    #include <mach-o/dyld.h>
  #endif
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
/* fopen with a short retry loop. On Windows, antivirus (Defender) can hold a
   transient lock on recently-written files (the build copies std/*.ls), making
   a single fopen fail spuriously. A failed open here is SEMANTIC, not just an
   error: module_user_file_exists falling through makes `import io` silently
   bind the compiler BUILTIN io module (different export set) → nondeterministic
   "module 'io' has no export 'read_file'" / "unknown type 'Str'" cascades.
   Retries only run on the failure path; the success path costs nothing. */
static FILE *fopen_retry(const char *path, const char *mode) {
    FILE *f = fopen(path, mode);
#ifdef _WIN32
    /* Only retry lock-style failures (EACCES/EBUSY...). ENOENT is a NORMAL
       outcome here — path resolution probes several candidate locations —
       and must stay a single cheap miss, not a 100ms retry loop. */
    for (int i = 0; f == NULL && errno != ENOENT && i < 5; i++) {
        Sleep(20);
        f = fopen(path, mode);
    }
#endif
    return f;
}

static char *read_file(const char *path) {
    FILE *file = fopen_retry(path, "rb");
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

/* Phase E.3.4: locate the directory containing the running ls executable.
   Mirrors main.c's static helper but lives in module.c too so module_load
   can use it for stdlib path resolution without exposing main internals.
   Writes a NUL-terminated path (no trailing separator) to `out`.
   Returns 0 on success. */
static int ls_executable_dir(char *out, size_t out_sz) {
    if (out == NULL || out_sz == 0) return -1;
    char buf[1024];
    size_t len = 0;

#ifdef _WIN32
    DWORD_W32_M n = GetModuleFileNameA(NULL, buf, (DWORD_W32_M)sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return -1;
    len = (size_t)n;
#elif defined(__APPLE__)
    uint32_t n = (uint32_t)sizeof(buf);
    if (_NSGetExecutablePath(buf, &n) != 0) return -1;
    len = strlen(buf);
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';
    len = (size_t)n;
#endif

    while (len > 0 && buf[len - 1] != '/' && buf[len - 1] != '\\') len--;
    if (len > 1) len--; /* drop trailing separator unless POSIX root */

    if (len + 1 > out_sz) return -1;
    memcpy(out, buf, len);
    out[len] = '\0';
    return 0;
}

/* Resolve an import path to an absolute .ls file path under LS_HOME.
   Resolution order (stdlib lives under <LS_HOME>/lib/, Zig-style):
     1. <LS_HOME>/lib/<rel_path>.ls   — dotted std imports: std.time → lib/std/time.ls
     2. <LS_HOME>/lib/std/<rel>.ls    — short names: io → lib/std/io.ls, path → lib/std/path.ls
   LS_HOME defaults to the directory containing ls.exe.
   Returns NULL if no file is found. Caller owns the returned string. */
static char *resolve_stdlib_path(const char *import_path) {
    /* Convert dots to path separators */
    size_t path_len = strlen(import_path);
    char *rel_path = (char *)malloc_safe(path_len + 1);
    for (size_t i = 0; i < path_len; i++) {
        rel_path[i] = (import_path[i] == '.') ? PATH_SEP : import_path[i];
    }
    rel_path[path_len] = '\0';

    /* Determine LS_HOME root */
    const char *ls_home_env = getenv("LS_HOME");
    char exe_dir[1024];
    const char *root = NULL;
    if (ls_home_env && ls_home_env[0] != '\0') {
        root = ls_home_env;
    } else if (ls_executable_dir(exe_dir, sizeof(exe_dir)) == 0) {
        root = exe_dir;
    } else {
        free(rel_path);
        return NULL;
    }

    /* Try 1: <root>/lib/<rel_path>.ls  (std.time → lib/std/time.ls) */
    size_t full_len = strlen(root) + 1 + 4 /*"lib/"*/ + strlen(rel_path) + 3 + 1;
    char *full = (char *)malloc_safe(full_len);
    snprintf(full, full_len, "%s%slib%s%s.ls",
             root, PATH_SEP_STR, PATH_SEP_STR, rel_path);
    FILE *f = fopen_retry(full, "rb");
    if (f != NULL) { fclose(f); free(rel_path); return full; }
    free(full);

    /* Try 2: <root>/lib/std/<rel_path>.ls  (io → lib/std/io.ls, path → lib/std/path.ls) */
    size_t full2_len = strlen(root) + 1 + 8 /*"lib/std/"*/ + strlen(rel_path) + 3 + 1;
    char *full2 = (char *)malloc_safe(full2_len);
    snprintf(full2, full2_len, "%s%slib%sstd%s%s.ls",
             root, PATH_SEP_STR, PATH_SEP_STR, PATH_SEP_STR, rel_path);
    free(rel_path);
    f = fopen_retry(full2, "rb");
    if (f != NULL) { fclose(f); return full2; }
    free(full2);
    return NULL;
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

bool module_user_file_exists(const char *import_path, const char *current_file) {
    /* "User file" = a file in the IMPORTER's own directory that overrides a
       compiler built-in module (user-priority shadowing). Only a user-dir file
       fully shadows the built-in; a stdlib file under resolve_stdlib_path is NOT
       a shadow — for std.core.math it is the LS-derived half that MERGES with the
       built-in primitives (see merge_math_derived_exports). Checking the stdlib
       path here would make `import std.core.math` skip the built-in branch and
       lose sqrt/sin/... So only the user-relative path counts as a shadow. */
    char *path = module_resolve_path(import_path, current_file);
    if (path != NULL) {
        FILE *f = fopen_retry(path, "rb");
        bool exists = (f != NULL);
        if (f) fclose(f);
        free(path);
        if (exists) return true;
    }
    return false;
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

    /* Three-level resolution: user file → std/ → compiler builtin (checker.c).
       Try the user-relative path first. */
    char *file_path = module_resolve_path(import_path, current_file);
    char *source = NULL;
    if (file_path != NULL) {
        source = read_file(file_path);
    }
    if (source == NULL) {
        /* Fall back to <LS_HOME>/std/<name>.ls (or std.foo → std/foo.ls) */
        free(file_path);
        file_path = resolve_stdlib_path(import_path);
        if (file_path != NULL) {
            source = read_file(file_path);
        }
    }
    if (source == NULL) {
        fprintf(stderr, "[module] cannot find module '%s' (checked user dir and std/)\n",
                import_path);
        free(file_path);
        return NULL;
    }

    /* B-5: dedup by the RESOLVED FILE, not by the import-path spelling. Two
       different spellings can resolve to the same .ls file — e.g. `import
       std.sys.io` (resolve Try 1: lib/std/sys/io.ls) and `import sys.io`
       (resolve Try 2: lib/std/<rel> = lib/std/sys/io.ls). The spelling-keyed
       module_find() above misses that, so without this the same module would be
       parsed, type-checked, and code-generated TWICE (each under its own symbol
       prefix). If the resolved file is already registered under another
       spelling, reuse that entry. The checker keys the module TYPE by mod->name
       (the canonical first-loaded spelling), so call sites through either
       spelling mangle to the single emitted copy. */
    for (int i = 0; i < reg->count; i++) {
        if (reg->modules[i].file_path != NULL &&
            strcmp(reg->modules[i].file_path, file_path) == 0) {
            free(file_path);
            free(source);
            return &reg->modules[i];
        }
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
