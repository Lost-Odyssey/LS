# test_string_loop.cmake — bug #26: string method temp allocas in loop → JIT
# stack overflow. n=200000 would crash before the entry-block-alloca fix.
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/string_loop_test.ls")
set(_expected "STRING_LOOP PASS")

execute_process(COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "string_loop JIT FAILED (rc=${jit_rc})\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "string_loop JIT missing '${_expected}'\n${jit_out}")
endif()
message(STATUS "string_loop JIT: OK")

set(aot_bin "${WORK_DIR}/string_loop_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "string_loop AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "string_loop AOT run FAILED (rc=${aot_run_rc})")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "string_loop AOT missing '${_expected}'\n${aot_out}")
endif()
message(STATUS "string_loop AOT: OK")
file(REMOVE "${aot_bin}")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "string_loop memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "string_loop memcheck leak\n${mc_err}")
endif()
message(STATUS "string_loop memcheck: OK clean")
message(STATUS "test_string_loop: ALL PASSED")
