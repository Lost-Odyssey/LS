# test_generics_g1.cmake — Phase G1: user-defined generic structs (JIT + AOT + memcheck)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/generics_g1_test.ls")

# Expected output lines (order matters for memcheck; line matching uses regex)
set(_expected
    "42" "hello"
    "10" "20"
    "99" "world"
    "7" "3.140000" "true"
    "count" "100" "ratio" "0.500000"
    "1" "two" "true"
    "five" "5")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "G1 JIT run FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "G1 JIT FAILED: missing line '${_line}'\nstdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "test_generics_g1 JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/generics_g1_test_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "G1 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    RESULT_VARIABLE aot_run_rc
    ERROR_VARIABLE  aot_run_err
)
# Note: exit code not checked — CG_DEBUG builds may leave printf return value in rax
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "G1 AOT FAILED: missing line '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "test_generics_g1 AOT: OK")

# ---- JIT memcheck ----
set(ENV{LS_MEMCHECK_STRICT} "1")
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    RESULT_VARIABLE mc_rc
    ERROR_VARIABLE  mc_err
)
set(ENV{LS_MEMCHECK_STRICT} "")
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "G1 JIT memcheck FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
message(STATUS "test_generics_g1 JIT memcheck: 0 leaks OK")
