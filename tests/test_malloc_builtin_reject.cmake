# test_malloc_builtin_reject.cmake — bare malloc/free/realloc/abort are no longer
# global builtins (A-FLIP, docs/plan_runtime_primitives.md). A bare `malloc(...)`
# must now be rejected as an undefined variable.
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
        "malloc-builtin-reject: expected compile error but got exit 0\nstdout:\n${_out}")
endif()
if(NOT "${_err}" MATCHES "undefined")
    message(FATAL_ERROR
        "malloc-builtin-reject: expected stderr to contain 'undefined'\nstderr:\n${_err}")
endif()
message(STATUS "test_malloc_builtin_reject: got expected rejection (rc=${_rc})")
