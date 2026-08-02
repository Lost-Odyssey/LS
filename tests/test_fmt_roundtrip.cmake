# test_fmt_roundtrip.cmake — `lls fmt` must be behavior-preserving.
#
# Oracle: running the formatted source must produce byte-identical stdout to
# running the original. That is stronger than "the formatted file still
# compiles", and it is what caught two inverse @-token spacing bugs in
# format.c space_between():
#   @time fib(10) -> @timefib(10)  (token fusion; formatted source won't build)
#   @print(v)     -> @print (v)    (call form that should glue, did not)
#
# Also pins idempotence: fmt(fmt(x)) == fmt(x).
#
# Required: LS_EXE, SAMPLE, WORK_DIR
#
# @subsystem tooling/cli
# @guards fmt must be behaviour-preserving over the whole corpus
# @sources format.c
cmake_minimum_required(VERSION 3.20)

foreach(_required_var LS_EXE SAMPLE WORK_DIR)
    if(NOT DEFINED ${_required_var})
        message(FATAL_ERROR "test_fmt_roundtrip.cmake: missing required -D${_required_var}=...")
    endif()
endforeach()

# 1. baseline: run the original
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _orig_out
    ERROR_VARIABLE  _orig_err
    RESULT_VARIABLE _orig_rc
)
if(NOT _orig_rc EQUAL 0)
    message(FATAL_ERROR "fmt-roundtrip: the ORIGINAL sample failed to run (rc=${_orig_rc})\n${_orig_err}")
endif()

# 2. format it
execute_process(
    COMMAND "${LS_EXE}" fmt "${SAMPLE}" --stdout
    OUTPUT_VARIABLE _fmt_src
    ERROR_VARIABLE  _fmt_err
    RESULT_VARIABLE _fmt_rc
)
if(NOT _fmt_rc EQUAL 0)
    message(FATAL_ERROR "fmt-roundtrip: `fmt --stdout` failed (rc=${_fmt_rc})\n${_fmt_err}")
endif()
string(LENGTH "${_fmt_src}" _fmt_len)
if(_fmt_len EQUAL 0)
    message(FATAL_ERROR "fmt-roundtrip: `fmt --stdout` produced EMPTY output")
endif()

# 3. run the formatted source
set(_fmt_file "${WORK_DIR}/fmt_roundtrip_formatted.lls")
file(WRITE "${_fmt_file}" "${_fmt_src}")
execute_process(
    COMMAND "${LS_EXE}" run "${_fmt_file}"
    OUTPUT_VARIABLE _new_out
    ERROR_VARIABLE  _new_err
    RESULT_VARIABLE _new_rc
)
if(NOT _new_rc EQUAL 0)
    message(FATAL_ERROR
        "fmt-roundtrip: the FORMATTED source failed to run (rc=${_new_rc}).\n"
        "  formatted file: ${_fmt_file}\n${_new_err}")
endif()

# 4. stdout must be byte-identical after normalizing the one nondeterministic
#    thing this sample emits: the elapsed times printed by @time / @bench. The
#    LINE STRUCTURE and the iteration counts are deliberately kept — if
#    formatting were to drop an @time, the missing line would still be caught.
foreach(_var _orig_out _new_out)
    string(REGEX REPLACE "\\[@time\\] [0-9.]+ ms" "[@time] T ms" ${_var} "${${_var}}")
    string(REGEX REPLACE "mean [0-9.]+ ns" "mean T ns" ${_var} "${${_var}}")
endforeach()

#    Bare variable names in STREQUAL: ${} expansion would split on ';'.
if(NOT _new_out STREQUAL _orig_out)
    message(FATAL_ERROR
        "fmt-roundtrip: formatting CHANGED program behavior.\n"
        "  formatted file: ${_fmt_file}\n"
        "--- original stdout ---\n${_orig_out}\n"
        "--- formatted stdout ---\n${_new_out}")
endif()

# 5. idempotence: fmt(fmt(x)) == fmt(x)
execute_process(
    COMMAND "${LS_EXE}" fmt "${_fmt_file}" --stdout
    OUTPUT_VARIABLE _fmt2_src
    ERROR_VARIABLE  _fmt2_err
    RESULT_VARIABLE _fmt2_rc
)
if(NOT _fmt2_rc EQUAL 0)
    message(FATAL_ERROR "fmt-roundtrip: second fmt pass failed (rc=${_fmt2_rc})\n${_fmt2_err}")
endif()
if(NOT _fmt2_src STREQUAL _fmt_src)
    message(FATAL_ERROR
        "fmt-roundtrip: fmt is NOT idempotent — fmt(fmt(x)) != fmt(x).\n"
        "  first pass:  ${_fmt_file}")
endif()

message(STATUS "fmt-roundtrip: OK (behavior preserved + idempotent)")
