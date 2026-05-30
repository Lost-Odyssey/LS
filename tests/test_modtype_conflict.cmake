# test_modtype_conflict.cmake — B-1 (B-safe): same struct/enum name from multiple
# modules → clear compile error instead of GEP crash / silent memory corruption.
#
# S2: struct Config with different field counts (mod_a: 1 field, mod_b: 3 fields)
# S6: struct Box with different layouts (mod_a: 2 fields, mod_b: 1 field)
# Both scenarios must: exit non-zero AND stderr contain "multiple modules"
cmake_minimum_required(VERSION 3.20)

macro(expect_conflict sample label)
    execute_process(
        COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/${sample}/main.ls"
        OUTPUT_VARIABLE _out  ERROR_VARIABLE _err  RESULT_VARIABLE _rc
    )
    if(_rc EQUAL 0)
        message(FATAL_ERROR "${label}: expected compile error but got exit 0\nstdout:\n${_out}")
    endif()
    if(NOT "${_err}" MATCHES "multiple imported modules")
        message(FATAL_ERROR "${label}: error does not mention 'multiple modules'\nstderr:\n${_err}")
    endif()
    message(STATUS "${label}: got expected conflict error (rc=${_rc})")
endmacro()

# S2: Config — different field counts
expect_conflict(modtype_s2 "S2_struct_diff_fields")

# S6: Box — different field count / layout
expect_conflict(modtype_s6 "S6_struct_diff_layout")

message(STATUS "test_modtype_conflict: ALL PASSED")
