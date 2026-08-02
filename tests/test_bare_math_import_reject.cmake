# test_bare_math_import_reject.cmake — the built-in math module moved to the
# canonical std.core.math path. Bare `import math` (with no user math.lls present)
# must be a clean compile error that points at the new path.
# Required: LS_EXE, SAMPLE
#
# @subsystem language/syntax
# @guards bare `import math` retired (moved to std.core.math)
# @sources module.c:module_load
cmake_minimum_required(VERSION 3.20)

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc
)
if(_rc EQUAL 0)
    message(FATAL_ERROR "bare-math-import-reject: expected compile error but got exit 0\n${_out}")
endif()
if(NOT "${_err}${_out}" MATCHES "std.core.math")
    message(FATAL_ERROR "bare-math-import-reject: expected a hint mentioning 'std.core.math'\n${_err}\n${_out}")
endif()
message(STATUS "bare-math-import-reject: bare import math rejected with std.core.math hint")
