# test_comptime_match.cmake — comptime field iteration, v3 ①: comptime match
# (active-variant value dispatch). `comptime match v { vr(p) => ... }` expands to
# a real per-variant `match`, binding the active payload — write-once enum
# show/serialize/visitor with no @derive.
#  JIT + AOT + memcheck 0/0/0; negative: comptime match on a non-enum is a clean error.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/comptime_match_test.ls")
set(_expected "COMPTIME MATCH DONE")
set(_needles "${_expected}" "Circle.5." "Square.9." "Empty" "Name.hi." "Leaf.7."
             "Nil" "XY.3." "XY/2" "Origin/0")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "comptime match JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
foreach(needle ${_needles})
    if(NOT "${jit_out}" MATCHES "${needle}")
        message(FATAL_ERROR "comptime match JIT missing '${needle}'\n${jit_out}")
    endif()
endforeach()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "comptime match JIT had a FAIL line\n${jit_out}")
endif()
message(STATUS "comptime match JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/comptime_match_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "comptime match AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0 OR NOT "${aot_out}" MATCHES "${_expected}"
   OR NOT "${aot_out}" MATCHES "Name.hi." OR "${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "comptime match AOT FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "comptime match AOT: OK")

# ---- positive: memcheck (owned Str payload drop/move through the match) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0 OR NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "comptime match memcheck not clean\n${mc_err}")
endif()
message(STATUS "comptime match memcheck: OK clean")

# ---- negative: comptime match on a non-enum is a clean compile error ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/comptime_match_reject.ls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "comptime_match_reject: expected compile error, got success\n${n_out}")
endif()
set(n_all "${n_out}${n_err}")
if(NOT "${n_all}" MATCHES "requires an enum subject")
    message(FATAL_ERROR "comptime_match_reject: missing diagnostic\n${n_all}")
endif()
message(STATUS "comptime_match_reject: rejected as expected")

message(STATUS "test_comptime_match: ALL PASSED")
