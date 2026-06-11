# test_string_borrow_reject.cmake - &string borrow params removed in P4
# (string->Str); the checker must reject with a hint pointing at &Str.
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
        "string-borrow-reject: expected compile error but got exit 0\nstdout:\n${_out}")
endif()
if(NOT "${_err}" MATCHES "&string has been removed")
    message(FATAL_ERROR
        "string-borrow-reject: expected stderr to mention '&string has been removed'\nstderr:\n${_err}")
endif()
message(STATUS "test_string_borrow_reject: got expected rejection (rc=${_rc})")
