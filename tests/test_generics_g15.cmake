# test_generics_g15.cmake — Phase G1.5: generic impl blocks (JIT + AOT + memcheck)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/generics_g15_test.lls")

# Expected output lines
set(_expected
    "true"          # is_empty() before push
    "3"             # len() after 3 pushes
    "30"            # peek() → Some(30)
    "30"            # pop()  → Some(30)
    "2"             # len() after pop
    "3"             # stack_string len
    "gamma"         # peek()
    "gamma"         # pop()
    "2"             # len after pop
    "false"         # is_empty()
    "99"            # pair first_val
    "hello"         # pair second_val
    "true"          # bool pair first_val
    "42"            # bool pair second_val
)

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "G1.5 JIT run FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "G1.5 JIT FAILED: missing line '${_line}'\nstdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "test_generics_g15 JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/generics_g15_test_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "G1.5 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    RESULT_VARIABLE aot_run_rc
    ERROR_VARIABLE  aot_run_err
)
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "G1.5 AOT FAILED: missing line '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "test_generics_g15 AOT: OK")

# ---- JIT memcheck ----
set(ENV{LS_MEMCHECK_STRICT} "1")
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    RESULT_VARIABLE mc_rc
    ERROR_VARIABLE  mc_err
)
set(ENV{LS_MEMCHECK_STRICT} "")
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "G1.5 JIT memcheck FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
message(STATUS "test_generics_g15 JIT memcheck: 0 leaks OK")
