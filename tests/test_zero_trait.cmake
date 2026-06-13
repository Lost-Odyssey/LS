# test_zero_trait.cmake — Phase 0 of std.complex/std.fft: compiler foundation for
# `T.zero()`. Static trait methods + static-method dispatch on a generic type
# parameter. JIT + AOT + memcheck 0/0/0.
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(POS "${SAMPLE_DIR}/zero_trait_smoke.ls")
set(_expected "ZERO_TRAIT PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "zero_trait JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "zero_trait JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "zero_trait JIT has FAIL lines\n${jit_out}")
endif()
message(STATUS "zero_trait JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/zero_trait_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "zero_trait AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "zero_trait AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "zero_trait AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "zero_trait AOT has FAIL lines\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "zero_trait AOT: OK")

# ---- memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "zero_trait memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "zero_trait memcheck leak\n${mc_err}")
endif()
message(STATUS "zero_trait memcheck: OK clean")

message(STATUS "test_zero_trait: ALL PASSED")
