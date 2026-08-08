# test_vec_builtin_syntax_reject.cmake — builtin vec(T) frontend syntax is gone.
#
# Required: LS_EXE, SAMPLE
#
# Phase 3 P3-1: the builtin `vec(T)` type syntax is no longer accepted.
#
# Retirements need a test because the failure mode of a half-done retirement is
# silence -- the parser still recognises the old spelling and produces something
# subtly different from `Vec(T)`. Refusing it at the type parser is what makes the
# migration complete rather than merely discouraged.
#
# @subsystem language/syntax
# @guards Phase 3 P3-1 builtin vec(T) syntax retired
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
        "vec-builtin-syntax: expected compile error but got exit 0\nstdout:\n${_out}")
endif()
if(NOT "${_err}" MATCHES "unknown.*type")
    message(FATAL_ERROR
        "vec-builtin-syntax: expected stderr to contain 'unknown ... type'\nstderr:\n${_err}")
endif()
message(STATUS "test_vec_builtin_syntax_reject: got expected rejection (rc=${_rc})")
