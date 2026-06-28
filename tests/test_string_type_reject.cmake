# test_string_type_reject.cmake — P5-4 S-1 negative smoke: the builtin `string`
# type keyword no longer exists; `string s = ...` must fail at compile time with
# a clear unknown-type error.
cmake_minimum_required(VERSION 3.20)

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc
)
if(_rc EQUAL 0)
    message(FATAL_ERROR "string_type_reject: expected rejection but run succeeded\nstdout:\n${_out}")
endif()
if(NOT "${_err}" MATCHES "unknown type 'string'")
    message(FATAL_ERROR "string_type_reject: expected \"unknown type 'string'\" in stderr\nstderr:\n${_err}")
endif()
message(STATUS "string_type_reject: rejected OK")
