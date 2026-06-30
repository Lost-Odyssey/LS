/* jit.c — LLJIT engine: incremental compilation, run, and REPL */
#include "jit.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"
#include "module.h"
#include "repl_edit.h"

#include <llvm-c/Core.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/Orc.h>
#include <llvm-c/OrcEE.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Error.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include <stdio.h>
#include <string.h>

/* ls_os_perf_now and all ls_os_* symbols are defined in runtime/os_win32.c
   or runtime/os_posix.c, which are compiled into ls.exe via CMakeLists.txt.
   Forward-declare here so jit_init can register them as AbsoluteSymbols. */
extern long long ls_os_perf_now(void);

/* float_fixed helpers defined in runtime/builtins.c */
extern void  __ls_float_fixed_exec(double, int);
extern void *__ls_float_fixed_ptr(void);

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

    /* Register all ls.exe-internal symbols explicitly.  On Windows, .exe
       symbols are not discoverable via GetProcAddress unless exported; using
       AbsoluteSymbols sidesteps that limitation on every platform.
       Covers: memcheck, profiler, perf, and all ls_os_* OS-backend symbols
       needed by std.os / std.time / std.env / std.proc / std.io / std.fs. */
    {
        /* memcheck */
        extern void *ls_mc_alloc(unsigned long long, const void *);
        extern void *ls_mc_realloc(void *, unsigned long long, const void *);
        extern void  ls_mc_free(void *, const void *);
        extern void  ls_mc_report(void);
        extern void  ls_mc_enter(const char *, const char *, int);
        extern void  ls_mc_leave(void);
        extern void  ls_mc_ensure_report(void);
        /* profiler */
        extern void ls_prof_enter(const char *, const char *, int);
        extern void ls_prof_leave(void);
        extern void ls_prof_report(void);
        /* os backend — process execution */
        extern void        ls_os_exec_run(const char *);
        extern void       *ls_os_exec_take_stdout(void);
        extern void       *ls_os_exec_stdout_ptr(void);
        extern void       *ls_os_exec_stderr_ptr(void);
        extern long long   ls_os_exec_stdout_len(void);
        extern void       *ls_os_exec_take_stderr(void);
        extern long long   ls_os_exec_stderr_len(void);
        extern int         ls_os_exec_get_code(void);
        extern int         ls_os_exec_get_ok(void);
        /* os backend — popen/pread/pclose */
        extern void       *ls_os_popen(const char *);
        extern long long   ls_os_pread(void *, void *, long long);
        extern int         ls_os_pclose(void *);
        extern int         ls_os_pid(void);
        extern int         ls_os_wait_exit_code(int);
        /* os backend — file positioning */
        extern int         ls_os_fseek64(void *, long long, int);
        extern long long   ls_os_ftell64(void *);
        extern int         ls_os_unlink(const char *);
        /* os backend — environment variables */
        extern const char *ls_os_getenv(const char *);
        extern int         ls_os_setenv(const char *, const char *);
        extern int         ls_os_unsetenv(const char *);
        extern void        ls_os_env_prepare(void);
        extern int         ls_os_env_count(void);
        extern const char *ls_os_env_entry(int);
        /* os backend — directory listing */
        extern void        ls_os_listdir_prepare(const char *);
        extern int         ls_os_listdir_count(void);
        extern const char *ls_os_listdir_entry(int);
        /* os backend — filesystem / path operations */
        extern const char *ls_os_last_error(void);
        extern int         ls_os_path_exists(const char *);
        extern int         ls_os_path_is_dir(const char *);
        extern int         ls_os_path_is_file(const char *);
        extern int         ls_os_mkdir(const char *);
        extern int         ls_os_mkdir_all(const char *);
        extern int         ls_os_rmdir(const char *);
        extern int         ls_os_rename_path(const char *, const char *);
        extern const char *ls_os_getcwd(void);
        extern int         ls_os_chdir(const char *);
        /* os backend — perf */
        extern long long   ls_os_perf_rdtsc(void);
        extern long long   ls_os_perf_rdtscp(void);
        /* ls_os_perf_now already declared above (extern at file top) */
        /* os backend — calendar / wall-clock time (std.time backend) */
        extern long long   ls_os_time_now_unix_ns(void);
        extern long long   ls_os_time_now_unix_ms(void);
        extern void        ls_os_time_from_unix_local(long long);
        extern void        ls_os_time_from_unix_utc(long long);
        extern int         ls_os_time_get_year(void);
        extern int         ls_os_time_get_month(void);
        extern int         ls_os_time_get_day(void);
        extern int         ls_os_time_get_hour(void);
        extern int         ls_os_time_get_minute(void);
        extern int         ls_os_time_get_second(void);
        extern int         ls_os_time_get_weekday(void);
        extern int         ls_os_time_get_yday(void);
        extern int         ls_os_time_get_utcoff(void);
        extern long long   ls_os_time_to_unix(int, int, int, int, int, int, int);
        extern const char *ls_os_time_format(int, int, int, int, int, int, int, int, const char *);
        extern int         ls_os_time_parse(const char *, const char *);
        extern void        ls_os_sleep_ms(long long);
        extern void        ls_os_sleep_us(long long);
        /* builtins — process args + readline */
        extern void        __ls_set_args(int, char **);
        extern int         __ls_get_argc(void);
        extern void       *__ls_get_argv(int);
        extern void        __ls_proc_exit(int);
        extern void        __ls_readline_exec(void);
        extern int         __ls_readline_ok(void);
        extern long long   __ls_readline_len(void);
        extern void       *__ls_readline_take(void);
        extern void       *__ls_readline_ptr(void);
        /* strconv float helpers */
        extern void        __ls_float_fixed_exec(double, int);
        extern void       *__ls_float_fixed_ptr(void);
        /* f-string bounded formatter */
        extern int         __ls_fstr_format(char *, size_t, const char *, ...);
        /* string bulk-scan helpers (for parsers) */
        extern int         __ls_str_skip_ws(const char *, int, int);
        extern int         __ls_str_scan_plain(const char *, int, int);
        extern int         __ls_str_scan_digits(const char *, int, int);
        extern int         __ls_str_find(const char *, int, const char *, int, int);
        extern void        __ls_bytecopy(void *, int, const void *, int, int);
        extern void       *__ls_ptr_at(void *, long long);
        extern void       *__ls_stdout(void);
        extern void       *__ls_stderr(void);
        extern void       *__ls_sink_stream(void);
        extern void        __ls_sink_set(void *, int);
        extern int         __ls_printf(const char *, ...);
        extern unsigned long long __ls_fxhash_bytes(const char *, int);
        /* byte-buffer integer loads (std.bytes — V2 bit-pattern parsing) */
        extern unsigned long long __ls_load_u8(const unsigned char *, long long);
        extern unsigned long long __ls_load_be_u16(const unsigned char *, long long);
        extern unsigned long long __ls_load_be_u32(const unsigned char *, long long);
        extern unsigned long long __ls_load_be_u64(const unsigned char *, long long);
        extern unsigned long long __ls_load_le_u16(const unsigned char *, long long);
        extern unsigned long long __ls_load_le_u32(const unsigned char *, long long);
        extern unsigned long long __ls_load_le_u64(const unsigned char *, long long);
        /* threads — generic Task(T): per-T thunk + result box (std.task) */
        extern void       *ls_thread_spawn(void *, void *, void *, void *);
        extern void        ls_thread_join(void *);
        /* mutexes + spin hint (std.sync) */
        extern void       *ls_mutex_init(void);
        extern int         ls_mutex_lock(void *);
        extern int         ls_mutex_trylock(void *);
        extern int         ls_mutex_unlock(void *);
        extern void        ls_mutex_destroy(void *);
        /* reader-writer locks (std.sync RwLock) */
        extern void       *ls_rwlock_init(void);
        extern int         ls_rwlock_rdlock(void *);
        extern int         ls_rwlock_wrlock(void *);
        extern int         ls_rwlock_rdunlock(void *);
        extern int         ls_rwlock_wrunlock(void *);
        extern void        ls_rwlock_destroy(void *);
        /* condition variables (std.chan blocking Chan) */
        extern void       *ls_cond_init(void);
        extern void        ls_cond_wait(void *, void *);
        extern void        ls_cond_signal(void *);
        extern void        ls_cond_broadcast(void *);
        extern void        ls_cond_destroy(void *);
        extern void        ls_cpu_relax(void);
        extern void        ls_cpu_yield(void);
        extern int         __ls_cpu_count(void);
        extern int         __ls_cache_kb(int);
        extern int         __ls_cpu_has_avx512(void);
        /* regex engine (runtime/ls_regex.c) — used by std.regex via std.c FFI */
        extern int         __ls_regex_compile(const char *, int);
        extern void        __ls_regex_free(int);
        extern const char *__ls_regex_last_error(void);
        extern int         __ls_regex_exec(int, const char *, int, int);
        extern int         __ls_regex_cap_start(int);
        extern int         __ls_regex_cap_len(int);
        extern int         __ls_regex_group_count(int);
        extern int         __ls_regex_named_count(int);
        extern const char *__ls_regex_named_name(int, int);
        extern int         __ls_regex_named_index(int, int);

        LLVMOrcExecutionSessionRef es = LLVMOrcLLJITGetExecutionSession(engine->jit);
        LLVMOrcSymbolStringPoolRef sp = LLVMOrcExecutionSessionGetSymbolStringPool(es);
        (void)sp;

/* helper macro — one line per symbol */
#define REG(i, sym) do { \
    pairs[i].Name = LLVMOrcLLJITMangleAndIntern(engine->jit, #sym); \
    pairs[i].Sym.Address = (LLVMOrcExecutorAddress)(uintptr_t)&sym; \
    pairs[i].Sym.Flags.GenericFlags = LLVMJITSymbolGenericFlagsExported; \
    pairs[i].Sym.Flags.TargetFlags = 0; \
} while(0)

        LLVMOrcCSymbolMapPair pairs[129];
        /* 0-5: memcheck */
        REG( 0, ls_mc_alloc);
        REG( 1, ls_mc_free);
        REG( 2, ls_mc_report);
        REG( 3, ls_mc_realloc);
        REG( 4, ls_mc_enter);
        REG( 5, ls_mc_leave);
        /* 6-8: profiler */
        REG( 6, ls_prof_enter);
        REG( 7, ls_prof_leave);
        REG( 8, ls_prof_report);
        /* 9-11: perf */
        REG( 9, ls_os_perf_now);
        REG(10, ls_os_perf_rdtsc);
        REG(11, ls_os_perf_rdtscp);
        /* 12-18: process execution */
        REG(12, ls_os_exec_run);
        REG(13, ls_os_exec_take_stdout);
        REG(14, ls_os_exec_stdout_len);
        REG(15, ls_os_exec_take_stderr);
        REG(16, ls_os_exec_stderr_len);
        REG(17, ls_os_exec_get_code);
        REG(18, ls_os_exec_get_ok);
        /* 19-23: popen/pid */
        REG(19, ls_os_popen);
        REG(20, ls_os_pread);
        REG(21, ls_os_pclose);
        REG(22, ls_os_pid);
        REG(23, ls_os_wait_exit_code);
        /* 24-26: file positioning */
        REG(24, ls_os_fseek64);
        REG(25, ls_os_ftell64);
        REG(26, ls_os_unlink);
        /* 27-32: environment */
        REG(27, ls_os_getenv);
        REG(28, ls_os_setenv);
        REG(29, ls_os_unsetenv);
        REG(30, ls_os_env_prepare);
        REG(31, ls_os_env_count);
        REG(32, ls_os_env_entry);
        /* 33-35: directory listing */
        REG(33, ls_os_listdir_prepare);
        REG(34, ls_os_listdir_count);
        REG(35, ls_os_listdir_entry);
        /* 36-45: filesystem / path */
        REG(36, ls_os_last_error);
        REG(37, ls_os_path_exists);
        REG(38, ls_os_path_is_dir);
        REG(39, ls_os_path_is_file);
        REG(40, ls_os_mkdir);
        REG(41, ls_os_mkdir_all);
        REG(42, ls_os_rmdir);
        REG(43, ls_os_rename_path);
        REG(44, ls_os_getcwd);
        REG(45, ls_os_chdir);
        /* 46-63: calendar time + sleep */
        REG(46, ls_os_time_now_unix_ns);
        REG(47, ls_os_time_now_unix_ms);
        REG(48, ls_os_time_from_unix_local);
        REG(49, ls_os_time_from_unix_utc);
        REG(50, ls_os_time_get_year);
        REG(51, ls_os_time_get_month);
        REG(52, ls_os_time_get_day);
        REG(53, ls_os_time_get_hour);
        REG(54, ls_os_time_get_minute);
        REG(55, ls_os_time_get_second);
        REG(56, ls_os_time_get_weekday);
        REG(57, ls_os_time_get_yday);
        REG(58, ls_os_time_get_utcoff);
        REG(59, ls_os_time_to_unix);
        REG(60, ls_os_time_format);
        REG(61, ls_os_time_parse);
        REG(62, ls_os_sleep_ms);
        REG(63, ls_os_sleep_us);
        REG(64, __ls_get_argc);
        REG(65, __ls_get_argv);
        REG(66, __ls_proc_exit);
        REG(67, __ls_readline_exec);
        REG(68, __ls_readline_ok);
        REG(69, __ls_readline_len);
        REG(70, __ls_readline_take);
        /* 71-72: strconv float helpers */
        REG(71, __ls_float_fixed_exec);
        REG(72, __ls_float_fixed_ptr);
        REG(73, __ls_fstr_format);
        REG(74, __ls_str_skip_ws);
        REG(75, __ls_str_scan_plain);
        REG(76, __ls_str_scan_digits);
        /* 77-86: regex engine */
        REG(77, __ls_regex_compile);
        REG(78, __ls_regex_free);
        REG(79, __ls_regex_last_error);
        REG(80, __ls_regex_exec);
        REG(81, __ls_regex_cap_start);
        REG(82, __ls_regex_cap_len);
        REG(83, __ls_regex_group_count);
        REG(84, __ls_regex_named_count);
        REG(85, __ls_regex_named_name);
        REG(86, __ls_regex_named_index);
        REG(87, __ls_readline_ptr);
        REG(88, ls_os_exec_stdout_ptr);
        REG(89, ls_os_exec_stderr_ptr);
        REG(90, __ls_str_find);
        REG(91, __ls_bytecopy);
        REG(92, __ls_fxhash_bytes);
        REG(93, ls_thread_spawn);
        REG(94, ls_thread_join);
        REG(95, ls_mutex_init);
        REG(96, ls_mutex_lock);
        REG(97, ls_mutex_trylock);
        REG(98, ls_mutex_unlock);
        REG(99, ls_mutex_destroy);
        REG(100, ls_cpu_relax);
        REG(101, ls_cpu_yield);
        REG(102, __ls_cpu_count);
        /* 103-109: byte-buffer integer loads (std.bytes) */
        REG(103, __ls_load_u8);
        REG(104, __ls_load_be_u16);
        REG(105, __ls_load_be_u32);
        REG(106, __ls_load_be_u64);
        REG(107, __ls_load_le_u16);
        REG(108, __ls_load_le_u32);
        REG(109, __ls_load_le_u64);
        /* 110-115: reader-writer locks (std.sync RwLock) */
        REG(110, ls_rwlock_init);
        REG(111, ls_rwlock_rdlock);
        REG(112, ls_rwlock_wrlock);
        REG(113, ls_rwlock_rdunlock);
        REG(114, ls_rwlock_wrunlock);
        REG(115, ls_rwlock_destroy);
        REG(116, ls_cond_init);
        REG(117, ls_cond_wait);
        REG(118, ls_cond_signal);
        REG(119, ls_cond_broadcast);
        REG(120, ls_cond_destroy);
        REG(121, __ls_ptr_at);
        /* 122-126: std.core.sink — stream handles + print() redirect */
        REG(122, __ls_stdout);
        REG(123, __ls_stderr);
        REG(124, __ls_sink_stream);
        REG(125, __ls_sink_set);
        REG(126, __ls_printf);
        REG(127, __ls_cache_kb);
        REG(128, __ls_cpu_has_avx512);
#undef REG

        LLVMOrcMaterializationUnitRef mu = LLVMOrcAbsoluteSymbols(pairs, 129);
        LLVMErrorRef e2 = LLVMOrcJITDylibDefine(engine->main_dylib, mu);
        if (handle_error(e2)) {
            /* Non-fatal; stdlib JIT calls won't resolve but other runs will. */
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
        const char *builtin_src = "def __builtins_init() -> int { return 0 }\n";
        AstNode *bast = parse(builtin_src, "<builtins>");
        if (bast) {
            /* checker is optional for this trivial code but needed by codegen */
            checker_check(bast, "<builtins>", NULL, NULL);
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
        /* Overloaded binaries hold their operands under binary.lowered (left/right
           become NULL after lowering); hash that so the key reflects the operands. */
        if (node->as.binary.lowered)
            h = hash_ast(h, node->as.binary.lowered);
        else
        {
            h = hash_ast(h, node->as.binary.left);
            h = hash_ast(h, node->as.binary.right);
        }
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
                                      struct ModuleRegistry *registry,
                                      CheckerGenericMethods *gm) {
    LLVMContextRef ctx = LLVMOrcThreadSafeContextGetContext(engine->ts_context);

    /* Create a codegen context that uses the JIT's LLVM context */
    CodegenContext cg;
    memset(&cg, 0, sizeof(CodegenContext));
    cg.context = ctx;
    cg.module = LLVMModuleCreateWithNameInContext(name, ctx);
    cg.builder = LLVMCreateBuilderInContext(ctx);
    cg.extern_builtins = true; /* builtins already defined in __builtins module */
    cg.memcheck_enabled = engine->memcheck_enabled;
    cg.profile_enabled = engine->profile_enabled;

    /* Set target from JIT */
    const char *dl = LLVMOrcLLJITGetDataLayoutStr(engine->jit);
    LLVMSetDataLayout(cg.module, dl);
    const char *triple = LLVMOrcLLJITGetTripleString(engine->jit);
    LLVMSetTarget(cg.module, triple);

    /* Initialize scope — codegen_compile expects a base scope to exist */
    /* We don't call codegen_init because we share the JIT's LLVM context */

    /* G1.5: transfer pending generic methods to codegen.
       The struct layouts are identical but C sees them as distinct anonymous types,
       so we copy element-by-element to avoid a pointer-type mismatch warning. */
    if (gm && gm->count > 0) {
        cg.pending_gm_count = gm->count;
        size_t sz = (size_t)gm->count * sizeof(cg.pending_generic_methods[0]);
        cg.pending_generic_methods = malloc(sz);
        for (int gi = 0; gi < gm->count; gi++) {
            cg.pending_generic_methods[gi].cloned_fn    = gm->methods[gi].cloned_fn;
            cg.pending_generic_methods[gi].mangled_name = gm->methods[gi].mangled_name;
            cg.pending_generic_methods[gi].struct_type  = gm->methods[gi].struct_type;
        }
        free(gm->methods);
        gm->methods = NULL;
        gm->count = 0;
    }

    /* Use codegen_compile to generate IR */
    if (codegen_compile(&cg, ast, registry) != 0) {
        LLVMDisposeBuilder(cg.builder);
        LLVMDisposeModule(cg.module);
        return NULL;
    }

    LLVMModuleRef module = cg.module;

    /* Verify the LLVM module for correctness (debug aid) */
    {
        char *err_msg = NULL;
        if (LLVMVerifyModule(module, LLVMReturnStatusAction, &err_msg)) {
            fprintf(stderr, "[jit] LLVM module verification FAILED:\n%s\n", err_msg);
        }
        if (err_msg) LLVMDisposeMessage(err_msg);
    }

    /* Optionally run O2 optimization pipeline on the module before handing it
       to LLJIT. Enables inlining, loop vectorization, DCE, etc. — the same
       passes AOT gets. Controlled by engine->jit_optimize (set from --optimize
       flag or LS_JIT_OPT=1 env var). Off by default: O2 adds ~1s compile time
       for large modules (e.g. std.json). */
    if (engine->jit_optimize) {
        char *target_triple = LLVMGetDefaultTargetTriple();
        LLVMTargetMachineRef tm = ls_opt_create_target_machine(target_triple, &engine->opt);
        if (tm) {
            ls_opt_run_passes(module, tm, &engine->opt);
            LLVMDisposeTargetMachine(tm);
        }
        LLVMDisposeMessage(target_triple);
    }

    /* Clean up codegen (but DON'T dispose context or module — they belong to JIT) */
    LLVMDisposeBuilder(cg.builder);
    /* Don't free scope chain here — codegen_compile manages it */
    free(cg.struct_types);

    return module;
}

/* ---- jit_run_file ---- */

static int jit_run_file_impl(const char *path, bool memcheck, bool profile, LsOptLevel opt_level);

int jit_run_file(const char *path) { return jit_run_file_impl(path, false, false, LS_OPT_O0); }
int jit_run_file_memcheck(const char *path) { return jit_run_file_impl(path, true, false, LS_OPT_O0); }
int jit_run_file_profile(const char *path) { return jit_run_file_impl(path, false, true, LS_OPT_O0); }
int jit_run_file_optimize(const char *path) { return jit_run_file_impl(path, false, false, LS_OPT_O2); }
int jit_run_file_optlevel(const char *path, LsOptLevel level) { return jit_run_file_impl(path, false, false, level); }

static int jit_run_file_impl(const char *path, bool memcheck, bool profile, LsOptLevel opt_level) {
    char *source = read_file(path);
    if (source == NULL) return 1;

    /* Parse */
    AstNode *ast = parse(source, path);
    if (ast == NULL) {
        free(source);
        return 1;
    }

    /* Type check (with module support) */
    ast_inject_std_str_import(ast);
    ModuleRegistry *reg = module_registry_new();
    CheckerGenericMethods gm = {0};
    if (!checker_check(ast, path, reg, &gm)) {
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
    engine.profile_enabled = profile;
    engine.jit_optimize = (opt_level != LS_OPT_O0);
    engine.opt = ls_opt_default_jit();   /* native CPU + verify_each from env */
    engine.opt.level = opt_level;        /* CLI level wins */

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
    LLVMModuleRef module = build_jit_module(&engine, ast, path, reg, &gm);
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
    if (profile) {
        extern void ls_prof_report(void);
        ls_prof_report();
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

/* Has a function with name `fname` already been emitted (as a definition) into
   the JIT in a previous snippet? */
static bool emitted_contains(char **emitted, int count, const char *fname) {
    for (int i = 0; i < count; i++)
        if (strcmp(emitted[i], fname) == 0) return true;
    return false;
}

/* If `input` is a `def NAME(...)` function declaration, copy NAME into `out`
   and return true. Used to detect (and politely refuse) REPL redefinition of a
   function already live in the JIT — re-emitting it would either silently keep
   the old body (same signature) or emit a type-mismatched function and fail
   codegen verification (different signature), because each snippet is its own
   JIT module and the existing symbol can't be replaced without ORC resource
   trackers (a larger REPL refactor). */
static bool repl_def_fn_name(const char *input, char *out, size_t cap) {
    Scanner s;
    scanner_init(&s, input);
    Token t1 = scanner_next(&s);
    if (t1.type != TOKEN_FN) return false;
    Token t2 = scanner_next(&s);
    if (t2.type != TOKEN_IDENTIFIER) return false;
    size_t n = (size_t)t2.length;
    if (n >= cap) n = cap - 1;
    memcpy(out, t2.start, n);
    out[n] = '\0';
    return true;
}

/* REPL meta-command (lines starting with ':'). Supports
   :inspect/:methods/:fields <Type> (static reflection, Stage 1.5) and :help.
   Builds a program from the accumulated imports + decls + globals (no wrapper,
   no new code) so every type defined or imported so far is visible, then runs
   checker_inspect_ex. Returns true if the line was a meta-command (handled). */
static bool repl_meta_command(const char *line, const char *imports,
                              const char *decls, const char *globals) {
    if (line == NULL || line[0] != ':') return false;
    const char *p = line + 1;
    while (*p == ' ' || *p == '\t') p++;
    char cmd[32]; int ci = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && ci < 31)
        cmd[ci++] = *p++;
    cmd[ci] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    char tname[128]; int ti = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && ti < 127)
        tname[ti++] = *p++;
    tname[ti] = '\0';

    bool want_fields, want_methods;
    if (strcmp(cmd, "methods") == 0 || strcmp(cmd, "m") == 0) {
        want_fields = false; want_methods = true;
    } else if (strcmp(cmd, "fields") == 0 || strcmp(cmd, "f") == 0) {
        want_fields = true; want_methods = false;
    } else if (strcmp(cmd, "inspect") == 0 || strcmp(cmd, "i") == 0) {
        want_fields = true; want_methods = true;
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
        printf("REPL commands:\n"
               "  :inspect <Type>   fields + methods\n"
               "  :methods <Type>   methods only\n"
               "  :fields  <Type>   fields only\n"
               "  :help             this help\n");
        return true;
    } else {
        fprintf(stderr, "unknown REPL command ':%s' (try :help)\n", cmd);
        return true;
    }
    if (tname[0] == '\0') {
        fprintf(stderr, "usage: :%s <TypeName>\n", cmd);
        return true;
    }

    size_t cap = strlen(imports) + strlen(decls) + strlen(globals) + 8;
    char *src = (char *)malloc_safe(cap);
    snprintf(src, cap, "%s\n%s\n%s\n", imports, decls, globals);
    AstNode *ast = parse(src, "<repl>");
    if (ast == NULL) {
        free(src);
        fprintf(stderr, "inspect: could not parse current REPL state\n");
        return true;
    }
    ModuleRegistry *reg = module_registry_new();
    checker_inspect_ex(ast, "<repl>", reg, tname, want_fields, want_methods);
    module_registry_free(reg);
    ast_free(ast);
    free(src);
    return true;
}

int jit_repl(void) {
    JitEngine engine;
    if (jit_init(&engine) != 0) {
        fprintf(stderr, "error: failed to initialize JIT\n");
        return 1;
    }

    ReplEditor *editor = repl_editor_new();
    bool color = repl_color_enabled();
    const char *CER  = color ? "\x1b[31m" : "";   /* red   — errors  */
    const char *CDIM = color ? "\x1b[90m" : "";   /* gray  — notices */
    const char *CRST = color ? "\x1b[0m"  : "";

    printf("LS REPL (type 'exit' or Ctrl-D to quit)\n");

    int snippet_id = 0;

    /* import statements — replayed at the top of every snippet so the module
       registry re-resolves them; decls — top-level; vars — replayed inside the
       __repl_N wrapper body. */
    char *accum_imports = (char *)malloc_safe(1); accum_imports[0] = '\0';
    size_t accum_imports_len = 0;
    /* P5-4 S-2: literals are Str (builtin string removed) — seed the prelude
       import exactly as if the user had typed `import std.core.str` first. It
       replays at the top of every snippet through the normal import
       accumulation (the same mechanism a manual import uses), avoiding the
       per-snippet AST-injection path that made the piped repl tests flaky. */
    accum_append(&accum_imports, &accum_imports_len, "import std.core.str");
    char *accum_decls   = (char *)malloc_safe(1); accum_decls[0] = '\0';
    size_t accum_decls_len = 0;
    char *accum_vars    = (char *)malloc_safe(1); accum_vars[0] = '\0';
    size_t accum_vars_len = 0;
    /* POD-scalar persisted vars become real top-level globals (defined once,
       referenced across snippets) so their *state* persists — `i += 1` mutates
       the live global instead of a per-snippet wrapper local. Replayed at top
       level as bare decls (`int i`, no init → no M-DEF, no re-init); the global
       defined in the introducing snippet keeps storage, later re-emitted copies
       are stripped to external declarations. Container/has_drop vars stay on the
       wrapper-replay path above (Phase 2). */
    char *accum_globals = (char *)malloc_safe(1); accum_globals[0] = '\0';
    size_t accum_globals_len = 0;

    /* Names of function definitions already live in the JIT. Re-emitted copies
       in later snippets (replayed decls, re-imported module functions) are
       stripped back to declarations so they resolve to the existing symbol
       instead of triggering a duplicate-definition error. */
    char **emitted = NULL;
    int emitted_count = 0, emitted_cap = 0;
    /* Names of persisted-var globals already defined in the JIT (same role as
       `emitted`, but for global variables). */
    char **emitted_globals = NULL;
    int emitted_globals_count = 0, emitted_globals_cap = 0;

    for (;;) {
        char *input = repl_editor_read(editor, "ls> ", "...> ", repl_input_is_complete);
        if (input == NULL) break;   /* EOF / Ctrl-D */

        /* Trim trailing whitespace for emptiness / exit checks */
        size_t ilen = strlen(input);
        while (ilen > 0 && (input[ilen-1]=='\n' || input[ilen-1]=='\r' ||
                            input[ilen-1]==' '  || input[ilen-1]=='\t'))
            ilen--;
        if (ilen == 0) { free(input); continue; }
        if ((ilen == 4 && strncmp(input, "exit", 4) == 0) ||
            (ilen == 4 && strncmp(input, "quit", 4) == 0)) { free(input); break; }

        /* Meta-commands (:inspect/:methods/:fields/:help) — static reflection of
           the types defined/imported so far. Handled outside the compile path. */
        if (input[0] == ':') {
            repl_meta_command(input, accum_imports, accum_decls, accum_globals);
            free(input);
            continue;
        }

        ReplLineKind kind = repl_classify(input);

        /* Refuse to redefine a function already live in the JIT: re-emitting it
           would silently keep the old body (same signature) or fail codegen
           verification with a confusing "ret non-void in void function" error
           (changed signature). True redefinition needs ORC resource-tracker
           symbol replacement — a larger REPL refactor (see known_limitations). */
        if (kind == REPL_DECL) {
            char fname[128];
            if (repl_def_fn_name(input, fname, sizeof(fname)) &&
                emitted_contains(emitted, emitted_count, fname)) {
                fprintf(stderr,
                    "%snote: '%s' is already defined - the REPL keeps the first "
                    "definition. Redefining a function isn't supported yet; "
                    "restart the REPL to change it.%s\n", CDIM, fname, CRST);
                free(input);
                continue;
            }
        }

        /* A persistable var decl (POD scalar, container, struct, or user type)
           persists as a top-level global so its *state* survives across snippets.
           Borrows/slices classify as REPL_EXPR and never match. */
        char *persist_name = (kind == REPL_VAR) ? repl_persisted_var_name(input) : NULL;

        /* Build full source. Persisted globals (bare decls) always go at top
           level so the checker resolves references and codegen emits them.
           Layout per kind:
             IMPORT  : imports, input, decls, globals
             DECL    : imports, decls, globals, input
             PERSIST : imports, decls, globals, <full decl>, wrapper{ vars }
             else    : imports, decls, globals, wrapper{ vars, input }   */
        size_t cap = accum_imports_len + accum_decls_len + accum_vars_len +
                     accum_globals_len + strlen(input) + 256;
        char *source_buf = (char *)malloc_safe(cap);
        if (kind == REPL_IMPORT) {
            snprintf(source_buf, cap, "%s\n%s\n%s\n%s\n",
                     accum_imports, input, accum_decls, accum_globals);
        } else if (kind == REPL_DECL) {
            snprintf(source_buf, cap, "%s\n%s\n%s\n%s\n",
                     accum_imports, accum_decls, accum_globals, input);
        } else if (persist_name != NULL) {
            snprintf(source_buf, cap,
                     "%s\n%s\n%s\n%s\n"
                     "def __repl_%d() -> int {\n%s\n  return 0\n}\n",
                     accum_imports, accum_decls, accum_globals, input, snippet_id,
                     (accum_vars_len > 0 ? accum_vars : ""));
        } else {
            snprintf(source_buf, cap,
                     "%s\n%s\n%s\n"
                     "def __repl_%d() -> int {\n%s\n  %s\n  return 0\n}\n",
                     accum_imports, accum_decls, accum_globals, snippet_id,
                     (accum_vars_len > 0 ? accum_vars : ""), input);
        }

        AstNode *ast = parse(source_buf, "<repl>");
        free(source_buf);
        if (ast == NULL) {
            fprintf(stderr, "%s  (parse error)%s\n", CER, CRST);
            free(persist_name); free(input); continue;
        }
        /* REPL Phase 2: flag replayed pre-existing globals (those already defined
           in a prior snippet) as is_repl_extern so the checker skips M-DEF/init
           and just registers the symbol — the live storage stays in the
           introducing snippet. Must run before checker_check (M-DEF fires there).
           The just-typed var is not yet in emitted_globals, so its full
           definition is untouched. */
        if (ast->kind == AST_PROGRAM) {
            for (int i = 0; i < ast->as.program.decl_count; i++) {
                AstNode *d = ast->as.program.decls[i];
                if (d->kind == AST_VAR_DECL && d->as.var_decl.name != NULL &&
                    emitted_contains(emitted_globals, emitted_globals_count,
                                     d->as.var_decl.name))
                    d->as.var_decl.is_repl_extern = true;
            }
        }
        /* Type check with a fresh module registry (resolves replayed imports). */
        ModuleRegistry *reg = module_registry_new();
        CheckerGenericMethods gm = {0};
        if (!checker_check(ast, "<repl>", reg, &gm)) {
            module_registry_free(reg);
            ast_free(ast);
            fprintf(stderr, "%s  (type error)%s\n", CER, CRST);
            free(persist_name); free(input); continue;
        }

        char mod_name[64];
        snprintf(mod_name, sizeof(mod_name), "repl_%d", snippet_id);
        LLVMModuleRef module = build_jit_module(&engine, ast, mod_name, reg, &gm);
        module_registry_free(reg);
        if (module == NULL) {
            ast_free(ast);
            fprintf(stderr, "%s  (codegen error)%s\n", CER, CRST);
            free(persist_name); free(input); continue;
        }

        /* Persisted-var global handling (Phase 1 POD scalars + Phase 2 containers):
           - Re-emitted copies of globals defined in a prior snippet are stripped
             to external declarations (initializer removed) so they resolve to the
             original storage rather than colliding / resetting. Only names in
             `emitted_globals` are touched — private literals (@Strlit/@fmt) and
             imported-module internals are left untouched.
           - When the new var needs a runtime initializer, codegen emits the
             fixed-name `__ls_global_stmts`; rename it uniquely so it neither
             collides across snippets nor gets stripped, and remember to call it.
             (Its call sites reference the value, so the rename is transparent.) */
        for (LLVMValueRef g = LLVMGetFirstGlobal(module); g;
             g = LLVMGetNextGlobal(g)) {
            if (emitted_contains(emitted_globals, emitted_globals_count,
                                 LLVMGetValueName(g))) {
                LLVMSetInitializer(g, NULL);
                LLVMSetLinkage(g, LLVMExternalLinkage);
            }
        }
        char gs_name[64];
        gs_name[0] = '\0';
        {
            LLVMValueRef gs = LLVMGetNamedFunction(module, "__ls_global_stmts");
            if (gs != NULL && !LLVMIsDeclaration(gs)) {
                snprintf(gs_name, sizeof(gs_name), "__ls_global_stmts_%d", snippet_id);
                LLVMSetValueName2(gs, gs_name, strlen(gs_name));
            }
        }
        /* Phase 2: has_drop globals (Vec/Map/Str/struct) make codegen emit the
           fixed-name `__ls_global_cleanup`. The REPL never calls it (globals leak
           to process exit — acceptable), but its fixed name would collide across
           snippets once promoted to external linkage. Rename it uniquely so each
           snippet's copy is distinct; it is simply never invoked. */
        {
            LLVMValueRef gc = LLVMGetNamedFunction(module, "__ls_global_cleanup");
            if (gc != NULL && !LLVMIsDeclaration(gc)) {
                char gc_name[64];
                snprintf(gc_name, sizeof(gc_name), "__ls_global_cleanup_%d", snippet_id);
                LLVMSetValueName2(gc, gc_name, strlen(gc_name));
            }
        }

        /* Strip bodies of functions already defined in a prior snippet, so the
           re-emitted copy resolves to the existing JIT symbol instead of
           triggering a duplicate definition.

           Turning a defined function back into a declaration is done in three
           passes — both VALUE uses and BLOCK uses must be dropped before any
           basic block is freed, or a freed block stays referenced and the
           release build segfaults nondeterministically (the L-010 family).

           Pass 1 — RAUW-undef every instruction result: drops all VALUE uses
             (incl. PHI value operands), so no instruction is used anymore.
           Pass 2 — erase every instruction (incl. terminators): a `br`/`switch`
             references its target blocks, so erasing all terminators drops all
             BLOCK uses too. Pass 1 left every instruction use-free, so erasing
             in any order is safe.
           Pass 3 — delete the now-empty, unreferenced blocks.

           Why pass 2 is required (the residual half of the original L-010 fix,
           which did only pass 1 + delete): deleting blocks front-to-back frees
           a loop header while a not-yet-deleted latch block still holds a
           back-edge `br header` — a dangling block use. RAUW-undef never
           touches block references, only value ones, so loopy functions (e.g.
           std_str__Str.to_float's exponent loop) kept flaking ~10% on any
           snippet that re-emits an imported module. */
        for (LLVMValueRef fn = LLVMGetFirstFunction(module); fn;
             fn = LLVMGetNextFunction(fn)) {
            if (LLVMIsDeclaration(fn)) continue;
            if (!emitted_contains(emitted, emitted_count, LLVMGetValueName(fn)))
                continue;
            /* Pass 1: drop value uses. */
            for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
                 bb = LLVMGetNextBasicBlock(bb)) {
                for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
                     inst = LLVMGetNextInstruction(inst)) {
                    LLVMTypeRef ity = LLVMTypeOf(inst);
                    if (LLVMGetTypeKind(ity) != LLVMVoidTypeKind)
                        LLVMReplaceAllUsesWith(inst, LLVMGetUndef(ity));
                }
            }
            /* Pass 2: erase all instructions (drops block uses via terminators). */
            for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
                 bb = LLVMGetNextBasicBlock(bb)) {
                LLVMValueRef inst;
                while ((inst = LLVMGetLastInstruction(bb)) != NULL)
                    LLVMInstructionEraseFromParent(inst);
            }
            /* Pass 3: delete the empty, unreferenced blocks. */
            while (LLVMGetFirstBasicBlock(fn))
                LLVMDeleteBasicBlock(LLVMGetFirstBasicBlock(fn));
        }

        /* Imported .ls-module functions are emitted with internal linkage (an
           AOT anti-collision measure), which hides them from other snippet
           modules in the JIT. The REPL is JIT-only and each snippet is its own
           module, so promote every function to external linkage: a definition
           in one snippet must satisfy the stripped declaration in the next. */
        for (LLVMValueRef fn = LLVMGetFirstFunction(module); fn;
             fn = LLVMGetNextFunction(fn)) {
            LLVMSetLinkage(fn, LLVMExternalLinkage);
        }

        if (jit_add_module(&engine, module) != 0) {
            ast_free(ast);
            fprintf(stderr, "%s  (jit error)%s\n", CER, CRST);
            free(persist_name); free(input); continue;
        }

        /* Record the function definitions this snippet contributed. */
        for (LLVMValueRef fn = LLVMGetFirstFunction(module); fn;
             fn = LLVMGetNextFunction(fn)) {
            if (LLVMIsDeclaration(fn)) continue;
            const char *fname = LLVMGetValueName(fn);
            if (emitted_contains(emitted, emitted_count, fname)) continue;
            if (emitted_count == emitted_cap) {
                emitted_cap = emitted_cap ? emitted_cap * 2 : 32;
                emitted = (char **)realloc_safe(emitted,
                                                (size_t)emitted_cap * sizeof(char *));
            }
            size_t n = strlen(fname) + 1;
            char *dup = (char *)malloc_safe(n);
            memcpy(dup, fname, n);
            emitted[emitted_count++] = dup;
        }

        /* Run this snippet's global initializers (the new var's runtime init, if
           any). Constant-init globals (`int i = 1`) fold into the global and need
           no call. Done AFTER the record loop: jit_lookup materializes the module,
           after which the LLVM IR can no longer be inspected for recording. Must
           still precede __repl_N so an expression never reads an uninitialized
           global (only relevant once a var and a use share a snippet). */
        if (gs_name[0] != '\0') {
            uint64_t gs_addr = jit_lookup(&engine, gs_name);
            if (gs_addr != 0) ((void (*)(void))(uintptr_t)gs_addr)();
        }

        /* Execute / acknowledge, then accumulate. */
        if (kind == REPL_IMPORT) {
            printf("%s  (imported)%s\n", CDIM, CRST);
            accum_append(&accum_imports, &accum_imports_len, input);
        } else if (kind == REPL_DECL) {
            printf("%s  (defined)%s\n", CDIM, CRST);
            accum_append(&accum_decls, &accum_decls_len, input);
        } else {
            char fn_name[64];
            snprintf(fn_name, sizeof(fn_name), "__repl_%d", snippet_id);
            uint64_t addr = jit_lookup(&engine, fn_name);
            if (addr != 0) {
                typedef int (*ReplFn)(void);
                ReplFn fn = (ReplFn)(uintptr_t)addr;
                int result = fn();
                if (result != 0) printf("=> %d\n", result);
            }
            if (kind == REPL_VAR) {
                printf("%s  (ok)%s\n", CDIM, CRST);
                if (persist_name != NULL) {
                    /* Persisted global: remember the name (future snippets flag
                       its re-emitted copy is_repl_extern and strip the storage)
                       and replay it as a bare decl `T name` (everything up to the
                       first '=', trimmed — the var decl's first '=' is always the
                       assignment operator). The replayed copy is the M-DEF default
                       for containers, but the is_repl_extern flag suppresses it. */
                    if (emitted_globals_count == emitted_globals_cap) {
                        emitted_globals_cap = emitted_globals_cap ? emitted_globals_cap * 2 : 16;
                        emitted_globals = (char **)realloc_safe(emitted_globals,
                            (size_t)emitted_globals_cap * sizeof(char *));
                    }
                    emitted_globals[emitted_globals_count++] = persist_name;
                    persist_name = NULL;   /* ownership moved into emitted_globals */

                    const char *eq = strchr(input, '=');
                    size_t blen = eq ? (size_t)(eq - input) : strlen(input);
                    while (blen > 0 && (input[blen-1]==' ' || input[blen-1]=='\t'))
                        blen--;
                    char *bare = (char *)malloc_safe(blen + 1);
                    memcpy(bare, input, blen);
                    bare[blen] = '\0';
                    accum_append(&accum_globals, &accum_globals_len, bare);
                    free(bare);
                } else {
                    size_t vlen = strlen(input) + 4;
                    char *var_line = (char *)malloc_safe(vlen);
                    snprintf(var_line, vlen, "  %s", input);
                    accum_append(&accum_vars, &accum_vars_len, var_line);
                    free(var_line);
                }
            }
        }

        free(persist_name);
        ast_free(ast);
        free(input);
        snippet_id++;
    }

    for (int i = 0; i < emitted_count; i++) free(emitted[i]);
    free(emitted);
    for (int i = 0; i < emitted_globals_count; i++) free(emitted_globals[i]);
    free(emitted_globals);
    free(accum_imports);
    free(accum_decls);
    free(accum_vars);
    free(accum_globals);
    repl_editor_free(editor);
    jit_destroy(&engine);
    return 0;
}
