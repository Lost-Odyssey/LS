# test_type_name_distinct.cmake — type_name static-buffer self-clobber regression.
# A type mismatch involving a nested Block type must show the REAL `got` name
# (`def(int, int) -> int`), not a copy of `expected` (the old corruption symptom).
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(F "${CMAKE_CURRENT_LIST_DIR}/samples/type_name_distinct_reject.ls")

execute_process(COMMAND "${LS}" run "${F}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
if(sr EQUAL 0)
    message(FATAL_ERROR "type_name_distinct should FAIL to compile but succeeded:\n${so}")
endif()
# The `got` side must be the real def type, proving no self-clobber to `expected`.
if(NOT "${se}${so}" MATCHES "got 'def\\(int, int\\)")
    message(FATAL_ERROR "type_name self-clobber regression — expected/got not distinct:\n${se}\n${so}")
endif()

message(STATUS "test_type_name_distinct: PASSED")
