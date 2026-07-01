# test_bit_match_reject.cmake — V1 bit-pattern negative checks.
# Each sample must be rejected at compile time (non-zero exit + a diagnostic on
# stderr); none should run.
#
# Required: LS_EXE, SAMPLE_DIR
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE_DIR)
    message(FATAL_ERROR "test_bit_match_reject.cmake requires LS_EXE and SAMPLE_DIR")
endif()

# sample -> regex the stderr diagnostic must match
set(_cases
    "bit_match_width_reject|total width"
    "bit_match_nonint_reject|must be an integer type"
)

foreach(_case IN LISTS _cases)
    string(REPLACE "|" ";" _parts "${_case}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _rx)
    execute_process(
        COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/${_name}.lls"
        OUTPUT_VARIABLE _out  ERROR_VARIABLE _err  RESULT_VARIABLE _rc
    )
    if(_rc EQUAL 0)
        message(FATAL_ERROR "bit_match_reject ${_name}: expected compile error, got exit 0\n${_out}")
    endif()
    if(NOT "${_err}" MATCHES "${_rx}")
        message(FATAL_ERROR "bit_match_reject ${_name}: stderr missing '${_rx}'\n${_err}")
    endif()
    message(STATUS "bit_match_reject ${_name}: rejected as expected (rc=${_rc})")
endforeach()

message(STATUS "test_bit_match_reject: ALL PASSED")
