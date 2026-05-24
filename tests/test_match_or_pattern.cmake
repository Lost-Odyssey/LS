# test_match_or_pattern.cmake — OR-pattern in match (bugs/18 fix)
# Tests: integer switch with OR patterns, string OR patterns (CondBr path),
#        JIT + AOT + memcheck

cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/match_or_pattern_test.ls")

set(_expected
    "PASS 1a" "PASS 1b" "PASS 1c" "PASS 1d"
    "PASS 2a" "PASS 2b" "PASS 2c" "PASS 2d"
    "PASS 3a" "PASS 3b" "PASS 3c"
    "PASS 4a" "PASS 4b" "PASS 4c"
    "PASS 5a" "PASS 5b" "PASS 5c"
)

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "match_or_pattern JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "match_or_pattern JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "match_or_pattern JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/match_or_pattern_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "match_or_pattern AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    RESULT_VARIABLE aot_run_rc
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "match_or_pattern AOT run FAILED (rc=${aot_run_rc})")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "match_or_pattern AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "match_or_pattern AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "match_or_pattern memcheck FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "match_or_pattern --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "match_or_pattern memcheck: OK clean")

message(STATUS "test_match_or_pattern: ALL PASSED")
