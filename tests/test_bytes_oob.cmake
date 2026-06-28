# test_bytes_oob.cmake — std.bytes.Reader bounds check: reading past the end of
# the buffer must abort (non-zero exit + "past end" diagnostic; the post-read line
# must NOT run).
#
# Required: LS_EXE, SAMPLE
cmake_minimum_required(VERSION 3.20)

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _out  ERROR_VARIABLE _err  RESULT_VARIABLE _rc
)
if(_rc EQUAL 0)
    message(FATAL_ERROR "bytes_reader_oob: expected non-zero exit (abort)\n${_out}")
endif()
if(NOT "${_out}" MATCHES "past end")
    message(FATAL_ERROR "bytes_reader_oob: missing 'past end' diagnostic\nstdout:\n${_out}")
endif()
if("${_out}" MATCHES "AFTER")
    message(FATAL_ERROR "bytes_reader_oob: ran past the out-of-range read\n${_out}")
endif()
message(STATUS "test_bytes_oob: aborted as expected (rc=${_rc})")
