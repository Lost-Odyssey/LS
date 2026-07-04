# test_own_rvalue_sites.cmake — OWN-1 unified owned-rvalue predicate regression
# (docs/plan_footgun_remediation.html phase 3, 2026-07-04).
#   The per-site owned-rvalue consumer whitelists (print-Str / f-strings /
#   print-struct / print-enum / discard statement / chained receiver / field-read
#   object spill) had drifted apart; every drift gap was a real leak: combinator
#   lowerings in @print's inline f-string, FIELD-read clones passed to print,
#   AST_TRY at every site, bare f-string / FIELD / array-INDEX / lowered-`+`
#   discards, and owned MATCH/TRY objects spilled for field reads. Unified into
#   cg_expr_yields_owned_rvalue / cg_expr_is_fresh_rvalue_kind.
# Self-verifying corpus; JIT + AOT + memcheck (0 leak / 0 double-free).
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/own_rvalue_sites_test.lls")
set(_expected "OWNRVAL PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "own_rvalue_sites JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "own_rvalue_sites JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "own_rvalue_sites JIT had a failed check\n${jit_out}")
endif()
message(STATUS "own_rvalue_sites JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/own_rvalue_sites_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "own_rvalue_sites AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "own_rvalue_sites AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "own_rvalue_sites AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "own_rvalue_sites AOT had a failed check\n${aot_out}")
endif()
message(STATUS "own_rvalue_sites AOT: OK")

# ---- memcheck (0 leak / 0 double-free) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "own_rvalue_sites memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "own_rvalue_sites memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "own_rvalue_sites memcheck: OK clean")
