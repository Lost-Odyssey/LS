/* main.c — Entry point: CLI parsing, token dump, parse, check, compile */
#include "common.h"
#include "scanner.h"
#include "parser.h"
#include "checker.h"
#include "diag.h"
#include "codegen.h"
#include "module.h"
#include "debug.h"
#include "jit.h"
#include "codegen_noalias.h"
#include "format.h"
#include "doc_assets.h"
#include "emit_c.h"
#include "driver_util.h"
#include "test_driver.h"
#include "link_driver.h"
#include <ctype.h>
#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>

#ifdef _WIN32
  /* Avoid <windows.h> here — it defines _TOKEN_INFORMATION_CLASS as TokenType
     which collides with our own TokenType enum. Forward-declare the single
     WinAPI we need. */
  typedef unsigned long DWORD_W32;
  __declspec(dllimport) DWORD_W32 __stdcall GetModuleFileNameA(
      void *hModule, char *lpFilename, DWORD_W32 nSize);
#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
  #include <limits.h>
#else
  #include <unistd.h>
  #include <limits.h>
#endif

/* Runtime entry declared at file scope (a block-scope extern triggers MSVC C4210). */
extern void __ls_set_args(int, char **);

#ifdef _WIN32
  #include <io.h>      /* _setmode, _fileno */
  #include <fcntl.h>   /* _O_BINARY */
#endif

/* Resolve the full path to the running ls executable (not just its
   containing directory). Returns 0 on success and writes a NUL-terminated
   path to `out`. Used by test_driver.c's self_exe_path(), which needs the
   executable itself (not its directory — that is module_executable_dir())
   to spawn `ls run <driver>` for `ls test` without depending on `ls` being
   first on PATH — on POSIX, coreutils already owns the name `ls` for
   directory listing. Exported via driver_util.h. */
int get_executable_path(char *out, size_t out_sz) {
    if (out == NULL || out_sz == 0) return -1;
#ifdef _WIN32
    DWORD_W32 n = GetModuleFileNameA(NULL, out, (DWORD_W32)out_sz);
    if (n == 0 || n >= out_sz) return -1;
#elif defined(__APPLE__)
    uint32_t n = (uint32_t)out_sz;
    if (_NSGetExecutablePath(out, &n) != 0) return -1;
#else
    ssize_t n = readlink("/proc/self/exe", out, out_sz - 1);
    if (n <= 0) return -1;
    out[n] = '\0';
#endif
    return 0;
}

/* Exported via driver_util.h (shared with test_driver.c). */
char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "error: could not open file '%s'\n", path);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = (char *)malloc_safe((size_t)size + 1);
    size_t rd = fread(buffer, 1, (size_t)size, file);
    buffer[rd] = '\0';
    fclose(file);
    return buffer;
}

static void print_token(Token *t) {
    printf("%-4d:%-3d  %-16s  '%.*s'\n",
           t->line, t->column,
           token_type_name(t->type),
           t->length, t->start);
}

static int cmd_tokens(const char *path) {
    char *source = read_file(path);
    if (source == NULL) return 1;

    Scanner scanner;
    scanner_init(&scanner, source);

    printf("%-9s %-16s  %s\n", "Loc", "Type", "Lexeme");
    printf("--------- ----------------  ----------\n");

    for (;;) {
        Token t = scanner_next(&scanner);
        print_token(&t);
        if (t.type == TOKEN_EOF || t.type == TOKEN_ERROR) break;
    }

    free(source);
    return 0;
}

static int cmd_parse(const char *path) {
    char *source = read_file(path);
    if (source == NULL) return 1;
    AstNode *ast = parse(source, path);
    if (ast == NULL) {
        free(source);
        return 1;
    }
    ast_print(ast, 0);
    ast_free(ast);
    free(source);
    return 0;
}

/* C2-3 (docs/plan_diagnostics_v2.md §3.4): with `--json` all diagnostics
   (parse + check stages) are collected and written as one schema-v1 JSON
   object to stdout; stderr stays empty and the exit code is unchanged
   (non-zero on any error). Unreadable input files keep the plain text
   path — there are no diagnostics to report, only a CLI error. */
static int cmd_check(const char *path, bool json) {
    char *source = read_file(path);
    if (source == NULL) return 1;

    DiagSink *js = NULL;
    if (json) {
        js = diag_json_sink_new();
        diag_set_sink(js);
    }

    bool ok = false;
    AstNode *ast = parse(source, path);
    if (ast != NULL) {
        ast_inject_std_str_import(ast);
        ModuleRegistry *reg = module_registry_new();
        ok = checker_check(ast, path, reg, NULL);
        module_registry_free(reg);
        ast_free(ast);
    }
    free(source);

    if (json) {
        diag_set_sink(NULL);
        bool truncated = diag_json_sink_checker_errors(js) >= CHECKER_MAX_ERRORS;
        diag_json_sink_write(js, stdout, truncated);
        diag_json_sink_free(js);
    } else if (ok) {
        printf("Type check passed.\n");
    }
    return ok ? 0 : 1;
}

/* Emit reusable C (Intel intrinsics) for the numeric/SIMD kernel subset.
   Parse + type-check (so resolved_type drives Simd register / op selection),
   then walk the AST emitting C source. Out-of-subset constructs error. */
static int cmd_emit_c(const char *path, const char *out_path,
                      const EmitCOpts *opts) {
    char *source = read_file(path);
    if (source == NULL) return 1;
    AstNode *ast = parse(source, path);
    if (ast == NULL) { free(source); return 1; }
    ast_inject_std_str_import(ast);
    ModuleRegistry *reg = module_registry_new();
    if (!checker_check(ast, path, reg, NULL)) {
        module_registry_free(reg);
        ast_free(ast);
        free(source);
        return 1;
    }
    int rc = emit_c(ast, out_path, path, opts);
    module_registry_free(reg);
    ast_free(ast);
    free(source);
    return rc;
}

/* Compile source to object file, then link to executable.
   memcheck: route all malloc/free through ls_mc_* tracker. AOT path links
   ls_memcheck.lib (located next to ls.exe) so ls_mc_alloc/free/realloc/report
   are resolved at link time and the program prints a leak report at exit. */
static int cmd_compile(const char *path, const char *output_path, bool dump_ir,
                       bool memcheck, bool profile, bool opt_set,
                       LsOptLevel opt_level, bool native, const char *target_cpu,
                       bool debug_info) {
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

    /* Codegen */
    CodegenContext ctx;
    codegen_init(&ctx, path);
    ctx.memcheck_enabled = memcheck;
    ctx.profile_enabled = profile;
    ctx.mode = CG_MODE_AOT;  /* AOT: forward argc/argv to __ls_set_args (bug #22) */
    /* CLI -O level / --native override codegen_init's env-derived default;
       absent any flag, the env default (LS_OPT / LS_NATIVE) is kept. */
    if (opt_set) ctx.opt.level = opt_level;
    if (native) ctx.opt.native = true;
    if (target_cpu) ctx.opt.target_cpu = target_cpu;  /* --target / cross-target AOT */
    ctx.debug_info = debug_info;  /* -g: line-table debug info (D1) */

    /* G1.5: transfer pending generic methods to codegen */
    if (gm.count > 0) {
        ctx.pending_gm_count = gm.count;
        size_t sz = (size_t)gm.count * sizeof(ctx.pending_generic_methods[0]);
        ctx.pending_generic_methods = malloc(sz);
        for (int gi = 0; gi < gm.count; gi++) {
            ctx.pending_generic_methods[gi].cloned_fn    = gm.methods[gi].cloned_fn;
            ctx.pending_generic_methods[gi].mangled_name = gm.methods[gi].mangled_name;
            ctx.pending_generic_methods[gi].struct_type  = gm.methods[gi].struct_type;
        }
        free(gm.methods);
    }

    if (codegen_compile(&ctx, ast, reg) != 0) {
        codegen_destroy(&ctx);
        module_registry_free(reg);
        ast_free(ast);
        free(source);
        return 1;
    }

    if (dump_ir) {
        codegen_dump_ir(&ctx);
    }

    /* Determine output paths */
    char obj_path[512];
    char exe_path[512];

    if (output_path) {
        snprintf(exe_path, sizeof(exe_path), "%s", output_path);
    } else {
#ifdef _WIN32
        snprintf(exe_path, sizeof(exe_path), "output.exe");
#else
        snprintf(exe_path, sizeof(exe_path), "output");
#endif
    }

#ifdef _WIN32
    snprintf(obj_path, sizeof(obj_path), "%s.obj", exe_path);
#else
    snprintf(obj_path, sizeof(obj_path), "%s.o", exe_path);
#endif

    /* Emit object file */
    if (codegen_emit_object(&ctx, obj_path) != 0) {
        codegen_destroy(&ctx);
        ast_free(ast);
        free(source);
        return 1;
    }

    printf("Emitted object: %s\n", obj_path);

    /* Link to executable — runtime-archive resolution + platform link command
       live in link_driver.c (W4). */
    LinkConfig lc = {0};
    lc.exe_path = exe_path;
    lc.obj_path = obj_path;
    lc.memcheck = memcheck;
    lc.profile = profile;
    lc.debug_info = debug_info;
    int link_result = link_driver_link(&lc);

    /* Clean up object file */
    /* remove(obj_path); */

    codegen_destroy(&ctx);
    module_registry_free(reg);
    ast_free(ast);
    free(source);

    if (link_result != 0) {
        fprintf(stderr, "error: linker failed (exit code %d)\n", link_result);
        return 1;
    }

    printf("Compiled: %s\n", exe_path);
    return 0;
}

/* Dump LLVM IR without compiling. When opt_set, the requested pass pipeline is
   run first so the optimized IR (inlining, vectorization, etc.) can be inspected. */
static int cmd_emit_ir(const char *path, bool opt_set, LsOptLevel opt_level, bool native,
                       const char *target_cpu, bool debug_info) {
    char *source = read_file(path);
    if (source == NULL) return 1;

    AstNode *ast = parse(source, path);
    if (ast == NULL) { free(source); return 1; }

    ast_inject_std_str_import(ast);
    ModuleRegistry *reg = module_registry_new();
    CheckerGenericMethods gm2 = {0};
    if (!checker_check(ast, path, reg, &gm2)) {
        module_registry_free(reg);
        ast_free(ast); free(source); return 1;
    }

    CodegenContext ctx;
    codegen_init(&ctx, path);
    ctx.mode = CG_MODE_AOT;  /* emit-ir is an AOT path: forward argc/argv (bug #22) */
    if (gm2.count > 0) {
        ctx.pending_gm_count = gm2.count;
        size_t sz = (size_t)gm2.count * sizeof(ctx.pending_generic_methods[0]);
        ctx.pending_generic_methods = malloc(sz);
        for (int gi = 0; gi < gm2.count; gi++) {
            ctx.pending_generic_methods[gi].cloned_fn    = gm2.methods[gi].cloned_fn;
            ctx.pending_generic_methods[gi].mangled_name = gm2.methods[gi].mangled_name;
            ctx.pending_generic_methods[gi].struct_type  = gm2.methods[gi].struct_type;
        }
        free(gm2.methods);
    }

    if (opt_set) ctx.opt.level = opt_level;
    if (native) ctx.opt.native = true;
    if (target_cpu) ctx.opt.target_cpu = target_cpu;  /* --target / cross-target AOT */
    ctx.debug_info = debug_info;  /* -g: line-table debug info (D1) */

    if (codegen_compile(&ctx, ast, reg) != 0) {
        codegen_destroy(&ctx); module_registry_free(reg);
        ast_free(ast); free(source); return 1;
    }

    /* A4: run noalias recovery even for unoptimized dumps so `emit-ir` shows
       the final attribute set (the pass is idempotent — ls_opt_run_passes
       below finds the markers already consumed and does nothing). */
    ls_noalias_recover(ctx.module);

    /* Optionally optimize before dumping so the user can inspect the post-pass
       IR (e.g. vector widths under -O3 --native). */
    if (opt_set && opt_level != LS_OPT_O0) {
        char *triple = LLVMGetDefaultTargetTriple();
        LLVMTargetMachineRef tm = ls_opt_create_target_machine(triple, &ctx.opt);
        LLVMDisposeMessage(triple);
        if (tm) {
            ls_opt_run_passes(ctx.module, tm, &ctx.opt);
            LLVMDisposeTargetMachine(tm);
        }
    }

    codegen_dump_ir(&ctx);

    codegen_destroy(&ctx);
    module_registry_free(reg);
    ast_free(ast);
    free(source);
    return 0;
}

/* Resolve a function for `ls ir`/`ls asm`: exact (defined) match first, else a
   unique substring match over defined functions (handles name mangling, e.g.
   'area' -> 'Point.area'). Prints diagnostics + candidates on miss/ambiguity. */
static LLVMValueRef inspect_find_function(LLVMModuleRef mod, const char *query) {
    LLVMValueRef exact = LLVMGetNamedFunction(mod, query);
    if (exact && LLVMCountBasicBlocks(exact) > 0) return exact;

    LLVMValueRef match = NULL;
    int count = 0;
    for (LLVMValueRef f = LLVMGetFirstFunction(mod); f; f = LLVMGetNextFunction(f)) {
        if (LLVMCountBasicBlocks(f) == 0) continue;   /* skip declarations */
        size_t nlen; const char *nm = LLVMGetValueName2(f, &nlen);
        if (nm && strstr(nm, query)) { count++; if (!match) match = f; }
    }
    if (count == 1) {
        size_t nlen; const char *nm = LLVMGetValueName2(match, &nlen);
        if (strcmp(nm, query) != 0)
            fprintf(stderr, "(resolved '%s' -> '%s')\n", query, nm);
        return match;
    }
    if (count == 0) {
        fprintf(stderr, "ir/asm: no defined function matching '%s'\n", query);
        return NULL;
    }
    fprintf(stderr, "ir/asm: '%s' is ambiguous; candidates:\n", query);
    for (LLVMValueRef f = LLVMGetFirstFunction(mod); f; f = LLVMGetNextFunction(f)) {
        if (LLVMCountBasicBlocks(f) == 0) continue;
        size_t nlen; const char *nm = LLVMGetValueName2(f, &nlen);
        if (nm && strstr(nm, query)) fprintf(stderr, "  %s\n", nm);
    }
    return NULL;
}

/* Print just one function's assembly: from its label line until the next
   function or data section. Platform-aware end markers — on Windows/COFF small
   leaf functions have no .Lfunc_end, so the next function's `.def` (or a
   `.section`) bounds them; SEH functions end at `.seh_endproc`; ELF emits
   `.Lfunc_endN`. Returns 0 if the function's label was found. */
static int inspect_print_asm_slice(const char *asm_text, const char *fnname) {
    char lbl1[600], lbl2[600];
    snprintf(lbl1, sizeof(lbl1), "%s:", fnname);
    snprintf(lbl2, sizeof(lbl2), "\"%s\":", fnname);
    size_t l1 = strlen(lbl1), l2 = strlen(lbl2);

    const char *p = asm_text;
    bool in_fn = false, printed = false;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        const char *t = p; size_t tl = len;
        while (tl > 0 && (*t == ' ' || *t == '\t')) { t++; tl--; }
        if (!in_fn) {
            if ((tl >= l1 && strncmp(t, lbl1, l1) == 0) ||
                (tl >= l2 && strncmp(t, lbl2, l2) == 0))
                in_fn = true;
        } else {
            /* A new function's metadata or a data section bounds this one. */
            if (strncmp(t, ".def", 4) == 0 || strncmp(t, ".section", 8) == 0)
                break;
        }
        if (in_fn) {
            fwrite(p, 1, len, stdout); fputc('\n', stdout);
            printed = true;
            if (strncmp(t, ".seh_endproc", 12) == 0 ||
                strncmp(t, ".Lfunc_end", 10) == 0) break;
        }
        if (!eol) break;
        p = eol + 1;
    }
    return printed ? 0 : 1;
}

/* `ls ir <fn> <file>` / `ls asm <fn> <file>` — per-function LLVM IR or x64
   assembly, optionally optimized (-O) and host-targeted (--native), for manual
   optimization. Mirrors the emit-ir pipeline, then filters to one function. */
static int cmd_ir_asm(const char *fn_query, const char *path, bool want_asm,
                      LsOptLevel opt_level, bool native, const char *target_cpu,
                      bool debug_info) {
    char *source = read_file(path);
    if (source == NULL) return 1;
    AstNode *ast = parse(source, path);
    if (ast == NULL) { free(source); return 1; }
    ast_inject_std_str_import(ast);
    ModuleRegistry *reg = module_registry_new();
    CheckerGenericMethods gm = {0};
    if (!checker_check(ast, path, reg, &gm)) {
        module_registry_free(reg); ast_free(ast); free(source); return 1;
    }

    CodegenContext ctx;
    codegen_init(&ctx, path);
    ctx.mode = CG_MODE_AOT;
    if (gm.count > 0) {
        ctx.pending_gm_count = gm.count;
        ctx.pending_generic_methods =
            malloc((size_t)gm.count * sizeof(ctx.pending_generic_methods[0]));
        for (int gi = 0; gi < gm.count; gi++) {
            ctx.pending_generic_methods[gi].cloned_fn    = gm.methods[gi].cloned_fn;
            ctx.pending_generic_methods[gi].mangled_name = gm.methods[gi].mangled_name;
            ctx.pending_generic_methods[gi].struct_type  = gm.methods[gi].struct_type;
        }
        free(gm.methods);
    }
    ctx.opt.level = opt_level;
    if (native) ctx.opt.native = true;
    if (target_cpu) ctx.opt.target_cpu = target_cpu;  /* --target / cross-target inspection */
    ctx.debug_info = debug_info;  /* -g: line-table debug info (D1) */

    int rc = 1;
    if (codegen_compile(&ctx, ast, reg) == 0) {
        char *triple = LLVMGetDefaultTargetTriple();
        LLVMTargetMachineRef tm = ls_opt_create_target_machine(triple, &ctx.opt);
        LLVMDisposeMessage(triple);
        if (tm && ctx.opt.level != LS_OPT_O0)
            ls_opt_run_passes(ctx.module, tm, &ctx.opt);   /* reflect -O in output */

        LLVMValueRef fn = inspect_find_function(ctx.module, fn_query);
        if (fn != NULL && !want_asm) {
            char *s = LLVMPrintValueToString(fn);
            printf("%s\n", s);
            LLVMDisposeMessage(s);
            rc = 0;
        } else if (fn != NULL && tm != NULL) {
            size_t nlen; const char *resolved = LLVMGetValueName2(fn, &nlen);
            LLVMMemoryBufferRef buf = NULL; char *emiterr = NULL;
            if (LLVMTargetMachineEmitToMemoryBuffer(tm, ctx.module, LLVMAssemblyFile,
                                                    &emiterr, &buf)) {
                fprintf(stderr, "asm emit failed: %s\n", emiterr ? emiterr : "?");
                if (emiterr) LLVMDisposeMessage(emiterr);
            } else {
                size_t n = LLVMGetBufferSize(buf);
                const char *start = LLVMGetBufferStart(buf);
                char *txt = (char *)malloc(n + 1);
                memcpy(txt, start, n); txt[n] = '\0';
                if (inspect_print_asm_slice(txt, resolved) != 0)
                    fprintf(stderr,
                        "asm: '%s' has no emitted code (likely inlined; try -O0 "
                        "or -O1)\n", resolved);
                else rc = 0;
                free(txt);
                LLVMDisposeMemoryBuffer(buf);
            }
        }
        if (tm) LLVMDisposeTargetMachine(tm);
    }

    codegen_destroy(&ctx);
    module_registry_free(reg);
    ast_free(ast);
    free(source);
    return rc;
}

/* `ls inspect <Type> <file>` — static reflection: type-check `file`, then print
   the fields + methods of the named concrete struct/enum. No codegen. */
static int cmd_inspect(const char *type_query, const char *path) {
    char *source = read_file(path);
    if (source == NULL) return 1;
    AstNode *ast = parse(source, path);
    if (ast == NULL) {
        free(source);
        return 1;
    }
    ast_inject_std_str_import(ast);
    ModuleRegistry *reg = module_registry_new();
    int rc = checker_inspect(ast, path, reg, type_query);
    module_registry_free(reg);
    ast_free(ast);
    free(source);
    return rc;
}

/* Write `content` to `path` (binary, no newline translation). 0 on success.
   Exported via driver_util.h (shared with test_driver.c). */
int write_file_str(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "error: could not write file '%s'\n", path);
        return 1;
    }
    fwrite(content, 1, strlen(content), f);
    fclose(f);
    return 0;
}

/* True if any line in `s` begins (after leading blanks) with a '#' preprocessor
   directive — those files (#if/#else/#endif) are skipped by the formatter,
   since conditional compilation drops inactive branches from the token stream. */
static bool has_directive(const char *s) {
    const char *p = s;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '#') return true;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return false;
}

/* ls fmt <files...> [--check] [--stdout]
   --check : exit 1 if any file would change (CI gate), do not rewrite
   --stdout: print formatted output instead of rewriting in place */
static int cmd_fmt(int argc, char *argv[]) {
    bool check = false, to_stdout = false;
#ifdef _WIN32
    /* avoid text-mode CRLF translation mangling formatted --stdout output */
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    int width = 0;   /* 0 = off; >0 = warn/fail on lines exceeding `width` columns */
    const char *paths[256];
    int npaths = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0) check = true;
        else if (strcmp(argv[i], "--stdout") == 0) to_stdout = true;
        else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) width = atoi(argv[++i]);
        else if (npaths < 256) paths[npaths++] = argv[i];
    }
    if (npaths == 0) {
        fprintf(stderr, "error: 'fmt' requires at least one file path\n");
        return 1;
    }

    int rc = 0, would_change = 0, over_width = 0;
    for (int i = 0; i < npaths; i++) {
        char *src = read_file(paths[i]);
        if (src == NULL) { rc = 1; continue; }
        if (has_directive(src)) {
            fprintf(stderr, "fmt: skipped %s (preprocessor directives)\n", paths[i]);
            free(src);
            continue;
        }
        char *out = ls_format_source(src);
        if (out == NULL) {
            fprintf(stderr, "fmt: skipped %s (scan error)\n", paths[i]);
            free(src);
            rc = 1;
            continue;
        }
        bool changed = (strcmp(src, out) != 0);
        if (to_stdout) {
            fputs(out, stdout);
        } else if (check) {
            if (changed) { printf("would reformat: %s\n", paths[i]); would_change++; }
        } else {
            if (changed) {
                if (write_file_str(paths[i], out) == 0)
                    printf("formatted: %s\n", paths[i]);
                else
                    rc = 1;
            }
        }
        /* long-line lint (opt-in): the gofmt-philosophy answer to over-width
           lines — surface them for the author to break, never auto-wrap. */
        if (width > 0) {
            int line = 1, col = 0;
            for (const char *p = out; *p; p++) {
                if (*p == '\n') {
                    if (col > width)
                        { fprintf(stderr, "%s:%d: line too long (%d > %d)\n",
                                  paths[i], line, col, width); over_width++; }
                    line++; col = 0;
                } else if (*p == '\r') {
                    /* ignore */
                } else if ((*p & 0xC0) != 0x80) {
                    col++;   /* count UTF-8 code points, not bytes */
                }
            }
        }
        free(out);
        free(src);
    }
    if (check && would_change > 0) rc = 1;
    if (check && over_width > 0) rc = 1;
    return rc;
}

/* ---- ls test: native test runner — moved to test_driver.c (W4) ---------- */

/* ---- ls doc: API reference generator ------------------------------------ */

typedef struct { char *d; size_t n, c; } DBuf;
static void db_init(DBuf *b) { b->c = 4096; b->n = 0; b->d = malloc_safe(b->c); b->d[0] = 0; }
static void db_putn(DBuf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->c) { while (b->n + n + 1 > b->c) b->c *= 2; b->d = realloc(b->d, b->c); }
    memcpy(b->d + b->n, s, n); b->n += n; b->d[b->n] = 0;
}
static void db_puts(DBuf *b, const char *s) { db_putn(b, s, strlen(s)); }
static void db_esc(DBuf *b, const char *s) {   /* HTML-escape a NUL-terminated string */
    for (; *s; s++) {
        if (*s == '&') db_puts(b, "&amp;");
        else if (*s == '<') db_puts(b, "&lt;");
        else if (*s == '>') db_puts(b, "&gt;");
        else db_putn(b, s, 1);
    }
}

/* byte offset of each 1-based source line; returns count via *out_n */
static size_t *build_lineoff(const char *src, int *out_n) {
    int n = 1;
    for (const char *p = src; *p; p++) if (*p == '\n') n++;
    size_t *off = malloc_safe((size_t)n * sizeof(size_t));
    int i = 0; off[0] = 0;
    for (const char *p = src; *p; p++) if (*p == '\n') { i++; off[i] = (size_t)(p - src) + 1; }
    *out_n = n;
    return off;
}

/* signature = source from `line`'s first non-blank char to the first '{'/';',
   newlines/runs collapsed to single spaces. Caller frees. */
static char *extract_sig(const char *src, const size_t *lineoff, int nlines, int line) {
    if (line < 1 || line > nlines) return NULL;
    size_t s = lineoff[line - 1];
    while (src[s] == ' ' || src[s] == '\t') s++;
    DBuf b; db_init(&b);
    int sp = 0, depth = 0;   /* () / [] nesting; a struct-literal default like
                                `opts = Opts{}` has its '{' at depth>0, so we
                                must only stop at the body '{' (depth 0). */
    for (size_t i = s; src[i]; i++) {
        char ch = src[i];
        if (depth == 0 && (ch == '{' || ch == ';')) break;
        if (ch == '(' || ch == '[') depth++;
        else if ((ch == ')' || ch == ']') && depth > 0) depth--;
        if (ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') { sp = 1; }
        else { if (sp && b.n > 0) db_putn(&b, " ", 1); sp = 0; db_putn(&b, &ch, 1); }
    }
    return b.d;
}

/* is source `line` (1-based) a comment line (first non-blank chars are `//`)? */
static bool is_comment_line(const char *src, const size_t *lineoff, int line) {
    size_t s = lineoff[line - 1];
    while (src[s] == ' ' || src[s] == '\t') s++;
    return src[s] == '/' && src[s + 1] == '/';
}

/* first line of the contiguous comment block ending at `line`-1; 0 if none. */
static int doc_block_top(const char *src, const size_t *lineoff, int line) {
    int L = line - 1, top = L;
    while (top >= 1 && is_comment_line(src, lineoff, top)) top--;
    top++;
    return (top > L || top < 1) ? 0 : top;
}

/* last line of the top-of-file comment block (the module header); 0 if line 1
   isn't a comment. */
static int top_block_end(const char *src, const size_t *lineoff, int nlines) {
    if (nlines < 1 || !is_comment_line(src, lineoff, 1)) return 0;
    int e = 1;
    while (e < nlines && is_comment_line(src, lineoff, e + 1)) e++;
    return e;
}

/* join comment lines a..b (1-based) into one escaped doc string (`//`/`///`/`//!`
   markers + one leading space stripped). NULL if range empty. */
static char *extract_comment_range(const char *src, const size_t *lineoff, int a, int b) {
    if (a < 1 || b < a) return NULL;
    DBuf bb; db_init(&bb);
    for (int l = a; l <= b; l++) {
        size_t s = lineoff[l - 1];
        while (src[s] == ' ' || src[s] == '\t') s++;
        s += 2;                          /* skip // */
        if (src[s] == '/') s++;          /* skip 3rd / of /// */
        if (src[s] == '!') s++;          /* skip ! of //! */
        if (src[s] == ' ') s++;
        size_t e = s; while (src[e] && src[e] != '\n') e++;
        while (e > s && (src[e - 1] == '\r' || src[e - 1] == ' ')) e--;
        char *t = malloc_safe(e - s + 1);
        memcpy(t, src + s, e - s); t[e - s] = 0;
        /* keep one line per source comment line (rendered with white-space:
           pre-wrap), so list/indent structure in the doc is preserved */
        if (l > a) db_putn(&bb, "\n", 1);
        db_esc(&bb, t);
        free(t);
    }
    return bb.d;
}

/* derive a module path from a file path: .../lib/std/core/vec.ls -> std.core.vec */
static void module_name_of(const char *path, char *out, size_t cap) {
    const char *p = strstr(path, "lib/");
    if (!p) p = strstr(path, "lib\\");
    p = p ? p + 4 : path;
    size_t j = 0;
    for (const char *q = p; *q && j + 1 < cap; q++) {
        if (*q == '/' || *q == '\\') out[j++] = '.';
        else out[j++] = *q;
    }
    out[j] = 0;
    if (j >= 4 && strcmp(out + j - 4, ".lls") == 0) out[j - 4] = 0;        /* strip .lls */
    else if (j >= 3 && strcmp(out + j - 3, ".ls") == 0) out[j - 3] = 0;    /* strip legacy .ls */
}

static int g_doc_items;  /* counts emitted API items (for --check / summary) */

/* emit one API row (signature + doc); skips `_`-prefixed (internal) names.
   `mend` = last line of the module-header block, so a decl's own doc is never
   mistaken for (or duplicated from) the file header. */
static void doc_emit_item(DBuf *o, const char *src, const size_t *lineoff,
                         int nlines, int mend, const char *name, int line) {
    if (name == NULL || name[0] == '_') return;   /* internal helper */
    char *sig = extract_sig(src, lineoff, nlines, line);
    int top = doc_block_top(src, lineoff, line);
    char *doc = (top > mend) ? extract_comment_range(src, lineoff, top, line - 1) : NULL;
    db_puts(o, "<tr><td class=\"s\"><span class=\"sig\">");
    if (sig) db_esc(o, sig); else db_esc(o, name);
    db_puts(o, "</span></td><td class=\"d\">");
    if (doc) db_puts(o, doc);
    db_puts(o, "</td></tr>\n");
    free(sig); free(doc);
    g_doc_items++;
}

static char *xstrdup(const char *s) { char *r = malloc_safe(strlen(s) + 1); strcpy(r, s); return r; }

/* replace every occurrence of `find` in `s` with `repl`; returns new buffer */
static char *str_replace_all(const char *s, const char *find, const char *repl) {
    size_t fl = strlen(find), rl = strlen(repl);
    DBuf b; db_init(&b);
    for (const char *p = s; *p; ) {
        if (strncmp(p, find, fl) == 0) { db_putn(&b, repl, rl); p += fl; }
        else { db_putn(&b, p, 1); p++; }
    }
    return b.d;
}

/* split "std.core.vec" -> category "core", short "vec" (into caller buffers) */
static void split_module(const char *m, char *cat, char *shrt, size_t cap) {
    const char *d1 = strchr(m, '.');
    const char *last = strrchr(m, '.');
    if (d1 && last && d1 != last) {
        const char *d2 = strchr(d1 + 1, '.');
        size_t cl = (size_t)(d2 - (d1 + 1));
        if (cl >= cap) cl = cap - 1;
        memcpy(cat, d1 + 1, cl); cat[cl] = 0;
        snprintf(shrt, cap, "%s", last + 1);
    } else {
        snprintf(cat, cap, "%s", "(root)");
        snprintf(shrt, cap, "%s", last ? last + 1 : m);
    }
}

/* one-line overview description: the part after " — " / " - " in the module doc */
static const char *desc_of(const char *moddoc) {
    if (!moddoc) return "";
    const char *p = strstr(moddoc, " \xe2\x80\x94 ");   /* " — " (UTF-8 em dash) */
    if (p) return p + 5;
    const char *h = strstr(moddoc, " - ");
    if (h) return h + 3;
    return moddoc;
}

/* The default CSS + HTML skeleton live in doc_assets.c (ls_doc_default_css /
   ls_doc_default_template), so the big string literals stay out of main.c. */

/* emit a module's API table rows for one parsed file into `sec` */
static void doc_emit_section_body(DBuf *sec, const char *src, const size_t *lineoff,
                                 int nlines, int mend, AstNode *ast) {
    for (int d = 0; d < ast->as.program.decl_count; d++) {
        AstNode *dn = ast->as.program.decls[d];
        switch (dn->kind) {
        case AST_FN_DECL:
            doc_emit_item(sec, src, lineoff, nlines, mend, dn->as.fn_decl.name, dn->line); break;
        case AST_STRUCT_DECL:
            doc_emit_item(sec, src, lineoff, nlines, mend, dn->as.struct_decl.name, dn->line); break;
        case AST_ENUM_DECL:
            doc_emit_item(sec, src, lineoff, nlines, mend, dn->as.enum_decl.name, dn->line); break;
        case AST_TRAIT_DECL:
            doc_emit_item(sec, src, lineoff, nlines, mend, dn->as.trait_decl.name, dn->line); break;
        case AST_IMPL_DECL:
            if (dn->as.impl_decl.method_count > 0 && dn->as.impl_decl.name) {
                db_puts(sec, "<tr><td class=\"grp\" colspan=\"2\">methods ");
                db_esc(sec, dn->as.impl_decl.name); db_puts(sec, "</td></tr>\n");
            }
            for (int m = 0; m < dn->as.impl_decl.method_count; m++) {
                AstNode *mn = dn->as.impl_decl.methods[m];
                if (mn->kind == AST_FN_DECL)
                    doc_emit_item(sec, src, lineoff, nlines, mend, mn->as.fn_decl.name, mn->line);
            }
            break;
        case AST_IMPL_TRAIT_DECL:
            if (dn->as.impl_trait_decl.method_count > 0) {
                db_puts(sec, "<tr><td class=\"grp\" colspan=\"2\">methods ");
                db_esc(sec, dn->as.impl_trait_decl.struct_name ? dn->as.impl_trait_decl.struct_name : "");
                db_puts(sec, ": "); db_esc(sec, dn->as.impl_trait_decl.trait_name ? dn->as.impl_trait_decl.trait_name : "");
                db_puts(sec, "</td></tr>\n");
            }
            for (int m = 0; m < dn->as.impl_trait_decl.method_count; m++) {
                AstNode *mn = dn->as.impl_trait_decl.methods[m];
                if (mn->kind == AST_FN_DECL)
                    doc_emit_item(sec, src, lineoff, nlines, mend, mn->as.fn_decl.name, mn->line);
            }
            break;
        default: break;
        }
    }
}

/* ---- ls symbol: hover/go-to-definition backend (Phase 3, editor-support) ----
   Reuses the exact extraction helpers `ls doc` already has (extract_sig /
   doc_block_top / extract_comment_range) — this is deliberately NOT a real
   symbol resolver: it never touches the type checker or symbol table (both
   are torn down inside checker_check before main.c ever sees them, see
   docs/plan_editor_lsp.md §9). It's a name-based lookup over the current
   file's top-level decls + `methods X { }` bodies, plus one level of import
   expansion via the same module_resolve_path() the real compiler uses. No
   scope/shadowing resolution — a name present in more than one searched file
   comes back as multiple candidates; the LSP client (not this command)
   decides what to do with more than one (definition lookups can legitimately
   return a list, hover just uses the first). */

/* JSON-escape (not the HTML db_esc a few lines up — doc comments can contain
   literal newlines from extract_comment_range's multi-line join). */
static void json_esc(DBuf *b, const char *s) {
    if (!s) return;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  db_puts(b, "\\\""); break;
        case '\\': db_puts(b, "\\\\"); break;
        case '\n': db_puts(b, "\\n");  break;
        case '\r': db_puts(b, "\\r");  break;
        case '\t': db_puts(b, "\\t");  break;
        default:
            if (*p < 0x20) {
                char esc[8];
                snprintf(esc, sizeof esc, "\\u%04x", *p);
                db_puts(b, esc);
            } else {
                db_putn(b, (const char *)p, 1);
            }
        }
    }
}

/* The maximal [A-Za-z_][A-Za-z0-9_]* run covering 1-based (line, col).
   LSP hover/definition positions sometimes land one past the last character
   of the word (end-of-token cursor), so a miss at `col` also tries `col-1`. */
static char *extract_ident_at(const char *src, const size_t *lineoff, int nlines,
                               int line, int col) {
    if (line < 1 || line > nlines || col < 1) return NULL;
    size_t ls = lineoff[line - 1];
    size_t le = ls;
    while (src[le] && src[le] != '\n' && src[le] != '\r') le++;

    size_t at = ls + (size_t)(col - 1);
    bool at_ok = at < le && (isalnum((unsigned char)src[at]) || src[at] == '_');
    if (!at_ok && at > ls && at <= le) {
        size_t prev = at - 1;
        if (isalnum((unsigned char)src[prev]) || src[prev] == '_') { at = prev; at_ok = true; }
    }
    if (!at_ok) return NULL;

    size_t s = at, e = at;
    while (s > ls && (isalnum((unsigned char)src[s - 1]) || src[s - 1] == '_')) s--;
    while (e < le && (isalnum((unsigned char)src[e]) || src[e] == '_')) e++;
    if (!(isalpha((unsigned char)src[s]) || src[s] == '_')) return NULL; /* bare digit run, not an identifier */

    char *out = malloc_safe(e - s + 1);
    memcpy(out, src + s, e - s);
    out[e - s] = 0;
    return out;
}

/* Emit one `{"file":...,"line":...,"kind":...,"signature":...,"doc":...}`
   item, comma-separated within the enclosing JSON array via *first. `kind`
   is one of "struct"/"enum"/"interface"/"function"/"method" — used by both
   `ls symbol` (Phase 3, one name) and `ls complete` (Phase 4, every name)
   below, so completion items carry the same icon/detail info hover does. */
static void symbol_emit_item(DBuf *out, bool *first, const char *filepath,
                              const char *src, const size_t *lineoff, int nlines,
                              int mend, const char *name, const char *kind, int line) {
    if (!*first) db_puts(out, ",");
    *first = false;

    char *sig = extract_sig(src, lineoff, nlines, line);
    int top = doc_block_top(src, lineoff, line);
    char *doc = (top > mend) ? extract_comment_range(src, lineoff, top, line - 1) : NULL;

    db_puts(out, "{\"file\":\"");
    json_esc(out, filepath);
    db_puts(out, "\",\"line\":");
    char linebuf[16];
    snprintf(linebuf, sizeof linebuf, "%d", line);
    db_puts(out, linebuf);
    db_puts(out, ",\"kind\":\"");
    json_esc(out, kind);
    db_puts(out, "\",\"name\":\"");
    json_esc(out, name);
    db_puts(out, "\",\"signature\":\"");
    json_esc(out, sig ? sig : name);
    db_puts(out, "\",\"doc\":");
    if (doc) { db_puts(out, "\""); json_esc(out, doc); db_puts(out, "\""); }
    else db_puts(out, "null");
    db_puts(out, "}");

    free(sig);
    free(doc);
}

/* Same top-level-decls + impl-methods walk as doc_emit_section_body, but
   filtering by name instead of emitting everything. */
static void symbol_search_decls(DBuf *out, bool *first, const char *filepath,
                                 const char *src, const size_t *lineoff, int nlines,
                                 int mend, AstNode *ast, const char *name) {
    for (int d = 0; d < ast->as.program.decl_count; d++) {
        AstNode *dn = ast->as.program.decls[d];
        switch (dn->kind) {
        case AST_FN_DECL:
            if (dn->as.fn_decl.name && strcmp(dn->as.fn_decl.name, name) == 0)
                symbol_emit_item(out, first, filepath, src, lineoff, nlines, mend, name, "function", dn->line);
            break;
        case AST_STRUCT_DECL:
            if (dn->as.struct_decl.name && strcmp(dn->as.struct_decl.name, name) == 0)
                symbol_emit_item(out, first, filepath, src, lineoff, nlines, mend, name, "struct", dn->line);
            break;
        case AST_ENUM_DECL:
            if (dn->as.enum_decl.name && strcmp(dn->as.enum_decl.name, name) == 0)
                symbol_emit_item(out, first, filepath, src, lineoff, nlines, mend, name, "enum", dn->line);
            break;
        case AST_TRAIT_DECL:
            if (dn->as.trait_decl.name && strcmp(dn->as.trait_decl.name, name) == 0)
                symbol_emit_item(out, first, filepath, src, lineoff, nlines, mend, name, "interface", dn->line);
            break;
        case AST_IMPL_DECL:
            for (int m = 0; m < dn->as.impl_decl.method_count; m++) {
                AstNode *mn = dn->as.impl_decl.methods[m];
                if (mn->kind == AST_FN_DECL && mn->as.fn_decl.name &&
                    strcmp(mn->as.fn_decl.name, name) == 0)
                    symbol_emit_item(out, first, filepath, src, lineoff, nlines, mend, name, "method", mn->line);
            }
            break;
        case AST_IMPL_TRAIT_DECL:
            for (int m = 0; m < dn->as.impl_trait_decl.method_count; m++) {
                AstNode *mn = dn->as.impl_trait_decl.methods[m];
                if (mn->kind == AST_FN_DECL && mn->as.fn_decl.name &&
                    strcmp(mn->as.fn_decl.name, name) == 0)
                    symbol_emit_item(out, first, filepath, src, lineoff, nlines, mend, name, "method", mn->line);
            }
            break;
        default: break;
        }
    }
}

/* Same walk, but collecting every non-internal (`_`-prefixed = skipped, same
   convention as ls doc's doc_emit_item) top-level decl + method instead of
   filtering by name — the data source for `ls complete`'s coarse, unfiltered
   suggestion list (see cmd_complete). */
static void symbol_collect_all(DBuf *out, bool *first, const char *filepath,
                                const char *src, const size_t *lineoff, int nlines,
                                int mend, AstNode *ast) {
    for (int d = 0; d < ast->as.program.decl_count; d++) {
        AstNode *dn = ast->as.program.decls[d];
        switch (dn->kind) {
        case AST_FN_DECL:
            if (dn->as.fn_decl.name && dn->as.fn_decl.name[0] != '_')
                symbol_emit_item(out, first, filepath, src, lineoff, nlines, mend,
                                  dn->as.fn_decl.name, "function", dn->line);
            break;
        case AST_STRUCT_DECL:
            if (dn->as.struct_decl.name && dn->as.struct_decl.name[0] != '_')
                symbol_emit_item(out, first, filepath, src, lineoff, nlines, mend,
                                  dn->as.struct_decl.name, "struct", dn->line);
            break;
        case AST_ENUM_DECL:
            if (dn->as.enum_decl.name && dn->as.enum_decl.name[0] != '_')
                symbol_emit_item(out, first, filepath, src, lineoff, nlines, mend,
                                  dn->as.enum_decl.name, "enum", dn->line);
            break;
        case AST_TRAIT_DECL:
            if (dn->as.trait_decl.name && dn->as.trait_decl.name[0] != '_')
                symbol_emit_item(out, first, filepath, src, lineoff, nlines, mend,
                                  dn->as.trait_decl.name, "interface", dn->line);
            break;
        case AST_IMPL_DECL:
            for (int m = 0; m < dn->as.impl_decl.method_count; m++) {
                AstNode *mn = dn->as.impl_decl.methods[m];
                if (mn->kind == AST_FN_DECL && mn->as.fn_decl.name && mn->as.fn_decl.name[0] != '_')
                    symbol_emit_item(out, first, filepath, src, lineoff, nlines, mend,
                                      mn->as.fn_decl.name, "method", mn->line);
            }
            break;
        case AST_IMPL_TRAIT_DECL:
            for (int m = 0; m < dn->as.impl_trait_decl.method_count; m++) {
                AstNode *mn = dn->as.impl_trait_decl.methods[m];
                if (mn->kind == AST_FN_DECL && mn->as.fn_decl.name && mn->as.fn_decl.name[0] != '_')
                    symbol_emit_item(out, first, filepath, src, lineoff, nlines, mend,
                                      mn->as.fn_decl.name, "method", mn->line);
            }
            break;
        default: break;
        }
    }
}

/* ls symbol <file> <line> <col>
   Prints {"query": "<ident or null>", "candidates": [...]} to stdout. Never
   hard-fails on a bad position/missing match — an empty candidates array is
   the normal "nothing to show" response an editor expects, not an error. */
static int cmd_symbol(const char *path, int line, int col) {
    char *source = read_file(path);
    if (source == NULL) { printf("{\"query\":null,\"candidates\":[]}\n"); return 0; }
    AstNode *ast = parse(source, path);
    if (ast == NULL) {
        printf("{\"query\":null,\"candidates\":[]}\n");
        free(source);
        return 0;
    }

    int nlines;
    size_t *lineoff = build_lineoff(source, &nlines);
    char *name = extract_ident_at(source, lineoff, nlines, line, col);

    DBuf out;
    db_init(&out);
    db_puts(&out, "{\"query\":");
    if (name) { db_puts(&out, "\""); json_esc(&out, name); db_puts(&out, "\""); }
    else db_puts(&out, "null");
    db_puts(&out, ",\"candidates\":[");

    bool first = true;
    if (name) {
        int mend = top_block_end(source, lineoff, nlines);
        symbol_search_decls(&out, &first, path, source, lineoff, nlines, mend, ast, name);

        /* One level of import expansion (this file's own imports, not their
           transitive imports) via the same resolver the real compiler uses. */
        for (int d = 0; d < ast->as.program.decl_count; d++) {
            AstNode *dn = ast->as.program.decls[d];
            if (dn->kind != AST_IMPORT_DECL) continue;
            char *resolved = module_resolve_import_path(dn->as.import_decl.path, path);
            if (!resolved) continue;
            char *isrc = read_file(resolved);
            if (isrc) {
                AstNode *iast = parse(isrc, resolved);
                if (iast) {
                    int inlines;
                    size_t *ilineoff = build_lineoff(isrc, &inlines);
                    int imend = top_block_end(isrc, ilineoff, inlines);
                    symbol_search_decls(&out, &first, resolved, isrc, ilineoff, inlines, imend, iast, name);
                    free(ilineoff);
                    ast_free(iast);
                }
                free(isrc);
            }
            free(resolved);
        }
    }

    db_puts(&out, "]}\n");
    printf("%s", out.d);

    free(out.d);
    free(name);
    free(lineoff);
    ast_free(ast);
    free(source);
    return 0;
}

/* ls complete <file>
   Prints {"items": [...]} — every non-internal top-level decl + method in
   the file plus one level of its imports, unfiltered (Phase 4, editor
   completion). Deliberately coarse: no receiver-type filtering (that would
   need the same symbol-table access Phase 3 established isn't available
   after checker_check() returns, see docs/plan_editor_lsp.md §9) — the LSP
   client does prefix/fuzzy filtering over this full list as the user types,
   same as it already does over static keyword/snippet completions. */
static int cmd_complete(const char *path) {
    char *source = read_file(path);
    if (source == NULL) { printf("{\"items\":[]}\n"); return 0; }
    AstNode *ast = parse(source, path);
    if (ast == NULL) {
        printf("{\"items\":[]}\n");
        free(source);
        return 0;
    }

    int nlines;
    size_t *lineoff = build_lineoff(source, &nlines);
    int mend = top_block_end(source, lineoff, nlines);

    DBuf out;
    db_init(&out);
    db_puts(&out, "{\"items\":[");

    bool first = true;
    symbol_collect_all(&out, &first, path, source, lineoff, nlines, mend, ast);

    for (int d = 0; d < ast->as.program.decl_count; d++) {
        AstNode *dn = ast->as.program.decls[d];
        if (dn->kind != AST_IMPORT_DECL) continue;
        char *resolved = module_resolve_import_path(dn->as.import_decl.path, path);
        if (!resolved) continue;
        char *isrc = read_file(resolved);
        if (isrc) {
            AstNode *iast = parse(isrc, resolved);
            if (iast) {
                int inlines;
                size_t *ilineoff = build_lineoff(isrc, &inlines);
                int imend = top_block_end(isrc, ilineoff, inlines);
                symbol_collect_all(&out, &first, resolved, isrc, ilineoff, inlines, imend, iast);
                free(ilineoff);
                ast_free(iast);
            }
            free(isrc);
        }
        free(resolved);
    }

    db_puts(&out, "]}\n");
    printf("%s", out.d);

    free(out.d);
    free(lineoff);
    ast_free(ast);
    free(source);
    return 0;
}

/* ls doc <files...> [-o out.html] [--css f] [--template f] [--title s]
   Generates an HTML API reference. Default style is the stdlib.html layout
   (grouped nav sidebar + overview table + per-module sections). Customize via
   --css (swap the stylesheet) or --template (custom skeleton with the
   {{TITLE}}/{{STYLE}}/{{NAV}}/{{OVERVIEW}}/{{CONTENT}} placeholders). */
static int cmd_doc(int argc, char *argv[]) {
    const char *out_path = NULL, *css_path = NULL, *tmpl_path = NULL;
    const char *title = "LS API reference";
    const char *paths[256];
    int npaths = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (strcmp(argv[i], "--css") == 0 && i + 1 < argc) css_path = argv[++i];
        else if (strcmp(argv[i], "--template") == 0 && i + 1 < argc) tmpl_path = argv[++i];
        else if (strcmp(argv[i], "--title") == 0 && i + 1 < argc) title = argv[++i];
        else if (npaths < 256) paths[npaths++] = argv[i];
    }
    if (npaths == 0) { fprintf(stderr, "error: 'doc' requires at least one file path\n"); return 1; }

    /* per-module collected data */
    char *m_name[256], *m_cat[256], *m_short[256], *m_desc[256];
    DBuf m_sec[256];
    int nmod = 0;
    g_doc_items = 0;

    for (int fi = 0; fi < npaths && nmod < 256; fi++) {
        char *src = read_file(paths[fi]);
        if (src == NULL) continue;
        AstNode *ast = parse(src, paths[fi]);
        if (ast == NULL || ast->kind != AST_PROGRAM) {
            fprintf(stderr, "doc: skipped %s (parse error)\n", paths[fi]);
            if (ast) ast_free(ast);
            free(src);
            continue;
        }
        int nlines; size_t *lineoff = build_lineoff(src, &nlines);
        char mod[512]; module_name_of(paths[fi], mod, sizeof(mod));
        char cat[128], shrt[256]; split_module(mod, cat, shrt, sizeof(cat));
        int mend = top_block_end(src, lineoff, nlines);
        char *mdoc = (mend >= 1) ? extract_comment_range(src, lineoff, 1, mend) : NULL;

        DBuf sec; db_init(&sec);
        db_puts(&sec, "<section id=\""); db_esc(&sec, mod); db_puts(&sec, "\">\n<h2>");
        db_esc(&sec, shrt); db_puts(&sec, "<span class=\"imp\">import "); db_esc(&sec, mod);
        db_puts(&sec, "</span></h2>\n");
        if (mdoc) { db_puts(&sec, "<p class=\"purpose\">"); db_puts(&sec, mdoc); db_puts(&sec, "</p>\n"); }
        db_puts(&sec, "<table class=\"api\">\n");
        doc_emit_section_body(&sec, src, lineoff, nlines, mend, ast);
        db_puts(&sec, "</table>\n</section>\n");

        m_name[nmod] = xstrdup(mod);
        m_cat[nmod] = xstrdup(cat);
        m_short[nmod] = xstrdup(shrt);
        m_desc[nmod] = xstrdup(mdoc ? mdoc : "");
        m_sec[nmod] = sec;
        nmod++;

        free(mdoc); free(lineoff); ast_free(ast); free(src);
    }

    /* order modules by category (stdlib grouping), then any leftovers */
    int oidx[256], no = 0;
    bool taken[256] = {false};
    const char *order[] = {"core", "mem", "sync", "sci", "text", "sys", "chart"};
    for (int k = 0; k < (int)(sizeof(order) / sizeof(order[0])); k++)
        for (int i = 0; i < nmod; i++)
            if (!taken[i] && strcmp(m_cat[i], order[k]) == 0) { oidx[no++] = i; taken[i] = true; }
    for (int i = 0; i < nmod; i++) if (!taken[i]) { oidx[no++] = i; taken[i] = true; }

    /* NAV: Overview link + grouped module links */
    DBuf nav; db_init(&nav);
    db_puts(&nav, "<a href=\"#overview\">Overview</a>\n");
    const char *curcat = NULL;
    for (int j = 0; j < no; j++) {
        int i = oidx[j];
        if (!curcat || strcmp(curcat, m_cat[i]) != 0) {
            curcat = m_cat[i];
            db_puts(&nav, "<div class=\"group\">"); db_esc(&nav, curcat); db_puts(&nav, "</div>\n");
        }
        db_puts(&nav, "<a class=\"item\" href=\"#"); db_esc(&nav, m_name[i]); db_puts(&nav, "\">");
        db_esc(&nav, m_short[i]); db_puts(&nav, "</a>\n");
    }

    /* OVERVIEW table (stdlib modmap style: grouped, module | description) */
    DBuf ov; db_init(&ov);
    db_puts(&ov, "<section id=\"overview\"><h2>Overview</h2>\n<table class=\"modmap\">\n");
    const char *ovcat = NULL;
    for (int j = 0; j < no; j++) {
        int i = oidx[j];
        if (!ovcat || strcmp(ovcat, m_cat[i]) != 0) {
            ovcat = m_cat[i];
            db_puts(&ov, "<tr><td class=\"grp\" colspan=\"2\">"); db_esc(&ov, ovcat); db_puts(&ov, "</td></tr>\n");
        }
        db_puts(&ov, "<tr><td class=\"m\"><a href=\"#"); db_esc(&ov, m_name[i]); db_puts(&ov, "\">");
        db_esc(&ov, m_short[i]); db_puts(&ov, "</a></td><td>");
        /* overview = one-line summary: first line of the description only */
        for (const char *dp = desc_of(m_desc[i]); *dp && *dp != '\n'; dp++)
            db_putn(&ov, dp, 1);
        db_puts(&ov, "</td></tr>\n");
    }
    db_puts(&ov, "</table></section>\n");

    /* CONTENT = sections in nav order */
    DBuf content; db_init(&content);
    for (int j = 0; j < no; j++) db_puts(&content, m_sec[oidx[j]].d);

    /* assemble via template: default skeleton or --template file, default CSS or --css file */
    char *skel = tmpl_path ? read_file(tmpl_path) : NULL;
    if (skel == NULL) { if (tmpl_path) fprintf(stderr, "doc: using default template (could not read %s)\n", tmpl_path); skel = xstrdup(ls_doc_default_template); }
    char *css = css_path ? read_file(css_path) : NULL;
    if (css == NULL) { if (css_path) fprintf(stderr, "doc: using default css (could not read %s)\n", css_path); css = xstrdup(ls_doc_default_css); }

    char *s1 = str_replace_all(skel, "{{STYLE}}", css);
    char *s2 = str_replace_all(s1, "{{TITLE}}", title);
    char *s3 = str_replace_all(s2, "{{NAV}}", nav.d);
    char *s4 = str_replace_all(s3, "{{OVERVIEW}}", ov.d);
    char *html = str_replace_all(s4, "{{CONTENT}}", content.d);

    int rc = 0;
    if (out_path) {
        rc = write_file_str(out_path, html);
        if (rc == 0) printf("doc: wrote %s (%d modules, %d items)\n", out_path, nmod, g_doc_items);
    } else {
        fputs(html, stdout);
    }

    free(skel); free(css); free(s1); free(s2); free(s3); free(s4); free(html);
    free(nav.d); free(ov.d); free(content.d);
    for (int i = 0; i < nmod; i++) {
        free(m_name[i]); free(m_cat[i]); free(m_short[i]); free(m_desc[i]); free(m_sec[i].d);
    }
    return rc;
}

static void usage(void) {
    fprintf(stderr,
        "Usage: lls <command> [options]\n"
        "\n"
        "Commands:\n"
        "  tokens <file>              Print token stream\n"
        "  parse <file>               Parse and print AST\n"
        "  check <file> [--json]      Parse and type-check\n"
        "       [--json]              print diagnostics as one JSON object on stdout\n"
        "                             (schema v1, see docs/plan_diagnostics_v2.md)\n"
        "  symbol <file> <line> <col>  Print {query,candidates:[{file,line,kind,name,signature,doc}]}\n"
        "       JSON for the identifier at (line, col) — name-based lookup across the\n"
        "       file + its imports, for editor hover/go-to-definition (not a real\n"
        "       scope-resolving symbol lookup, see docs/plan_editor_lsp.md)\n"
        "  complete <file>            Print {items:[{file,line,kind,name,signature,doc}]}\n"
        "       every decl/method in the file + its imports, unfiltered, for editor\n"
        "       completion lists (same caveat as 'symbol' above)\n"
        "  inspect <Type> <file>      Print a type's fields + methods (reflection)\n"
        "  ir  <fn> <file> [-O|-On] [--native]   Print one function's LLVM IR\n"
        "  asm <fn> <file> [-O|-On] [--native]   Print one function's assembly\n"
        "  emit-ir <file>             Emit LLVM IR to stderr\n"
        "  compile <file> [-o out]    Compile to executable\n"
        "       [-O0|-O1|-O2|-O3|-Os|-Oz]  optimization level (default -O2)\n"
        "       [--native]            target the host CPU (unlocks AVX etc.; non-portable)\n"
        "       [-g]                  emit line-table debug info (PDB on Windows)\n"
        "  run <file>                 JIT execute (Phase 5)\n"
        "  run [-O|-O1..-O3|-Os|-Oz] <file>  JIT with optimization (native CPU)\n"
        "  repl                       Interactive REPL (Phase 5)\n"
        "  fmt <files...> [--check|--stdout] [--width N]   Format LS source\n"
        "  test <files...> [--filter p] [--memcheck]  Run def test_*() functions\n"
        "  doc <files...> [-o out.html] [--css f] [--template f] [--title s]\n"
        "       Generate an HTML API reference (default style = stdlib layout)\n"
    );
}

/* --- Subcommand handlers, dispatched by name via g_commands[] below.
 *
 * Every handler takes the full original (argc, argv) — argv[1] is the
 * command name, argv[2..] are its own arguments — exactly as the old
 * if/strcmp chain passed them, so each handler's internal validation and
 * error text are unchanged verbatim from before this split. */

static int handle_fmt(int argc, char *argv[]) {
    return cmd_fmt(argc, argv);
}

static int handle_test(int argc, char *argv[]) {
    return test_driver_run(argc, argv);
}

static int handle_doc(int argc, char *argv[]) {
    return cmd_doc(argc, argv);
}

static int handle_tokens(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "error: 'tokens' requires a file path\n");
        return 1;
    }
    return cmd_tokens(argv[2]);
}

static int handle_parse(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "error: 'parse' requires a file path\n");
        return 1;
    }
    return cmd_parse(argv[2]);
}

static int handle_check(int argc, char *argv[]) {
    bool json = false;
    const char *file = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0)
            json = true;
        else
            file = argv[i];
    }
    if (file == NULL) {
        fprintf(stderr, "error: 'check' requires a file path\n");
        return 1;
    }
    return cmd_check(file, json);
}

static int handle_symbol(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "error: 'symbol' requires <file> <line> <col>\n");
        return 1;
    }
    return cmd_symbol(argv[2], atoi(argv[3]), atoi(argv[4]));
}

static int handle_complete(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "error: 'complete' requires a file path\n");
        return 1;
    }
    return cmd_complete(argv[2]);
}

static int handle_inspect(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "error: 'inspect' requires <TypeName> <file>\n");
        return 1;
    }
    return cmd_inspect(argv[2], argv[3]);
}

static int handle_ir_asm(int argc, char *argv[]) {
    const char *cmd = argv[1];
    bool want_asm = (strcmp(cmd, "asm") == 0);
    const char *fn = NULL;
    const char *file = NULL;
    CodegenFlags cf = {0};
    cf.opt_level = LS_OPT_O2;   /* default -O2: show real codegen */
    for (int i = 2; i < argc; i++) {
        int consumed = parse_codegen_flags(argc, argv, i, &cf);
        if (consumed > 0) { i += consumed - 1; continue; }
        if (fn == NULL) fn = argv[i];
        else if (file == NULL) file = argv[i];
    }
    if (fn == NULL || file == NULL) {
        fprintf(stderr, "error: '%s' requires <function> <file>\n", cmd);
        return 1;
    }
    return cmd_ir_asm(fn, file, want_asm, cf.opt_level, cf.native, cf.target, cf.debug_info);
}

static int handle_emit_ir(int argc, char *argv[]) {
    const char *file = NULL;
    CodegenFlags cf = {0};
    cf.opt_level = LS_OPT_O2;
    for (int i = 2; i < argc; i++) {
        int consumed = parse_codegen_flags(argc, argv, i, &cf);
        if (consumed > 0) { i += consumed - 1; continue; }
        if (file == NULL) file = argv[i];
    }
    if (file == NULL) {
        fprintf(stderr, "error: 'emit-ir' requires a file path\n");
        return 1;
    }
    return cmd_emit_ir(file, cf.opt_set, cf.opt_level, cf.native, cf.target, cf.debug_info);
}

static int handle_emit_c(int argc, char *argv[]) {
    const char *file = NULL;
    const char *output = NULL;
    EmitCOpts opts = {0};
    char *only_buf = NULL;               /* owns the split --only names */
    const char *only_names[256];
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) output = argv[++i];
        else if (strcmp(argv[i], "--skip-unsupported") == 0) opts.skip_unsupported = true;
        else if (strncmp(argv[i], "--only=", 7) == 0 || strcmp(argv[i], "--only") == 0) {
            const char *list = (argv[i][6] == '=') ? argv[i] + 7
                             : (i + 1 < argc ? argv[++i] : "");
            only_buf = strdup(list);      /* split on commas in place */
            int c = 0;
            char *tok = strtok(only_buf, ",");
            while (tok && c < 256) { only_names[c++] = tok; tok = strtok(NULL, ","); }
            opts.only = only_names;
            opts.only_count = c;
        }
        else if (file == NULL) file = argv[i];
    }
    if (file == NULL) {
        fprintf(stderr, "error: 'emit-c' requires a file path (usage: ls emit-c "
                        "<file.ls> -o <out.c> [--only f1,f2] [--skip-unsupported])\n");
        free(only_buf);
        return 1;
    }
    if (output == NULL) {
        fprintf(stderr, "error: 'emit-c' requires -o <out.c>\n");
        free(only_buf);
        return 1;
    }
    int rc = cmd_emit_c(file, output, &opts);
    free(only_buf);
    return rc;
}

static int handle_compile(int argc, char *argv[]) {
    const char *output = NULL;
    const char *file = NULL;
    bool dump_ir = false;
    bool memcheck = false;
    bool profile = false;
    CodegenFlags cf = {0};
    cf.opt_level = LS_OPT_O2;
    for (int i = 2; i < argc; i++) {
        int consumed = parse_codegen_flags(argc, argv, i, &cf);
        if (consumed > 0) { i += consumed - 1; continue; }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(argv[i], "--dump-ir") == 0) {
            dump_ir = true;
        } else if (strcmp(argv[i], "--memcheck") == 0) {
            memcheck = true;
        } else if (strcmp(argv[i], "--profile") == 0) {
            profile = true;
        } else if (file == NULL) {
            file = argv[i];
        }
    }
    if (file == NULL) {
        fprintf(stderr, "error: 'compile' requires a file path\n");
        return 1;
    }
    return cmd_compile(file, output, dump_ir, memcheck, profile,
                       cf.opt_set, cf.opt_level, cf.native, cf.target, cf.debug_info);
}

static int handle_run(int argc, char *argv[]) {
    bool memcheck = false;
    bool profile = false;
    CodegenFlags cf = {0};
    cf.opt_level = LS_OPT_O2;
    const char *file = NULL;
    int file_idx = -1;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--memcheck") == 0) { memcheck = true; continue; }
        if (strcmp(argv[i], "--profile") == 0) { profile = true; continue; }
        if (strcmp(argv[i], "--optimize") == 0) { cf.opt_set = true; cf.opt_level = LS_OPT_O2; continue; }
        int consumed = parse_codegen_flags(argc, argv, i, &cf);
        if (consumed > 0) {
            if (cf.target != NULL) {
                /* JIT always targets the host (cannot run foreign ISA); ignore. */
                fprintf(stderr, "note: 'run' (JIT) always targets the host CPU; "
                                "--target is ignored. Use 'compile --target=...' for AOT.\n");
                cf.target = NULL;
            }
            if (cf.debug_info) {
                /* JIT debug info (PDB registration) is not implemented yet
                   (docs/plan_debug_info.md phase 2); ignore rather than
                   silently mis-parse -g as the file path. */
                fprintf(stderr, "note: 'run' (JIT) does not support -g yet; ignored.\n");
                cf.debug_info = false;
            }
            i += consumed - 1;
            continue;
        }
        if (file == NULL) { file = argv[i]; file_idx = i; }
    }
    if (file == NULL) {
        fprintf(stderr, "error: 'run' requires a file path\n");
        return 1;
    }
    {
        /* argv[file_idx] is the script name; pass it as g_argv[0] so that
           proc.program() returns the script name and proc.args() returns
           the remaining arguments (i=1 .. n-1), matching POSIX convention. */
        int script_argc = argc - file_idx;
        char **script_argv = &argv[file_idx];
        __ls_set_args(script_argc, script_argv);
    }
    if (memcheck) return jit_run_file_memcheck(file);
    if (profile) return jit_run_file_profile(file);
    if (cf.opt_set) return jit_run_file_optlevel(file, cf.opt_level);
    return jit_run_file(file);
}

static int handle_repl(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return jit_repl();
}

/* Dispatch table replacing the old if/strcmp chain. `min_args` documents
   each command's structural minimum argc (informational — every handler
   already performs its own, more specific argc/positional validation with
   its original error text, so min_args is not used as a gate here to avoid
   any risk of message drift). */
typedef int (*CommandHandler)(int argc, char *argv[]);

typedef struct {
    const char *name;
    int min_args;
    CommandHandler handler;
    const char *usage;
} CommandEntry;

static const CommandEntry g_commands[] = {
    { "fmt",      3, handle_fmt,      "fmt <files...> [--check|--stdout] [--width N]" },
    { "test",     2, handle_test,     "test <files...> [--filter p] [--memcheck]" },
    { "doc",      3, handle_doc,      "doc <files...> [-o out.html] [--css f] [--template f] [--title s]" },
    { "tokens",   3, handle_tokens,   "tokens <file>" },
    { "parse",    3, handle_parse,    "parse <file>" },
    { "check",    3, handle_check,    "check <file> [--json]" },
    { "symbol",   5, handle_symbol,   "symbol <file> <line> <col>" },
    { "complete", 3, handle_complete, "complete <file>" },
    { "inspect",  4, handle_inspect,  "inspect <TypeName> <file>" },
    { "ir",       4, handle_ir_asm,   "ir <fn> <file> [-O|-On] [--native] [--target=<cpu>] [-g]" },
    { "asm",      4, handle_ir_asm,   "asm <fn> <file> [-O|-On] [--native] [--target=<cpu>] [-g]" },
    { "emit-ir",  3, handle_emit_ir,  "emit-ir <file> [-O|-On] [--native] [--target=<cpu>] [-g]" },
    { "emit-c",   4, handle_emit_c,   "emit-c <file> -o <out.c> [--only f1,f2] [--skip-unsupported]" },
    { "compile",  3, handle_compile,  "compile <file> [-o out] [-O|-On] [--native] [--target=<cpu>] [-g]" },
    { "run",      3, handle_run,      "run <file> [-O|-On] [--memcheck] [--profile]" },
    { "repl",     2, handle_repl,     "repl" },
};
#define LS_COMMAND_COUNT (sizeof(g_commands) / sizeof(g_commands[0]))

int main(int argc, char *argv[]) {
#ifdef LS_LEAKCHECK
    ls_lc_init();   /* compiler self heap-leak tracking; reports at exit */
#endif
    if (argc < 2) {
        usage();
        return 1;
    }

    const char *cmd = argv[1];

    for (size_t ci = 0; ci < LS_COMMAND_COUNT; ci++) {
        if (strcmp(g_commands[ci].name, cmd) == 0) {
            return g_commands[ci].handler(argc, argv);
        }
    }

    fprintf(stderr, "error: unknown command '%s'\n", cmd);
    usage();
    return 1;
}
