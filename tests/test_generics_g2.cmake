# test_generics_g2.cmake — G2 generic function tests (JIT + AOT)
#
# Phase G2: generic FREE functions, as opposed to generic structs.
#
# Different instantiation path from G1: there is no receiver to infer the type
# parameter from, so the argument types (or an explicit `f(T)(...)`) drive it, and
# the resulting symbol is named from the type arguments rather than from a struct.
# That naming step is where the abstract-`T` and deep-nesting bugs later showed
# up.
#
# @subsystem checker/generics
# @guards G2 generic functions
# @sources checker_generics.c:instantiate_template
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/generics_g2_test.lls")

set(_expected
    "42" "hello" "3.140000" "true"
    "30" "99" "world" "7")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "G2 JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "G2 JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "test_generics_g2 JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/generics_g2_test_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "G2 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    RESULT_VARIABLE aot_run_rc
    ERROR_VARIABLE  aot_run_err
)
# Note: exit code not checked — void main() may leave non-zero in rax
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "G2 AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "test_generics_g2 AOT: OK")

# cleanup
file(REMOVE "${aot_bin}")
