# std.map M-0: struct Map(K,V) + construct + set/get/has?/len + overwrite +
# grow/rehash (Robin Hood + Fibonacci scatter), POD K/V. JIT + memcheck 0/0/0 +
# AOT. See docs/plan_std_map.md. Self-verifying sample prints "MAP PASS".

cmake_minimum_required(VERSION 3.20)
set(LS  "${LS_EXE}")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/map_basic_test.ls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/map_basic.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "map_basic JIT failed (rc=${jr}):\n${je}\n${jo}")
endif()
if(NOT jo MATCHES "MAP PASS" OR jo MATCHES "FAIL ")
    message(FATAL_ERROR "map_basic JIT: bad output:\n${jo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "map_basic memcheck mismatch:\n${me}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "map_basic AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ao MATCHES "MAP PASS" OR ao MATCHES "FAIL ")
    message(FATAL_ERROR "map_basic AOT: bad output (rc=${ar}):\n${ao}")
endif()

message(STATUS "map_basic all passed")
