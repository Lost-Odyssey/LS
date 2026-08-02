# test_explicit_drop_reject.cmake - A-2 (docs/bugs_deferred_p5_4.md §2):
# explicit `.__drop()` calls must be rejected cleanly by the checker, not crash
# at JIT with "Symbols not found".
#
# The corpus is the shape a user actually reaches for: a `Destroy` impl whose
# `~` tries to release its own field by hand -- `self.inner.__drop()` -- the way
# a C++ destructor would chain. In LS that is always wrong twice over. The field
# is released automatically after `~` returns, so the manual call is a plain
# double-free; and `Inner.__drop` is COMPILER-GENERATED, so whether the symbol
# exists at all depends on emission order. That second half is why the failure
# was so bad before the check: instead of a type error the program reached the
# JIT and died on "Symbols not found", pointing at a mangled name the user never
# wrote and cannot find in their source.
#
# So this is a reject-only test on purpose -- there is no positive twin to run.
# The assertion is the diagnostic text plus a non-zero exit; the thing that must
# NOT happen is reaching the backend at all.
#
# Required: LS_EXE, SAMPLE
#
# @subsystem codegen/ownership
# @guards bugs_deferred_p5_4 A-2
# @sources checker_call.c:check_expr_call
cmake_minimum_required(VERSION 3.20)

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc
)
if(_rc EQUAL 0)
    message(FATAL_ERROR
        "explicit-drop-reject: expected compile error but got exit 0\nstdout:\n${_out}")
endif()
if(NOT "${_err}" MATCHES "cannot call __drop")
    message(FATAL_ERROR
        "explicit-drop-reject: expected stderr to contain 'cannot call __drop'\nstderr:\n${_err}")
endif()
# Must NOT reach JIT symbol resolution.
if("${_err}" MATCHES "Symbols not found")
    message(FATAL_ERROR
        "explicit-drop-reject: reached JIT (Symbols not found) instead of clean checker error\nstderr:\n${_err}")
endif()
message(STATUS "test_explicit_drop_reject: got expected rejection (rc=${_rc})")
