# test_enum_method_has_drop.cmake — enum `impl` with has_drop payloads
# Tests: string/vec payloads in enum methods + memcheck validates 0 leaks
#        JIT + AOT + memcheck
#
# The corpus is a miniature `JsonValue` -- Null / Bool / Int / String(Str) /
# Array(Vec(JsonValue)) -- chosen because it is SELF-RECURSIVE through a
# container. That combination is what makes the ownership question hard: the
# generated `__drop` for the enum has to reach into the Vec, which drops each
# element, each of which is another JsonValue that may itself be an Array.
# A shape with only scalar payloads would exercise none of it.
#
# The methods are `&self` borrows that `match self` and return a STATIC Str.
# That is the subtle part worth keeping: reading a borrowed enum must not
# consume or clone the payload, and returning a static literal out of an arm
# must not make the arm think it owns something. Get either wrong and the
# program still prints the right words -- the damage shows up only as a leak
# or a double-free under memcheck, which is why this test is memcheck-gated
# rather than output-gated.
#
# @subsystem codegen/ownership
# @guards enum methods over has_drop payloads
# @sources codegen_own.c:emit_enum_drop, codegen_own.c:emit_auto_drop_fn

cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/enum_method_has_drop.lls")

set(_expected
    "PASS 4a" "PASS 4b" "PASS 4c" "PASS 4d"
)

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "enum_method_has_drop JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "enum_method_has_drop JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "enum_method_has_drop JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/enum_method_has_drop_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "enum_method_has_drop AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    RESULT_VARIABLE aot_run_rc
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "enum_method_has_drop AOT run FAILED (rc=${aot_run_rc})")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "enum_method_has_drop AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "enum_method_has_drop AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "enum_method_has_drop memcheck FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "enum_method_has_drop --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "enum_method_has_drop memcheck: OK clean")

message(STATUS "test_enum_method_has_drop: ALL PASSED")
