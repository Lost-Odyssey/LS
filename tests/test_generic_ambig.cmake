# test_generic_ambig.cmake — cross-module same-name generic detection.
#
# Two imported modules (ma, mb) each define `struct Box(T)`. A bare use of
# `Box(int)` must be rejected with a clear cross-module ambiguity error instead
# of silently binding to whichever module was imported first. Mirrors the B-4
# behaviour for non-generic types (test_modtype_conflict).
#
# Required: LS_EXE, SAMPLE_DIR
cmake_minimum_required(VERSION 3.20)

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/gen_ambig/main.ls"
    OUTPUT_VARIABLE _out  ERROR_VARIABLE _err  RESULT_VARIABLE _rc
)
if(_rc EQUAL 0)
    message(FATAL_ERROR "generic-ambig: expected compile error but got exit 0\nstdout:\n${_out}")
endif()
if(NOT "${_err}" MATCHES "multiple imported modules")
    message(FATAL_ERROR "generic-ambig: error does not mention 'multiple imported modules'\nstderr:\n${_err}")
endif()
message(STATUS "test_generic_ambig: got expected cross-module generic conflict (rc=${_rc})")
