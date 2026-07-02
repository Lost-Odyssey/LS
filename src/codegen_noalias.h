/* codegen_noalias.h — A4: function-scoped `noalias` recovery pass.
 *
 * Upgrades the "ls-noalias-cand" string-attribute markers that codegen puts
 * on writable-borrow (&!T / &!self) parameters into real `noalias` attributes
 * — but only on functions whose whole visible call tree provably stays on
 * the current thread (no atomics, no indirect/inline-asm calls, no unknown
 * externals, transitively). All markers are stripped, so the pass is
 * idempotent. See codegen_noalias.c for the full rules and soundness notes;
 * design doc: docs/plan_opt_noalias_recovery.md.
 *
 * Call AFTER the module is fully emitted and BEFORE optimization. Wired into
 * ls_opt_run_passes (AOT / JIT file mode / optimized emit-ir & emit-asm) and
 * cmd_emit_ir (so unoptimized dumps show the result too).
 *
 * LS_NO_NOALIAS=1 disables the recovery (markers still stripped). */
#ifndef LS_CODEGEN_NOALIAS_H
#define LS_CODEGEN_NOALIAS_H

#include <llvm-c/Types.h>

void ls_noalias_recover(LLVMModuleRef module);

#endif
