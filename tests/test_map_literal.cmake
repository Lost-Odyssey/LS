# std.map M-LIT: `{ k: v, ... }` key-value literals (frontend §F1) — `:` pairs
# build AST_MAP_LIT, routed by LHS type to the __from_pairs protocol. POD/string/
# Vec values, empty `{}`, trailing comma, anonymous-struct-literal regression.
# See docs/plan_std_map.md §F1. JIT + memcheck 0/0/0 + AOT. Prints "MLIT PASS".

cmake_minimum_required(VERSION 3.20)
set(LS  "${LS_EXE}")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/map_literal_test.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/map_literal.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "map_literal JIT failed (rc=${jr}):\n${je}\n${jo}")
endif()
if(NOT jo MATCHES "MLIT PASS" OR jo MATCHES "FAIL ")
    message(FATAL_ERROR "map_literal JIT: bad output:\n${jo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "map_literal memcheck mismatch:\n${me}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "map_literal AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ao MATCHES "MLIT PASS" OR ao MATCHES "FAIL ")
    message(FATAL_ERROR "map_literal AOT: bad output (rc=${ar}):\n${ao}")
endif()

message(STATUS "map_literal all passed")
