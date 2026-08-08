# test_unterminated_comment_reject.cmake — an unterminated block comment must
# be a clean scanner error, not a silent swallow of the rest of the file.
#
# The while loop in scanner.c's skip_whitespace() used to exit the same way
# whether the comment closed properly (depth==0) or the file ran out while it
# was still open (depth>0). The latter case reached EOF with zero diagnostic:
# `lls check` on a file whose `/*` was never closed reported "Type check
# passed" -- because everything after the opener, including a real `def
# main() {...}`, had been silently eaten as comment text.
#
# Both single-level and nested unterminated comments must be caught, and a
# PROPERLY closed nested comment must keep compiling (the fix must not turn
# valid nesting into a false rejection).
#
# Required: LS_EXE, SAMPLE_DIR
#
# @subsystem frontend/lexer
# @guards unterminated block comment swallowed the rest of the file (2026-07-29)
# @sources scanner.c:skip_whitespace
cmake_minimum_required(VERSION 3.20)

foreach(_required_var LS_EXE SAMPLE_DIR)
    if(NOT DEFINED ${_required_var})
        message(FATAL_ERROR "test_unterminated_comment_reject.cmake: missing required -D${_required_var}=...")
    endif()
endforeach()

# ---- single-level unterminated: must be rejected with a clear diagnostic ----
execute_process(COMMAND "${LS_EXE}" check "${SAMPLE_DIR}/unterminated_comment_reject.lls"
    OUTPUT_VARIABLE s_out ERROR_VARIABLE s_err RESULT_VARIABLE s_rc)
if(s_rc EQUAL 0)
    message(FATAL_ERROR
        "unterminated-comment: single-level case was ACCEPTED (rc=0) -- the "
        "rest of the file was silently swallowed as comment text again.\n${s_out}${s_err}")
endif()
if(NOT "${s_out}${s_err}" MATCHES "unterminated block comment")
    message(FATAL_ERROR "unterminated-comment: missing the diagnostic text\n${s_out}${s_err}")
endif()
message(STATUS "unterminated-comment: single-level case rejected with a clear diagnostic")

# ---- nested unterminated: same requirement ----
execute_process(COMMAND "${LS_EXE}" check "${SAMPLE_DIR}/unterminated_comment_nested_reject.lls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR
        "unterminated-comment: nested case was ACCEPTED (rc=0)\n${n_out}${n_err}")
endif()
if(NOT "${n_out}${n_err}" MATCHES "unterminated block comment")
    message(FATAL_ERROR "unterminated-comment: nested case missing the diagnostic text\n${n_out}${n_err}")
endif()
message(STATUS "unterminated-comment: nested case rejected with a clear diagnostic")

message(STATUS "test_unterminated_comment_reject: OK")
