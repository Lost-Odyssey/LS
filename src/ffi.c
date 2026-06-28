/* ffi.c — Cross-platform dynamic library loading implementation */
#ifdef _WIN32
  #include <windows.h>
#endif

#include "ffi.h"

#include <stdio.h>
#include <string.h>

/* ---- Platform-specific implementation ---- */

#ifdef _WIN32

/* Windows: LoadLibrary / GetProcAddress / FreeLibrary */

static char ffi_err_buf[512];

FfiLibrary *ffi_load(const char *path) {
    if (path == NULL) return NULL;

    /* Try loading as-is first */
    HMODULE h = LoadLibraryA(path);

    /* If failed and no extension, try appending .dll */
    if (h == NULL && strchr(path, '.') == NULL) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s.dll", path);
        h = LoadLibraryA(buf);
    }

    if (h == NULL) {
        DWORD err = GetLastError();
        snprintf(ffi_err_buf, sizeof(ffi_err_buf),
                 "LoadLibraryA failed for '%s' (error %lu)", path, (unsigned long)err);
        return NULL;
    }

    FfiLibrary *lib = (FfiLibrary *)malloc_safe(sizeof(FfiLibrary));
    lib->handle = (ffi_handle_t)h;
    lib->path = (char *)malloc_safe(strlen(path) + 1);
    strcpy(lib->path, path);
    return lib;
}

void ffi_unload(FfiLibrary *lib) {
    if (lib == NULL) return;
    if (lib->handle) FreeLibrary((HMODULE)lib->handle);
    free(lib->path);
    free(lib);
}

void *ffi_symbol(FfiLibrary *lib, const char *name) {
    if (lib == NULL || lib->handle == NULL || name == NULL) return NULL;
    void *sym = (void *)GetProcAddress((HMODULE)lib->handle, name);
    if (sym == NULL) {
        DWORD err = GetLastError();
        snprintf(ffi_err_buf, sizeof(ffi_err_buf),
                 "GetProcAddress failed for '%s' (error %lu)", name, (unsigned long)err);
    }
    return sym;
}

const char *ffi_error(void) {
    return ffi_err_buf;
}

#else /* POSIX: dlopen / dlsym / dlclose */

#include <dlfcn.h>

FfiLibrary *ffi_load(const char *path) {
    if (path == NULL) return NULL;

    /* Try loading as-is first */
    void *h = dlopen(path, RTLD_NOW);

    /* If failed and no extension, try appending platform suffix */
    if (h == NULL && strchr(path, '.') == NULL) {
        char buf[512];
#ifdef __APPLE__
        snprintf(buf, sizeof(buf), "%s.dylib", path);
#else
        snprintf(buf, sizeof(buf), "%s.so", path);
#endif
        h = dlopen(buf, RTLD_NOW);
    }

    if (h == NULL) {
        return NULL;
    }

    FfiLibrary *lib = (FfiLibrary *)malloc_safe(sizeof(FfiLibrary));
    lib->handle = h;
    lib->path = (char *)malloc_safe(strlen(path) + 1);
    strcpy(lib->path, path);
    return lib;
}

void ffi_unload(FfiLibrary *lib) {
    if (lib == NULL) return;
    if (lib->handle) dlclose(lib->handle);
    free(lib->path);
    free(lib);
}

void *ffi_symbol(FfiLibrary *lib, const char *name) {
    if (lib == NULL || lib->handle == NULL || name == NULL) return NULL;
    return dlsym(lib->handle, name);
}

const char *ffi_error(void) {
    const char *err = dlerror();
    return err ? err : "";
}

#endif /* _WIN32 */

/* ---- Runtime helpers callable from compiled code ---- */

void *ls_ffi_load(const char *path) {
    FfiLibrary *lib = ffi_load(path);
    if (lib == NULL) {
        fprintf(stderr, "[ffi error] failed to load library '%s': %s\n",
                path ? path : "(null)", ffi_error());
        return NULL;
    }
    /* Return the raw handle. The FfiLibrary wrapper is leaked intentionally
       because compiled code only works with opaque handles.
       We store the handle and free the wrapper. */
    void *handle = (void *)lib->handle;
    free(lib->path);
    free(lib);
    return handle;
}

void ls_ffi_unload(void *handle) {
    if (handle == NULL) return;
#ifdef _WIN32
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

void *ls_ffi_symbol(void *handle, const char *name) {
    if (handle == NULL || name == NULL) return NULL;
#ifdef _WIN32
    void *sym = (void *)GetProcAddress((HMODULE)handle, name);
    if (sym == NULL) {
        fprintf(stderr, "[ffi error] symbol '%s' not found (error %lu)\n",
                name, (unsigned long)GetLastError());
    }
    return sym;
#else
    void *sym = dlsym(handle, name);
    if (sym == NULL) {
        fprintf(stderr, "[ffi error] symbol '%s' not found: %s\n",
                name, dlerror());
    }
    return sym;
#endif
}
