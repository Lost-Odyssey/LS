# test_derive_declaration_gap.cmake — @derive on a generic struct must not
# care what sits between the struct and its own inherent methods(T) block.
#
# expand_derives's look-ahead used to check only decls[i+1] and give up at the
# first non-match; an unrelated interface/function/struct between the
# struct+derive and the inherent block made the fold land BEFORE that block,
# failing with "requires an inherent methods block" on code that had one.
#
# Required: LS_EXE, SAMPLE_DIR
#
# @subsystem checker/reflection
# @guards @derive assumed the inherent methods block was strictly adjacent (2026-07-29)
# @sources checker_derive.c:expand_derives
cmake_minimum_required(VERSION 3.20)

foreach(_required_var LS_EXE SAMPLE_DIR)
    if(NOT DEFINED ${_required_var})
        message(FATAL_ERROR "test_derive_declaration_gap.cmake: missing required -D${_required_var}=...")
    endif()
endforeach()

set(_want "3\n4\n1\n2\ntrue\n10\nALL PASS\n")

execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/derive_declaration_gap_test.lls"
    OUTPUT_VARIABLE j_out ERROR_VARIABLE j_err RESULT_VARIABLE j_rc)
if(NOT j_rc EQUAL 0)
    message(FATAL_ERROR
        "derive-declaration-gap: run FAILED rc=${j_rc} -- an unrelated "
        "declaration between @derive and the inherent methods block should "
        "not affect compilation\n${j_err}")
endif()
string(REPLACE "\r\n" "\n" j_out "${j_out}")
if(NOT j_out STREQUAL _want)
    message(FATAL_ERROR
        "derive-declaration-gap: wrong values.\n--- want ---\n${_want}\n--- got ---\n${j_out}")
endif()
message(STATUS "derive-declaration-gap: values correct")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${SAMPLE_DIR}/derive_declaration_gap_test.lls"
    OUTPUT_VARIABLE m_out ERROR_VARIABLE m_err RESULT_VARIABLE m_rc)
if(NOT m_rc EQUAL 0)
    message(FATAL_ERROR "derive-declaration-gap: memcheck run FAILED rc=${m_rc}\n${m_err}")
endif()
if(NOT "${m_out}${m_err}" MATCHES "OK clean")
    message(FATAL_ERROR "derive-declaration-gap: memcheck not clean\n${m_out}${m_err}")
endif()
message(STATUS "derive-declaration-gap: memcheck clean")

message(STATUS "test_derive_declaration_gap: OK")
