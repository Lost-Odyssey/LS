/* ffi.h — Cross-platform dynamic library loading for C FFI */
#ifndef LS_FFI_H
#define LS_FFI_H

#include "common.h"

/* ---- Platform-specific dynamic library types ---- */

#ifdef _WIN32
  /* Use opaque pointer to avoid pulling in windows.h here.
     The actual HMODULE is a void* in practice. */
  typedef void *ffi_handle_t;
#else
  typedef void *ffi_handle_t;
#endif

/* ---- FFI Library ---- */

typedef struct {
    ffi_handle_t handle;
    char *path;          /* path used to load (owned) */
} FfiLibrary;

/* Load a dynamic library by path. Returns NULL on failure.
   The caller owns the returned FfiLibrary and must call ffi_unload(). */
FfiLibrary *ffi_load(const char *path);

/* Unload a dynamic library and free the FfiLibrary struct. */
void ffi_unload(FfiLibrary *lib);

/* Look up a symbol (function) in a loaded library.
   Returns the symbol address, or NULL if not found. */
void *ffi_symbol(FfiLibrary *lib, const char *name);

/* Return the last platform-specific error message (static buffer). */
const char *ffi_error(void);

/* ---- Runtime helpers callable from JIT/AOT compiled code ---- */

/* These functions have C linkage and can be called from LLVM-generated code.
   They use opaque pointers (void*) as library handles at the IR level. */

/* Load a library at runtime (called from compiled code).
   Returns an opaque handle, or NULL on failure. */
void *ls_ffi_load(const char *path);

/* Unload a library at runtime. */
void ls_ffi_unload(void *handle);

/* Look up a symbol at runtime. Returns function pointer or NULL. */
void *ls_ffi_symbol(void *handle, const char *name);

#endif /* LS_FFI_H */
