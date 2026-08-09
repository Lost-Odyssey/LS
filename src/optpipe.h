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

/* Read LS_OPT as an explicit user request. Returns true and writes *out only
   when the variable is set to a value the level parser accepts ("0".."3","s",
   "z"); returns false (out untouched) when it is unset, empty, or malformed.
   Unlike ls_opt_default_{aot,jit}, this reports *absence* instead of
   substituting a fallback level, which is what a caller whose own default is
   not O2 needs -- notably `lls run`, where the default is deliberately O0
   (docs/plan_jit_tiering.md: enabling any level costs +80~150% end to end).
   Use this, not ls_opt_default_jit().level, to honour LS_OPT there. */
bool ls_opt_env_level(LsOptLevel *out);

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

/* --- CLI codegen-flag convergence (Task 5.1) ---
 *
 * The `ir`/`asm`/`emit-ir`/`compile`/`run` subcommands each accept the same
 * family of codegen flags (-O<n>, --native, --target=<cpu>|--target <cpu>,
 * -g) but historically re-implemented the per-token parsing loop four times,
 * and the copies had already drifted (e.g. only some recognized --target).
 * CodegenFlags + parse_codegen_flags() are the single shared implementation;
 * each subcommand's own argv loop calls parse_codegen_flags() once per
 * position and, on a non-zero return, advances by that many slots. */
typedef struct {
    LsOptLevel opt_level;   /* meaningful only when opt_set is true */
    bool opt_set;           /* true iff an explicit -O<n> token was seen */
    bool native;            /* --native */
    const char *target;     /* --target=<cpu> / --target <cpu>; NULL = unset */
    bool debug_info;        /* -g */
} CodegenFlags;

/* Try to parse a codegen flag at argv[i]. On a match, updates *out and
   returns the number of argv slots consumed (1, or 2 for the space-separated
   "--target <cpu>" form). Returns 0 (leaving *out untouched) if argv[i] is
   not a recognized codegen flag, so callers can fall through to their own
   command-specific flags/positional handling. */
int parse_codegen_flags(int argc, char **argv, int i, CodegenFlags *out);

#endif /* LS_OPTPIPE_H */
