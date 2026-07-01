# test_opt_owned_rvalue.cmake — owned (has_drop Str) Option/Result combinator
# results consumed as bare rvalues + the identity-closure combinator.
# Guards two L-013-family fixes (both previously failed memcheck):
#   1. owned combinator result (unwrap_or / `!` / expect, lowered to
#      AST_MATCH / AST_FORCE_UNWRAP) consumed as print arg / discarded /
#      chained receiver was LEAKED (consumer whitelists missed the lowered
#      nodes — fixed in codegen_expr.c / codegen_stmt.c).
#   2. `map(|x| x)` identity closure lowers to `Some({ x })`; cg_store_owned
#      now peels the block to recognize the moved binder — was DOUBLE-FREE.
# JIT + AOT + memcheck (0 leak / 0 double-free).
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/opt_owned_rvalue_test.lls")
set(_expected "OPTOWN PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "opt_owned_rvalue JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "opt_owned_rvalue JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL:")
    message(FATAL_ERROR "opt_owned_rvalue JIT had a failed check\n${jit_out}")
endif()
message(STATUS "opt_owned_rvalue JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/opt_owned_rvalue_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "opt_owned_rvalue AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "opt_owned_rvalue AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "opt_owned_rvalue AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL:")
    message(FATAL_ERROR "opt_owned_rvalue AOT had a failed check\n${aot_out}")
endif()
message(STATUS "opt_owned_rvalue AOT: OK")

# ---- memcheck (0 leak / 0 double-free) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "opt_owned_rvalue memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "opt_owned_rvalue memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "opt_owned_rvalue memcheck: OK clean")
