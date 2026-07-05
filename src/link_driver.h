/* link_driver.h — AOT link step (extracted from main.c's cmd_compile, W4):
   locate the runtime archives next to the lls executable, build the platform
   link command, and run it. */
#ifndef LS_LINK_DRIVER_H
#define LS_LINK_DRIVER_H

#include <stdbool.h>

typedef struct {
    const char *exe_path;   /* output executable path */
    const char *obj_path;   /* input object file path */
    bool memcheck;          /* --memcheck: link ls_memcheck archive */
    bool profile;           /* --profile: link ls_profiler archive */
    bool debug_info;        /* -g: pass -g to the linker driver (PDB/DWARF) */
} LinkConfig;

/* Build and run the link command (prints "Linking: <cmd>" first, exactly as
   cmd_compile always did). Returns the linker's raw exit status from
   system() — 0 on success; the caller decides how to report failure. */
int link_driver_link(const LinkConfig *cfg);

#endif /* LS_LINK_DRIVER_H */
