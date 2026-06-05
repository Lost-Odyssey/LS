/* jit.h — LLJIT-based JIT engine with incremental compilation */
#ifndef LS_JIT_H
#define LS_JIT_H

#include "ast.h"
#include "types.h"
#include "common.h"

#include <llvm-c/Core.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/Orc.h>
#include <llvm-c/OrcEE.h>
#include <llvm-c/Target.h>

/* Function version entry for incremental compilation tracking */
typedef struct {
    char *name;
    uint64_t hash;
} JitFnEntry;

/* JIT engine context */
typedef struct {
    LLVMOrcLLJITRef jit;
    LLVMOrcJITDylibRef main_dylib;
    LLVMOrcThreadSafeContextRef ts_context;

    /* Function version registry for incremental recompilation */
    JitFnEntry *fn_registry;
    int fn_count;
    int fn_cap;

    bool initialized;
    bool memcheck_enabled;  /* propagate to CodegenContext for each module */
    bool profile_enabled;   /* propagate to CodegenContext for each module */
    bool jit_optimize;      /* --optimize: run O2 pipeline on each module before JIT */
} JitEngine;

/* Initialize the JIT engine */
int jit_init(JitEngine *engine);

/* Destroy the JIT engine and free all resources */
void jit_destroy(JitEngine *engine);

/* Compile and add an LLVM module to the JIT. Transfers ownership of module. */
int jit_add_module(JitEngine *engine, LLVMModuleRef module);

/* Look up a symbol by name and return its address. Returns 0 on failure. */
uint64_t jit_lookup(JitEngine *engine, const char *name);

/* Execute a file via JIT: parse -> check -> codegen -> run main() */
int jit_run_file(const char *path);

/* Same as jit_run_file but with memcheck tracking enabled. Routes every
   alloc/free through ls_mc_* and prints a leak/double-free report at exit. */
int jit_run_file_memcheck(const char *path);

/* Same as jit_run_file but with function-level profiling instrumentation.
   Injects ls_prof_enter/leave at every function boundary and prints a
   sorted timing report at exit. */
int jit_run_file_profile(const char *path);

/* Same as jit_run_file but runs the full O2 optimization pipeline on each
   JIT module before execution. Enables inlining, loop vectorization, DCE, etc.
   Trade-off: larger modules (e.g. std.json) take ~1s extra to compile. */
int jit_run_file_optimize(const char *path);

/* Run the REPL (interactive incremental JIT) */
int jit_repl(void);

/* Compute a simple hash of an AST function node for change detection */
uint64_t jit_hash_fn(AstNode *fn_node);

/* Check whether a function needs recompilation based on AST hash */
bool jit_needs_recompile(JitEngine *engine, const char *name, uint64_t new_hash);

/* Update the function registry with a new hash */
void jit_update_registry(JitEngine *engine, const char *name, uint64_t hash);

#endif /* LS_JIT_H */
