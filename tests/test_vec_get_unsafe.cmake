# test_vec_get_unsafe.cmake — vec.get_unsafe(i) unchecked index load.
# JIT + AOT + memcheck (string elems must not double-free).
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/vec_get_unsafe_test.ls")
set(_expected "VEC_GET_UNSAFE PASS")

execute_process(COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "vec_get_unsafe JIT FAILED (rc=${jit_rc})\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "vec_get_unsafe JIT missing '${_expected}'\n${jit_out}")
endif()
message(STATUS "vec_get_unsafe JIT: OK")

set(aot_bin "${WORK_DIR}/vec_get_unsafe_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "vec_get_unsafe AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "vec_get_unsafe AOT run FAILED (rc=${aot_run_rc})")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "vec_get_unsafe AOT missing '${_expected}'\n${aot_out}")
endif()
message(STATUS "vec_get_unsafe AOT: OK")
file(REMOVE "${aot_bin}")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "vec_get_unsafe memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "vec_get_unsafe memcheck leak\n${mc_err}")
endif()
message(STATUS "vec_get_unsafe memcheck: OK clean")
message(STATUS "test_vec_get_unsafe: ALL PASSED")
