/* jit.h — LLJIT-based JIT engine with incremental compilation */
#ifndef LS_JIT_H
#define LS_JIT_H

#include "ast.h"
#include "types.h"
#include "common.h"
#include "optpipe.h"

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
    bool jit_optimize;      /* run the IR pass pipeline on each module before JIT */
    LsOptConfig opt;        /* level + CPU for the pre-JIT pass pipeline (native) */
} JitEngine;

/* Initialize the JIT engine */
int jit_init(JitEngine *engine);

/* Destroy the JIT engine and free all resources */
void jit_destroy(JitEngine *engine);

/* Compile and add an LLVM module to the JIT. Transfers ownership of module. */
int jit_add_module(JitEngine *engine, LLVMModuleRef module);

/* Look up a symbol by name and return its address. Returns 0 on failure. */
uint64_t jit_lookup(JitEngine *engine, const char *name);

/* Execute a file via JIT: parse -> check -> codegen -> run main().
   Sole entry point for `lls run`; the three knobs are orthogonal and any
   combination is valid, which is why they are parameters rather than the
   family of fixed-combination wrappers this replaced (those hard-coded
   LS_OPT_O0 for the memcheck and profile shapes, so `-O2`/`LS_OPT` was
   silently dropped whenever either was requested -- see test_ls_opt_env).

     memcheck  route every alloc/free through ls_mc_* and print a
               leak/double-free report at exit
     profile   inject ls_prof_enter/leave at every function boundary and
               print a sorted timing report at exit
     level     run the IR optimization pipeline at this level (native CPU)
               before execution; LS_OPT_O0 means no passes at all

   Callers resolve `level` themselves (CLI -O, then ls_opt_env_level, then
   O0); this function does not consult the environment for it. */
int jit_run_file_ex(const char *path, bool memcheck, bool profile, LsOptLevel level);

/* Plain unoptimized run without instrumentation == jit_run_file_ex(p,0,0,O0). */
int jit_run_file(const char *path);

/* Run the REPL (interactive incremental JIT) */
int jit_repl(void);

/* Compute a simple hash of an AST function node for change detection */
uint64_t jit_hash_fn(AstNode *fn_node);

/* Check whether a function needs recompilation based on AST hash */
bool jit_needs_recompile(JitEngine *engine, const char *name, uint64_t new_hash);

/* Update the function registry with a new hash */
void jit_update_registry(JitEngine *engine, const char *name, uint64_t hash);

#endif /* LS_JIT_H */
