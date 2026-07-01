# KI-D: lazy generic method monomorphization + method-level where bounds.

cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
set(SRC_OK "${CMAKE_CURRENT_LIST_DIR}/samples/vec_kid_lazy_test.lls")
set(SRC_BAD "${CMAKE_CURRENT_LIST_DIR}/samples/vec_kid_missing_eq_fail.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/vec_kid.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(COMMAND "${LS}" run "${SRC_OK}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "vec_kid JIT failed (rc=${jr}):\n${je}\n${jo}")
endif()
if(NOT jo MATCHES "KID LAZY PASS" OR jo MATCHES "FAIL ")
    message(FATAL_ERROR "vec_kid JIT: bad output:\n${jo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${SRC_OK}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0)
    message(FATAL_ERROR "vec_kid memcheck run failed (rc=${mr}):\n${me}\n${mo}")
endif()
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "vec_kid memcheck mismatch:\n${me}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC_OK}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce OUTPUT_VARIABLE co TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "vec_kid AOT compile failed:\n${ce}\n${co}")
endif()
execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao ERROR_VARIABLE ae RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ar EQUAL 0 OR NOT ao MATCHES "KID LAZY PASS" OR ao MATCHES "FAIL ")
    message(FATAL_ERROR "vec_kid AOT: bad output (rc=${ar}):\n${ae}\n${ao}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC_BAD}" -o "${CMAKE_BINARY_DIR}/vec_kid_bad.exe"
    RESULT_VARIABLE br ERROR_VARIABLE be OUTPUT_VARIABLE bo TIMEOUT 30)
if(br EQUAL 0)
    message(FATAL_ERROR "vec_kid negative unexpectedly compiled:\n${bo}")
endif()
set(bad_out "${be}\n${bo}")
if(NOT bad_out MATCHES "requires T: Eq" OR NOT bad_out MATCHES "does not implement Eq")
    message(FATAL_ERROR "vec_kid negative: expected missing Eq where-bound error, got:\n${bad_out}")
endif()

message(STATUS "vec_kid all passed")
