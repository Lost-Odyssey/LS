# test_field_borrow_owned_local.cmake — regression for the owned-local field-borrow
# leak/double-free. `&local.field` (a struct/enum-typed field) of an OWNED local
# struct passed to a read-only `&T` free-function param used to deep-CLONE the field
# (struct: clone leaked at loop-enclosing scope; enum: shallow copy double-freed the
# shared payload). The fix borrows the field in place via GEP — no clone. Without it
# the new sample reports 2 leaks + 1 double-free; with it, 0/0/0. JIT + AOT + memcheck.
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(F "${CMAKE_CURRENT_LIST_DIR}/samples/field_borrow_owned_local_test.ls")

execute_process(COMMAND "${LS}" run "${F}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
if(NOT sr EQUAL 0 OR NOT so MATCHES "FBOWNED OK" OR so MATCHES "FB FAIL")
    message(FATAL_ERROR "field_borrow_owned_local JIT bad (rc=${sr}):\n${se}\n${so}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${F}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0 OR NOT "${me}" MATCHES "OK clean")
    message(FATAL_ERROR "field_borrow_owned_local --memcheck not clean:\n${me}")
endif()

set(EXE "${CMAKE_BINARY_DIR}/field_borrow_owned_local_test.exe")
execute_process(COMMAND "${LS}" compile "${F}" -o "${EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "field_borrow_owned_local AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ar EQUAL 0 OR NOT ao MATCHES "FBOWNED OK" OR ao MATCHES "FB FAIL")
    message(FATAL_ERROR "field_borrow_owned_local AOT run: rc=${ar}\n${ao}")
endif()

message(STATUS "test_field_borrow_owned_local: ALL PASSED")
