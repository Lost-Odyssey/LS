/* debug.h — LLVM IR printing and diagnostic tools */
#ifndef LS_DEBUG_H
#define LS_DEBUG_H

#include "codegen.h"

/* Dump LLVM IR of the module to a file */
int debug_dump_ir_to_file(CodegenContext *ctx, const char *path);

/* Verify the module and print any issues */
int debug_verify_module(CodegenContext *ctx);

#endif /* LS_DEBUG_H */
