# Regression for B-MAP-M5-002: Block/closure params of reference type `&T`
# (Block(&Map)/Block(&struct)) use the pointer borrow ABI end-to-end. JIT +
# memcheck 0/0/0 + AOT. Self-verifying sample prints "BLOCKREF PASS".

cmake_minimum_required(VERSION 3.20)
set(LS  "${LS_EXE}")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/block_ref_param_test.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/block_ref_param.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "block_ref_param JIT failed (rc=${jr}):\n${je}\n${jo}")
endif()
if(NOT jo MATCHES "BLOCKREF PASS" OR jo MATCHES "FAIL ")
    message(FATAL_ERROR "block_ref_param JIT: bad output:\n${jo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "block_ref_param memcheck mismatch:\n${me}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "block_ref_param AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ao MATCHES "BLOCKREF PASS" OR ao MATCHES "FAIL ")
    message(FATAL_ERROR "block_ref_param AOT: bad output (rc=${ar}):\n${ao}")
endif()

message(STATUS "block_ref_param all passed")
