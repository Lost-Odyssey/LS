# M-DEF: implicit empty/default init — `T v` ≡ `T v = {}` for any type whose
# `= {}` is already a legal initializer (user containers Vec/Map, struct
# zero-init, the built-in map). POD `int x` keeps its no-init behavior.
# See docs/plan_std_map.md §F2 / M-DEF. JIT + memcheck 0/0/0 + AOT.
# Self-verifying sample prints "MDEF PASS" (and "FAIL <l>" on any failure).

cmake_minimum_required(VERSION 3.20)
set(LS  "${LS_EXE}")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/implicit_empty_init_test.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/implicit_empty_init.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "implicit_empty_init JIT failed (rc=${jr}):\n${je}\n${jo}")
endif()
if(NOT jo MATCHES "MDEF PASS" OR jo MATCHES "FAIL ")
    message(FATAL_ERROR "implicit_empty_init JIT: bad output:\n${jo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "implicit_empty_init memcheck mismatch:\n${me}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "implicit_empty_init AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ao MATCHES "MDEF PASS" OR ao MATCHES "FAIL ")
    message(FATAL_ERROR "implicit_empty_init AOT: bad output (rc=${ar}):\n${ao}")
endif()

message(STATUS "implicit_empty_init all passed")
