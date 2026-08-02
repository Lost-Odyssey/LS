# test_diag_ownership_wording.cmake — pins the wording of the @move-non-movable
# and unsupported-capture diagnostics. Both must fire in ONE check pass.
#
# These two messages document the movable-type set and the capture strategies to
# the user; both had drifted away from their implementations (retired builtin
# type names `string`/`vec`/`map`, a by-ref capture strategy that no longer
# exists, a missing has_drop-enum/Block movable case, and the retired `__move`
# spelling). The negative assertions below are the point of the test: they fail
# if any of the stale wording comes back.
#
# Required: LS_EXE, SAMPLE
#
# @subsystem diagnostics
# @guards stale ownership diagnostics (0c4855e)
# @sources checker_borrow.c:capture_type_supported, checker_borrow.c:type_is_movable, checker_call.c:intrinsic_retired_spelling
cmake_minimum_required(VERSION 3.20)

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc
)
set(_all "${_err}${_out}")

if(_rc EQUAL 0)
    message(FATAL_ERROR "diag-wording: expected compile errors but got exit 0\n${_all}")
endif()

# ---- positive: the current, true wording ----------------------------------
# NOTE: MATCHES takes a REGEX, so every literal paren below is escaped. An
# unescaped "(...)" silently becomes a capture group and the assertion stops
# testing what it looks like it tests.
#
# @move on POD names the real movable set (single authority: type_is_movable).
foreach(_want
        "@move\\(\\) applied to non-movable type 'int'"
        "only has_drop struct \\(incl. Str/Vec/Map\\), has_drop enum, and Block can be moved")
    if(NOT "${_all}" MATCHES "${_want}")
        message(FATAL_ERROR "diag-wording: missing expected text\n  want: ${_want}\n--- got ---\n${_all}")
    endif()
endforeach()

# Unsupported capture names the real strategies.
foreach(_want
        "in a closure is not yet implemented"
        "array\\(POD,N\\) \\(by-copy\\)"
        "Block \\(by-clone\\)"
        "has_drop struct incl. Str/Vec/Map \\(by-move\\)"
        "enum \\(by-copy when POD, by-move when has_drop\\)")
    if(NOT "${_all}" MATCHES "${_want}")
        message(FATAL_ERROR "diag-wording: missing expected capture text\n  want: ${_want}\n--- got ---\n${_all}")
    endif()
endforeach()

# ---- negative: the retired wording must never come back --------------------
# by-ref capture does not exist (capture_type_is_by_ref_cg is `return false`).
if("${_all}" MATCHES "by-ref")
    message(FATAL_ERROR "diag-wording: diagnostic advertises by-ref capture, which no longer exists\n${_all}")
endif()
# Retired builtin type names in the movable/capture enumerations.
foreach(_bad "vec\\(T\\)/map\\(K,V\\)" "only string, vec, map")
    if("${_all}" MATCHES "${_bad}")
        message(FATAL_ERROR "diag-wording: retired builtin type name in diagnostic: ${_bad}\n${_all}")
    endif()
endforeach()
# Retired `__move` spelling (the user can only write @move).
if("${_all}" MATCHES "__move\\(\\)")
    message(FATAL_ERROR "diag-wording: diagnostic names the retired __move spelling\n${_all}")
endif()

message(STATUS "diag-wording: @move + capture diagnostics match their implementations")
