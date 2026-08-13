# test_diag_dedupe.cmake — a mistake produces ONE diagnostic, whatever statement
# it sits in.  Pins the COUNT, not just the presence: every other negative test
# in the suite asserts a message is present, which is why the loop bodies could
# double-report for so long without anything going red.
#
# Two independent judgements, and they pull in opposite directions:
#   * the four `undefined variable` errors must appear exactly ONCE each --
#     if/while/C-for/range, i.e. the duplicate is gone;
#   * the generic-instantiation error must still appear AT ALL -- the tempting
#     fix (silence pass 1 wholesale) scores perfectly on the first judgement by
#     making this one disappear, taking rc down to 0 with it.
# A fix that trades one for the other fails here.
#
# Uses two corpora: one to count, one that must be checked in isolation.
#
# Required: LS_EXE, SAMPLE_DIR, REPO_DIR
#
# @subsystem checker/diagnostics
# @guards one diagnostic per mistake in loop bodies (two-pass move analysis)
# @sources checker.c:checker_error
cmake_minimum_required(VERSION 3.20)

set(SAMPLE "${SAMPLE_DIR}/diag_dedupe_reject.lls")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "LS_HOME=${REPO_DIR}" "${LS_EXE}" check "${SAMPLE}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc
)
set(_all "${_err}${_out}")

if(_rc EQUAL 0)
    message(FATAL_ERROR "diag-dedupe: expected type errors but got exit 0\n${_all}")
endif()

# ---- exactly one diagnostic per mistake, in every statement context ----
# The `undefined variable '<name>'` phrasing only occurs on the diagnostic line;
# the caret snippet echoes the source line, which does not contain it.
foreach(_ctx if while forc range)
    set(_needle "undefined variable 'dedupe_in_${_ctx}'")
    string(REGEX MATCHALL "${_needle}" _hits "${_all}")
    list(LENGTH _hits _n)
    if(_n EQUAL 0)
        message(FATAL_ERROR "diag-dedupe: no diagnostic at all for '${_ctx}'\n${_all}")
    elseif(NOT _n EQUAL 1)
        message(FATAL_ERROR
            "diag-dedupe: '${_ctx}' reported ${_n} times, expected 1.\n"
            "A loop body is checked twice (move discovery, then reporting); only\n"
            "move diagnostics may be silenced on the first pass, so type errors\n"
            "have to be de-duplicated where they are emitted.\n${_all}")
    endif()
endforeach()

# ---- and nothing was bought by throwing a diagnostic away ----
# Separate file on purpose: it must be the FIRST error the checker sees.  Any
# earlier diagnostic sets had_error, which already prevents the failed instance
# from being cached -- so pass 2 re-checks it and the error survives even under
# the broken fix.  Keeping this case in the counting corpus made it inert
# (caught by injecting the broken fix and watching the test stay green).
set(SURVIVE "${SAMPLE_DIR}/diag_dedupe_survive_reject.lls")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "LS_HOME=${REPO_DIR}" "${LS_EXE}" check "${SURVIVE}"
    OUTPUT_VARIABLE _s_out
    ERROR_VARIABLE  _s_err
    RESULT_VARIABLE _s_rc
)
set(_s_all "${_s_err}${_s_out}")
if(_s_rc EQUAL 0)
    message(FATAL_ERROR
        "diag-dedupe: the generic-instantiation error inside a loop was lost --"
        " exit 0 means it did not even set had_error.\n${_s_all}")
endif()
string(REGEX MATCHALL "cannot return a borrow of a POD scalar" _inst_hits "${_s_all}")
list(LENGTH _inst_hits _inst_n)
if(_inst_n EQUAL 0)
    message(FATAL_ERROR
        "diag-dedupe: the generic-instantiation error inside a loop VANISHED.\n"
        "This is what happens when pass 1 is silenced wholesale: the suppressed\n"
        "checker_error skips had_error, the failed instance is queued as if it\n"
        "succeeded, and pass 2 never re-checks the body -- invalid IR then reaches\n"
        "codegen with rc=0.  De-duplicate at emission instead.\n${_all}")
elseif(NOT _inst_n EQUAL 1)
    message(FATAL_ERROR
        "diag-dedupe: the generic-instantiation error appeared ${_inst_n} times,"
        " expected 1\n${_all}")
endif()

message(STATUS "diag-dedupe: 4 contexts x 1 diagnostic, instantiation error intact")
message(STATUS "test_diag_dedupe: ALL PASSED")
