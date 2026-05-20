# test_trait_builtin_impl.cmake — Step 11: impl trait for builtin types (JIT + AOT)
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/trait_int_impl_test.ls")

# Expected output lines
set(_expected "int:42" "f64:3" "bool:true" "int:99" "f64:2" "bool:false")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "trait_builtin_impl JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "(^|\n).*${_line}")
        message(FATAL_ERROR
            "trait_builtin_impl JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "test_trait_builtin_impl JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/trait_int_impl_test_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "trait_builtin_impl AOT compile FAILED:\n${aot_err}")
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
            "trait_builtin_impl AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "test_trait_builtin_impl AOT: OK")

file(REMOVE "${aot_bin}")
