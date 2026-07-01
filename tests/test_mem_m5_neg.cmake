# test_mem_m5_neg.cmake — M-5 negative tests: move-after-use must be rejected
# at compile time. Each sample uses a moved variable; the checker must emit a
# [move error] and exit non-zero.
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

# sample basename -> expected stderr substring
set(_cases
    "test_mem_m5_neg_push|moved variable 's'"
    "test_mem_m5_neg_index|moved variable 's'"
    "test_mem_m5_neg_branch|maybe-moved variable 's'"
)

foreach(_case ${_cases})
    string(REPLACE "|" ";" _parts "${_case}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _expect)
    execute_process(
        COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/${_name}.lls"
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE  _err
        RESULT_VARIABLE _rc
    )
    if(_rc EQUAL 0)
        message(FATAL_ERROR
            "${_name}: expected move-after-use rejection but compile/run succeeded\nstdout:\n${_out}")
    endif()
    if(NOT "${_err}" MATCHES "${_expect}")
        message(FATAL_ERROR
            "${_name}: expected stderr to contain '${_expect}'\nstderr:\n${_err}")
    endif()
    message(STATUS "test_mem_m5_neg ${_name}: rejected OK")
endforeach()
