/* debug.c — LLVM IR printing and diagnostic tools */
#include "debug.h"
#include <stdio.h>

/* Dump LLVM IR to a file */
int debug_dump_ir_to_file(CodegenContext *ctx, const char *path) {
    char *ir = LLVMPrintModuleToString(ctx->module);
    if (ir == NULL) return -1;

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        LLVMDisposeMessage(ir);
        return -1;
    }
    fputs(ir, f);
    fclose(f);
    LLVMDisposeMessage(ir);
    return 0;
}

/* Verify module and print diagnostics */
int debug_verify_module(CodegenContext *ctx) {
    char *error = NULL;
    if (LLVMVerifyModule(ctx->module, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, "Module verification failed:\n%s\n", error);
        LLVMDisposeMessage(error);
        return -1;
    }
    LLVMDisposeMessage(error);
    printf("Module verification passed.\n");
    return 0;
}
