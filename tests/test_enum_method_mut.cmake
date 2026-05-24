# test_enum_method_mut.cmake — enum `impl` with &!self mutable methods
# Tests: &!self, self = Variant reassignment in method
#        JIT + AOT + memcheck

cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/enum_method_mut.ls")

set(_expected
    "PASS 2a" "PASS 2b"
)

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "enum_method_mut JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "enum_method_mut JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "enum_method_mut JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/enum_method_mut_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "enum_method_mut AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    RESULT_VARIABLE aot_run_rc
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "enum_method_mut AOT run FAILED (rc=${aot_run_rc})")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "enum_method_mut AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "enum_method_mut AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "enum_method_mut memcheck FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "enum_method_mut --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "enum_method_mut memcheck: OK clean")

message(STATUS "test_enum_method_mut: ALL PASSED")
