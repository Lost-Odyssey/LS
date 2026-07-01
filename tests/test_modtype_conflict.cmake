# test_modtype_conflict.cmake — B-4: bare reference to a type defined by 2+
# imported modules is ambiguous → clear compile error advising qualification
# (`mod.Type`). Importing both is now allowed (B-4 relaxed B-1); the error fires
# only on the unqualified USE. Diagnostic still contains "multiple imported modules".
#
# S2: bare `Config` (mod_a: 1 field, mod_b: 3 fields) → ambiguous-use error
# S6: bare `Box`    (mod_a: 2 fields, mod_b: 1 field) → ambiguous-use error
# Both scenarios must: exit non-zero AND stderr contain "multiple imported modules"
cmake_minimum_required(VERSION 3.20)

macro(expect_conflict sample label)
    execute_process(
        COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/${sample}/main.lls"
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
