# test_parser_hang_regress.cmake — parser must never infinite-loop on malformed
# input. Each corpus file (fuzz-found hangs + minimized repros) is run through
# `ls parse` with a hard timeout; a hang (timeout) or a crash (exit code outside
# {0,1}) fails the test. A clean parse error (exit 1) is the expected outcome.
#
# Root cause guarded: synchronize() stops without consuming on a statement
# keyword, so a brace-body recovery loop (`...; recover; continue;`) could spin
# forever on it (e.g. `struct P return`). Fixed by recover_in_body() in
# src/parser.c, which guarantees forward progress. See docs/plan_fuzzing.md.
#
# Required: LS_EXE, REGRESS_DIR
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT REGRESS_DIR)
    message(FATAL_ERROR "test_parser_hang_regress.cmake requires LS_EXE and REGRESS_DIR")
endif()

file(GLOB _cases "${REGRESS_DIR}/*.lls")
if(_cases STREQUAL "")
    message(FATAL_ERROR "no regression corpus in ${REGRESS_DIR}")
endif()

set(_fail 0)
foreach(_f IN LISTS _cases)
    get_filename_component(_name "${_f}" NAME)
    execute_process(
        COMMAND "${LS_EXE}" parse "${_f}"
        OUTPUT_QUIET ERROR_QUIET
        RESULT_VARIABLE _rc
        TIMEOUT 15
    )
    # On timeout, execute_process sets RESULT_VARIABLE to a non-integer message.
    if(NOT "${_rc}" MATCHES "^-?[0-9]+$")
        message(WARNING "HANG: ${_name} (parser did not terminate: ${_rc})")
        set(_fail 1)
    elseif(NOT (_rc EQUAL 0 OR _rc EQUAL 1))
        message(WARNING "CRASH: ${_name} (exit ${_rc}, expected clean 0/1)")
        set(_fail 1)
    else()
        message(STATUS "ok: ${_name} (rc=${_rc})")
    endif()
endforeach()

if(_fail)
    message(FATAL_ERROR "test_parser_hang_regress: FAILED (see HANG/CRASH above)")
endif()
message(STATUS "test_parser_hang_regress: ALL PASSED")
