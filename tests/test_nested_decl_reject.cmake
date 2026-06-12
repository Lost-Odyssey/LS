# test_nested_decl_reject.cmake - A-1 (docs/bugs_deferred_p5_4.md §1):
# nested fn/struct/impl definitions inside a function body must be rejected
# cleanly at parse time, not crash in codegen with "no terminator".
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
        "nested-decl-reject: expected compile error but got exit 0\nstdout:\n${_out}")
endif()
if(NOT "${_err}" MATCHES "nested .* definition is not allowed")
    message(FATAL_ERROR
        "nested-decl-reject: expected stderr to contain 'nested ... definition is not allowed'\nstderr:\n${_err}")
endif()
# Must NOT reach codegen verification failure.
if("${_err}" MATCHES "does not have terminator")
    message(FATAL_ERROR
        "nested-decl-reject: reached codegen (no-terminator) instead of clean parse error\nstderr:\n${_err}")
endif()
message(STATUS "test_nested_decl_reject: got expected rejection (rc=${_rc})")
