# test_bare_print_reject.cmake — Stage F: bare `print(...)` must be a clean compile
# error (the intrinsic is only spelled `@print`). docs/plan_print_sink.md Stage F.
# Required: LS_EXE, SAMPLE
cmake_minimum_required(VERSION 3.20)

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc
)
if(_rc EQUAL 0)
    message(FATAL_ERROR "bare-print-reject: expected compile error but got exit 0\n${_out}")
endif()
if(NOT "${_err}${_out}" MATCHES "undefined variable 'print'")
    message(FATAL_ERROR "bare-print-reject: expected \"undefined variable 'print'\"\n${_err}\n${_out}")
endif()
message(STATUS "bare-print-reject: bare print is a clean 'undefined' error")
