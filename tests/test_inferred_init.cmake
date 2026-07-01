# Inferred aggregate init: `Type v = {}` zero-inits a struct (type inferred from
# the declared LHS), and empty map literal still works. JIT + AOT + memcheck.

cmake_minimum_required(VERSION 3.20)
set(LS  "${LS_EXE}")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/inferred_init_test.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/inferred_init.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "inferred_init JIT failed (rc=${jr}):\n${je}")
endif()
if(NOT jo MATCHES "INIT PASS" OR jo MATCHES "FAIL ")
    message(FATAL_ERROR "inferred_init JIT: bad output:\n${jo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr)
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "inferred_init memcheck mismatch:\n${me}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "inferred_init AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar)
if(NOT ao MATCHES "INIT PASS" OR ao MATCHES "FAIL ")
    message(FATAL_ERROR "inferred_init AOT: bad output (rc=${ar}):\n${ao}")
endif()

message(STATUS "inferred_init all passed")
