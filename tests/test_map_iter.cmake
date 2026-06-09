# std.map M-3: MapIter + keys/values/each + `for e in m` (Iterator protocol,
# yields Entry(K,V); no `for (k,v)` destructuring). POD + has_drop keys (string)
# + has_drop container values (Vec). See docs/plan_std_map.md §7.
# JIT + memcheck 0/0/0 + AOT. Self-verifying sample prints "ITER PASS".

cmake_minimum_required(VERSION 3.20)
set(LS  "${LS_EXE}")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/map_iter_test.ls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/map_iter.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "map_iter JIT failed (rc=${jr}):\n${je}\n${jo}")
endif()
if(NOT jo MATCHES "ITER PASS" OR jo MATCHES "FAIL ")
    message(FATAL_ERROR "map_iter JIT: bad output:\n${jo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "map_iter memcheck mismatch:\n${me}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "map_iter AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ao MATCHES "ITER PASS" OR ao MATCHES "FAIL ")
    message(FATAL_ERROR "map_iter AOT: bad output (rc=${ar}):\n${ao}")
endif()

message(STATUS "map_iter all passed")
