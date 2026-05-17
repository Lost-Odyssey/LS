/* jit.c — LLJIT engine: incremental compilation, run, and REPL */
#include "jit.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"
#include "module.h"

#include <llvm-c/Core.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/Orc.h>
#include <llvm-c/OrcEE.h>
#include <llvm-c/Target.h>
#include <llvm-c/Error.h>

#include <stdio.h>
#include <string.h>

/* ---- Helpers ---- */

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "error: could not open file '%s'\n", path);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buf = (char *)malloc_safe((size_t)size + 1);
    size_t rd = fread(buf, 1, (size_t)size, file);
    buf[rd] = '\0';
    fclose(file);
    return buf;
}

/* Print and consume an LLVMErrorRef. Returns true if there was an error. */
static bool handle_error(LLVMErrorRef err) {
    if (err == LLVMErrorSuccess) return false;
    char *msg = LLVMGetErrorMessage(err);
    fprintf(stderr, "[jit error] %s\n", msg);
    LLVMDisposeErrorMessage(msg);
    return true;
}

/* ---- JIT Engine lifecycle ---- */

int jit_init(JitEngine *engine) {
    memset(engine, 0, sizeof(JitEngine));

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    /* Create LLJIT via builder */
    LLVMOrcLLJITBuilderRef builder = LLVMOrcCreateLLJITBuilder();
    LLVMErrorRef err = LLVMOrcCreateLLJIT(&engine->jit, builder);
    if (handle_error(err)) {
        return -1;
    }

    engine->main_dylib = LLVMOrcLLJITGetMainJITDylib(engine->jit);
    engine->ts_context = LLVMOrcCreateNewThreadSafeContext();

    /* Register host process symbols so JIT can call printf, puts, etc. */
    {
        LLVMOrcExecutionSessionRef es = LLVMOrcLLJITGetExecutionSession(engine->jit);
        LLVMOrcDefinitionGeneratorRef gen = NULL;
        char prefix = LLVMOrcLLJITGetGlobalPrefix(engine->jit);
        err = LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess(&gen, prefix, NULL, NULL);
        if (!handle_error(err) && gen) {
            LLVMOrcJITDylibAddGenerator(engine->main_dylib, gen);
        }
        (void)es;
    }

    /* Register memcheck runtime symbols explicitly. On Windows .exe symbols
       aren't found by GetProcAddress unless the linker emitted an export
       table; using AbsoluteSymbols sidesteps that and works on every platform. */
    {
        extern void *ls_mc_alloc(unsigned long long, const void *);
        extern void *ls_mc_realloc(void *, unsigned long long, const void *);
        extern void  ls_mc_free(void *, const void *);
        extern void  ls_mc_report(void);
        /* D.1 — call-stack tracking: codegen injects ls_mc_enter/ls_mc_leave
           at function entry/return. Must be reachable from JIT-compiled code. */
        extern void  ls_mc_enter(const char *, const char *, int);
        extern void  ls_mc_leave(void);
        extern void  ls_mc_ensure_report(void);

        LLVMOrcExecutionSessionRef es = LLVMOrcLLJITGetExecutionSession(engine->jit);
        LLVMOrcSymbolStringPoolRef sp = LLVMOrcExecutionSessionGetSymbolStringPool(es);
        (void)sp;

        LLVMOrcCSymbolMapPair pairs[7];
        pairs[0].Name = LLVMOrcLLJITMangleAndIntern(engine->jit, "ls_mc_alloc");
        pairs[0].Sym.Address = (LLVMOrcExecutorAddress)(uintptr_t)&ls_mc_alloc;
        pairs[0].Sym.Flags.GenericFlags = LLVMJITSymbolGenericFlagsExported;
        pairs[0].Sym.Flags.TargetFlags = 0;

        pairs[1].Name = LLVMOrcLLJITMangleAndIntern(engine->jit, "ls_mc_free");
        pairs[1].Sym.Address = (LLVMOrcExecutorAddress)(uintptr_t)&ls_mc_free;
        pairs[1].Sym.Flags.GenericFlags = LLVMJITSymbolGenericFlagsExported;
        pairs[1].Sym.Flags.TargetFlags = 0;

        pairs[2].Name = LLVMOrcLLJITMangleAndIntern(engine->jit, "ls_mc_report");
        pairs[2].Sym.Address = (LLVMOrcExecutorAddress)(uintptr_t)&ls_mc_report;
        pairs[2].Sym.Flags.GenericFlags = LLVMJITSymbolGenericFlagsExported;
        pairs[2].Sym.Flags.TargetFlags = 0;

        pairs[3].Name = LLVMOrcLLJITMangleAndIntern(engine->jit, "ls_mc_realloc");
        pairs[3].Sym.Address = (LLVMOrcExecutorAddress)(uintptr_t)&ls_mc_realloc;
        pairs[3].Sym.Flags.GenericFlags = LLVMJITSymbolGenericFlagsExported;
        pairs[3].Sym.Flags.TargetFlags = 0;

        pairs[4].Name = LLVMOrcLLJITMangleAndIntern(engine->jit, "ls_mc_enter");
        pairs[4].Sym.Address = (LLVMOrcExecutorAddress)(uintptr_t)&ls_mc_enter;
        pairs[4].Sym.Flags.GenericFlags = LLVMJITSymbolGenericFlagsExported;
        pairs[4].Sym.Flags.TargetFlags = 0;

        pairs[5].Name = LLVMOrcLLJITMangleAndIntern(engine->jit, "ls_mc_leave");
        pairs[5].Sym.Address = (LLVMOrcExecutorAddress)(uintptr_t)&ls_mc_leave;
        pairs[5].Sym.Flags.GenericFlags = LLVMJITSymbolGenericFlagsExported;
        pairs[5].Sym.Flags.TargetFlags = 0;

        pairs[6].Name = LLVMOrcLLJITMangleAndIntern(engine->jit, "ls_mc_ensure_report");
        pairs[6].Sym.Address = (LLVMOrcExecutorAddress)(uintptr_t)&ls_mc_ensure_report;
        pairs[6].Sym.Flags.GenericFlags = LLVMJITSymbolGenericFlagsExported;
        pairs[6].Sym.Flags.TargetFlags = 0;

        LLVMOrcMaterializationUnitRef mu = LLVMOrcAbsoluteSymbols(pairs, 7);
        LLVMErrorRef e2 = LLVMOrcJITDylibDefine(engine->main_dylib, mu);
        if (handle_error(e2)) {
            /* Non-fatal; --memcheck won't work but other JIT runs will. */
        }
    }

    engine->initialized = true;

    /* Inject a builtins module (defines print with body) so later modules
       can just declare print as external and resolve it from here. */
    {
        LLVMContextRef ctx = LLVMOrcThreadSafeContextGetContext(engine->ts_context);
        CodegenContext cg;
        memset(&cg, 0, sizeof(CodegenContext));
        cg.context = ctx;
        cg.module = LLVMModuleCreateWithNameInContext("__builtins", ctx);
        cg.builder = LLVMCreateBuilderInContext(ctx);
        cg.extern_builtins = false; /* define print WITH body */

        const char *dl = LLVMOrcLLJITGetDataLayoutStr(engine->jit);
        LLVMSetDataLayout(cg.module, dl);
        const char *triple = LLVMOrcLLJITGetTripleString(engine->jit);
        LLVMSetTarget(cg.module, triple);

        /* Manually call codegen_compile with a minimal empty program
           — we only need declare_builtins to run.
           Instead, just create a tiny valid module with the builtins. */
        /* We replicate the builtin declarations here since declare_builtins is static.
           A cleaner approach: create a trivial AST and compile it. */
        const char *builtin_src = "fn __builtins_init() -> int { return 0 }\n";
        AstNode *bast = parse(builtin_src, "<builtins>");
        if (bast) {
            /* checker is optional for this trivial code but needed by codegen */
            checker_check(bast, "<builtins>", NULL);
            codegen_compile(&cg, bast, NULL);
            ast_free(bast);
        }

        LLVMModuleRef bmod = cg.module;
        LLVMDisposeBuilder(cg.builder);
        free(cg.struct_types);

        /* Add builtins module to JIT */
        if (jit_add_module(engine, bmod) != 0) {
            fprintf(stderr, "[jit] warning: failed to add builtins module\n");
        }
    }

    return 0;
}

void jit_destroy(JitEngine *engine) {
    if (!engine->initialized) return;

    /* Free function registry */
    for (int i = 0; i < engine->fn_count; i++) {
        free(engine->fn_registry[i].name);
    }
    free(engine->fn_registry);

    if (engine->ts_context) {
        LLVMOrcDisposeThreadSafeContext(engine->ts_context);
    }

    if (engine->jit) {
        LLVMErrorRef err = LLVMOrcDisposeLLJIT(engine->jit);
        handle_error(err);
    }

    memset(engine, 0, sizeof(JitEngine));
}

/* ---- Module injection ---- */

int jit_add_module(JitEngine *engine, LLVMModuleRef module) {
    if (!engine->initialized) return -1;

    /* Set data layout and target triple from JIT */
    const char *dl = LLVMOrcLLJITGetDataLayoutStr(engine->jit);
    LLVMSetDataLayout(module, dl);
    const char *triple = LLVMOrcLLJITGetTripleString(engine->jit);
    LLVMSetTarget(module, triple);

    /* Wrap into ThreadSafeModule — transfers ownership of module */
    LLVMOrcThreadSafeModuleRef tsm = LLVMOrcCreateNewThreadSafeModule(
        module, engine->ts_context);

    LLVMErrorRef err = LLVMOrcLLJITAddLLVMIRModule(
        engine->jit, engine->main_dylib, tsm);

    if (handle_error(err)) {
        return -1;
    }
    return 0;
}

/* ---- Symbol lookup ---- */

uint64_t jit_lookup(JitEngine *engine, const char *name) {
    if (!engine->initialized) return 0;

    LLVMOrcExecutorAddress addr = 0;
    LLVMErrorRef err = LLVMOrcLLJITLookup(engine->jit, &addr, name);
    if (handle_error(err)) {
        return 0;
    }
    return (uint64_t)addr;
}

/* ---- AST hashing for incremental compilation ---- */

/* Simple FNV-1a hash helper */
static uint64_t fnv_hash(uint64_t h, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static uint64_t hash_ast(uint64_t h, AstNode *node);

static uint64_t hash_ast(uint64_t h, AstNode *node) {
    if (node == NULL) return h;

    /* Hash the node kind */
    h = fnv_hash(h, &node->kind, sizeof(node->kind));

    switch (node->kind) {
    case AST_INT_LIT:
        h = fnv_hash(h, &node->as.int_lit.value, sizeof(node->as.int_lit.value));
        break;
    case AST_FLOAT_LIT:
        h = fnv_hash(h, &node->as.float_lit.value, sizeof(node->as.float_lit.value));
        break;
    case AST_STRING_LIT:
        if (node->as.string_lit.value)
            h = fnv_hash(h, node->as.string_lit.value, (size_t)node->as.string_lit.length);
        break;
    case AST_BOOL_LIT:
        h = fnv_hash(h, &node->as.bool_lit.value, sizeof(node->as.bool_lit.value));
        break;
    case AST_NIL_LIT:
        break;
    case AST_IDENT:
        if (node->as.ident.name)
            h = fnv_hash(h, node->as.ident.name, strlen(node->as.ident.name));
        break;
    case AST_UNARY:
        h = fnv_hash(h, &node->as.unary.op, sizeof(node->as.unary.op));
        h = hash_ast(h, node->as.unary.operand);
        break;
    case AST_BINARY:
        h = fnv_hash(h, &node->as.binary.op, sizeof(node->as.binary.op));
        h = hash_ast(h, node->as.binary.left);
        h = hash_ast(h, node->as.binary.right);
        break;
    case AST_CALL:
        h = hash_ast(h, node->as.call.callee);
        for (int i = 0; i < node->as.call.arg_count; i++)
            h = hash_ast(h, node->as.call.args[i]);
        break;
    case AST_RETURN:
        h = hash_ast(h, node->as.return_stmt.value);
        break;
    case AST_IF:
        h = hash_ast(h, node->as.if_stmt.cond);
        h = hash_ast(h, node->as.if_stmt.then_block);
        h = hash_ast(h, node->as.if_stmt.else_block);
        break;
    case AST_WHILE:
        h = hash_ast(h, node->as.while_stmt.cond);
        h = hash_ast(h, node->as.while_stmt.body);
        break;
    case AST_BLOCK:
        for (int i = 0; i < node->as.block.stmt_count; i++)
            h = hash_ast(h, node->as.block.stmts[i]);
        break;
    case AST_EXPR_STMT:
        h = hash_ast(h, node->as.expr_stmt.expr);
        break;
    case AST_VAR_DECL:
        if (node->as.var_decl.name)
            h = fnv_hash(h, node->as.var_decl.name, strlen(node->as.var_decl.name));
        h = hash_ast(h, node->as.var_decl.init);
        break;
    case AST_ASSIGN:
        h = fnv_hash(h, &node->as.assign.op, sizeof(node->as.assign.op));
        h = hash_ast(h, node->as.assign.target);
        h = hash_ast(h, node->as.assign.value);
        break;
    case AST_FN_DECL:
        if (node->as.fn_decl.name)
            h = fnv_hash(h, node->as.fn_decl.name, strlen(node->as.fn_decl.name));
        h = hash_ast(h, node->as.fn_decl.body);
        break;
    case AST_MATCH:
        h = hash_ast(h, node->as.match.subject);
        for (int i = 0; i < node->as.match.arm_count; i++) {
            h = hash_ast(h, node->as.match.arms[i].pattern);
            h = hash_ast(h, node->as.match.arms[i].body);
        }
        break;
    case AST_FIELD:
        h = hash_ast(h, node->as.field_access.object);
        if (node->as.field_access.field)
            h = fnv_hash(h, node->as.field_access.field, strlen(node->as.field_access.field));
        break;
    default:
        /* For other nodes, just include the kind */
        break;
    }
    return h;
}

uint64_t jit_hash_fn(AstNode *fn_node) {
    uint64_t h = 0xcbf29ce484222325ULL; /* FNV offset basis */
    return hash_ast(h, fn_node);
}

/* ---- Incremental registry ---- */

bool jit_needs_recompile(JitEngine *engine, const char *name, uint64_t new_hash) {
    for (int i = 0; i < engine->fn_count; i++) {
        if (strcmp(engine->fn_registry[i].name, name) == 0) {
            return engine->fn_registry[i].hash != new_hash;
        }
    }
    return true; /* Not in registry → needs compilation */
}

void jit_update_registry(JitEngine *engine, const char *name, uint64_t hash) {
    for (int i = 0; i < engine->fn_count; i++) {
        if (strcmp(engine->fn_registry[i].name, name) == 0) {
            engine->fn_registry[i].hash = hash;
            return;
        }
    }
    /* New entry */
    if (engine->fn_count >= engine->fn_cap) {
        engine->fn_cap = GROW_CAPACITY(engine->fn_cap);
        engine->fn_registry = GROW_ARRAY(JitFnEntry, engine->fn_registry, engine->fn_cap);
    }
    size_t len = strlen(name);
    engine->fn_registry[engine->fn_count].name = (char *)malloc_safe(len + 1);
    memcpy(engine->fn_registry[engine->fn_count].name, name, len + 1);
    engine->fn_registry[engine->fn_count].hash = hash;
    engine->fn_count++;
}

/* ---- Build a JIT module from AST using codegen ---- */

/* Build an LLVM module from a type-checked AST, using the JIT's thread-safe context.
   Returns the module (ownership transferred to caller) or NULL on failure. */
static LLVMModuleRef build_jit_module(JitEngine *engine, AstNode *ast, const char *name,
                                      struct ModuleRegistry *registry) {
    LLVMContextRef ctx = LLVMOrcThreadSafeContextGetContext(engine->ts_context);

    /* Create a codegen context that uses the JIT's LLVM context */
    CodegenContext cg;
    memset(&cg, 0, sizeof(CodegenContext));
    cg.context = ctx;
    cg.module = LLVMModuleCreateWithNameInContext(name, ctx);
    cg.builder = LLVMCreateBuilderInContext(ctx);
    cg.extern_builtins = true; /* builtins already defined in __builtins module */
    cg.memcheck_enabled = engine->memcheck_enabled;

    /* Set target from JIT */
    const char *dl = LLVMOrcLLJITGetDataLayoutStr(engine->jit);
    LLVMSetDataLayout(cg.module, dl);
    const char *triple = LLVMOrcLLJITGetTripleString(engine->jit);
    LLVMSetTarget(cg.module, triple);

    /* Initialize scope — codegen_compile expects a base scope to exist */
    /* We don't call codegen_init because we share the JIT's LLVM context */

    /* Use codegen_compile to generate IR */
    if (codegen_compile(&cg, ast, registry) != 0) {
        LLVMDisposeBuilder(cg.builder);
        LLVMDisposeModule(cg.module);
        return NULL;
    }

    LLVMModuleRef module = cg.module;

    /* Clean up codegen (but DON'T dispose context or module — they belong to JIT) */
    LLVMDisposeBuilder(cg.builder);
    /* Don't free scope chain here — codegen_compile manages it */
    free(cg.struct_types);

    return module;
}

/* ---- jit_run_file ---- */

static int jit_run_file_impl(const char *path, bool memcheck);

int jit_run_file(const char *path) { return jit_run_file_impl(path, false); }
int jit_run_file_memcheck(const char *path) { return jit_run_file_impl(path, true); }

static int jit_run_file_impl(const char *path, bool memcheck) {
    char *source = read_file(path);
    if (source == NULL) return 1;

    /* Parse */
    AstNode *ast = parse(source, path);
    if (ast == NULL) {
        free(source);
        return 1;
    }

    /* Type check (with module support) */
    ModuleRegistry *reg = module_registry_new();
    if (!checker_check(ast, path, reg)) {
        module_registry_free(reg);
        ast_free(ast);
        free(source);
        return 1;
    }

    /* Initialize JIT */
    JitEngine engine;
    if (jit_init(&engine) != 0) {
        module_registry_free(reg);
        ast_free(ast);
        free(source);
        return 1;
    }
    engine.memcheck_enabled = memcheck;

    /* Log incremental info */
    if (ast->kind == AST_PROGRAM) {
        for (int i = 0; i < ast->as.program.decl_count; i++) {
            AstNode *decl = ast->as.program.decls[i];
            if (decl->kind == AST_FN_DECL) {
                uint64_t h = jit_hash_fn(decl);
                bool recompile = jit_needs_recompile(&engine, decl->as.fn_decl.name, h);
                if (recompile) {
                    fprintf(stderr, "[jit] compiling function '%s' (hash=%016llx)\n",
                            decl->as.fn_decl.name, (unsigned long long)h);
                }
                jit_update_registry(&engine, decl->as.fn_decl.name, h);
            }
        }
    }

    /* Build module */
    LLVMModuleRef module = build_jit_module(&engine, ast, path, reg);
    if (module == NULL) {
        jit_destroy(&engine);
        module_registry_free(reg);
        ast_free(ast);
        free(source);
        return 1;
    }

    /* Inspect main()'s return type before transferring the module to the JIT.
       We need this to call it with the correct ABI later. */
    bool main_returns_int = false;
    {
        LLVMValueRef main_fn_val = LLVMGetNamedFunction(module, "main");
        if (main_fn_val) {
            LLVMTypeRef fn_t   = LLVMGlobalGetValueType(main_fn_val);
            LLVMTypeRef ret_t  = LLVMGetReturnType(fn_t);
            LLVMTypeKind ret_k = LLVMGetTypeKind(ret_t);
            main_returns_int = (ret_k == LLVMIntegerTypeKind);
        }
    }

    /* Add module to JIT */
    if (jit_add_module(&engine, module) != 0) {
        jit_destroy(&engine);
        module_registry_free(reg);
        ast_free(ast);
        free(source);
        return 1;
    }

    /* Look up and execute main() */
    uint64_t main_addr = jit_lookup(&engine, "main");
    if (main_addr == 0) {
        fprintf(stderr, "error: 'main' function not found\n");
        jit_destroy(&engine);
        module_registry_free(reg);
        ast_free(ast);
        free(source);
        return 1;
    }

    /* Call main() with the correct ABI.
       - fn main()        → void return; always exit 0.
       - fn main() -> int → int return; propagate as exit code.
         If the user writes fn main() -> int but omits explicit return,
         codegen emits 'ret i32 0' automatically (LLVMConstNull fallback). */
    int result = 0;
    if (main_returns_int) {
        typedef int (*MainFnInt)(void);
        MainFnInt main_fn = (MainFnInt)(uintptr_t)main_addr;
        result = main_fn();
    } else {
        typedef void (*MainFnVoid)(void);
        MainFnVoid main_fn = (MainFnVoid)(uintptr_t)main_addr;
        main_fn();
    }

    /* Memcheck report MUST run before jit_destroy: the LsMcSite globals
       referenced by tracked allocations live in the JIT module memory and
       become invalid after teardown. atexit ordering would otherwise crash. */
    if (memcheck) {
        extern void ls_mc_report(void);
        ls_mc_report();
    }

    jit_destroy(&engine);
    module_registry_free(reg);
    ast_free(ast);
    free(source);
    return result;
}

/* ---- REPL ---- */

/* Read a complete multi-line block (matching braces) from stdin.
   Returns the full text in decl_buf, updating decl_len. */
static void read_multiline_block(char *decl_buf, size_t buf_size, size_t *decl_len) {
    int brace_count = 0;
    for (size_t i = 0; i < *decl_len; i++) {
        if (decl_buf[i] == '{') brace_count++;
        else if (decl_buf[i] == '}') brace_count--;
    }

    char line[4096];
    while (brace_count > 0) {
        printf("  ...> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) break;
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll - 1] == '\n' || line[ll - 1] == '\r'))
            line[--ll] = '\0';
        if (*decl_len + ll + 2 < buf_size) {
            decl_buf[(*decl_len)++] = '\n';
            memcpy(decl_buf + *decl_len, line, ll + 1);
            *decl_len += ll;
        }
        for (size_t i = 0; i < ll; i++) {
            if (line[i] == '{') brace_count++;
            else if (line[i] == '}') brace_count--;
        }
    }
}

/* Check if a line starts with a type keyword (for variable declaration detection) */
static bool starts_with_type(const char *line) {
    static const char *type_keywords[] = {
        "int ", "i8 ", "i16 ", "i32 ", "i64 ",
        "u8 ", "u16 ", "u32 ", "u64 ",
        "f32 ", "f64 ", "bool ", "string ", "void ", "object ",
        "*int ", "*i8 ", "*i16 ", "*i32 ", "*i64 ",
        "*u8 ", "*u16 ", "*u32 ", "*u64 ",
        "*f32 ", "*f64 ", "*bool ", "*string ", "*object ",
        NULL
    };
    for (int i = 0; type_keywords[i]; i++) {
        if (strncmp(line, type_keywords[i], strlen(type_keywords[i])) == 0)
            return true;
    }
    return false;
}

/* Append a string to a dynamically-growing buffer */
static void accum_append(char **buf, size_t *len, const char *text) {
    size_t tlen = strlen(text);
    size_t needed = *len + tlen + 2;
    *buf = (char *)realloc_safe(*buf, needed + 1);
    (*buf)[*len] = '\n';
    memcpy(*buf + *len + 1, text, tlen);
    *len = *len + 1 + tlen;
    (*buf)[*len] = '\0';
}

typedef enum {
    REPL_DECL,      /* fn, struct, impl — top-level, compiled as own module */
    REPL_VAR,       /* int x = ..., string s = ... — accumulated in wrapper body */
    REPL_EXPR,      /* expression/statement — wrapped in __repl_N() */
} ReplLineKind;

int jit_repl(void) {
    printf("LS REPL (type 'exit' or Ctrl+C to quit)\n");

    JitEngine engine;
    if (jit_init(&engine) != 0) {
        fprintf(stderr, "error: failed to initialize JIT\n");
        return 1;
    }

    char line[4096];
    int snippet_id = 0;

    /* Top-level declarations (fn, struct) — compiled as separate JIT modules */
    char *accum_decls = (char *)malloc_safe(1);
    accum_decls[0] = '\0';
    size_t accum_decls_len = 0;

    /* Variable declarations — re-included in every __repl_N function body */
    char *accum_vars = (char *)malloc_safe(1);
    accum_vars[0] = '\0';
    size_t accum_vars_len = 0;

    while (1) {
        printf("ls> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        /* Trim trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0) continue;
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;

        /* Classify the input line */
        ReplLineKind kind;
        if (strncmp(line, "fn ", 3) == 0 ||
            strncmp(line, "struct ", 7) == 0 ||
            strncmp(line, "impl ", 5) == 0 ||
            strncmp(line, "extern ", 7) == 0) {
            kind = REPL_DECL;
        } else if (starts_with_type(line)) {
            kind = REPL_VAR;
        } else {
            kind = REPL_EXPR;
        }

        /* Step 1: Read complete input (handle multi-line blocks for decls) */
        char new_input[4096];
        snprintf(new_input, sizeof(new_input), "%s", line);
        size_t input_len = strlen(new_input);
        if (kind == REPL_DECL) {
            read_multiline_block(new_input, sizeof(new_input), &input_len);
        }

        /* Step 2: Build the source to parse + typecheck + codegen.
           - All accumulated decls (fn/struct) go at top level
           - A wrapper __repl_N function contains: accum_vars + new line
           - For REPL_DECL: the new decl itself is top-level, and we add a
             dummy wrapper just to validate but won't execute it
           - For REPL_VAR / REPL_EXPR: the wrapper contains all vars + new code */
        char new_code[4096];
        char source_buf[32768];

        if (kind == REPL_DECL) {
            /* new_code is the declaration itself */
            snprintf(new_code, sizeof(new_code), "%s", new_input);
            snprintf(source_buf, sizeof(source_buf),
                     "%s\n%s\n", accum_decls, new_code);
        } else if (kind == REPL_VAR) {
            /* Wrap all vars + new var in __repl_N, then return 0 */
            snprintf(new_code, sizeof(new_code), "%s", new_input);
            snprintf(source_buf, sizeof(source_buf),
                     "%s\n"
                     "fn __repl_%d() -> int {\n"
                     "%s\n"
                     "  %s\n"
                     "  return 0\n"
                     "}\n",
                     accum_decls, snippet_id,
                     (accum_vars_len > 0 ? accum_vars : ""),
                     new_code);
        } else {
            /* REPL_EXPR: wrap all vars + expression in __repl_N */
            snprintf(new_code, sizeof(new_code), "%s", new_input);
            snprintf(source_buf, sizeof(source_buf),
                     "%s\n"
                     "fn __repl_%d() -> int {\n"
                     "%s\n"
                     "  %s\n"
                     "  return 0\n"
                     "}\n",
                     accum_decls, snippet_id,
                     (accum_vars_len > 0 ? accum_vars : ""),
                     new_code);
        }

        /* Parse */
        AstNode *ast = parse(source_buf, "<repl>");
        if (ast == NULL) {
            fprintf(stderr, "  (parse error)\n");
            continue;
        }

        /* Type check */
        if (!checker_check(ast, "<repl>", NULL)) {
            ast_free(ast);
            fprintf(stderr, "  (type error)\n");
            continue;
        }

        /* Step 3: Codegen full AST, then strip old function bodies */

        /* Count old declarations to know which functions are new */
        int old_decl_count = 0;
        if (accum_decls_len > 0) {
            AstNode *old_ast = parse(accum_decls, "<repl>");
            if (old_ast && old_ast->kind == AST_PROGRAM) {
                old_decl_count = old_ast->as.program.decl_count;
            }
            if (old_ast) ast_free(old_ast);
        }

        /* Gather new function names */
        int total_decls = ast->as.program.decl_count;
        const char *new_fn_names[64];
        int new_fn_count = 0;
        for (int i = old_decl_count; i < total_decls && new_fn_count < 64; i++) {
            AstNode *d = ast->as.program.decls[i];
            if (d->kind == AST_FN_DECL) {
                new_fn_names[new_fn_count++] = d->as.fn_decl.name;
            }
        }

        char mod_name[64];
        snprintf(mod_name, sizeof(mod_name), "repl_%d", snippet_id);
        LLVMModuleRef module = build_jit_module(&engine, ast, mod_name, NULL);

        /* Strip bodies of old functions to avoid duplicate definitions */
        if (module) {
            LLVMValueRef fn = LLVMGetFirstFunction(module);
            while (fn) {
                const char *fname = LLVMGetValueName(fn);
                bool is_new = false;
                for (int i = 0; i < new_fn_count; i++) {
                    if (strcmp(new_fn_names[i], fname) == 0) {
                        is_new = true;
                        break;
                    }
                }
                if (!is_new && !LLVMIsDeclaration(fn)) {
                    while (LLVMGetFirstBasicBlock(fn)) {
                        LLVMDeleteBasicBlock(LLVMGetFirstBasicBlock(fn));
                    }
                }
                fn = LLVMGetNextFunction(fn);
            }
        }

        if (module == NULL) {
            ast_free(ast);
            fprintf(stderr, "  (codegen error)\n");
            continue;
        }

        /* Add to JIT */
        if (jit_add_module(&engine, module) != 0) {
            ast_free(ast);
            fprintf(stderr, "  (jit error)\n");
            continue;
        }

        /* Step 4: Execute or acknowledge */
        if (kind == REPL_DECL) {
            printf("  (defined)\n");
        } else {
            /* Execute __repl_N for var decls and expressions */
            char fn_name[64];
            snprintf(fn_name, sizeof(fn_name), "__repl_%d", snippet_id);
            uint64_t addr = jit_lookup(&engine, fn_name);
            if (addr != 0) {
                typedef int (*ReplFn)(void);
                ReplFn fn = (ReplFn)(uintptr_t)addr;
                int result = fn();
                if (result != 0) {
                    printf("=> %d\n", result);
                }
            }
            if (kind == REPL_VAR) {
                printf("  (ok)\n");
            }
        }

        /* Step 5: Accumulate on success */
        if (kind == REPL_DECL) {
            accum_append(&accum_decls, &accum_decls_len, new_code);
        } else if (kind == REPL_VAR) {
            /* Accumulate as "  type name = expr" for inclusion in future wrapper bodies */
            char var_line[4096];
            snprintf(var_line, sizeof(var_line), "  %s", new_code);
            accum_append(&accum_vars, &accum_vars_len, var_line);
        }

        ast_free(ast);
        snippet_id++;
    }

    free(accum_decls);
    free(accum_vars);
    jit_destroy(&engine);
    return 0;
}
