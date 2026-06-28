# test_field_borrow.cmake — read-only `&field` / `&element` borrow (twin of the
# `&!field` writable field borrow). `&obj.field` / `&arr[i]` to a read-only `&T`
# param (fn or Block) lends a zero-copy read-only borrow of a has_drop field/
# element; the source stays alive. JIT + AOT + memcheck 0/0/0.
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(F "${CMAKE_CURRENT_LIST_DIR}/samples/field_borrow_test.ls")

execute_process(COMMAND "${LS}" run "${F}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
if(NOT sr EQUAL 0 OR NOT so MATCHES "FIELDBORROW OK" OR so MATCHES "FB FAIL")
    message(FATAL_ERROR "field_borrow JIT bad (rc=${sr}):\n${se}\n${so}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${F}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0 OR NOT "${me}" MATCHES "OK clean")
    message(FATAL_ERROR "field_borrow --memcheck not clean:\n${me}")
endif()

set(EXE "${CMAKE_BINARY_DIR}/field_borrow_test.exe")
execute_process(COMMAND "${LS}" compile "${F}" -o "${EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "field_borrow AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ar EQUAL 0 OR NOT ao MATCHES "FIELDBORROW OK" OR ao MATCHES "FB FAIL")
    message(FATAL_ERROR "field_borrow AOT run: rc=${ar}\n${ao}")
endif()

message(STATUS "test_field_borrow: ALL PASSED")
