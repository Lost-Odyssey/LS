# test_explicit_drop_reject.cmake - A-2 (docs/bugs_deferred_p5_4.md §2):
# explicit `.__drop()` calls must be rejected cleanly by the checker, not crash
# at JIT with "Symbols not found".
#
# Required: LS_EXE, SAMPLE
cmake_minimum_required(VERSION 3.20)

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc
)
if(_rc EQUAL 0)
    message(FATAL_ERROR
        "explicit-drop-reject: expected compile error but got exit 0\nstdout:\n${_out}")
endif()
if(NOT "${_err}" MATCHES "cannot call __drop")
    message(FATAL_ERROR
        "explicit-drop-reject: expected stderr to contain 'cannot call __drop'\nstderr:\n${_err}")
endif()
# Must NOT reach JIT symbol resolution.
if("${_err}" MATCHES "Symbols not found")
    message(FATAL_ERROR
        "explicit-drop-reject: reached JIT (Symbols not found) instead of clean checker error\nstderr:\n${_err}")
endif()
message(STATUS "test_explicit_drop_reject: got expected rejection (rc=${_rc})")
