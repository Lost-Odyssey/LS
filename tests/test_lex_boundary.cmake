# test_lex_boundary.cmake — B5: \xHH hex escapes and correctly-closed
# multi-level nested block comments both work correctly (confirmed by probe)
# but had zero test coverage. Locks in both so a future scanner/formatter
# change cannot silently break them.
#
# Also pins a SECOND bug this sample found by accident: `lls fmt` recovered
# comment text using a naive "find the first close marker" search, which for
# a nested comment stops at the INNERMOST close and drops every outer close
# marker from the re-emitted text. Feeding that truncated comment back through
# the scanner (which correctly tracks nesting depth) reads as an unterminated
# comment -- the fmt round-trip test_fmt_roundtrip's own oracle (fmtround.py)
# is what caught it. Requires `lls fmt --stdout` on this sample to reproduce
# the full comment text byte-for-byte AND for the formatted output to still
# compile.
#
# Required: LS_EXE, SAMPLE
#
# @subsystem frontend/lexer
# @guards B5 lexical boundaries (\xHH escapes, nested block comments)
# @sources scanner.c:skip_whitespace
cmake_minimum_required(VERSION 3.20)

foreach(_required_var LS_EXE SAMPLE)
    if(NOT DEFINED ${_required_var})
        message(FATAL_ERROR "test_lex_boundary.cmake: missing required -D${_required_var}=...")
    endif()
endforeach()

execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "lex-boundary: run FAILED rc=${rc}\n${err}")
endif()
if(NOT "${out}" MATCHES "hex:ABC done")
    message(FATAL_ERROR "lex-boundary: \\xHH escape did not decode to ABC\n${out}")
endif()
message(STATUS "lex-boundary: run values correct")

# ---- fmt must preserve the full nested comment, not just its innermost close ----
execute_process(COMMAND "${LS_EXE}" fmt "${SAMPLE}" --stdout
    OUTPUT_VARIABLE fmt_out ERROR_VARIABLE fmt_err RESULT_VARIABLE fmt_rc)
if(NOT fmt_rc EQUAL 0)
    message(FATAL_ERROR "lex-boundary: fmt --stdout FAILED rc=${fmt_rc}\n${fmt_err}")
endif()
if(NOT "${fmt_out}" MATCHES "back to level 2 \\*/ back to level 1 \\*/")
    message(FATAL_ERROR
        "lex-boundary: fmt dropped the outer close markers of the nested "
        "comment -- it only kept the innermost one.\n${fmt_out}")
endif()

set(_fmt_file "${SAMPLE}.fmt.lls")
file(WRITE "${_fmt_file}" "${fmt_out}")
execute_process(COMMAND "${LS_EXE}" run "${_fmt_file}"
    OUTPUT_VARIABLE fmtrun_out ERROR_VARIABLE fmtrun_err RESULT_VARIABLE fmtrun_rc)
file(REMOVE "${_fmt_file}")
if(NOT fmtrun_rc EQUAL 0)
    message(FATAL_ERROR
        "lex-boundary: the FORMATTED source failed to run (rc=${fmtrun_rc}) -- "
        "a truncated nested comment reads as unterminated when re-parsed.\n${fmtrun_err}")
endif()
if(NOT "${fmtrun_out}" MATCHES "hex:ABC done")
    message(FATAL_ERROR "lex-boundary: formatted source produced wrong values\n${fmtrun_out}")
endif()
message(STATUS "lex-boundary: fmt round-trip preserves nested comment and still compiles")

message(STATUS "test_lex_boundary: OK")
