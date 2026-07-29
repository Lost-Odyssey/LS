# test_lex_boundary.cmake — B5: \xHH hex escapes and correctly-closed
# multi-level nested block comments both work correctly (confirmed by probe)
# but had zero test coverage. Locks in both so a future scanner/formatter
# change cannot silently break them.
#
# Required: LS_EXE, SAMPLE
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

message(STATUS "test_lex_boundary: OK")
