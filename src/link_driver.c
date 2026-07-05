/* link_driver.c — AOT link step (extracted from main.c's cmd_compile, W4).
   Resolves the runtime archives (ls_memcheck / ls_profiler / ls_os_backend)
   next to the lls executable, assembles the platform link command (clang on
   Windows, cc elsewhere), and runs it via system(). Pure code motion from
   main.c — paths, flags and command layout are unchanged. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "link_driver.h"
#include "driver_util.h"

/* Resolve the directory containing the running ls executable.
   Returns 0 on success and writes a NUL-terminated path (no trailing
   separator) to `out`. Used by AOT --memcheck to locate ls_memcheck.lib
   alongside ls.exe. */
static int get_executable_dir(char *out, size_t out_sz) {
    char buf[1024];
    if (get_executable_path(buf, sizeof(buf)) != 0) return -1;
    size_t len = strlen(buf);

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

int link_driver_link(const LinkConfig *cfg) {
    const char *exe_path = cfg->exe_path;
    const char *obj_path = cfg->obj_path;

    /* Resolve ls_memcheck archive path (next to ls.exe) when --memcheck is on.
       If the archive is missing we still attempt the link — the user will get
       a clear "unresolved external ls_mc_alloc" error from the linker, plus
       our warning here. */
    char mc_lib[1280] = "";
    if (cfg->memcheck) {
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
    if (cfg->profile) {
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
           CRT), so the linker needs the dynamic CRT import libraries.
           -g (D1): clang -g makes lld-link collect the object's .debug$S
           CodeView into a PDB next to the exe. */
        if (clang) {
            snprintf(link_cmd, sizeof(link_cmd),
                     "cmd.exe /c \"\"%s\" %s -o \"%s\" \"%s\" %s %s %s"
                     " -llegacy_stdio_definitions -lucrt"
                     " -Xlinker /NODEFAULTLIB:libucrt.lib"
                     " -Xlinker /NODEFAULTLIB:libcmt.lib\"",
                     clang, cfg->debug_info ? "-g" : "",
                     exe_path, obj_path, mc_lib, prof_lib, os_lib);
        } else {
            /* Fallback: assume clang is in PATH */
            snprintf(link_cmd, sizeof(link_cmd),
                     "clang %s -o \"%s\" \"%s\" %s %s %s"
                     " -llegacy_stdio_definitions -lucrt"
                     " -Xlinker /NODEFAULTLIB:libucrt.lib"
                     " -Xlinker /NODEFAULTLIB:libcmt.lib",
                     cfg->debug_info ? "-g" : "",
                     exe_path, obj_path, mc_lib, prof_lib, os_lib);
        }
    }
#else
    /* -lpthread for os_posix.c's ls_thread_* (std.task). Harmless on modern
       glibc (≥2.34, pthread folded into libc); required on older glibc / musl.
       -no-pie: codegen emits objects with LLVMRelocDefault (non-PIC); modern
       distros (Ubuntu >=17.10, etc.) default `cc` to -pie, which rejects the
       absolute relocations in those objects ("recompile with -fPIE"). */
    snprintf(link_cmd, sizeof(link_cmd),
             "cc %s -no-pie \"%s\" -o \"%s\" %s %s %s -lm -lpthread",
             cfg->debug_info ? "-g" : "",
             obj_path, exe_path, mc_lib, prof_lib, os_lib);
#endif

    printf("Linking: %s\n", link_cmd);
    return system(link_cmd);
}
