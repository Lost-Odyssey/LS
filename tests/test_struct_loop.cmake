# test_struct_loop.cmake — bug #24: struct literal alloca in loop body caused
# JIT stack overflow. n=200000 would crash before fix. JIT + AOT + memcheck.
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/struct_loop_test.lls")
set(_expected "STRUCT_LOOP PASS")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "struct_loop JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "struct_loop JIT missing '${_expected}'\nstdout:\n${jit_out}")
endif()
message(STATUS "struct_loop JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/struct_loop_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "struct_loop AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "struct_loop AOT run FAILED (rc=${aot_run_rc})")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "struct_loop AOT missing '${_expected}'\nstdout:\n${aot_out}")
endif()
message(STATUS "struct_loop AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "struct_loop memcheck FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "struct_loop memcheck leak\nstderr:\n${mc_err}")
endif()
message(STATUS "struct_loop memcheck: OK clean")

message(STATUS "test_struct_loop: ALL PASSED")
