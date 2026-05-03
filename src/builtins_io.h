/* builtins_io.h — Compiler-level built-in `io` module (Phase 9).
 *
 * Exposes a stdlib `io` module that does NOT correspond to a source file.
 * Functions emit IR that calls libc directly (fopen / fread / fwrite /
 * fclose / fseek / ftell). Both AOT and JIT resolve these symbols against
 * the C runtime that ls.exe is already linked against — no FFI overhead.
 *
 * v1 surface (this iteration):
 *   - OpenMode enum: Read / Write / Append / ReadBinary / WriteBinary / AppendBinary
 *   - File struct:   { object handle, bool is_binary }
 *   - One-shot:      io.read_file / io.write_file / io.exists
 *   - Handle-based:  io.open / io.close / io.read_all / io.write
 *
 * Not in v1 (queued for v2): seek/tell/size/rewind/append_file/remove/read_line,
 * SeekFrom enum, method syntax (`f.read_all()`), automatic drop on File.
 */
#ifndef LS_BUILTINS_IO_H
#define LS_BUILTINS_IO_H

#include "ast.h"
#include "types.h"

/* Forward decl — full def in checker.h */
typedef struct Checker Checker;

/* Build the TYPE_MODULE for `io`. Registers the synthesized File struct
   and OpenMode enum into the checker's registries (so type-checking sees
   them as real named types). */
Type *builtin_io_make_type(Checker *c);

/* OpenMode variant indices — codegen uses these to map enum disc → C mode string.
   Indices >= IO_OPEN_READ_BINARY are binary modes (seek/tell/size are gated on this). */
enum {
    IO_OPEN_READ           = 0,
    IO_OPEN_WRITE          = 1,
    IO_OPEN_APPEND         = 2,
    IO_OPEN_READ_BINARY    = 3,
    IO_OPEN_WRITE_BINARY   = 4,
    IO_OPEN_APPEND_BINARY  = 5,
    IO_OPEN_MODE_COUNT
};

/* SeekFrom variant indices — map directly to C SEEK_SET / SEEK_CUR / SEEK_END
   numeric values (0 / 1 / 2). */
enum {
    IO_SEEK_START   = 0,
    IO_SEEK_CURRENT = 1,
    IO_SEEK_END     = 2,
    IO_SEEK_FROM_COUNT
};

/* ---- Codegen API (separate translation unit; LLVM-dependent) ---- */
#ifdef LS_INCLUDE_CODEGEN
#include "codegen.h"

LLVMValueRef builtin_io_emit_call(CodegenContext *ctx, const char *fn_name,
                                  AstNode **args, int arg_count,
                                  Type *result_type);
#endif

#endif /* LS_BUILTINS_IO_H */
