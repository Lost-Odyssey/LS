# std.map M-2: has_drop K/V ownership. has_drop keys (string), has_drop values
# (string / Vec(int) / nested Map), set/overwrite/get/remove/clear/grow/rehash and
# Map-as-struct-field auto-drop — all memcheck 0/0/0. Also exercises the owned-
# rvalue-enum match double-drop fix (match m.get(k) for container values).
# See docs/plan_std_map.md §8. Self-verifying sample prints "OWNDROP PASS".

cmake_minimum_required(VERSION 3.20)
set(LS  "${LS_EXE}")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/map_owndrop_test.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/map_owndrop.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "map_owndrop JIT failed (rc=${jr}):\n${je}\n${jo}")
endif()
if(NOT jo MATCHES "OWNDROP PASS" OR jo MATCHES "FAIL ")
    message(FATAL_ERROR "map_owndrop JIT: bad output:\n${jo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "map_owndrop memcheck mismatch:\n${me}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "map_owndrop AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ao MATCHES "OWNDROP PASS" OR ao MATCHES "FAIL ")
    message(FATAL_ERROR "map_owndrop AOT: bad output (rc=${ar}):\n${ao}")
endif()

message(STATUS "map_owndrop all passed")
