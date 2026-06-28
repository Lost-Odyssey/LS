# test_vec_global_drop.cmake — VR-LIM-002 global user Vec(T) cleanup.

cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE OR NOT WORK_DIR)
    message(FATAL_ERROR "test_vec_global_drop.cmake requires LS_EXE, SAMPLE, WORK_DIR")
endif()

if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _jit_out ERROR_VARIABLE _jit_err RESULT_VARIABLE _jit_rc
)
if(NOT _jit_rc EQUAL 0)
    message(FATAL_ERROR "vec_global_drop JIT failed (rc=${_jit_rc})\nstderr:\n${_jit_err}")
endif()
if(_jit_out MATCHES "FAIL" OR NOT _jit_out MATCHES "VEC_GLOBAL_DROP PASS")
    message(FATAL_ERROR "vec_global_drop JIT bad output:\n${_jit_out}")
endif()

execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE _mc_out ERROR_VARIABLE _mc_err RESULT_VARIABLE _mc_rc
)
if(NOT _mc_rc EQUAL 0)
    message(FATAL_ERROR "vec_global_drop memcheck run failed (rc=${_mc_rc})\nstderr:\n${_mc_err}")
endif()
if(NOT _mc_err MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "vec_global_drop memcheck SUMMARY mismatch\nstderr:\n${_mc_err}")
endif()

set(_aot_bin "${WORK_DIR}/vec_global_drop_aot")
if(WIN32)
    set(_aot_bin "${_aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${_aot_bin}"
    RESULT_VARIABLE _aot_rc ERROR_VARIABLE _aot_err
)
if(NOT _aot_rc EQUAL 0)
    message(FATAL_ERROR "vec_global_drop AOT compile failed:\n${_aot_err}")
endif()
execute_process(
    COMMAND "${_aot_bin}"
    OUTPUT_VARIABLE _aot_out RESULT_VARIABLE _aot_run_rc
)
file(REMOVE "${_aot_bin}")
if(NOT _aot_run_rc EQUAL 0)
    message(FATAL_ERROR "vec_global_drop AOT run failed (rc=${_aot_run_rc})\nstdout:\n${_aot_out}")
endif()
if(_aot_out MATCHES "FAIL" OR NOT _aot_out MATCHES "VEC_GLOBAL_DROP PASS")
    message(FATAL_ERROR "vec_global_drop AOT bad output:\n${_aot_out}")
endif()

message(STATUS "vec_global_drop: JIT + AOT + memcheck PASS")
