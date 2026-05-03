/* main.c — Entry point: CLI parsing, token dump, parse, check, compile */
#include "common.h"
#include "scanner.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"
#include "module.h"
#include "debug.h"
#include "jit.h"

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
    ModuleRegistry *reg = module_registry_new();
    bool ok = checker_check(ast, path, reg);
    if (ok) {
        printf("Type check passed.\n");
    }
    module_registry_free(reg);
    ast_free(ast);
    free(source);
    return ok ? 0 : 1;
}

/* Compile source to object file, then link to executable.
   memcheck: route all malloc/free through ls_mc_* tracker (Phase A; AOT
   currently doesn't link the runtime archive — JIT is the supported path
   for now). */
static int cmd_compile(const char *path, const char *output_path, bool dump_ir,
                       bool memcheck) {
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

    /* Codegen */
    CodegenContext ctx;
    codegen_init(&ctx, path);
    ctx.memcheck_enabled = memcheck;

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

    /* Link to executable */
    char link_cmd[2048];
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
        if (clang) {
            snprintf(link_cmd, sizeof(link_cmd),
                     "cmd.exe /c \"\"%s\" -o \"%s\" \"%s\" -llegacy_stdio_definitions\"",
                     clang, exe_path, obj_path);
        } else {
            /* Fallback: assume clang is in PATH */
            snprintf(link_cmd, sizeof(link_cmd),
                     "clang -o \"%s\" \"%s\" -llegacy_stdio_definitions", exe_path, obj_path);
        }
    }
#else
    snprintf(link_cmd, sizeof(link_cmd),
             "cc \"%s\" -o \"%s\" -lm", obj_path, exe_path);
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

    ModuleRegistry *reg = module_registry_new();
    if (!checker_check(ast, path, reg)) {
        module_registry_free(reg);
        ast_free(ast); free(source); return 1;
    }

    CodegenContext ctx;
    codegen_init(&ctx, path);

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
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
                output = argv[++i];
            } else if (strcmp(argv[i], "--dump-ir") == 0) {
                dump_ir = true;
            } else if (strcmp(argv[i], "--memcheck") == 0) {
                memcheck = true;
            } else if (file == NULL) {
                file = argv[i];
            }
        }
        if (file == NULL) {
            fprintf(stderr, "error: 'compile' requires a file path\n");
            return 1;
        }
        return cmd_compile(file, output, dump_ir, memcheck);
    }

    if (strcmp(cmd, "run") == 0) {
        bool memcheck = false;
        const char *file = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--memcheck") == 0) memcheck = true;
            else if (file == NULL) file = argv[i];
        }
        if (file == NULL) {
            fprintf(stderr, "error: 'run' requires a file path\n");
            return 1;
        }
        return memcheck ? jit_run_file_memcheck(file) : jit_run_file(file);
    }

    if (strcmp(cmd, "repl") == 0) {
        return jit_repl();
    }

    fprintf(stderr, "error: unknown command '%s'\n", cmd);
    usage();
    return 1;
}
