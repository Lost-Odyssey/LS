# test_global_vec_lit.cmake — BF-042: global POD vec literal initializer
# (JIT + AOT + memcheck). Locks the fix for `vec(int) g = [1,2,3]` at global
# scope reading back empty/0.
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/global_vec_lit/main.ls")
set(_expected "sum=6" "sum2=10" "GLOBAL_VEC_LIT PASS")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "global_vec_lit JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "global_vec_lit JIT missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "global_vec_lit JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/global_vec_lit_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "global_vec_lit AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "global_vec_lit AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "global_vec_lit AOT missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "global_vec_lit AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck (vec data must be freed at exit — 0 leak / 0 dfree) ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "global_vec_lit memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "global_vec_lit --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "global_vec_lit memcheck: OK clean")

message(STATUS "test_global_vec_lit: ALL PASSED")
