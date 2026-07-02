# test_deep_nesting.cmake — generic deep-nesting regression guard for the
# recursive-descent stdlib parsers (json, html), found by stdfuzz.
#
# Two failure classes this guards:
#   1. Unbounded recursion on deeply-nested input ("[[[[..." / "<a><a><a>...")
#      overflowed the native stack and crashed. Each parser now caps nesting
#      depth and returns cleanly. The sample feeds such input and MUST NOT crash.
#   2. json only: Vec.copy deep-cloned each element TWICE, so a legal deep-
#      BALANCED array parsed in O(2^depth) and hung. Fixed to clone once. A
#      regression makes the sample hang -> this test times out.
#
# The sample self-verifies (prints "PASS ..." on success, "FAIL ..." on failure).
# Required: LS_EXE, SAMPLE, LS_HOME, TEST_NAME
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE OR NOT LS_HOME)
    message(FATAL_ERROR "requires LS_EXE, SAMPLE, LS_HOME")
endif()
if(NOT TEST_NAME)
    set(TEST_NAME "deep_nesting")
endif()
set(ENV{LS_HOME} "${LS_HOME}")

execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE  err
    TIMEOUT 30
)

# TIMEOUT -> RESULT_VARIABLE is a non-integer message = an exponential regressed.
if(NOT "${rc}" MATCHES "^-?[0-9]+$")
    message(FATAL_ERROR "${TEST_NAME}: HANG (parser did not terminate: ${rc}) "
        "— an exponential clone likely regressed")
endif()
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${TEST_NAME}: run failed (rc=${rc}) — likely a "
        "stack-overflow crash on deep input\nstdout: ${out}\nstderr: ${err}")
endif()
if(out MATCHES "FAIL")
    message(FATAL_ERROR "${TEST_NAME}: a self-check FAILED\nstdout: ${out}")
endif()
if(NOT out MATCHES "PASS")
    message(FATAL_ERROR "${TEST_NAME}: no PASS marker in output\nstdout: ${out}")
endif()
if(NOT err MATCHES "\\[memcheck\\] OK clean")
    message(FATAL_ERROR "${TEST_NAME}: memcheck not clean\nstderr: ${err}")
endif()

message(STATUS "${TEST_NAME}: ALL PASSED")
