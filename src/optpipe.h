/* optpipe.h — centralized LLVM optimization-pipeline & target-machine config.
 *
 * Single source of truth for "how hard do we optimize, and for which CPU",
 * shared by the AOT path (codegen_emit_object) and the JIT path (jit.c).
 * Before this module both paths hardcoded `default<O2>` with a "generic" CPU
 * and empty features — which on x86-64 caps the vectorizer at SSE2 (128-bit)
 * and offers no -O level control. See docs/plan_opt_pipeline.md. */
#ifndef LS_OPTPIPE_H
#define LS_OPTPIPE_H

#include <stdbool.h>
#include <llvm-c/Types.h>
#include <llvm-c/TargetMachine.h>

typedef enum {
    LS_OPT_O0 = 0,  /* no IR passes; backend CodeGenOptLevel = None */
    LS_OPT_O1,
    LS_OPT_O2,      /* default */
    LS_OPT_O3,
    LS_OPT_OS,      /* -Os: optimize for size */
    LS_OPT_OZ,      /* -Oz: aggressively minimize size */
} LsOptLevel;

typedef struct {
    LsOptLevel level;
    bool native;       /* true: target the host CPU + features (unlocks AVX, etc.);
                          false: portable "generic" baseline. JIT is always native. */
    const char *target_cpu; /* NULL: follow `native`/generic above. Non-NULL (e.g.
                          "graniterapids"): emit for that named CPU and its full
                          default feature set (AVX-512/AMX/...). The CPU name alone
                          drives the feature set, like clang -mcpu=<name>. This is
                          the AOT cross-target knob (--target / LS_TARGET); the
                          product is run on real HW or under Intel SDE. The special
                          value "native" means the host; "generic" means baseline.
                          ALWAYS NULL for JIT — you cannot JIT foreign ISA and run
                          it on this host (see docs/plan_simd.md §5). */
    bool verify_each;  /* debug: verify the module after each pass */
} LsOptConfig;

/* Sensible defaults. AOT: O2 + generic (portable). JIT: O2 + native (output
   never leaves this machine). Both honour env overrides:
   LS_OPT=0..3|s|z, LS_NATIVE=0|1, LS_VERIFY_EACH=1. */
LsOptConfig ls_opt_default_aot(void);
LsOptConfig ls_opt_default_jit(void);

/* Parse a CLI token ("-O0".."-O3","-Os","-Oz"). Returns true and writes *out on
   match; false (out untouched) otherwise. "-O" alone is treated as O2. */
bool ls_opt_parse_flag(const char *arg, LsOptLevel *out);

/* level -> "default<On>" pass pipeline string. Returns NULL for O0 (skip passes). */
const char *ls_opt_pass_string(LsOptLevel level);

/* level -> backend codegen opt level. */
LLVMCodeGenOptLevel ls_opt_codegen_level(LsOptLevel level);

/* Create a TargetMachine for `triple`, configured per cfg (CPU/features from the
   host when cfg->native, else generic; CodeGenOptLevel from cfg->level).
   Caller owns the result (LLVMDisposeTargetMachine). NULL on failure. */
LLVMTargetMachineRef ls_opt_create_target_machine(const char *triple,
                                                  const LsOptConfig *cfg);

/* Run the IR optimization pipeline on `module` using `tm`, per cfg.
   No-op for O0. Returns 0 on success, -1 on a pass error (message to stderr). */
int ls_opt_run_passes(LLVMModuleRef module, LLVMTargetMachineRef tm,
                      const LsOptConfig *cfg);

#endif /* LS_OPTPIPE_H */
