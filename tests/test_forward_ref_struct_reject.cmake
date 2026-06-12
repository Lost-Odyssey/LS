# test_forward_ref_struct_reject.cmake - A-3 (docs/bugs_deferred_p5_4.md §3):
# a forward struct field reference must produce a clean type error and exit
# gracefully (rc != 0), NOT segfault (rc 139 / 0xC0000005).
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
        "forward-ref-reject: expected compile error but got exit 0\nstdout:\n${_out}")
endif()
# A segfault on Windows surfaces as a large/negative status, never as 1. Require
# the clean checker error to be present AND the run to exit gracefully.
if(NOT "${_err}" MATCHES "unknown type")
    message(FATAL_ERROR
        "forward-ref-reject: expected stderr to contain 'unknown type'\nstderr:\n${_err}\nrc=${_rc}")
endif()
# Graceful exit code is 1; a crash would be e.g. 139 (POSIX) or 0xC0000005.
if(NOT "${_rc}" EQUAL 1)
    message(FATAL_ERROR
        "forward-ref-reject: expected graceful exit code 1 (got ${_rc}); likely a crash\nstderr:\n${_err}")
endif()
message(STATUS "test_forward_ref_struct_reject: graceful rejection (rc=${_rc})")
