# test_fmt_directive_stdout.cmake — `fmt --stdout` must never swallow a file.
#
# The formatter deliberately refuses files containing preprocessor directives
# (#if WINDOWS / ...): it cannot see the tokens inside inactive branches, so
# reformatting them is unsafe. In-place mode handles that correctly by leaving
# the file untouched. But the skip branch used to `continue` before the
# to_stdout check, so --stdout wrote ZERO bytes and still exited 0 --
# `lls fmt f.lls --stdout > f.lls` or any pipeline use destroyed the file
# (condcomp_test.lls: 2100 bytes -> 0).
#
# Contract: on a refused file, --stdout passes the source through verbatim and
# keeps the explanatory note on stderr.
#
# Required: LS_EXE, SAMPLE
cmake_minimum_required(VERSION 3.20)

foreach(_required_var LS_EXE SAMPLE)
    if(NOT DEFINED ${_required_var})
        message(FATAL_ERROR "test_fmt_directive_stdout.cmake: missing required -D${_required_var}=...")
    endif()
endforeach()

file(READ "${SAMPLE}" _src)
string(LENGTH "${_src}" _src_len)
if(_src_len EQUAL 0)
    message(FATAL_ERROR "fmt-directive-stdout: sample is empty, the test would be vacuous: ${SAMPLE}")
endif()

execute_process(
    COMMAND "${LS_EXE}" fmt "${SAMPLE}" --stdout
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "fmt-directive-stdout: rc=${_rc}\n${_err}")
endif()

string(LENGTH "${_out}" _out_len)
if(_out_len EQUAL 0)
    message(FATAL_ERROR
        "fmt-directive-stdout: --stdout produced EMPTY output for a refused file "
        "(source is ${_src_len} bytes). `lls fmt f --stdout > f` would destroy it.")
endif()

# Bare variable names in STREQUAL: ${} expansion would split the content on ';'.
if(NOT _out STREQUAL _src)
    message(FATAL_ERROR
        "fmt-directive-stdout: refused file was not passed through verbatim.\n"
        "  source bytes: ${_src_len}\n  output bytes: ${_out_len}")
endif()

if(NOT "${_err}" MATCHES "preprocessor directives")
    message(FATAL_ERROR
        "fmt-directive-stdout: expected the 'skipped ... (preprocessor directives)' "
        "note on stderr, got:\n${_err}")
endif()

message(STATUS "fmt-directive-stdout: OK (refused file passed through verbatim)")
