# test_literal_overflow.cmake — numeric literals that don't fit are parse errors:
# decimal/hex > 64 bits, float overflow (1e999), and absurdly long tokens.
# Boundary values (i64 max, full-64-bit hex, denormal underflow, 1e308) keep working.
#
# Numeric literals that do not fit must be REJECTED at parse time, not silently
# saturated.
#
# `strtoull` / `strtod` do not report overflow through their return value, so
# without an `errno` check an out-of-range integer became `ULLONG_MAX` and `1e999`
# became infinity -- a wrong constant compiled in with no diagnostic. Over-long
# tokens were additionally truncated before parsing, so a 70-bit binary literal
# was cut to 61 bits and did not even set the overflow flag.
#
# The corpus keeps boundary values passing (i64 max, all-f hex, 5e-324, 1e308)
# alongside the rejections, because the easy over-correction is to reject the
# largest legal values too.
#
# @subsystem frontend/lexer
# @guards numeric literal overflow saturated silently (1aab966)
# @sources parser_expr.c:prefix_int_lit, parser_expr.c:prefix_float_lit
cmake_minimum_required(VERSION 3.20)

# ---- reject: all overflow shapes diagnosed, non-zero exit ----
execute_process(COMMAND "${LS_EXE}" check "${SAMPLE_DIR}/literal_overflow_reject.lls"
    OUTPUT_VARIABLE r_out ERROR_VARIABLE r_err RESULT_VARIABLE r_rc)
if(r_rc EQUAL 0)
    message(FATAL_ERROR "literal_overflow reject: expected parse errors, got exit 0\n${r_out}")
endif()
set(_all "${r_out}${r_err}")
if(NOT "${_all}" MATCHES "integer literal out of range")
    message(FATAL_ERROR "literal_overflow reject: missing integer overflow error\n${_all}")
endif()
if(NOT "${_all}" MATCHES "float literal magnitude too large")
    message(FATAL_ERROR "literal_overflow reject: missing float overflow error\n${_all}")
endif()
if(NOT "${_all}" MATCHES "numeric literal too long")
    message(FATAL_ERROR "literal_overflow reject: missing too-long error\n${_all}")
endif()
message(STATUS "literal_overflow reject: OK")

# ---- boundary values still accepted and run pinned ----
set(SRC "${SAMPLE_DIR}/literal_overflow_ok_test.lls")
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "literal_overflow ok JIT FAILED (rc=${jit_rc})\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "LIT OK 9223372036854775807 -1")
    message(FATAL_ERROR "literal_overflow ok JIT wrong output\n${jit_out}")
endif()
message(STATUS "literal_overflow boundary: OK")

message(STATUS "test_literal_overflow: ALL PASSED")
