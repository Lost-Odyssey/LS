# test_pod_borrow_forin_reject.cmake — where a diagnostic POINTS, for the two
# ways POD-scalar elements meet element borrows.
#
#   (1) `for x in &v` is screened at the loop      -> anchored in the USER's file
#   (2) a direct get_ref() call instantiates the
#       template and trips the rule in its body    -> anchored in vec.lls
#
# (2) is the regression guard: a template's AST carries the DEFINING file's line
# numbers while the checker's source_path during monomorphization is the
# consumer's, so mixing them stamped `<consumer>.lls:185` onto a 23-line file.
#
# Both anchors are checked the same way -- the reported line is extracted and the
# compiler must be able to echo that source line back.  diag.c reads the source
# line lazily and silently degrades to a bare one-liner when it cannot, so the
# snippet's presence IS the in-range check, performed against the file the
# compiler actually opened.  Hard-coding line numbers would rot on the next edit
# to either file; counting lines in CMake is not an option either (file(STRINGS)
# re-encodes non-ASCII and reports 104 lines for a 652-line vec.lls).
#
# Required: LS_EXE, SAMPLE_DIR, REPO_DIR
#
# @subsystem checker/generics
# @guards generic-template diagnostics carry the defining file's path
# @sources checker_generics.c:check_and_queue_generic_method
cmake_minimum_required(VERSION 3.20)

set(SAMPLE "${SAMPLE_DIR}/pod_borrow_forin_reject.lls")

# LS_HOME is pinned so the template that gets resolved is the one in the source
# tree, not a stale copy next to the executable.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "LS_HOME=${REPO_DIR}" "${LS_EXE}" check "${SAMPLE}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc
)
set(_all "${_err}${_out}")

if(_rc EQUAL 0)
    message(FATAL_ERROR "pod-borrow-forin-reject: expected type errors but got exit 0\n${_all}")
endif()

# ---- (1) the for-in screen: rejected AT THE LOOP, in the user's file ----
if(NOT "${_all}" MATCHES "cannot borrow elements of 'Vec\\(int\\)'")
    message(FATAL_ERROR
        "pod-borrow-forin-reject: `for x in &v` over POD elements must be rejected at\n"
        "the loop with an actionable message, not by desugaring into the container's\n"
        "internals.\n${_all}")
endif()
if(NOT "${_all}" MATCHES "pod_borrow_forin_reject\\.lls:([0-9]+):[0-9]+: cannot borrow elements")
    message(FATAL_ERROR
        "pod-borrow-forin-reject: the for-in rejection is not anchored in the user's file\n${_all}")
endif()
set(_loop_line "${CMAKE_MATCH_1}")
if(NOT "${_all}" MATCHES "[\r\n] *${_loop_line} \\|")
    message(FATAL_ERROR
        "pod-borrow-forin-reject: no source snippet for the for-in rejection at line"
        " ${_loop_line}\n${_all}")
endif()

# The binder must NOT cascade: the loop is not desugared, so `x` never becomes an
# undefined variable.  This is what the screen buys the user.
if("${_all}" MATCHES "undefined variable 'x'")
    message(FATAL_ERROR
        "pod-borrow-forin-reject: the rejected loop cascaded into an 'undefined variable'\n"
        "for its binder -- the screen must stop before desugaring.\n${_all}")
endif()

# ---- (2) the template's own rule: anchored in vec.lls ----
if(NOT "${_all}" MATCHES "cannot return a borrow of a POD scalar")
    message(FATAL_ERROR
        "pod-borrow-forin-reject: a direct get_ref() call must still trip the\n"
        "aggregate-only borrow-return rule\n${_all}")
endif()
if(NOT "${_all}" MATCHES "vec\\.lls:([0-9]+):[0-9]+: cannot return a borrow of a POD scalar")
    message(FATAL_ERROR
        "pod-borrow-forin-reject: the template's error is not attributed to vec.lls.\n"
        "The offending `return` lives in the generic template, so the anchor must\n"
        "name the defining file.\n${_all}")
endif()
set(_tmpl_line "${CMAKE_MATCH_1}")
if(_tmpl_line LESS 1)
    message(FATAL_ERROR "pod-borrow-forin-reject: nonsense line number ${_tmpl_line}\n${_all}")
endif()
if(NOT "${_all}" MATCHES "[\r\n] *${_tmpl_line} \\|")
    message(FATAL_ERROR
        "pod-borrow-forin-reject: no source snippet for vec.lls:${_tmpl_line} -- the"
        " compiler could not read that line back, i.e. the anchor is out of range for"
        " the file it names.\n${_all}")
endif()

# ---- and the template's error must NOT be stamped onto the consumer ----
if("${_all}" MATCHES "pod_borrow_forin_reject\\.lls:[0-9]+:[0-9]+: cannot return a borrow of a POD scalar")
    message(FATAL_ERROR
        "pod-borrow-forin-reject: the template's error was stamped onto the consumer file.\n"
        "This is the regression: consumer source_path + definer line number.\n${_all}")
endif()

message(STATUS "pod-borrow-forin-reject: loop rejected at sample:${_loop_line}, template at vec.lls:${_tmpl_line}")
message(STATUS "test_pod_borrow_forin_reject: ALL PASSED")
