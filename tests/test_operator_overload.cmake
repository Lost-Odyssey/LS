# test_operator_overload.cmake — operator overloading end-to-end (JIT + AOT)
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/operator_overload_demo.ls")

# c=(4,6) e=(3,8) a==b=false a!=b=true a<b=true a>b=false total=(8,12)
set(_expected "4.000000" "6.000000" "3.000000" "8.000000" "false" "true" "12.000000")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "operator_overload JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "(^|\n).*${_line}")
        message(FATAL_ERROR
            "operator_overload JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "test_operator_overload JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/operator_overload_demo_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "operator_overload AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    RESULT_VARIABLE aot_run_rc
    ERROR_VARIABLE  aot_run_err
)
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "(^|\n).*${_line}")
        message(FATAL_ERROR
            "operator_overload AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "test_operator_overload AOT: OK")

file(REMOVE "${aot_bin}")
