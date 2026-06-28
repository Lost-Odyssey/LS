# test_trait_constraint.cmake — Trait constraint tests (JIT + AOT + negative)
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/trait_constraint_test.ls")

set(_expected "5" "circle" "square" "circle" "5" "99")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "trait_constraint JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "trait_constraint JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "test_trait_constraint JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/trait_constraint_test_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "trait_constraint AOT compile FAILED:\n${aot_err}")
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
            "trait_constraint AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "test_trait_constraint AOT: OK")

file(REMOVE "${aot_bin}")

# ---- Negative: unsatisfied trait bound ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/trait_constraint_reject.ls"
    OUTPUT_VARIABLE rej_out
    ERROR_VARIABLE  rej_err
    RESULT_VARIABLE rej_rc
)
if(rej_rc EQUAL 0)
    message(FATAL_ERROR "trait_constraint_reject should have failed but succeeded")
endif()
if(NOT "${rej_err}" MATCHES "does not satisfy interface")
    message(FATAL_ERROR
        "trait_constraint_reject: expected 'does not satisfy trait' error\nstderr:\n${rej_err}")
endif()
message(STATUS "test_trait_constraint reject: OK")
