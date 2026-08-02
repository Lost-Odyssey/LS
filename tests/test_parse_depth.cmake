# test_parse_depth.cmake — parser recursion depth guard (P0 hardening).
#  * Negative: 5000-level nested parens / prefix-deref stars / nested blocks
#    must fail FAST with a clean diagnostic ("nesting too deep"), exit 1 —
#    not a stack-overflow crash (historically rc=127 with empty output).
#  * Error-flood cap: the nested-block corpus must not render more than
#    LS_MAX_PARSE_ERRORS diagnostics (regression: 4746 errors / 84 MB / 94 s).
#  * Positive: 64-level parens still parse clean (guard must not misfire).
#
# @subsystem frontend/parser
# @guards P0 parser recursion depth guard
# @sources parser.c:parse
cmake_minimum_required(VERSION 3.20)

set(deep_dir "${WORK_DIR}/parse_depth_scratch")
file(MAKE_DIRECTORY "${deep_dir}")

string(REPEAT "(" 5000 open_p)
string(REPEAT ")" 5000 close_p)
string(REPEAT "*" 5000 stars)
string(REPEAT "{" 5000 open_b)
string(REPEAT "}" 5000 close_b)

file(WRITE "${deep_dir}/deep_paren.lls"
    "def main() -> int {\n    int x = ${open_p}1${close_p}\n    return x\n}\n")
file(WRITE "${deep_dir}/deep_ptr.lls"
    "def main() -> int {\n    ${stars}int p = nil\n    return 0\n}\n")
file(WRITE "${deep_dir}/deep_block.lls"
    "def main() -> int {\n${open_b}${close_b}\n    return 0\n}\n")

string(REPEAT "(" 64 open_ok)
string(REPEAT ")" 64 close_ok)
file(WRITE "${deep_dir}/ok_nest.lls"
    "def main() -> int {\n    int x = ${open_ok}1${close_ok}\n    return x\n}\n")

# ---- negative: each deep corpus fails fast with the guard diagnostic ----
foreach(case deep_paren deep_ptr deep_block)
    execute_process(COMMAND "${LS_EXE}" check "${deep_dir}/${case}.lls"
        OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc
        TIMEOUT 30)
    if(rc STREQUAL "" OR rc MATCHES "timeout")
        message(FATAL_ERROR "parse_depth ${case}: TIMED OUT (guard not fast)\n")
    endif()
    if(rc EQUAL 0)
        message(FATAL_ERROR "parse_depth ${case}: unexpectedly accepted\n${out}")
    endif()
    # A crash (stack overflow) surfaces as a large/negative rc with no
    # diagnostic; require the clean guard message instead.
    if(NOT "${out}${err}" MATCHES "nesting too deep")
        message(FATAL_ERROR
            "parse_depth ${case}: rc=${rc} but no 'nesting too deep' diagnostic (crash?)\n${out}\n${err}")
    endif()
    message(STATUS "parse_depth ${case} negative: OK (rc=${rc})")
endforeach()

# ---- error-flood cap: nested-block corpus renders a bounded diagnostic set ----
execute_process(COMMAND "${LS_EXE}" check "${deep_dir}/deep_block.lls"
    OUTPUT_VARIABLE fb_out ERROR_VARIABLE fb_err RESULT_VARIABLE fb_rc
    TIMEOUT 30)
string(LENGTH "${fb_out}${fb_err}" flood_len)
if(flood_len GREATER 2000000)
    message(FATAL_ERROR
        "parse_depth flood cap: diagnostics exploded to ${flood_len} bytes (error cap regressed)")
endif()
message(STATUS "parse_depth flood cap: OK (${flood_len} bytes)")

# ---- positive: legitimate nesting is untouched ----
execute_process(COMMAND "${LS_EXE}" check "${deep_dir}/ok_nest.lls"
    OUTPUT_VARIABLE ok_out ERROR_VARIABLE ok_err RESULT_VARIABLE ok_rc
    TIMEOUT 30)
if(NOT ok_rc EQUAL 0)
    message(FATAL_ERROR "parse_depth positive: 64-level nesting rejected\n${ok_out}\n${ok_err}")
endif()
message(STATUS "parse_depth positive: OK")

file(REMOVE_RECURSE "${deep_dir}")
message(STATUS "parse_depth: ALL OK")
