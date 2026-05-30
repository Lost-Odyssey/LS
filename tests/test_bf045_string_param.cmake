# test_bf045_string_param.cmake — BF-045: owned string param stored into a returned
# struct field / returned directly must be cloned (param is a cap=-2 borrow). Was
# AOT garbage / JIT lucky-UAF. JIT + AOT + memcheck (the AOT path is the key one).
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/bf045_string_param/main.ls")
set(_expected "x=HELLO y=WORLD w=WIDGET" "BF045 PASS")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "bf045 JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "bf045 JIT missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "bf045 JIT: OK")

# ---- AOT (the path that exposed the bug) ----
set(aot_bin "${WORK_DIR}/bf045_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "bf045 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "bf045 AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "bf045 AOT FAILED (likely the bug): missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "bf045 AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "bf045 memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "bf045 --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "bf045 memcheck: OK clean")

message(STATUS "test_bf045_string_param: ALL PASSED")
