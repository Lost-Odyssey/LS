# test_lstest.cmake — the `ls test` runner's own regression.
#   Verifies: (1) an all-pass file exits 0 and reports "0 failed";
#   (2) a file with a failing test exits NON-ZERO and prints FAIL
#       (this is the anti-"假绿" guarantee — a broken assertion must fail CI).
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(PASS "${CMAKE_CURRENT_LIST_DIR}/samples/selftest_pass.ls")
set(FAIL "${CMAKE_CURRENT_LIST_DIR}/samples/selftest_fail.ls")

# (1) all-pass fixture -> exit 0, runs assertions, no FAIL
execute_process(COMMAND "${LS}" test "${PASS}"
    OUTPUT_VARIABLE po ERROR_VARIABLE pe RESULT_VARIABLE pr TIMEOUT 60)
if(NOT pr EQUAL 0)
    message(FATAL_ERROR "ls test on all-pass fixture exited ${pr}:\n${pe}\n${po}")
endif()
if(po MATCHES "FAIL")
    message(FATAL_ERROR "all-pass fixture unexpectedly reported FAIL:\n${po}")
endif()
if(NOT po MATCHES "3 passed")
    message(FATAL_ERROR "all-pass fixture did not report 3 passed:\n${po}")
endif()

# (2) failing fixture -> NON-ZERO exit + FAIL in output
execute_process(COMMAND "${LS}" test "${FAIL}"
    OUTPUT_VARIABLE fo ERROR_VARIABLE fe RESULT_VARIABLE fr TIMEOUT 60)
if(fr EQUAL 0)
    message(FATAL_ERROR "ls test on failing fixture exited 0 (假绿!):\n${fo}")
endif()
if(NOT fo MATCHES "FAIL  test_broken")
    message(FATAL_ERROR "failing fixture did not report the broken test:\n${fo}")
endif()

message(STATUS "test_lstest: ALL PASSED")
