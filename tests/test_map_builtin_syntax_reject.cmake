# test_map_builtin_syntax_reject.cmake - builtin map(K,V) frontend syntax is gone.
#
# Required: LS_EXE, SAMPLE
#
# M6-1: the builtin `map(K,V)` type syntax is no longer accepted.
#
# Twin of the vector retirement, same reasoning: a retired spelling that still
# parses is worse than one that errors, because it silently gives you the old
# type. The only remaining hash map is the pure-LS `Map(K,V)`.
#
# @subsystem language/syntax
# @guards M6-1 builtin map(K,V) syntax retired
# @sources parser_type.c:parse_type
cmake_minimum_required(VERSION 3.20)

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc
)
if(_rc EQUAL 0)
    message(FATAL_ERROR
        "map-builtin-syntax: expected compile error but got exit 0\nstdout:\n${_out}")
endif()
if(NOT "${_err}" MATCHES "unknown.*type")
    message(FATAL_ERROR
        "map-builtin-syntax: expected stderr to contain 'unknown ... type'\nstderr:\n${_err}")
endif()
message(STATUS "test_map_builtin_syntax_reject: got expected rejection (rc=${_rc})")
