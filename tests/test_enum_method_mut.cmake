# test_enum_method_mut.cmake — enum `impl` with &!self mutable methods
# Tests: &!self, self = Variant reassignment in method
#        JIT + AOT + memcheck
#
# `&!self` methods on an enum: the receiver is mutable, so the method may switch
# the ACTIVE VARIANT, not merely edit a field.
#
# That is what separates this from the struct case. Assigning a new variant over
# `*self` has to release whatever the old variant owned before overwriting the
# tag; skipping it leaks, doing it twice double-frees. The corpus mutates through
# `&!self` and checks the resulting values, so a mis-sequenced release shows up
# rather than hiding behind a passing exit code.
#
# @subsystem codegen/enum
# @guards enum &!self mutable methods
# @sources codegen_decl.c:emit_enum_ctor

cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/enum_method_mut.lls")

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
