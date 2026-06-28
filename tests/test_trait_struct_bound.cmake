# test_trait_struct_bound.cmake — Step 13: trait bounds on generic structs (JIT + AOT + negative)
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/trait_struct_bound_test.ls")
set(_expected "Circle" "5" "Circle" "9")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "trait_struct_bound JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "(^|\n).*${_line}")
        message(FATAL_ERROR
            "trait_struct_bound JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "test_trait_struct_bound JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/trait_struct_bound_test_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "trait_struct_bound AOT compile FAILED:\n${aot_err}")
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
            "trait_struct_bound AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "test_trait_struct_bound AOT: OK")

file(REMOVE "${aot_bin}")

# ---- Negative: type doesn't satisfy bound ----
set(REJECT "${SAMPLE_DIR}/trait_struct_bound_reject.ls")
execute_process(
    COMMAND "${LS_EXE}" run "${REJECT}"
    OUTPUT_VARIABLE rej_out
    ERROR_VARIABLE  rej_err
    RESULT_VARIABLE rej_rc
)
if(rej_rc EQUAL 0)
    message(FATAL_ERROR "trait_struct_bound_reject should have FAILED but passed")
endif()
if(NOT "${rej_err}" MATCHES "does not satisfy interface")
    message(FATAL_ERROR
        "trait_struct_bound_reject: expected 'does not satisfy trait' error\nstderr:\n${rej_err}")
endif()
message(STATUS "test_trait_struct_bound REJECT: OK")
