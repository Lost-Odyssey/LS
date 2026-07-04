# test_array_lit_own.cmake — array-literal element ownership & single evaluation
# (2026-07-04). The AST_ARRAY_LIT const-fold probe used to emit every element
# expression to test LLVMIsConstant, then return NULL on a non-constant element:
# the var-decl fallback re-emitted every element -> double side effects + the
# first emission's owned results leaked. Fixed by an AST pre-scan (pure literals
# only) so the probe emits nothing for non-literal arrays.
# Self-verifying corpus; JIT + AOT + memcheck (0 leak / 0 double-free).
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/array_lit_own_test.lls")
set(_expected "ARRLIT PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "array_lit_own JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "array_lit_own JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "array_lit_own JIT had a failed check\n${jit_out}")
endif()
message(STATUS "array_lit_own JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/array_lit_own_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "array_lit_own AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "array_lit_own AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "array_lit_own AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "array_lit_own AOT had a failed check\n${aot_out}")
endif()
message(STATUS "array_lit_own AOT: OK")

# ---- memcheck (0 leak / 0 double-free) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "array_lit_own memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "array_lit_own memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "array_lit_own memcheck: OK clean")
