/* optpipe.c — see optpipe.h / docs/plan_opt_pipeline.md */
#include "optpipe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/Error.h>
#include <llvm-c/Transforms/PassBuilder.h>

/* Parse the LS_OPT env value ("0".."3","s","z") into a level. */
static LsOptLevel parse_level_str(const char *s, LsOptLevel fallback) {
    if (s == NULL || s[0] == '\0') return fallback;
    if (strcmp(s, "0") == 0) return LS_OPT_O0;
    if (strcmp(s, "1") == 0) return LS_OPT_O1;
    if (strcmp(s, "2") == 0) return LS_OPT_O2;
    if (strcmp(s, "3") == 0) return LS_OPT_O3;
    if (strcmp(s, "s") == 0 || strcmp(s, "S") == 0) return LS_OPT_OS;
    if (strcmp(s, "z") == 0 || strcmp(s, "Z") == 0) return LS_OPT_OZ;
    return fallback;
}

/* An env flag is "on" unless it is set and starts with '0'. */
static bool env_flag(const char *name, bool fallback) {
    const char *v = getenv(name);
    if (v == NULL || v[0] == '\0') return fallback;
    return v[0] != '0';
}

/* LS_TARGET="<cpu>" cross-targets AOT codegen at a named CPU (e.g. graniterapids).
   Empty/unset -> NULL (follow native/generic). The CLI --target overrides this. */
static const char *env_target_cpu(void) {
    const char *v = getenv("LS_TARGET");
    if (v == NULL || v[0] == '\0') return NULL;
    return v;
}

LsOptConfig ls_opt_default_aot(void) {
    LsOptConfig c;
    c.level = parse_level_str(getenv("LS_OPT"), LS_OPT_O2);
    c.native = env_flag("LS_NATIVE", false);
    c.target_cpu = env_target_cpu();
    c.verify_each = env_flag("LS_VERIFY_EACH", false);
    return c;
}

LsOptConfig ls_opt_default_jit(void) {
    LsOptConfig c;
    c.level = parse_level_str(getenv("LS_OPT"), LS_OPT_O2);
    c.native = true;  /* JIT output never leaves this machine */
    c.target_cpu = NULL;  /* JIT always targets the host; cannot run foreign ISA */
    c.verify_each = env_flag("LS_VERIFY_EACH", false);
    return c;
}

bool ls_opt_parse_flag(const char *arg, LsOptLevel *out) {
    if (arg == NULL || arg[0] != '-' || arg[1] != 'O') return false;
    const char *rest = arg + 2;
    if (rest[0] == '\0') { *out = LS_OPT_O2; return true; }  /* "-O" == -O2 */
    if (strcmp(rest, "0") == 0) { *out = LS_OPT_O0; return true; }
    if (strcmp(rest, "1") == 0) { *out = LS_OPT_O1; return true; }
    if (strcmp(rest, "2") == 0) { *out = LS_OPT_O2; return true; }
    if (strcmp(rest, "3") == 0) { *out = LS_OPT_O3; return true; }
    if (strcmp(rest, "s") == 0) { *out = LS_OPT_OS; return true; }
    if (strcmp(rest, "z") == 0) { *out = LS_OPT_OZ; return true; }
    return false;
}

const char *ls_opt_pass_string(LsOptLevel level) {
    switch (level) {
        case LS_OPT_O0: return NULL;
        case LS_OPT_O1: return "default<O1>";
        case LS_OPT_O2: return "default<O2>";
        case LS_OPT_O3: return "default<O3>";
        case LS_OPT_OS: return "default<Os>";
        case LS_OPT_OZ: return "default<Oz>";
    }
    return "default<O2>";
}

LLVMCodeGenOptLevel ls_opt_codegen_level(LsOptLevel level) {
    switch (level) {
        case LS_OPT_O0: return LLVMCodeGenLevelNone;
        case LS_OPT_O1: return LLVMCodeGenLevelLess;
        case LS_OPT_O2:
        case LS_OPT_OS:
        case LS_OPT_OZ: return LLVMCodeGenLevelDefault;
        case LS_OPT_O3: return LLVMCodeGenLevelAggressive;
    }
    return LLVMCodeGenLevelDefault;
}

LLVMTargetMachineRef ls_opt_create_target_machine(const char *triple,
                                                  const LsOptConfig *cfg) {
    LLVMTargetRef target = NULL;
    char *err = NULL;
    if (LLVMGetTargetFromTriple(triple, &target, &err)) {
        fprintf(stderr, "[opt] target lookup failed for '%s': %s\n",
                triple, err ? err : "(unknown)");
        if (err) LLVMDisposeMessage(err);
        return NULL;
    }

    /* CPU/features selection, in priority order:
         1. target_cpu = an explicit CPU name (e.g. "graniterapids") -> emit for it;
            the CPU name drives the full default feature set (like -mcpu=<name>), so
            features="" already unlocks that CPU's AVX-512/AMX/etc. (AOT cross-target).
         2. native (or target_cpu=="native") -> host CPU + host features.
         3. otherwise -> portable "generic" baseline.
       LLVMGetHostCPU* return malloc'd strings that must be disposed. */
    char *host_cpu = NULL;
    char *host_feat = NULL;
    const char *cpu = "generic";
    const char *features = "";
    const char *tc = cfg->target_cpu;
    bool tc_named = (tc && tc[0] && strcmp(tc, "native") != 0 && strcmp(tc, "generic") != 0);
    bool want_host = cfg->native || (tc && strcmp(tc, "native") == 0);
    if (tc_named) {
        cpu = tc;            /* e.g. "graniterapids" / "graniterapids-d" */
        features = "";       /* CPU name selects its own feature set */
    } else if (want_host) {
        host_cpu = LLVMGetHostCPUName();
        host_feat = LLVMGetHostCPUFeatures();
        if (host_cpu) cpu = host_cpu;
        if (host_feat) features = host_feat;
    }

    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, cpu, features,
        ls_opt_codegen_level(cfg->level),
        LLVMRelocDefault, LLVMCodeModelDefault);

    if (host_cpu) LLVMDisposeMessage(host_cpu);
    if (host_feat) LLVMDisposeMessage(host_feat);
    return tm;
}

int ls_opt_run_passes(LLVMModuleRef module, LLVMTargetMachineRef tm,
                      const LsOptConfig *cfg) {
    const char *passes = ls_opt_pass_string(cfg->level);
    if (passes == NULL) return 0;  /* O0: no IR passes */

    LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
    if (cfg->verify_each)
        LLVMPassBuilderOptionsSetVerifyEach(opts, 1);

    int rc = 0;
    LLVMErrorRef err = LLVMRunPasses(module, passes, tm, opts);
    if (err) {
        char *msg = LLVMGetErrorMessage(err);
        fprintf(stderr, "[opt] pass pipeline '%s' failed: %s\n",
                passes, msg ? msg : "(unknown)");
        LLVMDisposeErrorMessage(msg);
        rc = -1;
    }
    LLVMDisposePassBuilderOptions(opts);
    return rc;
}
