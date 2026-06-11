/* main.c — Entry point: CLI parsing, token dump, parse, check, compile */
#include "common.h"
#include "scanner.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"
#include "module.h"
#include "debug.h"
#include "jit.h"

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

/* Resolve the directory containing the running ls executable.
   Returns 0 on success and writes a NUL-terminated path (no trailing
   separator) to `out`. Used by AOT --memcheck to locate ls_memcheck.lib
   alongside ls.exe. */
static int get_executable_dir(char *out, size_t out_sz) {
    if (out == NULL || out_sz == 0) return -1;
    char buf[1024];
    size_t len = 0;

#ifdef _WIN32
    DWORD_W32 n = GetModuleFileNameA(NULL, buf, (DWORD_W32)sizeof(buf));
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

    /* Strip trailing filename component, leaving the directory. */
    while (len > 0 && buf[len - 1] != '/' && buf[len - 1] != '\\') {
        len--;
    }
    /* Drop the trailing separator unless that would leave an empty string
       (root on POSIX). */
    if (len > 1) len--;

    if (len + 1 > out_sz) return -1;
    memcpy(out, buf, len);
    out[len] = '\0';
    return 0;
}

static char *read_file(const char *path) {
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

static int cmd_check(const char *path) {
    char *source = read_file(path);
    if (source == NULL) return 1;
    AstNode *ast = parse(source, path);
    if (ast == NULL) {
        free(source);
        return 1;
    }
    ast_inject_std_str_import(ast);
    ModuleRegistry *reg = module_registry_new();
    bool ok = checker_check(ast, path, reg, NULL);
    if (ok) {
        printf("Type check passed.\n");
    }
    module_registry_free(reg);
    ast_free(ast);
    free(source);
    return ok ? 0 : 1;
}

/* Compile source to object file, then link to executable.
   memcheck: route all malloc/free through ls_mc_* tracker. AOT path links
   ls_memcheck.lib (located next to ls.exe) so ls_mc_alloc/free/realloc/report
   are resolved at link time and the program prints a leak report at exit. */
static int cmd_compile(const char *path, const char *output_path, bool dump_ir,
                       bool memcheck, bool profile) {
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
    ctx.aot_entry = true;  /* AOT: forward argc/argv to __ls_set_args (bug #22) */

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

    /* Resolve ls_memcheck archive path (next to ls.exe) when --memcheck is on.
       If the archive is missing we still attempt the link — the user will get
       a clear "unresolved external ls_mc_alloc" error from the linker, plus
       our warning here. */
    char mc_lib[1280] = "";
    if (memcheck) {
        char libdir[1024];
        if (get_executable_dir(libdir, sizeof(libdir)) == 0) {
#ifdef _WIN32
            snprintf(mc_lib, sizeof(mc_lib), "\"%s\\ls_memcheck.lib\"", libdir);
#else
            /* Use -L<dir> -lls_memcheck so the linker resolves libls_memcheck.a */
            snprintf(mc_lib, sizeof(mc_lib), "-L\"%s\" -lls_memcheck", libdir);
#endif
        } else {
            fprintf(stderr,
                    "warning: --memcheck enabled but could not locate ls.exe directory; "
                    "linker may fail to resolve ls_mc_* symbols\n");
        }
    }

    /* Resolve ls_profiler archive path when --profile is on. */
    char prof_lib[1280] = "";
    if (profile) {
        char libdir[1024];
        if (get_executable_dir(libdir, sizeof(libdir)) == 0) {
#ifdef _WIN32
            snprintf(prof_lib, sizeof(prof_lib), "\"%s\\ls_profiler.lib\"", libdir);
#else
            snprintf(prof_lib, sizeof(prof_lib), "-L\"%s\" -lls_profiler", libdir);
#endif
        } else {
            fprintf(stderr,
                    "warning: --profile enabled but could not locate ls.exe directory; "
                    "linker may fail to resolve ls_prof_* symbols\n");
        }
    }

    /* ls_os_backend is always linked — any program importing std.os or std.time
       needs ls_os_* symbols.  The archive sits next to ls.exe just like
       ls_memcheck.lib / ls_profiler.lib. */
    char os_lib[1280] = "";
    {
        char libdir[1024];
        if (get_executable_dir(libdir, sizeof(libdir)) == 0) {
#ifdef _WIN32
            snprintf(os_lib, sizeof(os_lib), "\"%s\\ls_os_backend.lib\"", libdir);
#else
            snprintf(os_lib, sizeof(os_lib), "-L\"%s\" -lls_os_backend", libdir);
#endif
        }
    }

    /* Link to executable */
    char link_cmd[2560];
#ifdef _WIN32
    {
        /* Use clang as linker driver via cmd.exe /c for proper quoting */
        const char *clang_paths[] = {
            "C:\\Program Files\\LLVM\\bin\\clang.exe",
            "C:\\llvm\\bin\\clang.exe",
            NULL
        };
        const char *clang = NULL;
        for (int ci = 0; clang_paths[ci]; ci++) {
            FILE *tf = fopen(clang_paths[ci], "rb");
            if (tf) { fclose(tf); clang = clang_paths[ci]; break; }
        }
        /* ls_memcheck.lib and ls_os_backend.lib are built with /MD (dynamic
           CRT), so the linker needs the dynamic CRT import libraries. */
        if (clang) {
            snprintf(link_cmd, sizeof(link_cmd),
                     "cmd.exe /c \"\"%s\" -o \"%s\" \"%s\" %s %s %s"
                     " -llegacy_stdio_definitions -lucrt"
                     " -Xlinker /NODEFAULTLIB:libucrt.lib"
                     " -Xlinker /NODEFAULTLIB:libcmt.lib\"",
                     clang, exe_path, obj_path, mc_lib, prof_lib, os_lib);
        } else {
            /* Fallback: assume clang is in PATH */
            snprintf(link_cmd, sizeof(link_cmd),
                     "clang -o \"%s\" \"%s\" %s %s %s"
                     " -llegacy_stdio_definitions -lucrt"
                     " -Xlinker /NODEFAULTLIB:libucrt.lib"
                     " -Xlinker /NODEFAULTLIB:libcmt.lib",
                     exe_path, obj_path, mc_lib, prof_lib, os_lib);
        }
    }
#else
    snprintf(link_cmd, sizeof(link_cmd),
             "cc \"%s\" -o \"%s\" %s %s %s -lm", obj_path, exe_path, mc_lib, prof_lib, os_lib);
#endif

    printf("Linking: %s\n", link_cmd);
    int link_result = system(link_cmd);

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

/* Dump LLVM IR without compiling */
static int cmd_emit_ir(const char *path) {
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
    ctx.aot_entry = true;  /* emit-ir is an AOT path: forward argc/argv (bug #22) */
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

    if (codegen_compile(&ctx, ast, reg) != 0) {
        codegen_destroy(&ctx); module_registry_free(reg);
        ast_free(ast); free(source); return 1;
    }

    codegen_dump_ir(&ctx);

    codegen_destroy(&ctx);
    module_registry_free(reg);
    ast_free(ast);
    free(source);
    return 0;
}

static void usage(void) {
    fprintf(stderr,
        "Usage: ls <command> [options]\n"
        "\n"
        "Commands:\n"
        "  tokens <file>              Print token stream\n"
        "  parse <file>               Parse and print AST\n"
        "  check <file>               Parse and type-check\n"
        "  emit-ir <file>             Emit LLVM IR to stdout\n"
        "  compile <file> [-o out]    Compile to executable\n"
        "  run <file>                 JIT execute (Phase 5)\n"
        "  run --optimize <file>      JIT with O2 optimization (inlining+vectorize)\n"
        "  repl                       Interactive REPL (Phase 5)\n"
    );
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "tokens") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: 'tokens' requires a file path\n");
            return 1;
        }
        return cmd_tokens(argv[2]);
    }

    if (strcmp(cmd, "parse") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: 'parse' requires a file path\n");
            return 1;
        }
        return cmd_parse(argv[2]);
    }

    if (strcmp(cmd, "check") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: 'check' requires a file path\n");
            return 1;
        }
        return cmd_check(argv[2]);
    }

    if (strcmp(cmd, "emit-ir") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: 'emit-ir' requires a file path\n");
            return 1;
        }
        return cmd_emit_ir(argv[2]);
    }

    if (strcmp(cmd, "compile") == 0) {
        const char *output = NULL;
        const char *file = NULL;
        bool dump_ir = false;
        bool memcheck = false;
        bool profile = false;
        for (int i = 2; i < argc; i++) {
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
        return cmd_compile(file, output, dump_ir, memcheck, profile);
    }

    if (strcmp(cmd, "run") == 0) {
        bool memcheck = false;
        bool profile = false;
        bool optimize = false;
        const char *file = NULL;
        int file_idx = -1;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--memcheck") == 0) memcheck = true;
            else if (strcmp(argv[i], "--profile") == 0) profile = true;
            else if (strcmp(argv[i], "--optimize") == 0 || strcmp(argv[i], "-O") == 0) optimize = true;
            else if (file == NULL) { file = argv[i]; file_idx = i; }
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
            extern void __ls_set_args(int, char **);
            __ls_set_args(script_argc, script_argv);
        }
        if (memcheck) return jit_run_file_memcheck(file);
        if (profile) return jit_run_file_profile(file);
        if (optimize) return jit_run_file_optimize(file);
        return jit_run_file(file);
    }

    if (strcmp(cmd, "repl") == 0) {
        return jit_repl();
    }

    fprintf(stderr, "error: unknown command '%s'\n", cmd);
    usage();
    return 1;
}
