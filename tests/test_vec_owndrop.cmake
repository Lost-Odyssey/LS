# Vec(T) element ownership / clone / drop correctness (plan_vec_ownership_drop.md
# §008 index-read-through of has_drop struct + §009 rvalue string move-into-
# container). JIT + memcheck 0/0/0 + AOT. Self-verifying sample prints
# "OWNDROP PASS" (and "FAIL <l>" on any failed assertion).

cmake_minimum_required(VERSION 3.20)
set(LS  "${LS_EXE}")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/vec_owndrop_test.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/vec_owndrop.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "vec_owndrop JIT failed (rc=${jr}):\n${je}\n${jo}")
endif()
if(NOT jo MATCHES "OWNDROP PASS" OR jo MATCHES "FAIL ")
    message(FATAL_ERROR "vec_owndrop JIT: bad output:\n${jo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "vec_owndrop memcheck mismatch:\n${me}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "vec_owndrop AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ao MATCHES "OWNDROP PASS" OR ao MATCHES "FAIL ")
    message(FATAL_ERROR "vec_owndrop AOT: bad output (rc=${ar}):\n${ao}")
endif()

message(STATUS "vec_owndrop all passed")
